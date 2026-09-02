// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "Script.h"
#include "TestClientOptions.h"
#include "TestSession.h"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "FrameMessages.h"
#include "PlaybackMessages.h"
#include "ProjectOpReply.h"
#include "ProjectRequests.h"
#include "ProjectSnapshot.h"
#include "SessionMessages.h"
#include "ShotRigRequests.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
#include "TaskMessages.h"
#include "ViewportMessages.h"
// vsr_scivis_studio_model
#include "Project.h"
#include "Shot.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::test_client;
using namespace std::chrono_literals;

namespace {

constexpr auto TEST_TIMEOUT = 10s;

std::vector<std::string> lines(const std::string &text)
{
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    out.push_back(line);
  return out;
}

bool hasLine(const std::vector<std::string> &records, const std::string &exact)
{
  for (const auto &r : records)
    if (r == exact)
      return true;
  return false;
}

bool hasLineStarting(
    const std::vector<std::string> &records, const std::string &prefix)
{
  for (const auto &r : records)
    if (r.rfind(prefix, 0) == 0)
      return true;
  return false;
}

size_t countStarting(
    const std::vector<std::string> &records, const std::string &prefix)
{
  size_t n = 0;
  for (const auto &r : records)
    n += r.rfind(prefix, 0) == 0;
  return n;
}

std::vector<std::string> failLines(const std::vector<std::string> &records)
{
  std::vector<std::string> out;
  for (const auto &r : records)
    if (r.rfind("FAIL ", 0) == 0)
      out.push_back(r);
  return out;
}

// Parses `script` and runs it through a fresh runner on `session`, returning
// run()'s verdict and the record stream.
struct RunResult
{
  bool ok{false};
  std::vector<std::string> records;
};

RunResult runScript(
    TestSession &session, const std::string &script, RunnerOptions options = {})
{
  std::vector<Command> commands;
  std::string error;
  REQUIRE(parseScript(script, commands, &error));
  std::ostringstream out;
  options.timeout = TEST_TIMEOUT;
  CommandRunner runner(&session, &out, options);
  RunResult result;
  result.ok = runner.run(commands);
  result.records = lines(out.str());
  return result;
}

// A fake server on a bare NetworkServer that misbehaves in one scripted way,
// so the client's failure paths run without a StudioServer.
struct ScriptedServer
{
  enum class Behaviour
  {
    MismatchedHello, // a Hello of the wrong version, nothing else
    HelloOnly, // the right Hello, then silence: no Bootstrap ever comes
    RefuseOnHello, // answers the client's Hello with an Error and closes
    SilentAfterBootstrap // an empty Bootstrap, then nothing, Pings included
  };

  explicit ScriptedServer(Behaviour behaviour);
  ~ScriptedServer();

  unsigned short port() const;

  std::shared_ptr<vsr::network::NetworkServer> channel;
  std::atomic<size_t> pingsReceived{0};
  // RefuseOnHello: the Error is flushed and the socket closed off the IO
  // thread, the way StudioServer's farewell works.
  std::atomic<bool> refusing{false};
  vsr::network::MessageFuture farewell;
  std::thread closer;
};

ScriptedServer::ScriptedServer(Behaviour behaviour)
{
  channel = std::make_shared<vsr::network::NetworkServer>(0);
  channel->setConnectHandler([this, behaviour]() {
    Hello hello;
    hello.version = behaviour == Behaviour::MismatchedHello
        ? PROTOCOL_VERSION + 1
        : PROTOCOL_VERSION;
    hello.buildInfo = "scripted server";
    channel->send(encode(hello));
    if (behaviour == Behaviour::SilentAfterBootstrap) {
      channel->send(encode(BootstrapBegin{}));
      channel->send(encode(BootstrapEnd{}));
    }
  });
  channel->registerHandler(uint8_t(StudioMessageType::Ping),
      [this](const vsr::network::Message &) { ++pingsReceived; });
  if (behaviour == Behaviour::RefuseOnHello) {
    channel->registerHandler(uint8_t(StudioMessageType::Hello),
        [this](const vsr::network::Message &) {
          Error error;
          error.message = "the scripted server refuses";
          farewell = channel->send(encode(error));
          refusing.store(true);
        });
    closer = std::thread([this] {
      // Also woken by the destructor, with no farewell to flush.
      if (!waitFor([&] { return refusing.load(); }) || !farewell.valid())
        return;
      farewell.wait_for(1s);
      channel->restart();
    });
  }
  channel->start();
}

ScriptedServer::~ScriptedServer()
{
  refusing.store(true);
  if (closer.joinable())
    closer.join();
  channel->stop();
}

unsigned short ScriptedServer::port() const
{
  return channel->port();
}

/*
 * A fake project server: the Bootstrap on the client's Hello, then scripted
 * answers to the project requests the runner's commands send, kept minimal
 * but shaped like the real ones -- replies by request id, results, Server
 * Task messages, a ProjectSnapshot after every mutation. Every handler runs
 * on the server's IO thread and answers at once, so a task's completion is
 * usually in the client's queue before the script awaits it.
 *
 * Milestone 6: SetPlaying flips the active shot's `playing` (another shot id
 * is refused) and, like a shot that auto-stops at once, follows its snapshot
 * with a second one at rest on frame 5; StartRendering streams four 2x2 frames at frames 0, 1, 2, 0
 * (a loop wrap), spaced so the client's latest-wins slot sees each; SetTime
 * answers with one frame at the scrubbed frame, or with a TimeAdvanceWarning
 * when the frame is 99; Pick misses at (0,0) and hits surface 4 elsewhere,
 * after a stray PickReply nobody asked for; RequestArrayHistogram bins
 * array 0 and refuses every other array as not scalar. SetOutline and
 * ViewportSettings are only recorded.
 */
struct ProjectOpsServer
{
  using Message = vsr::network::Message;

  ProjectOpsServer();
  ~ProjectOpsServer();

  unsigned short port() const;
  // Every request of type T received so far, decoded, in arrival order.
  template <typename T>
  std::vector<T> requests();

  void onMessage(const Message &msg);
  void send(Message msg);
  void sendSnapshot();
  template <typename Result>
  void reply(uint64_t requestId, const Result &result);
  void replyOk(uint64_t requestId);
  void replyError(uint64_t requestId, const std::string &error);
  uint64_t startTask(uint64_t requestId);

  std::shared_ptr<vsr::network::NetworkServer> channel;
  std::mutex mutex;
  std::vector<Message> received;
  Project project;
  uint64_t nextTaskId{1};
  int nextShot{2};
  int nextDataset{1};
  // Network lag, staged: the next `deferSnapshots` snapshots are held back
  // until the request after them arrives (and go out ahead of its reply,
  // as the order on the wire demands); every snapshot sent normally waits
  // `snapshotDelay` first, off the IO thread so the reply before it is not
  // held up too.
  std::atomic<int> deferSnapshots{0};
  std::chrono::milliseconds snapshotDelay{0};
  std::vector<Message> deferred;
  std::vector<std::thread> delayedSends;
  // StartRendering's frames go out from here: a send posted from a handler
  // is only written once the handler returns, so pacing them needs a thread
  // of their own.
  std::thread streamer;

  void sendFrame(const std::string &shotId, int frame);
};

ProjectOpsServer::ProjectOpsServer()
{
  Shot shot;
  shot.id = "shot_0001";
  shot.name = "Shot 1";
  project.shots.push_back(shot);
  project.activeShotId = shot.id;
  LightRig rig;
  rig.id = "lightRig_0001";
  rig.name = "Default";
  project.lightRigs.push_back(rig);
  CameraRig cameraRig;
  cameraRig.id = "cameraRig_0001";
  cameraRig.name = "Default";
  project.cameraRigs.push_back(cameraRig);
  project.dirty = true;

  channel = std::make_shared<vsr::network::NetworkServer>(0);
  channel->setConnectHandler([this]() {
    Hello hello;
    hello.version = PROTOCOL_VERSION;
    hello.buildInfo = "fake project server";
    channel->send(encode(hello));
  });
  for (int value = 1; value < vsr::network::MESSAGE_TYPE_INVALID; ++value) {
    if (!isStudioMessageType(uint8_t(value)))
      continue;
    channel->registerHandler(
        uint8_t(value), [this](const Message &msg) { onMessage(msg); });
  }
  channel->start();
}

ProjectOpsServer::~ProjectOpsServer()
{
  for (auto &thread : delayedSends)
    thread.join();
  if (streamer.joinable())
    streamer.join();
  channel->stop();
}

unsigned short ProjectOpsServer::port() const
{
  return channel->port();
}

template <typename T>
std::vector<T> ProjectOpsServer::requests()
{
  std::lock_guard lock(mutex);
  std::vector<T> out;
  for (const auto &msg : received)
    if (auto decoded = decode<T>(msg))
      out.push_back(std::move(*decoded));
  return out;
}

void ProjectOpsServer::send(Message msg)
{
  channel->send(std::move(msg));
}

void ProjectOpsServer::sendSnapshot()
{
  if (deferSnapshots > 0) {
    --deferSnapshots;
    deferred.push_back(encode(ProjectSnapshot{project}));
    return;
  }
  if (snapshotDelay.count() > 0) {
    delayedSends.emplace_back(
        [this, snapshot = encode(ProjectSnapshot{project})]() mutable {
          std::this_thread::sleep_for(snapshotDelay);
          send(std::move(snapshot));
        });
    return;
  }
  send(encode(ProjectSnapshot{project}));
}

template <typename Result>
void ProjectOpsServer::reply(uint64_t requestId, const Result &result)
{
  auto r = makeOkReply(requestId);
  setResults(r, result);
  send(encode(r));
}

void ProjectOpsServer::replyOk(uint64_t requestId)
{
  send(encode(makeOkReply(requestId)));
}

void ProjectOpsServer::replyError(uint64_t requestId, const std::string &error)
{
  send(encode(makeErrorReply(requestId, error)));
}

uint64_t ProjectOpsServer::startTask(uint64_t requestId)
{
  const auto taskId = nextTaskId++;
  reply(requestId, TaskStartedResult{taskId});
  return taskId;
}

void ProjectOpsServer::sendFrame(const std::string &shotId, int frame)
{
  FrameHeader header;
  header.width = 2;
  header.height = 2;
  header.shotId = shotId;
  header.frame = frame;
  const std::vector<std::byte> pixels(2 * 2 * 4, std::byte{0x7f});
  send(encodeFrame(header, pixels.data(), pixels.size()));
}

void ProjectOpsServer::onMessage(const Message &msg)
{
  std::lock_guard lock(mutex);
  received.push_back(msg);
  const auto type = messageType(msg);
  if (!type)
    return;

  switch (*type) {
  case StudioMessageType::Hello: {
    send(encode(BootstrapBegin{}));
    FrameConfig config;
    config.width = 640;
    config.height = 480;
    send(encode(config));
    send(encode(ProjectSnapshot{project})); // never staged: the bootstrap's
    send(encode(BootstrapEnd{}));
    return;
  }
  case StudioMessageType::Ping:
    send(encode(Pong{}));
    return;
  default:
    break;
  }

  // A request arrived: whatever earlier snapshots were held back go first.
  for (auto &held : deferred)
    send(std::move(held));
  deferred.clear();

  switch (*type) {
  case StudioMessageType::CreateShot: {
    const auto req = *decode<CreateShot>(msg);
    // A reply to a request nobody sent: the runner must look past it.
    replyOk(999999);
    Shot shot;
    char id[16];
    std::snprintf(id, sizeof(id), "shot_%04d", nextShot++);
    shot.id = id;
    shot.name = req.name;
    project.shots.push_back(shot);
    project.activeShotId = shot.id;
    reply(req.requestId, ShotCreatedResult{shot.id});
    sendSnapshot();
    return;
  }
  case StudioMessageType::RemoveShot: {
    const auto req = *decode<RemoveShot>(msg);
    auto *shot = project::findShot(project, req.shotId);
    if (!shot || project.shots.size() < 2) {
      replyError(req.requestId,
          shot ? "cannot remove the last shot" : "shot not found");
      return;
    }
    project.shots.erase(project.shots.begin() + (shot - project.shots.data()));
    project.activeShotId = project.shots.front().id;
    replyOk(req.requestId);
    sendSnapshot();
    return;
  }
  case StudioMessageType::UpdateShot: {
    const auto req = *decode<UpdateShot>(msg);
    auto *shot = project::findShot(project, req.shot.id);
    if (!shot) {
      replyError(req.requestId, "shot not found");
      return;
    }
    *shot = req.shot;
    replyOk(req.requestId);
    sendSnapshot();
    return;
  }
  case StudioMessageType::SetActiveShot: {
    const auto req = *decode<SetActiveShot>(msg);
    if (!project::findShot(project, req.shotId)) {
      replyError(req.requestId, "shot not found");
      return;
    }
    project.activeShotId = req.shotId;
    replyOk(req.requestId);
    sendSnapshot();
    return;
  }

  case StudioMessageType::SaveProject: {
    const auto req = *decode<SaveProject>(msg);
    const auto taskId = startTask(req.requestId);
    TaskProgress progress;
    progress.taskId = taskId;
    progress.message = "writing";
    send(encode(progress));
    project.projectDirectory = req.directory.value_or("/data/unnamed");
    project.name = project.projectDirectory.filename().string();
    project.dirty = false;
    TaskCompleted completed;
    completed.taskId = taskId;
    send(encode(completed));
    sendSnapshot();
    return;
  }
  case StudioMessageType::OpenProject: {
    const auto req = *decode<OpenProject>(msg);
    TaskFailed failed;
    failed.taskId = startTask(req.requestId);
    failed.error = "project directory does not exist";
    send(encode(failed));
    return;
  }
  case StudioMessageType::ImportStaticDataset: {
    const auto req = *decode<ImportStaticDataset>(msg);
    const auto taskId = startTask(req.requestId);
    TaskProgress progress;
    progress.taskId = taskId;
    progress.message = "importing";
    send(encode(progress));
    Dataset dataset;
    char id[20];
    std::snprintf(id, sizeof(id), "dataset_%04d", nextDataset++);
    dataset.id = id;
    dataset.name = req.name;
    dataset.status = DatasetStatus::Available;
    project.datasets.push_back(dataset);
    TaskCompleted completed;
    completed.taskId = taskId;
    completed.message = dataset.id;
    send(encode(completed));
    sendSnapshot();
    return;
  }
  case StudioMessageType::DeclareFileAnimationDataset: {
    const auto req = *decode<DeclareFileAnimationDataset>(msg);
    Dataset dataset;
    char id[20];
    std::snprintf(id, sizeof(id), "dataset_%04d", nextDataset++);
    dataset.id = id;
    dataset.name = req.name;
    dataset.sourceKind = DatasetSourceKind::FileAnimation;
    dataset.declared = true;
    project.datasets.push_back(dataset);
    reply(req.requestId, DatasetCreatedResult{dataset.id});
    sendSnapshot();
    return;
  }
  case StudioMessageType::CancelTask: {
    const auto req = *decode<CancelTask>(msg);
    replyError(req.requestId, "unknown task " + std::to_string(req.taskId));
    return;
  }

  case StudioMessageType::CreateLightRig: {
    const auto req = *decode<CreateLightRig>(msg);
    LightRig rig;
    rig.id = "lightRig_0002";
    rig.name = req.name;
    project.lightRigs.push_back(rig);
    reply(req.requestId, LightRigCreatedResult{rig.id});
    sendSnapshot();
    return;
  }
  case StudioMessageType::AddLightToRig: {
    const auto req = *decode<AddLightToRig>(msg);
    SceneNodeRef node;
    node.layerName = "studio";
    node.nodeIndex = 7;
    reply(req.requestId, LightAddedResult{node});
    return;
  }
  case StudioMessageType::CreateCameraRig: {
    const auto req = *decode<CreateCameraRig>(msg);
    CameraRig rig;
    rig.id = "cameraRig_0002";
    rig.name = req.name.empty() ? "Camera Rig 2" : req.name;
    project.cameraRigs.push_back(rig);
    reply(req.requestId, CameraRigCreatedResult{rig.id});
    sendSnapshot();
    return;
  }
  case StudioMessageType::CreateColorMap: {
    const auto req = *decode<CreateColorMap>(msg);
    ColorMapRecord record;
    record.id = "colorMap_0001";
    record.name = req.name;
    project.colorMaps.push_back(record);
    SceneObjectRef object;
    object.type = ANARI_ARRAY1D;
    object.objectIndex = 3;
    reply(req.requestId, ColorMapCreatedResult{record.id, object});
    sendSnapshot();
    return;
  }

  case StudioMessageType::ListRoots: {
    const auto req = *decode<ListRoots>(msg);
    reply(req.requestId, ListRootsResult{{"/data"}});
    return;
  }
  case StudioMessageType::ListDirectory: {
    const auto req = *decode<ListDirectory>(msg);
    if (req.directory != "/data") {
      replyError(req.requestId,
          "'" + req.directory.generic_string()
              + "' is outside every Data Root");
      return;
    }
    ListDirectoryResult result;
    DirectoryEntry dir;
    dir.name = "runs";
    dir.kind = EntryKind::Directory;
    DirectoryEntry file;
    file.name = "mesh.obj";
    file.size = 32;
    file.mtimeSeconds = 1700000000;
    result.entries = {dir, file};
    reply(req.requestId, result);
    return;
  }

  case StudioMessageType::SetPlaying: {
    const auto req = *decode<SetPlaying>(msg);
    if (req.shotId != project.activeShotId) {
      replyError(req.requestId,
          "shot '" + req.shotId + "' is not the active shot");
      return;
    }
    auto *shot = project::findShot(project, req.shotId);
    shot->playing = req.playing;
    replyOk(req.requestId);
    sendSnapshot();
    if (req.playing) {
      // The auto-stop, right behind: two snapshots in one poll.
      shot->playing = false;
      shot->currentFrame = 5;
      sendSnapshot();
    }
    return;
  }
  case StudioMessageType::StartRendering: {
    // Paced so the client's latest-wins slot consumes every header.
    if (streamer.joinable())
      streamer.join();
    streamer = std::thread([this, shotId = project.activeShotId] {
      for (const int frame : {0, 1, 2, 0}) {
        sendFrame(shotId, frame);
        std::this_thread::sleep_for(30ms);
      }
    });
    return;
  }
  case StudioMessageType::SetTime: {
    const auto req = *decode<SetTime>(msg);
    if (req.frame == 99) {
      TimeAdvanceWarning warning;
      warning.shotId = req.shotId;
      warning.frame = req.frame;
      warning.message = "frame 99 failed to load";
      send(encode(warning));
      return;
    }
    sendFrame(req.shotId, req.frame);
    return;
  }
  case StudioMessageType::Pick: {
    const auto req = *decode<Pick>(msg);
    // A reply to a pick nobody sent: the runner must look past it.
    PickReply stray;
    stray.requestId = 777;
    send(encode(stray));
    PickReply reply;
    reply.requestId = req.requestId;
    reply.hit = !(req.x == 0 && req.y == 0);
    if (reply.hit) {
      reply.worldPosition = {0.5f, 0.25f, -1.f};
      SceneObjectRef identity;
      identity.type = ANARI_SURFACE;
      identity.objectIndex = 4;
      reply.objectIdentity = identity;
    }
    send(encode(reply));
    return;
  }
  case StudioMessageType::SetOutline:
  case StudioMessageType::ViewportSettings:
    return; // recorded above, nothing to answer
  case StudioMessageType::RequestArrayHistogram: {
    const auto req = *decode<RequestArrayHistogram>(msg);
    if (req.array.type != ANARI_ARRAY || req.array.objectIndex != 0) {
      replyError(req.requestId,
          "array " + std::to_string(req.array.objectIndex)
              + " element type ANARI_FLOAT32_VEC3 is not scalar");
      return;
    }
    ArrayHistogramResult result;
    result.bins = {1, 2, 3};
    result.minValue = 0.f;
    result.maxValue = 1.f;
    reply(req.requestId, result);
    return;
  }

  default: {
    Error error;
    error.message = std::string(toString(*type)) + " is not served by the fake";
    send(encode(error));
    return;
  }
  }
}

} // namespace

SCENARIO("the test client parses its script language", "[StudioTestClient]")
{
  GIVEN("a script with comments, quotes and ; separators")
  {
    const std::string script =
        "# a comment line\n"
        "\n"
        "connect 127.0.0.1 4242   # trailing comment\n"
        "set-param camera 0 note string \"hello world\"; ping\n"
        "  assert lastError contains \"a ; b # c\"  \n"
        "\r\n"
        "await-frame 2 timeout=250\n";
    std::vector<Command> commands;
    std::string error;
    REQUIRE(parseScript(script, commands, &error));

    THEN("every command carries its name, arguments, line and text")
    {
      REQUIRE(commands.size() == 5);

      REQUIRE(commands[0].name == "connect");
      REQUIRE(
          commands[0].args == std::vector<std::string>{"127.0.0.1", "4242"});
      REQUIRE(commands[0].lineNumber == 3);
      REQUIRE(commands[0].text == "connect 127.0.0.1 4242");

      REQUIRE(commands[1].name == "set-param");
      REQUIRE(commands[1].args
          == std::vector<std::string>{
              "camera", "0", "note", "string", "hello world"});
      REQUIRE(commands[1].lineNumber == 4);
      REQUIRE(
          commands[1].text == "set-param camera 0 note string \"hello world\"");

      REQUIRE(commands[2].name == "ping");
      REQUIRE(commands[2].args.empty());
      REQUIRE(commands[2].lineNumber == 4);

      REQUIRE(commands[3].name == "assert");
      REQUIRE(commands[3].args
          == std::vector<std::string>{"lastError", "contains", "a ; b # c"});
      REQUIRE(commands[3].lineNumber == 5);

      REQUIRE(commands[4].name == "await-frame");
      REQUIRE(commands[4].lineNumber == 7);
    }

    THEN("a trailing timeout=<ms> is split off, once")
    {
      auto &await = commands[4];
      const auto timeout = takeTimeoutSuffix(await, &error);
      REQUIRE(timeout);
      REQUIRE(*timeout == 250ms);
      REQUIRE(await.args == std::vector<std::string>{"2"});
      REQUIRE_FALSE(takeTimeoutSuffix(await, &error));
      REQUIRE(error.empty());
      REQUIRE(await.args == std::vector<std::string>{"2"});
    }
  }

  GIVEN("-e style input")
  {
    std::vector<Command> commands;
    REQUIRE(parseScript("connect; start-rendering ;await-frame 3;", commands));
    THEN("the ; pieces are separate commands on the same line")
    {
      REQUIRE(commands.size() == 3);
      REQUIRE(commands[0].name == "connect");
      REQUIRE(commands[1].name == "start-rendering");
      REQUIRE(commands[2].name == "await-frame");
      REQUIRE(commands[2].args == std::vector<std::string>{"3"});
      for (const auto &c : commands)
        REQUIRE(c.lineNumber == 1);
    }
  }

  GIVEN("malformed input")
  {
    std::vector<Command> commands;
    std::string error;
    THEN("an unterminated quote names its line")
    {
      REQUIRE_FALSE(
          parseScript("ping\nassert a == \"open\n", commands, &error));
      REQUIRE(error.find("line 2") != std::string::npos);
      REQUIRE(commands.size() == 1);
    }
    THEN("a malformed timeout is an error, not an argument")
    {
      Command c;
      c.name = "ping";
      c.args = {"timeout=soon"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
      REQUIRE(error.find("timeout=soon") != std::string::npos);
    }
    THEN("a timeout no deadline can hold is an error too")
    {
      Command c;
      c.name = "ping";
      c.args = {"timeout=99999999999999999999"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
      REQUIRE(error.find("malformed timeout") != std::string::npos);
      c.args = {"timeout=9223372036854775807"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
      c.args = {"timeout=-5"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
    }
  }

  GIVEN("$name variables in arguments")
  {
    std::vector<Command> commands;
    REQUIRE(parseScript(
        "update-shot $lastShotId name=$title path=$root/x $ 5$ $$root",
        commands));
    auto &command = commands[0];
    const auto lookup =
        [](const std::string &name) -> std::optional<std::string> {
      if (name == "lastShotId")
        return "shot_0002";
      if (name == "title")
        return "Intro";
      if (name == "root")
        return "/data";
      return {};
    };
    std::string error;

    THEN("every name expands, alone or inside a token; a bare $ stays")
    {
      REQUIRE(expandVariables(command, lookup, &error));
      REQUIRE(command.args
          == std::vector<std::string>{
              "shot_0002", "name=Intro", "path=/data/x", "$", "5$", "$/data"});
      REQUIRE(command.name == "update-shot");
    }
    THEN("an unknown variable is an error that leaves the arguments alone")
    {
      command.args = {"$lastShotId", "$nosuch"};
      REQUIRE_FALSE(expandVariables(command, lookup, &error));
      REQUIRE(error == "unknown variable $nosuch");
      REQUIRE(
          command.args == std::vector<std::string>{"$lastShotId", "$nosuch"});
    }
  }

  GIVEN("integer arguments")
  {
    long long signedValue = 0;
    unsigned long long unsignedValue = 0;
    std::chrono::milliseconds ms;
    THEN("whole decimal numbers that fit parse, nothing else does")
    {
      REQUIRE(parseInteger("-42", signedValue));
      REQUIRE(signedValue == -42);
      REQUIRE(parseNonNegative("18446744073709551615", unsignedValue));
      REQUIRE(unsignedValue == 18446744073709551615ull);
      REQUIRE_FALSE(parseInteger("", signedValue));
      REQUIRE_FALSE(parseInteger("12x", signedValue));
      REQUIRE_FALSE(parseInteger(" 12", signedValue));
      REQUIRE_FALSE(parseInteger("99999999999999999999", signedValue));
      REQUIRE_FALSE(parseNonNegative("-1", unsignedValue));
      REQUIRE_FALSE(parseNonNegative("+1", unsignedValue));
      REQUIRE(parseMilliseconds("0", ms));
      REQUIRE(parseMilliseconds("86400000", ms));
      REQUIRE(ms == 24h);
      REQUIRE_FALSE(parseMilliseconds("9223372036854775807", ms));
    }
  }
}

SCENARIO("the test client parses its command line", "[StudioTestClient]")
{
  TestClientOptions options;
  std::string error;
  const auto argv = [](std::initializer_list<const char *> items) {
    std::vector<std::string> out{"scivisStudioTestClient"};
    out.insert(out.end(), items.begin(), items.end());
    return out;
  };

  GIVEN("every flag")
  {
    REQUIRE(parseTestClientOptions(argv({"--host",
                                       "10.0.0.1",
                                       "--port",
                                       "4242",
                                       "-e",
                                       "connect; ping",
                                       "-e",
                                       "shutdown",
                                       "--timeout",
                                       "750",
                                       "--keep-going",
                                       "--quiet-events"}),
        options,
        &error));
    THEN("all values are recorded")
    {
      REQUIRE(options.runner.host == "10.0.0.1");
      REQUIRE(options.runner.port == 4242);
      REQUIRE(options.inlineScripts
          == std::vector<std::string>{"connect; ping", "shutdown"});
      REQUIRE(options.runner.timeout == 750ms);
      REQUIRE(options.runner.keepGoing);
      REQUIRE(options.runner.quietEvents);
      REQUIRE(options.scriptPath.empty());
    }
  }

  GIVEN("defaults and malformed input")
  {
    THEN("nothing given means stdin and the default endpoint")
    {
      REQUIRE(parseTestClientOptions(argv({}), options, &error));
      REQUIRE(options.runner.host == "127.0.0.1");
      REQUIRE(options.runner.port == DEFAULT_PORT);
      REQUIRE(options.runner.timeout == 5000ms);
      REQUIRE(options.scriptPath.empty());
      REQUIRE(options.inlineScripts.empty());
    }
    THEN("--script and -e are exclusive")
    {
      REQUIRE_FALSE(parseTestClientOptions(
          argv({"--script", "a.studio", "-e", "ping"}), options, &error));
    }
    THEN("an unknown flag and a bad port are rejected by name")
    {
      REQUIRE_FALSE(parseTestClientOptions(argv({"--bogus"}), options, &error));
      REQUIRE(error.find("--bogus") != std::string::npos);
      REQUIRE_FALSE(
          parseTestClientOptions(argv({"--port", "70000"}), options, &error));
      REQUIRE(error.find("--port") != std::string::npos);
    }
    THEN("a --timeout no deadline can hold is rejected by name")
    {
      REQUIRE_FALSE(parseTestClientOptions(
          argv({"--timeout", "99999999999999999999"}), options, &error));
      REQUIRE(error.find("--timeout") != std::string::npos);
      REQUIRE_FALSE(parseTestClientOptions(
          argv({"--timeout", "9223372036854775807"}), options, &error));
    }
    THEN("the usage names every flag and every assert value")
    {
      const auto usage = testClientUsage("scivisStudioTestClient");
      for (const char *flag : {"--host",
               "--port",
               "--script",
               "-e",
               "--timeout",
               "--keep-going",
               "--quiet-events"})
        REQUIRE(usage.find(flag) != std::string::npos);
      for (const auto &name : CommandRunner::assertNames())
        REQUIRE(usage.find(name) != std::string::npos);
    }
  }
}

SCENARIO(
    "the command runner fails cleanly without a server", "[StudioTestClient]")
{
  TestSession session;

  GIVEN("a port nobody listens on")
  {
    unsigned short closedPort = 0;
    {
      vsr::network::NetworkServer probe(0);
      closedPort = probe.port();
    }
    const auto result = runScript(session,
        "connect 127.0.0.1 " + std::to_string(closedPort) + "\n"
        "assert state == NeverConnected\n");

    THEN("connect FAILs with the socket error and the state is untouched")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 1);
      REQUIRE(result.records[0].rfind("FAIL connect", 0) == 0);
      REQUIRE(result.records[0].find("connect failed") != std::string::npos);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }

  GIVEN("commands that need a connection or a frame")
  {
    const auto result = runScript(session,
        "ping\n"
        "set-frame-config 8 8\n"
        "set-param camera 0 fovy float32 1\n"
        "save-frame /nonexistent/frame.ppm\n"
        "dump-frame\n"
        "dump-project\n"
        "assert frame.width == 8\n"
        "assert project.shots == 1\n"
        "assert nonsense == 1\n"
        "assert state ~= Connected\n"
        "frobnicate\n"
        "assert state == NeverConnected\n"
        "assert scene.objects >= 0\n"
        "assert frames.received == 0\n"
        "assert errors.received == 0\n"
        "assert lastError == \"\"\n"
        "sleep 9223372036854775807\n"
        "set-param camera 0 n uint8 300\n"
        "set-param camera 0 n int8 -129\n"
        "set-param camera 0 n uint32 -1\n"
        "set-param camera 0 n int64 -9223372036854775808\n"
        "send-raw 20 -1\n"
        "send-raw 20 +f\n",
        [] {
          RunnerOptions o;
          o.keepGoing = true;
          return o;
        }());

    THEN("--keep-going runs everything, records each FAIL and still fails")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 23);
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 18);
      REQUIRE(fails[0].find("not connected") != std::string::npos);
      REQUIRE(fails[3].find("no frame") != std::string::npos);
      REQUIRE(fails[5].find("no Project Replica") != std::string::npos);
      REQUIRE(fails[8].find("unknown value 'nonsense'") != std::string::npos);
      REQUIRE(
          fails[8].find("param.<type>.<index>.<name>") != std::string::npos);
      REQUIRE(fails[9].find("unknown operator") != std::string::npos);
      REQUIRE(fails[10].find("unknown command") != std::string::npos);
      REQUIRE(hasLine(result.records, "OK assert state == NeverConnected"));
      REQUIRE(hasLine(result.records, "OK assert lastError == \"\""));
    }

    THEN("values a component cannot hold are rejected, not wrapped")
    {
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 18);
      REQUIRE(fails[11].find("usage: sleep") != std::string::npos);
      REQUIRE(
          fails[12].find("not a uint8 component: 300") != std::string::npos);
      REQUIRE(
          fails[13].find("not a int8 component: -129") != std::string::npos);
      REQUIRE(
          fails[14].find("not a uint32 component: -1") != std::string::npos);
      // int64's minimum fits; only the missing connection stops it.
      REQUIRE(fails[15].find("not connected") != std::string::npos);
      REQUIRE(fails[16].find("not a hex byte: -1") != std::string::npos);
      REQUIRE(fails[17].find("not a hex byte: +f") != std::string::npos);
    }
  }

  GIVEN("the same failing script without --keep-going")
  {
    const auto result = runScript(session, "ping\nassert state == Lost\n");
    THEN("the first FAIL ends the run")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 1);
    }
  }
}

SCENARIO("the test client refuses a server speaking another protocol version",
    "[StudioTestClient]")
{
  GIVEN("a server whose Hello carries the wrong version")
  {
    ScriptedServer server(ScriptedServer::Behaviour::MismatchedHello);
    TestSession session;
    const auto result = runScript(session,
        "connect 127.0.0.1 " + std::to_string(server.port()) + "\n"
        "assert state == NeverConnected\n");

    THEN("connect FAILs naming the mismatch after printing the Hello")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 2);
      REQUIRE(result.records[0]
          == "EVT Hello version=" + std::to_string(PROTOCOL_VERSION + 1)
              + " buildInfo=\"scripted server\"");
      REQUIRE(result.records[1].rfind("FAIL connect", 0) == 0);
      REQUIRE(result.records[1].find("protocol version mismatch")
          != std::string::npos);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }

  GIVEN("a server that answers the client's Hello with an Error and closes")
  {
    ScriptedServer server(ScriptedServer::Behaviour::RefuseOnHello);
    TestSession session;
    const auto result = runScript(
        session, "connect 127.0.0.1 " + std::to_string(server.port()) + "\n");

    THEN("the Error is heard before the close: connect FAILs as refused")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(hasLine(
          result.records, "EVT Error message=\"the scripted server refuses\""));
      REQUIRE(result.records.back().rfind("FAIL connect", 0) == 0);
      REQUIRE(result.records.back().find(
                  "server refused: the scripted server refuses")
          != std::string::npos);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }
}

SCENARIO("the test client stays unconnected until the Bootstrap completes",
    "[StudioTestClient]")
{
  GIVEN("a server that says Hello and never bootstraps")
  {
    ScriptedServer server(ScriptedServer::Behaviour::HelloOnly);
    TestSession session;
    const auto endpoint = "127.0.0.1 " + std::to_string(server.port());
    const auto result = runScript(session,
        "connect " + endpoint + " timeout=300\n"
        "assert state == NeverConnected\n"
        "ping\n"
        "connect " + endpoint + " timeout=300\n"
        "assert state == NeverConnected\n",
        [] {
          RunnerOptions o;
          o.keepGoing = true;
          return o;
        }());

    THEN(
        "the deadline FAILs connect, the state is untouched, and connect"
        " may be tried again")
    {
      REQUIRE_FALSE(result.ok);
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 3);
      REQUIRE(fails[0].rfind("FAIL connect", 0) == 0);
      REQUIRE(fails[0].find("no complete Bootstrap") != std::string::npos);
      REQUIRE(
          fails[1].find("not connected (NeverConnected)") != std::string::npos);
      REQUIRE(fails[2].rfind("FAIL connect", 0) == 0);
      REQUIRE(fails[2].find("no complete Bootstrap") != std::string::npos);
      REQUIRE(countStarting(result.records, "EVT Hello") == 2);
      REQUIRE(countStarting(result.records, "OK assert state == NeverConnected")
          == 2);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }
}

SCENARIO("the test client's liveness timers end a wait on a silent server",
    "[StudioTestClient]")
{
  GIVEN(
      "a server that bootstraps and then never speaks again, and a session"
      " with fast timings")
  {
    ScriptedServer server(ScriptedServer::Behaviour::SilentAfterBootstrap);
    SessionTimings timings;
    timings.pingAfterQuiet = 100ms;
    timings.lossAfterSilence = 400ms;
    TestSession session(timings);

    const auto started = std::chrono::steady_clock::now();
    const auto result = runScript(session,
        "connect 127.0.0.1 " + std::to_string(server.port()) + "\n"
        "assert state == Connected\n"
        "ping\n"
        "expect-pong timeout=8000\n"
        "assert state == Lost\n",
        [] {
          RunnerOptions o;
          o.keepGoing = true;
          return o;
        }());
    const auto elapsed = std::chrono::steady_clock::now() - started;

    THEN(
        "the session pings, declares the loss, and the wait FAILs at once"
        " naming it")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(hasLine(result.records, "OK assert state == Connected"));
      REQUIRE(hasLine(result.records, "OK ping"));
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 1);
      REQUIRE(fails[0]
          == "FAIL expect-pong timeout=8000: connection lost while waiting"
             " for Pong: no traffic from server for 400 ms");
      REQUIRE(hasLine(result.records, "OK assert state == Lost"));
      REQUIRE(elapsed < 4s);
      // The script's Ping and at least one liveness Ping after the quiet.
      REQUIRE(server.pingsReceived >= 2);
    }
  }
}

SCENARIO("the test client drives project ops against a fake server",
    "[StudioTestClient]")
{
  GIVEN("a fake project server and a session")
  {
    ProjectOpsServer server;
    TestSession session;
    const auto endpoint = "127.0.0.1 " + std::to_string(server.port());

    WHEN("the previous request's snapshot lags behind the next request")
    {
      server.deferSnapshots = 1;
      server.snapshotDelay = 150ms;
      // await-snapshot must not take A's late snapshot for B's.
      const auto result = runScript(session,
          "connect " + endpoint + "\n"
          "create-shot A\n"
          "create-shot B\n"
          "await-snapshot\n"
          "assert project.shots == 3\n"
          "assert project.activeShot == $lastShotId\n"
          "assert snapshots.received == 3\n"
          "disconnect\n");

      THEN("the wait ends on the snapshot that follows B's reply")
      {
        REQUIRE(result.ok);
      }
    }

    WHEN("a script runs the request, task, browse and wait commands")
    {
      const std::string script =
          "connect " + endpoint + "\n"
          "assert project.shots == 1\n"
          "assert snapshots.received == 1\n"
          // The reply is matched by request id: a stray one is looked past.
          "create-shot Intro\n"
          "await-snapshot\n"
          "assert project.shots == 2\n"
          "assert project.activeShot == shot_0002\n"
          "assert var.lastShotId == shot_0002\n"
          "assert shot.$lastShotId.name == Intro\n"
          "update-shot $lastShotId name=\"Intro Cut\" frameCount=10 fps=30 loop=off binding.dataset_0009=on\n"
          "await-snapshot\n"
          "assert shot.$lastShotId.name == \"Intro Cut\"\n"
          "assert shot.$lastShotId.frameCount == 10\n"
          "assert shot.$lastShotId.fps == 30\n"
          "assert shot.$lastShotId.loop == false\n"
          "assert shot.$lastShotId.binding.dataset_0009 == true\n"
          "expect-fail remove-shot shot_9999\n"
          "assert replies.failed == 1\n"
          "remove-shot $lastShotId\n"
          "await-snapshot\n"
          "assert project.shots == 1\n"
          // The task ends before await-task runs: the session's record is
          // what the wait consults.
          "save-project /data/p1\n"
          "assert var.lastTaskId == 1\n"
          "sleep 50\n"
          "await-task\n"
          "await-snapshot\n"
          "assert tasks.completed == 1\n"
          "assert project.dirty == false\n"
          "assert project.directory == /data/p1\n"
          "assert project.name == p1\n"
          "open-project /data/missing\n"
          "await-task expect-fail\n"
          "assert tasks.failed == 1\n"
          "expect-fail cancel-task 7\n"
          // Two requests in flight, collected in send order.
          "no-wait import-static-dataset /data/a.obj A OBJ\n"
          "no-wait import-static-dataset /data/b.obj B\n"
          "assert replies.pending == 2\n"
          "await-reply\n"
          "assert var.lastTaskId == 3\n"
          "await-reply\n"
          "assert var.lastTaskId == 4\n"
          "assert replies.pending == 0\n"
          "await-task 3\n"
          "assert var.lastDatasetId == dataset_0001\n"
          "await-task 4\n"
          "assert var.lastDatasetId == dataset_0002\n"
          "await-snapshot\n"
          "assert project.datasets == 2\n"
          "assert dataset.dataset_0002.name == B\n"
          "assert dataset.$lastDatasetId.status == Available\n"
          "list-roots\n"
          "assert var.dataRoot == /data\n"
          "list-directory $dataRoot\n"
          "assert browse.entries == 2\n"
          "expect-fail list-directory /elsewhere\n"
          "assert browse.entries == 0\n"
          "declare-file-animation-dataset Series VOLUME_ANIMATION f0 f1 set-frame-count=false\n"
          "assert var.lastDatasetId == dataset_0003\n"
          "await-snapshot\n"
          "assert dataset.dataset_0003.declared == true\n"
          "create-light-rig Studio\n"
          "assert var.lastLightRigId == lightRig_0002\n"
          "add-light $lastLightRigId point\n"
          "assert var.lastLightLayer == studio\n"
          "assert var.lastLightNode == 7\n"
          "create-camera-rig\n"
          "assert var.lastCameraRigId == cameraRig_0002\n"
          "create-color-map Warm\n"
          "assert var.lastColorMapId == colorMap_0001\n"
          "assert var.lastObjectRef == array1d:3\n"
          "await-snapshot\n"
          "assert project.colorMaps == 1\n"
          "dump-project\n"
          "assert replies.failed == 3\n"
          "assert errors.received == 0\n"
          "disconnect\n";
      const auto result = runScript(session, script);
      for (const auto &f : failLines(result.records))
        WARN(f);
      REQUIRE(result.ok);

      THEN("the records show the replies with their results and the waits")
      {
        const auto &r = result.records;
        REQUIRE(hasLine(
            r, "EVT ProjectOpReply requestId=999999 ok=true error=\"\""));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=1 ok=true error=\"\" shotId=shot_0002"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=3 ok=false error=\"shot not found\""));
        REQUIRE(hasLine(r, "OK expect-fail remove-shot shot_9999"));
        REQUIRE(hasLine(
            r, "EVT ProjectOpReply requestId=5 ok=true error=\"\" taskId=1"));
        REQUIRE(hasLine(r,
            "EVT TaskProgress taskId=1 current=0 total=0 message=\"writing\""));
        REQUIRE(hasLine(r, "EVT TaskCompleted taskId=1 message=\"\""));
        REQUIRE(hasLine(r,
            "EVT TaskFailed taskId=2 error=\"project directory does not exist\""));
        REQUIRE(hasLine(r, "OK await-task expect-fail"));
        REQUIRE(
            hasLine(r, "EVT TaskCompleted taskId=4 message=\"dataset_0002\""));
        REQUIRE(hasLine(
            r, "EVT ProjectOpReply requestId=10 ok=true error=\"\" roots=1"));
        REQUIRE(hasLine(r, "EVT DataRoot path=\"/data\""));
        REQUIRE(hasLine(r,
            "EVT DirectoryEntry name=\"runs\" kind=Directory size=0 mtime=0"));
        REQUIRE(hasLine(r,
            "EVT DirectoryEntry name=\"mesh.obj\" kind=File size=32 mtime=1700000000"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=13 ok=true error=\"\" datasetId=dataset_0003"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=15 ok=true error=\"\" lightNode=studio:7"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=17 ok=true error=\"\" colorMapId=colorMap_0001"
            " object=array1d:3"));
        REQUIRE(hasLineStarting(r,
            "EVT ProjectSnapshot activeShot=shot_0002 shots=2 datasets=0 lightRigs=1"
            " cameraRigs=1 colorMaps=0 dirty=true"));
        REQUIRE(hasLineStarting(r, "EVT Shot id=shot_0001 name=\"Shot 1\""));
        REQUIRE(
            hasLineStarting(r, "EVT Dataset id=dataset_0003 name=\"Series\""));
        REQUIRE(
            hasLineStarting(r, "EVT ColorMap id=colorMap_0001 name=\"Warm\""));
        REQUIRE(hasLine(r,
            "OK update-shot $lastShotId name=\"Intro Cut\" frameCount=10"
            " fps=30 loop=off binding.dataset_0009=on"));
      }

      THEN("the expanded ids and the whole edited Shot reached the wire")
      {
        const auto removes = server.requests<RemoveShot>();
        REQUIRE(removes.size() == 2);
        REQUIRE(removes[0].shotId == "shot_9999");
        REQUIRE(removes[1].shotId == "shot_0002");
        const auto updates = server.requests<UpdateShot>();
        REQUIRE(updates.size() == 1);
        REQUIRE(updates[0].shot.id == "shot_0002");
        REQUIRE(updates[0].shot.name == "Intro Cut");
        REQUIRE(updates[0].shot.frameCount == 10);
        REQUIRE(updates[0].shot.fps == 30.f);
        REQUIRE_FALSE(updates[0].shot.loop);
        REQUIRE(updates[0].shot.datasetBindings.size() == 1);
        REQUIRE(updates[0].shot.datasetBindings[0].datasetId == "dataset_0009");
        const auto imports = server.requests<ImportStaticDataset>();
        REQUIRE(imports.size() == 2);
        REQUIRE(imports[0].importerType == vsr::io::ImporterType::OBJ);
        REQUIRE(imports[1].importerType == vsr::io::ImporterType::NONE);
        REQUIRE(imports[1].sourcePath == "/data/b.obj");
        const auto declares = server.requests<DeclareFileAnimationDataset>();
        REQUIRE(declares.size() == 1);
        REQUIRE(declares[0].sourceList == std::vector<std::string>{"f0", "f1"});
        REQUIRE_FALSE(declares[0].setActiveShotFrameCount);
        const auto listings = server.requests<ListDirectory>();
        REQUIRE(listings.size() == 2);
        REQUIRE(listings[0].directory == "/data");
        const auto cancels = server.requests<CancelTask>();
        REQUIRE(cancels.size() == 1);
        REQUIRE(cancels[0].taskId == 7);
      }
    }

    WHEN("a script runs the playback, pick, viewport and histogram commands")
    {
      const std::string script =
          "connect " + endpoint + "\n"
          "assert shot.active.playing == false\n"
          "assert shot.active.frameCount == @shot.shot_0001.frameCount\n"
          // The SetPlaying's snapshot and the auto-stop's arrive together;
          // each await-snapshot consumes one.
          "set-playing active on\n"
          "await-snapshot\n"
          "await-snapshot\n"
          "assert snapshots.received == 3\n"
          "assert shot.active.playing == false\n"
          "assert shot.shot_0001.currentFrame == 5\n"
          "expect-fail set-playing shot_9999 on\n"
          "assert lastReplyError contains active\n"
          "assert replies.failed == 1\n"
          // Frames 0, 1, 2, 0: three advances, the wrap is not a step.
          "start-rendering\n"
          "await-frame-advance 3\n"
          "assert frames.advanced == 3\n"
          "assert frames.maxStep == 1\n"
          "assert frame.frame == 0\n"
          "set-time active 7\n"
          "await-frame-at 7\n"
          "assert frame.frame == 7\n"
          "assert frames.advanced == 4\n"
          // A forward scrub is a forward step; only backward ones are not.
          "assert frames.maxStep == 7\n"
          "set-time shot_0001 99\n"
          "await-warning\n"
          "assert warnings.received == 1\n"
          "assert lastWarning contains load\n"
          "pick 0 0\n"
          "assert pick.hit == false\n"
          "assert pick.objectType == none\n"
          "assert pick.objectIndex == none\n"
          "pick 10 5\n"
          "assert pick.hit == true\n"
          "assert pick.objectType == surface\n"
          "assert pick.objectIndex == 4\n"
          "assert pick.worldPosition == \"0.5 0.25 -1\"\n"
          "assert var.lastPickType == surface\n"
          "assert var.lastPickIndex == 4\n"
          "set-outline $lastPickType $lastPickIndex\n"
          "set-outline volume:2\n"
          "set-outline none\n"
          "set-outline\n"
          "viewport-settings visualizeAOV=depth depthVisualMinimum=0 depthVisualMaximum=10\n"
          "viewport-settings showWorldBounds=on worldBoundsWidth=2 worldBoundsColor=1,0,0,1\n"
          "viewport-settings\n"
          "request-array-histogram array 0 3\n"
          "assert histogram.bins == 3\n"
          "assert histogram.total == 6\n"
          "assert histogram.min == 0\n"
          "assert histogram.max == 1\n"
          "expect-fail request-array-histogram array:5 16\n"
          "assert lastReplyError contains scalar\n"
          "assert errors.received == 0\n"
          "disconnect\n";
      const auto result = runScript(session, script);
      for (const auto &f : failLines(result.records))
        WARN(f);
      REQUIRE(result.ok);

      THEN("the records show the snapshot's time, the frames and the replies")
      {
        const auto &r = result.records;
        REQUIRE(hasLine(r,
            "EVT ProjectSnapshot activeShot=shot_0001 shots=1 datasets=0"
            " lightRigs=1 cameraRigs=1 colorMaps=0 dirty=true playing=true"
            " currentFrame=0"));
        REQUIRE(hasLine(r,
            "EVT ProjectSnapshot activeShot=shot_0001 shots=1 datasets=0"
            " lightRigs=1 cameraRigs=1 colorMaps=0 dirty=true playing=false"
            " currentFrame=5"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=2 ok=false error=\"shot 'shot_9999'"
            " is not the active shot\""));
        REQUIRE(countStarting(r,
                    "EVT Frame width=2 height=2 encoding=Raw"
                    " pixelFormat=RGBA8_sRGB shotId=shot_0001 frame=0 bytes=16")
            == 2);
        REQUIRE(hasLine(r,
            "EVT Frame width=2 height=2 encoding=Raw pixelFormat=RGBA8_sRGB"
            " shotId=shot_0001 frame=7 bytes=16"));
        REQUIRE(hasLine(r,
            "EVT TimeAdvanceWarning shotId=shot_0001 frame=99"
            " message=\"frame 99 failed to load\""));
        REQUIRE(countStarting(r,
                    "EVT PickReply requestId=777 hit=false"
                    " worldPosition=\"0 0 0\" objectType=none objectIndex=none")
            == 2);
        REQUIRE(hasLine(r,
            "EVT PickReply requestId=3 hit=false worldPosition=\"0 0 0\""
            " objectType=none objectIndex=none"));
        REQUIRE(hasLine(r,
            "EVT PickReply requestId=4 hit=true worldPosition=\"0.5 0.25 -1\""
            " objectType=surface objectIndex=4"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=5 ok=true error=\"\" bins=3 min=0"
            " max=1"));
        REQUIRE(hasLine(r, "OK set-outline $lastPickType $lastPickIndex"));
      }

      THEN("the wire carries the resolved ids, pixels and composed settings")
      {
        const auto playing = server.requests<SetPlaying>();
        REQUIRE(playing.size() == 2);
        REQUIRE(playing[0].shotId == "shot_0001");
        REQUIRE(playing[0].playing);
        REQUIRE(playing[1].shotId == "shot_9999");
        const auto times = server.requests<SetTime>();
        REQUIRE(times.size() == 2);
        REQUIRE(times[0].shotId == "shot_0001");
        REQUIRE(times[0].frame == 7);
        REQUIRE(times[1].frame == 99);
        const auto picks = server.requests<Pick>();
        REQUIRE(picks.size() == 2);
        REQUIRE(picks[0].x == 0);
        REQUIRE(picks[0].y == 0);
        REQUIRE(picks[1].x == 10);
        REQUIRE(picks[1].y == 5);
        const auto outlines = server.requests<SetOutline>();
        REQUIRE(outlines.size() == 4);
        REQUIRE(outlines[0].objectIdentity);
        REQUIRE(outlines[0].objectIdentity->type == ANARI_SURFACE);
        REQUIRE(outlines[0].objectIdentity->objectIndex == 4);
        REQUIRE(outlines[1].objectIdentity);
        REQUIRE(outlines[1].objectIdentity->type == ANARI_VOLUME);
        REQUIRE(outlines[1].objectIdentity->objectIndex == 2);
        REQUIRE_FALSE(outlines[2].objectIdentity);
        REQUIRE_FALSE(outlines[3].objectIdentity);
        const auto settings = server.requests<ViewportSettings>();
        REQUIRE(settings.size() == 3);
        REQUIRE(settings[0].visualizeAOV == vsr::rendering::AOVType::DEPTH);
        REQUIRE(settings[0].depthVisualMaximum == 10.f);
        REQUIRE_FALSE(settings[0].showWorldBounds);
        REQUIRE(settings[0].highlightSelection); // the default, sent whole
        REQUIRE(settings[1].visualizeAOV == vsr::rendering::AOVType::DEPTH);
        REQUIRE(settings[1].depthVisualMaximum == 10.f);
        REQUIRE(settings[1].showWorldBounds);
        REQUIRE(settings[1].worldBoundsWidth == 2);
        REQUIRE(settings[1].worldBoundsColor.x == 1.f);
        REQUIRE(settings[1].worldBoundsColor.y == 0.f);
        REQUIRE(settings[1].worldBoundsColor.w == 1.f);
        REQUIRE(settings[2].showWorldBounds);
        REQUIRE(settings[2].visualizeAOV == vsr::rendering::AOVType::DEPTH);
        const auto histograms = server.requests<RequestArrayHistogram>();
        REQUIRE(histograms.size() == 2);
        REQUIRE(histograms[0].array.type == ANARI_ARRAY);
        REQUIRE(histograms[0].array.objectIndex == 0);
        REQUIRE(histograms[0].binCount == 3);
        REQUIRE(histograms[1].array.objectIndex == 5);
        REQUIRE(histograms[1].binCount == 16);
      }
    }

    WHEN("a script misuses the playback, pick and viewport commands")
    {
      const auto result = runScript(session,
          "connect " + endpoint + "\n"
          "set-playing active\n"
          "set-playing shot_0001 maybe\n"
          "set-time active x\n"
          "await-frame-at\n"
          "await-frame-advance two\n"
          "pick 1\n"
          "set-outline camera\n"
          "set-outline camera 1 extra\n"
          "viewport-settings nokey\n"
          "viewport-settings bogus=1\n"
          "viewport-settings worldBoundsColor=1,2\n"
          "viewport-settings visualizeAOV=SHINY\n"
          "request-array-histogram array 0\n"
          "find-object camera first\n"
          "find-object camera name=Main\n"
          "find-object camera last\n"
          "assert pick.hit == false\n"
          "expect-fail request-array-histogram array 9 4\n"
          "assert histogram.bins == 0\n"
          "assert shot.active.bogus == 1\n"
          "assert frames.advanced == @nosuch\n"
          "assert frames.advanced == @frames.received\n"
          "assert frames.advanced == 0\n"
          "disconnect\n",
          [] {
            RunnerOptions o;
            o.keepGoing = true;
            return o;
          }());

      THEN("each FAILs by name and nothing reached the wire")
      {
        REQUIRE_FALSE(result.ok);
        const auto fails = failLines(result.records);
        REQUIRE(fails.size() == 20);
        REQUIRE(fails[0].find("usage: set-playing") != std::string::npos);
        REQUIRE(fails[1].find("usage: set-playing") != std::string::npos);
        REQUIRE(fails[2].find("usage: set-time") != std::string::npos);
        REQUIRE(fails[3].find("usage: await-frame-at") != std::string::npos);
        REQUIRE(
            fails[4].find("usage: await-frame-advance") != std::string::npos);
        REQUIRE(fails[5].find("usage: pick") != std::string::npos);
        REQUIRE(fails[6].find("usage: set-outline") != std::string::npos);
        REQUIRE(fails[7].find("usage: set-outline") != std::string::npos);
        REQUIRE(
            fails[8].find("usage: viewport-settings") != std::string::npos);
        REQUIRE(fails[9].find("unknown viewport setting 'bogus'")
            != std::string::npos);
        REQUIRE(fails[10].find("not a valid worldBoundsColor")
            != std::string::npos);
        REQUIRE(
            fails[11].find("not a valid visualizeAOV") != std::string::npos);
        REQUIRE(fails[12].find("usage: request-array-histogram")
            != std::string::npos);
        // The fake bootstraps no scene, so the mirror has nothing to find.
        REQUIRE(fails[13].find("no camera in the mirror") != std::string::npos);
        REQUIRE(fails[14].find("no camera named \"Main\" in the mirror")
            != std::string::npos);
        REQUIRE(fails[15].find("usage: find-object") != std::string::npos);
        REQUIRE(fails[16].find("no pick has been answered") != std::string::npos);
        // A refused histogram request leaves no histogram to assert on.
        REQUIRE(
            fails[17].find("no histogram has been answered") != std::string::npos);
        REQUIRE(fails[18].find("unknown shot field 'bogus'") != std::string::npos);
        REQUIRE(fails[19].find("unknown value 'nosuch'") != std::string::npos);
        REQUIRE(hasLine(
            result.records, "OK assert frames.advanced == @frames.received"));
        REQUIRE(hasLine(result.records, "OK assert frames.advanced == 0"));
        REQUIRE(server.requests<SetPlaying>().empty());
        REQUIRE(server.requests<SetTime>().empty());
        REQUIRE(server.requests<Pick>().empty());
        REQUIRE(server.requests<SetOutline>().empty());
        REQUIRE(server.requests<ViewportSettings>().empty());
        REQUIRE(server.requests<RequestArrayHistogram>().size() == 1);
      }
    }

    WHEN("a script misuses the prefixes, variables and waits")
    {
      const auto result = runScript(session,
          "connect " + endpoint + "\n"
          "assert $nosuch == 1\n"
          "expect-fail ping\n"
          "no-wait await-task\n"
          "await-reply\n"
          "await-task\n"
          "await-task 99 timeout=100\n"
          "expect-fail create-shot Fine\n"
          "update-shot shot_9999 name=x\n"
          "update-shot shot_0001 bogus=1\n"
          "update-shot shot_0001 frameCount=abc\n"
          "update-shot shot_0001 playing=true\n"
          "import-static-dataset /data/x.obj X TRIANGLES\n"
          "remove-dataset dataset_0001 keep\n"
          "await-snapshot timeout=100\n"
          "expect-fail\n"
          "create-shot\n"
          "assert var.lastShotId == shot_0003\n"
          "disconnect\n",
          [] {
            RunnerOptions o;
            o.keepGoing = true;
            return o;
          }());

      THEN("each FAILs by name and the rest still runs")
      {
        REQUIRE_FALSE(result.ok);
        const auto fails = failLines(result.records);
        REQUIRE(fails.size() == 14);
        REQUIRE(
            fails[0] == "FAIL assert $nosuch == 1: unknown variable $nosuch");
        REQUIRE(fails[1].find("expect-fail applies to request commands")
            != std::string::npos);
        REQUIRE(fails[2].find("no-wait applies to request commands")
            != std::string::npos);
        REQUIRE(fails[3].find("no no-wait request") != std::string::npos);
        REQUIRE(fails[4].find("$lastTaskId is unset") != std::string::npos);
        REQUIRE(fails[5] == "FAIL await-task 99 timeout=100: no the end of task 99"
                            " within 100 ms");
        REQUIRE(
            fails[6].find("expected the request to fail") != std::string::npos);
        REQUIRE(fails[7].find("no shot 'shot_9999'") != std::string::npos);
        REQUIRE(
            fails[8].find("unknown Shot field 'bogus'") != std::string::npos);
        REQUIRE(
            fails[9].find("not a valid frameCount: abc") != std::string::npos);
        REQUIRE(
            fails[10].find("playing is playback state") != std::string::npos);
        REQUIRE(fails[11].find("unknown importer 'TRIANGLES'")
            != std::string::npos);
        REQUIRE(fails[12].find("usage: remove-dataset") != std::string::npos);
        REQUIRE(fails[13].find("usage: expect-fail") != std::string::npos);
        // The expect-fail create-shot above still created a shot; the last
        // one without the prefix is the third.
        REQUIRE(
            hasLine(result.records, "OK assert var.lastShotId == shot_0003"));
        // No request went out without a snapshot to await, so this one holds.
        REQUIRE(hasLine(result.records, "OK await-snapshot timeout=100"));
        REQUIRE(hasLine(result.records, "OK disconnect"));
      }
    }
  }
}

SCENARIO(
    "the test client runs the milestone-3 command surface against a"
    " StudioServer",
    "[StudioTestClient]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the server-backed tests");
    return;
  }

  GIVEN("a server on a fresh project and a session")
  {
    auto server = std::make_unique<RunningServer>(0);
    REQUIRE(server->started);
    REQUIRE(server->transformNode != VSR_INVALID_INDEX);
    const auto port = server->port();
    const auto endpoint = "127.0.0.1 " + std::to_string(port);

    const auto &project = server->project();
    const auto *shot = project::activeShot(project);
    REQUIRE(shot);
    const auto cameraIndex = std::to_string(shot->camera.objectIndex);
    const auto camera = "camera " + cameraIndex;
    const auto cameraParam = "param.camera." + cameraIndex + ".";
    const auto *cameraObject =
        server->scene().getObject(ANARI_CAMERA, shot->camera.objectIndex);
    REQUIRE(cameraObject);
    const std::string cameraName = cameraObject->name();
    const auto ppm = (std::filesystem::temp_directory_path()
        / ("vsrStudioTestClient-" + std::to_string(port) + ".ppm"))
                         .string();

    TestSession session;

    WHEN("a script exercises every command")
    {
      const std::string script =
          "connect " + endpoint + "\n"
          "assert state == Connected\n"
          "assert scene.layers >= 1\n"
          "assert scene.cameras >= 1\n"
          "assert scene.renderers >= 1\n"
          "assert scene.objects > 0\n"
          "assert project.shots == 1\n"
          "assert project.activeShot == " + project.activeShotId + "\n"
          "assert project.datasets == 0\n"
          "assert frameConfig.width == " + std::to_string(shot->renderSettings.width) + "\n"
          "dump-scene\n"
          "dump-layers\n"
          "dump-project\n"
          "find-object camera first\n"
          "assert var.lastObjectType == camera\n"
          "find-object camera name=" + cameraName + "\n"
          "assert var.lastObjectIndex == " + cameraIndex + "\n"
          "assert var.lastObjectRef == camera:" + cameraIndex + "\n"
          "assert shot.active.playing == false\n"
          "assert shot.active.currentFrame == " + std::to_string(shot->currentFrame) + "\n"
          "ping\n"
          "expect-pong\n"
          "set-encodings raw\n"
          "set-frame-config 32 24\n"
          "assert frameConfig.width == 32\n"
          "assert frameConfig.height == 24\n"
          "start-rendering\n"
          "await-frame 2\n"
          "assert frames.received >= 2\n"
          "assert frame.width == 32\n"
          "assert frame.height == 24\n"
          "assert frame.encoding == Raw\n"
          "assert frame.shotId == " + project.activeShotId + "\n"
          "assert frame.frame == 0\n"
          "dump-frame\n"
          "save-frame " + ppm + "\n"
          "stop-rendering\n"
          "set-param " + camera + " fovy float32 0.9\n"
          "assert " + cameraParam + "fovy == 0.9\n"
          "set-param " + camera + " position float32_vec3 1 2 3\n"
          "assert " + cameraParam + "position == \"1 2 3\"\n"
          "set-param " + camera + " note string \"hello world\"\n"
          "assert " + cameraParam + "note == \"hello world\"\n"
          "assert " + cameraParam + "note contains world\n"
          "set-param " + camera + " flag bool true\n"
          "assert " + cameraParam + "flag == true\n"
          "set-param " + camera + " count int32 -7\n"
          "assert " + cameraParam + "count < 0\n"
          "set-param " + camera + " ucount uint32 7\n"
          "assert " + cameraParam + "ucount >= 7\n"
          "set-param " + camera + " uv float32_vec2 0.5 0.25\n"
          "assert " + cameraParam + "uv == \"0.5 0.25\"\n"
          "set-param " + camera + " tint float32_vec4 1 0 0 1\n"
          "assert " + cameraParam + "tint != \"0 0 0 0\"\n"
          "assert " + cameraParam + "tint == \"1 0 0 1\"\n"
          "set-param " + camera + " precise float32 0.123456789\n"
          "assert " + cameraParam + "precise == 0.123456789\n"
          "set-param " + camera + " big float32 1234567\n"
          "assert " + cameraParam + "big == 1234567\n"
          "set-param " + camera + " wide float64 0.1234567890123\n"
          "assert " + cameraParam + "wide == 0.1234567890123\n"
          "set-param " + camera + " tiny int8 -128\n"
          "assert " + cameraParam + "tiny == -128\n"
          "remove-param " + camera + " note\n"
          "set-node-transform studio " + std::to_string(server->transformNode)
          + " 2 0 0 0 0 2 0 0 0 0 2 0 5 6 7 1\n"
          "send-raw 255\n"
          "expect-error \"unknown message type 255\"\n"
          "send-raw 0\n"
          "expect-error \"unknown message type 0\"\n"
          "ping\n"
          "send-raw 60 0a0b 0c\n"
          "expect-error \"not implemented\"\n"
          "assert errors.received == 3\n"
          "assert lastError contains RenderShot\n"
          "sleep 20\n"
          "disconnect\n"
          "assert state == Disconnected\n"
          "assert scene.objects == 0\n"
          "reconnect\n"
          "assert state == Connected\n"
          "assert scene.objects > 0\n"
          "shutdown\n"
          "assert state == Disconnected\n";
      const auto result = runScript(session, script);
      // Every branch below assumes the whole script ran; name the FAIL lines
      // when it did not.
      for (const auto &f : failLines(result.records))
        WARN(f);
      REQUIRE(result.ok);

      THEN("every command records OK and the events tell the story")
      {
        const auto &r = result.records;
        REQUIRE(hasLineStarting(r,
            "EVT Hello version=" + std::to_string(PROTOCOL_VERSION)
                + " buildInfo=\"scivisStudioServer/helide\""));
        REQUIRE(countStarting(r, "EVT BootstrapBegin") == 2);
        REQUIRE(countStarting(r, "EVT BootstrapEnd") == 2);
        REQUIRE(countStarting(r, "EVT TransferScene objects=") == 2);
        REQUIRE(hasLineStarting(r, "EVT TransferLayer objects="));
        REQUIRE(hasLineStarting(r,
            "EVT ProjectSnapshot activeShot=" + project.activeShotId
                + " shots=1 datasets=0"));
        REQUIRE(hasLine(r, "OK connect " + endpoint));
        // One Pong for expect-pong, one that expect-error had to look past.
        REQUIRE(countStarting(r, "EVT Pong") == 2);
        REQUIRE(hasLine(r, "OK expect-pong"));
        REQUIRE(hasLine(r, "EVT FrameConfig width=32 height=24"));
        REQUIRE(countStarting(r,
                    "EVT Frame width=32 height=24 encoding=Raw"
                    " pixelFormat=RGBA8_sRGB shotId="
                        + project.activeShotId + " frame=0 bytes=3072")
            >= 3); // two awaited, one from dump-frame, maybe more in flight
        REQUIRE(
            hasLineStarting(r, "EVT Object type=camera index=" + cameraIndex));
        REQUIRE(hasLineStarting(r, "EVT Object type=renderer"));
        REQUIRE(countStarting(r,
                    "EVT Object type=camera index=" + cameraIndex + " subtype=")
            >= 2); // dump-scene's line and find-object's
        REQUIRE(hasLineStarting(r, "EVT Layer index=0 name=\"studio\""));
        REQUIRE(hasLineStarting(r,
            "EVT Project name=\"" + project.name + "\" activeShot="
                + project.activeShotId + " shots=1 datasets=0"));
        REQUIRE(hasLine(r, "EVT Error message=\"unknown message type 255\""));
        REQUIRE(hasLine(r, "EVT Error message=\"unknown message type 0\""));
        REQUIRE(hasLineStarting(
            r, "EVT Error message=\"RenderShot is not implemented"));
        REQUIRE(hasLine(r, "OK expect-error \"not implemented\""));
        REQUIRE(hasLine(r, "OK assert state == Disconnected"));
        REQUIRE(r.back() == "OK assert state == Disconnected");
      }

      THEN("the saved frame is a binary P6 PPM of the requested size")
      {
        std::ifstream file(ppm, std::ios::binary);
        REQUIRE(file);
        std::string magic;
        int width = 0;
        int height = 0;
        int maxval = 0;
        file >> magic >> width >> height >> maxval;
        REQUIRE(magic == "P6");
        REQUIRE(width == 32);
        REQUIRE(height == 24);
        REQUIRE(maxval == 255);
        file.get(); // the single whitespace after maxval
        std::vector<char> rgb(32 * 24 * 3);
        file.read(rgb.data(), std::streamsize(rgb.size()));
        REQUIRE(file.gcount() == std::streamsize(rgb.size()));
        std::filesystem::remove(ppm);
      }

      THEN("the edits reached the server and the shutdown ended its run()")
      {
        REQUIRE(waitFor([&] { return server->finished.load(); }));
        auto &scene = server->scene();
        auto *cam = scene.getObject(ANARI_CAMERA, shot->camera.objectIndex);
        REQUIRE(cam);
        REQUIRE(cam->parameterValueAs<float>("fovy") == 0.9f);
        REQUIRE(cam->parameter("note") == nullptr);
        REQUIRE(cam->parameterValueAs<int>("count") == -7);
        auto *layer = scene.layer("studio");
        REQUIRE(layer);
        auto node = layer->at(server->transformNode);
        REQUIRE(node);
        const auto xfm = (*node)->getTransform();
        REQUIRE(xfm[0][0] == 2.f);
        REQUIRE(xfm[3][0] == 5.f);
        REQUIRE(xfm[3][2] == 7.f);
      }
    }

    WHEN("the server answers a command nobody wrote as an expectation")
    {
      const auto result = runScript(session,
          "connect " + endpoint + "\n"
          "send-raw 60\n"
          "sleep 300\n"
          "assert errors.received == 1\n"
          "disconnect\n",
          [] {
            RunnerOptions o;
            o.keepGoing = true;
            return o;
          }());

      THEN("the Error FAILs the command in flight and the script")
      {
        REQUIRE_FALSE(result.ok);
        const auto fails = failLines(result.records);
        REQUIRE(fails.size() == 1);
        REQUIRE(fails[0].rfind("FAIL sleep 300: server answered Error"
                               " \"RenderShot is not implemented",
                    0)
            == 0);
        REQUIRE(hasLine(result.records, "OK assert errors.received == 1"));
        REQUIRE(hasLine(result.records, "OK disconnect"));
      }
    }

    WHEN("the server goes away and comes back")
    {
      auto first = runScript(session,
          "connect " + endpoint + "\n"
          "assert scene.objects > 0\n"
          "await-lost timeout=200\n");
      REQUIRE_FALSE(first.ok);
      REQUIRE(first.records.back().rfind("FAIL await-lost", 0) == 0);
      REQUIRE(
          first.records.back().find("still Connected") != std::string::npos);

      server->stop();
      REQUIRE(server->finished.load());

      const auto lost = runScript(session,
          "await-lost\n"
          "assert state == Lost\n"
          "assert scene.objects > 0\n"
          "assert project.shots == 1\n"
          "ping\n");

      THEN("await-lost sees Lost with the mirror frozen, and nothing sends")
      {
        REQUIRE_FALSE(lost.ok);
        REQUIRE(hasLine(lost.records, "OK await-lost"));
        REQUIRE(hasLine(lost.records, "OK assert state == Lost"));
        REQUIRE(hasLine(lost.records, "OK assert scene.objects > 0"));
        REQUIRE(hasLine(lost.records, "OK assert project.shots == 1"));
        REQUIRE(lost.records.back().rfind("FAIL ping", 0) == 0);
        REQUIRE(lost.records.back().find("Lost") != std::string::npos);

        AND_THEN("reconnect retries until a restarted server listens")
        {
          // The server comes back while reconnect is already being refused.
          std::thread restarter([&] {
            std::this_thread::sleep_for(500ms);
            server = std::make_unique<RunningServer>(int(port));
          });
          const auto back = runScript(session,
              "reconnect timeout=20000\n"
              "assert state == Connected\n"
              "assert scene.objects > 0\n"
              "ping\n"
              "expect-pong\n"
              "shutdown\n"
              "assert state == Disconnected\n"
              "reconnect timeout=300\n");
          restarter.join();
          REQUIRE(server->started);
          REQUIRE_FALSE(back.ok);
          REQUIRE(hasLine(back.records, "OK reconnect timeout=20000"));
          REQUIRE(hasLine(back.records, "OK assert state == Connected"));
          REQUIRE(hasLine(back.records, "OK expect-pong"));
          REQUIRE(hasLine(back.records, "OK shutdown"));
          REQUIRE(hasLine(back.records, "OK assert state == Disconnected"));
          REQUIRE(
              back.records.back().rfind("FAIL reconnect timeout=300", 0) == 0);
          REQUIRE(
              back.records.back().find("connect failed") != std::string::npos);
          REQUIRE(waitFor([&] { return server->finished.load(); }));
        }
      }
    }
  }
}
