// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "StudioServerTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "DataRoots.h"
#include "ServerOptions.h"
#include "ServerTaskRunner.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "FrameMessages.h"
#include "ProjectOpReply.h"
#include "ProjectRequests.h"
#include "PlaybackMessages.h"
#include "ProjectSnapshot.h"
#include "SessionMessages.h"
#include "ShotRigRequests.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
#include "TaskMessages.h"
// vsr_scivis_studio_model
#include "ProjectSerialization.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::protocol;
using vsr::network::Message;
using namespace std::chrono_literals;

namespace {

// A Data Root with the files the scenarios browse and import: a triangle
// mesh, a larger grid mesh for tasks that must stay queued long enough to be
// cancelled, a plain directory and a directory marked as a project.
struct DataRootFixture
{
  DataRootFixture();
  ~DataRootFixture();

  std::filesystem::path root;
  std::filesystem::path mesh;
  std::filesystem::path grid;
  std::filesystem::path plainDir;
  std::filesystem::path projectDir;
};

void writeTriangleObj(const std::filesystem::path &file)
{
  std::ofstream out(file);
  out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
}

void writeGridObj(const std::filesystem::path &file, int n)
{
  std::ofstream out(file);
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i)
      out << "v " << i << ' ' << j << " 0\n";
  const auto index = [n](int i, int j) { return j * (n + 1) + i + 1; };
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      out << "f " << index(i, j) << ' ' << index(i + 1, j) << ' '
          << index(i + 1, j + 1) << '\n';
      out << "f " << index(i, j) << ' ' << index(i + 1, j + 1) << ' '
          << index(i, j + 1) << '\n';
    }
  }
}

DataRootFixture::DataRootFixture()
{
  static int counter = 0;
  root = std::filesystem::temp_directory_path()
      / ("vsr_studio_server_project_ops_" + std::to_string(++counter));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  mesh = root / "mesh.obj";
  writeTriangleObj(mesh);
  grid = root / "grid.obj";
  writeGridObj(grid, 60);
  plainDir = root / "plain";
  std::filesystem::create_directories(plainDir);
  projectDir = root / "proj";
  std::filesystem::create_directories(projectDir);
  std::ofstream(projectDir / PROJECT_MANIFEST_FILENAME) << "";
}

DataRootFixture::~DataRootFixture()
{
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

// A started server on a fresh project with one bootstrapped client.
struct Session
{
  explicit Session(const std::filesystem::path &dataRoot);

  // Sends `req` with a fresh requestId and waits for its reply.
  template <typename R>
  ProjectOpReply request(R req);
  // Waits until `n` snapshots have arrived since the last clear().
  bool waitForSnapshots(size_t n);
  ProjectSnapshot latestSnapshot();
  // The shot the newest Frame was rendered for.
  std::optional<ShotID> latestFrameShot();
  bool waitForFrameOf(const ShotID &shotId);

  ServerOptions options;
  std::unique_ptr<StudioServer> server;
  std::unique_ptr<ServerLoop> loop;
  TestClient client;
  uint64_t nextRequestId{1};
};

Session::Session(const std::filesystem::path &dataRoot)
{
  options.port = 0;
  options.library = "helide";
  options.dataRoots = {dataRoot};
  server = std::make_unique<StudioServer>(options);
  std::string error;
  REQUIRE(server->start(&error));
  loop = std::make_unique<ServerLoop>(server.get());
  client.connect(server->port());
  REQUIRE(client.waitForCount(StudioMessageType::Hello, 1));
  client.send(Hello{});
  REQUIRE(client.waitForCount(StudioMessageType::BootstrapEnd, 1));
  REQUIRE(waitFor(
      [&] { return server->sessionState() == SessionState::Connected; }));
  client.clear();
}

template <typename R>
ProjectOpReply Session::request(R req)
{
  req.requestId = nextRequestId++;
  client.send(req);
  const auto reply = client.waitForReply(req.requestId);
  REQUIRE(reply);
  return *reply;
}

bool Session::waitForSnapshots(size_t n)
{
  return client.waitForCount(StudioMessageType::ProjectSnapshot, n);
}

ProjectSnapshot Session::latestSnapshot()
{
  const auto snapshot = client.lastDecoded<ProjectSnapshot>();
  REQUIRE(snapshot);
  return *snapshot;
}

std::optional<ShotID> Session::latestFrameShot()
{
  const auto msg = client.last(StudioMessageType::Frame);
  if (msg.header.type != uint8_t(StudioMessageType::Frame))
    return {};
  const auto frame = decodeFrame(msg);
  if (!frame)
    return {};
  return frame->header.shotId;
}

bool Session::waitForFrameOf(const ShotID &shotId)
{
  return waitFor([&] { return latestFrameShot() == shotId; });
}

// How a task ended: its completion message or its error.
struct TaskEnd
{
  bool completed{false};
  std::string text;
};

std::optional<TaskEnd> waitForTaskEnd(TestClient &client, uint64_t taskId)
{
  std::optional<TaskEnd> end;
  waitFor([&] {
    for (const auto &msg : client.messages()) {
      if (auto completed = decode<TaskCompleted>(msg);
          completed && completed->taskId == taskId) {
        end = TaskEnd{true, completed->message};
        return true;
      }
      if (auto failed = decode<TaskFailed>(msg);
          failed && failed->taskId == taskId) {
        end = TaskEnd{false, failed->error};
        return true;
      }
    }
    return false;
  });
  return end;
}

uint64_t startedTaskId(const ProjectOpReply &reply)
{
  REQUIRE(reply.ok);
  const auto started = results<TaskStartedResult>(reply);
  REQUIRE(started);
  REQUIRE(started->taskId != 0);
  return started->taskId;
}

const DirectoryEntry *findEntry(
    const ListDirectoryResult &listing, const std::string &name)
{
  auto itr = std::find_if(listing.entries.begin(),
      listing.entries.end(),
      [&](const DirectoryEntry &e) { return e.name == name; });
  return itr == listing.entries.end() ? nullptr : &*itr;
}

const Dataset *findDatasetNamed(const Project &project, const std::string &name)
{
  auto itr = std::find_if(project.datasets.begin(),
      project.datasets.end(),
      [&](const Dataset &d) { return d.name == name; });
  return itr == project.datasets.end() ? nullptr : &*itr;
}

} // namespace

SCENARIO("DataRoots admit only canonical paths inside a root", "[StudioServer]")
{
  const auto base =
      std::filesystem::temp_directory_path() / "vsr_studio_data_roots_test";
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base / "data" / "sub");
  std::filesystem::create_directories(base / "data2");
  std::filesystem::create_directories(base / "elsewhere" / "proj");

  GIVEN("one root and a project directory outside it")
  {
    DataRoots roots({base / "data"}, base / "elsewhere" / "proj");

    THEN("both are roots, canonicalized")
    {
      REQUIRE(roots.roots().size() == 2);
      REQUIRE(roots.roots()[0] == canonicalizePath(base / "data"));
      REQUIRE(
          roots.roots()[1] == canonicalizePath(base / "elsewhere" / "proj"));
    }

    THEN("paths inside a root resolve, even when their tail does not exist")
    {
      std::string error;
      REQUIRE(roots.resolve(base / "data" / "sub" / "new.vsr", &error));
      REQUIRE(roots.resolve(base / "data", &error));
      REQUIRE(roots.resolve(base / "data" / "sub" / ".." / "x", &error));
      REQUIRE(roots.isInside(base / "elsewhere" / "proj" / "datasets"));
    }

    THEN("a sibling sharing the root's prefix is outside")
    {
      std::string error;
      REQUIRE_FALSE(roots.resolve(base / "data2" / "a.obj", &error));
      REQUIRE(error.find("Data Roots") != std::string::npos);
      REQUIRE(error.find("data2") != std::string::npos);
      REQUIRE_FALSE(roots.isInside(base / "elsewhere"));
      REQUIRE_FALSE(roots.isInside(base / "data" / ".." / "data2"));
    }

    THEN("relative and empty paths are refused")
    {
      std::string error;
      REQUIRE_FALSE(roots.resolve("relative/file.obj", &error));
      REQUIRE(error.find("absolute") != std::string::npos);
      REQUIRE_FALSE(roots.resolve({}, &error));
    }
  }

  GIVEN("a project directory already inside a root")
  {
    DataRoots roots({base / "data"}, base / "data" / "sub");

    THEN("no second root is added")
    {
      REQUIRE(roots.roots().size() == 1);
    }
  }

  std::filesystem::remove_all(base);
}

SCENARIO("ServerTaskRunner runs queued tasks one at a time", "[StudioServer]")
{
  std::vector<Message> sent;
  ServerTaskRunner runner([&](Message &&msg) { sent.push_back(msg); });

  GIVEN("three queued tasks")
  {
    std::string cancelRunningError;
    uint64_t first = 0;
    first = runner.enqueue("first", [&](const TaskProgressFunction &progress) {
      progress("half way");
      // The running task cannot cancel itself (or be cancelled).
      REQUIRE_FALSE(runner.cancel(first, &cancelRunningError));
      REQUIRE(runner.running());
      TaskResult result;
      result.message = "dataset_0001";
      result.projectChanged = true;
      return result;
    });
    const auto second =
        runner.enqueue("second", [&](const TaskProgressFunction &) {
          TaskResult result;
          result.ok = false;
          result.error = "boom";
          return result;
        });
    const auto third = runner.enqueue(
        "third", [&](const TaskProgressFunction &) { return TaskResult{}; });

    THEN("ids increase and nothing is sent until a task runs")
    {
      REQUIRE(first < second);
      REQUIRE(second < third);
      REQUIRE(runner.queued() == 3);
      REQUIRE_FALSE(runner.running());
      REQUIRE(sent.empty());
    }

    WHEN("the third is cancelled while queued")
    {
      std::string error;
      REQUIRE(runner.cancel(third, &error));

      THEN("it is dropped with TaskFailed{cancelled} and unknown ids fail")
      {
        REQUIRE(runner.queued() == 2);
        REQUIRE(sent.size() == 1);
        const auto failed = decode<TaskFailed>(sent.back());
        REQUIRE(failed);
        REQUIRE(failed->taskId == third);
        REQUIRE(failed->error == "cancelled");
        REQUIRE_FALSE(runner.cancel(third, &error));
        REQUIRE(error.find("unknown task") != std::string::npos);
        REQUIRE_FALSE(runner.cancel(9999, &error));
      }

      AND_WHEN("the queue is run down")
      {
        sent.clear();
        const auto ranFirst = runner.runOne();
        const auto ranSecond = runner.runOne();
        const auto ranNothing = runner.runOne();

        THEN("each task reports progress and exactly one ending, in order")
        {
          REQUIRE(ranFirst);
          REQUIRE(ranFirst->taskId == first);
          REQUIRE(ranFirst->result.ok);
          REQUIRE(ranFirst->result.projectChanged);
          REQUIRE(cancelRunningError == "task already running");
          REQUIRE(ranSecond);
          REQUIRE(ranSecond->taskId == second);
          REQUIRE_FALSE(ranSecond->result.ok);
          REQUIRE_FALSE(ranNothing);
          REQUIRE_FALSE(runner.running());
          REQUIRE(runner.queued() == 0);

          REQUIRE(sent.size() == 3);
          const auto progress = decode<TaskProgress>(sent[0]);
          REQUIRE(progress);
          REQUIRE(progress->taskId == first);
          REQUIRE(progress->total == 0);
          REQUIRE(progress->message == "half way");
          const auto completed = decode<TaskCompleted>(sent[1]);
          REQUIRE(completed);
          REQUIRE(completed->taskId == first);
          REQUIRE(completed->message == "dataset_0001");
          const auto failed = decode<TaskFailed>(sent[2]);
          REQUIRE(failed);
          REQUIRE(failed->taskId == second);
          REQUIRE(failed->error == "boom");
        }
      }
    }

    WHEN("the session ends")
    {
      runner.dropQueued();

      THEN("the queue is forgotten silently")
      {
        REQUIRE(runner.queued() == 0);
        REQUIRE(sent.empty());
        REQUIRE_FALSE(runner.runOne());
      }
    }
  }
}

SCENARIO("StudioServer serves project, shot, rig and color map ops",
    "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the project ops test");
    return;
  }

  DataRootFixture data;
  Session session(data.root);
  auto &client = session.client;
  size_t snapshots = 0;

  GIVEN("a bootstrapped client on a fresh project")
  {
    WHEN("it asks for a new project")
    {
      const auto reply = session.request(NewProject{});

      THEN("the reply is ok and the scene resend precedes the snapshot")
      {
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        const auto scene = client.indexOf(StudioMessageType::TransferScene);
        const auto answer = client.indexOf(StudioMessageType::ProjectOpReply);
        const auto snapshot =
            client.indexOf(StudioMessageType::ProjectSnapshot);
        REQUIRE(scene < answer);
        REQUIRE(answer < snapshot);
        const auto project = session.latestSnapshot().project;
        REQUIRE(project.shots.size() == 1);
        REQUIRE(project.datasets.empty());
        REQUIRE(project.activeShotId == project.shots.front().id);

        AND_THEN("frames render the new project's shot")
        {
          client.send(StartRendering{});
          REQUIRE(client.waitForCount(StudioMessageType::Frame, 1));
          REQUIRE(session.waitForFrameOf(project.activeShotId));
        }
      }
    }

    WHEN("shots are created, switched, edited and removed")
    {
      const auto firstShot =
          session.server->projectContext().project().activeShotId;
      client.send(StartRendering{});
      REQUIRE(session.waitForFrameOf(firstShot));

      auto reply = session.request(CreateShot{0, "Two"});
      REQUIRE(reply.ok);
      const auto created = results<ShotCreatedResult>(reply);
      REQUIRE(created);
      REQUIRE(created->shotId != firstShot);
      REQUIRE(session.waitForSnapshots(++snapshots));

      THEN("the new shot is active and frames follow it")
      {
        const auto project = session.latestSnapshot().project;
        REQUIRE(project.shots.size() == 2);
        REQUIRE(project.activeShotId == created->shotId);
        REQUIRE(session.waitForFrameOf(created->shotId));
      }

      THEN("an unknown shot cannot be activated and nothing changes")
      {
        reply = session.request(SetActiveShot{0, "shot_9999"});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error == "shot not found");
        REQUIRE(client.count(StudioMessageType::ProjectSnapshot) == snapshots);
      }

      THEN("activating the first shot changes the frame header")
      {
        reply = session.request(SetActiveShot{0, firstShot});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(session.latestSnapshot().project.activeShotId == firstShot);
        REQUIRE(session.waitForFrameOf(firstShot));
      }

      THEN("UpdateShot normalizes fields and rejects unknown rigs")
      {
        auto project = session.latestSnapshot().project;
        UpdateShot update;
        update.shot = *project::findShot(project, created->shotId);
        update.shot.name = "Renamed";
        update.shot.frameCount = 0;
        update.shot.currentFrame = 7;
        update.shot.playing = true;
        reply = session.request(update);
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        project = session.latestSnapshot().project;
        const auto *shot = project::findShot(project, created->shotId);
        REQUIRE(shot);
        REQUIRE(shot->name == "Renamed");
        REQUIRE(shot->frameCount == 1);
        REQUIRE(shot->currentFrame == 0);
        REQUIRE_FALSE(shot->playing);

        update.shot = *shot;
        update.shot.lightRigId = "lightRig_9999";
        reply = session.request(update);
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error == "light rig not found");
        REQUIRE(client.count(StudioMessageType::ProjectSnapshot) == snapshots);
      }

      THEN("removing the active shot switches to the other; the last stays")
      {
        reply = session.request(RemoveShot{0, created->shotId});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        auto project = session.latestSnapshot().project;
        REQUIRE(project.shots.size() == 1);
        REQUIRE(project.activeShotId == firstShot);
        REQUIRE(session.waitForFrameOf(firstShot));

        reply = session.request(RemoveShot{0, firstShot});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error.find("last shot") != std::string::npos);
        REQUIRE(client.count(StudioMessageType::ProjectSnapshot) == snapshots);
      }
    }

    WHEN("light rigs are built up and torn down")
    {
      auto reply = session.request(CreateLightRig{0, "Rig"});
      REQUIRE(reply.ok);
      const auto rig = results<LightRigCreatedResult>(reply);
      REQUIRE(rig);
      REQUIRE(session.waitForSnapshots(++snapshots));
      REQUIRE(session.latestSnapshot().project.lightRigs.size() == 2);

      THEN("lights are added as scene objects and removed again")
      {
        const auto added = client.count(StudioMessageType::ObjectAdded);
        reply = session.request(AddLightToRig{0, rig->lightRigId, "point"});
        REQUIRE(reply.ok);
        const auto light = results<LightAddedResult>(reply);
        REQUIRE(light);
        REQUIRE(light->lightNode.layerName == "studio");
        REQUIRE(light->lightNode.nodeIndex != VSR_INVALID_INDEX);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(client.count(StudioMessageType::ObjectAdded) == added + 1);

        reply = session.request(AddLightToRig{0, rig->lightRigId, "laser"});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error.find("laser") != std::string::npos);

        const auto removed = client.count(StudioMessageType::ObjectRemoved);
        reply = session.request(
            RemoveLightFromRig{0, rig->lightRigId, light->lightNode});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(client.count(StudioMessageType::ObjectRemoved) == removed + 1);

        reply = session.request(
            RemoveLightFromRig{0, rig->lightRigId, light->lightNode});
        REQUIRE_FALSE(reply.ok);
      }

      THEN("clone, rename and remove keep names unique")
      {
        reply = session.request(CloneLightRig{0, rig->lightRigId});
        REQUIRE(reply.ok);
        const auto clone = results<LightRigCreatedResult>(reply);
        REQUIRE(clone);
        REQUIRE(clone->lightRigId != rig->lightRigId);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(session.latestSnapshot().project.lightRigs.size() == 3);

        reply =
            session.request(RenameLightRig{0, clone->lightRigId, "Default"});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error.find("already uses") != std::string::npos);

        reply = session.request(RenameLightRig{0, clone->lightRigId, "Clone"});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        const auto project = session.latestSnapshot().project;
        REQUIRE(light_rig::findLightRig(project, clone->lightRigId)->name
            == "Clone");

        reply = session.request(RemoveLightRig{0, clone->lightRigId});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(session.latestSnapshot().project.lightRigs.size() == 2);
        reply = session.request(RemoveLightRig{0, clone->lightRigId});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error == "light rig not found");
      }
    }

    WHEN("camera rigs are created, renamed and removed")
    {
      auto reply = session.request(CreateCameraRig{0, "Cam"});
      REQUIRE(reply.ok);
      const auto rig = results<CameraRigCreatedResult>(reply);
      REQUIRE(rig);
      REQUIRE(session.waitForSnapshots(++snapshots));
      REQUIRE(session.latestSnapshot().project.cameraRigs.size() == 2);

      THEN("renames are validated and removal is final")
      {
        reply =
            session.request(RenameCameraRig{0, rig->cameraRigId, "Default"});
        REQUIRE_FALSE(reply.ok);
        reply =
            session.request(RenameCameraRig{0, rig->cameraRigId, "Bad/Name"});
        REQUIRE_FALSE(reply.ok);
        reply = session.request(RenameCameraRig{0, rig->cameraRigId, "Cam 2"});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        const auto project = session.latestSnapshot().project;
        REQUIRE(camera_rig::findCameraRig(project, rig->cameraRigId)->name
            == "Cam 2");

        reply = session.request(RemoveCameraRig{0, rig->cameraRigId});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(session.latestSnapshot().project.cameraRigs.size() == 1);
        reply = session.request(RemoveCameraRig{0, rig->cameraRigId});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error == "camera rig not found");
      }
    }

    WHEN("color maps are created, renamed and removed")
    {
      const auto added = client.count(StudioMessageType::ObjectAdded);
      auto reply = session.request(CreateColorMap{0, "Heat"});
      REQUIRE(reply.ok);
      const auto created = results<ColorMapCreatedResult>(reply);
      REQUIRE(created);
      REQUIRE(session.waitForSnapshots(++snapshots));

      THEN("the record and its array object both appear")
      {
        REQUIRE(created->object.type == ANARI_ARRAY1D);
        REQUIRE(created->object.objectIndex != VSR_INVALID_INDEX);
        REQUIRE(client.count(StudioMessageType::ObjectAdded) == added + 1);
        const auto project = session.latestSnapshot().project;
        REQUIRE(project.colorMaps.size() == 1);
        REQUIRE(project.colorMaps.front().id == created->colorMapId);
        REQUIRE(project.colorMaps.front().name == "Heat");
      }

      THEN("rename collisions are refused, remove takes the object too")
      {
        reply = session.request(CreateColorMap{0, "Cold"});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        reply = session.request(RenameColorMap{0, created->colorMapId, "cold"});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error.find("already uses") != std::string::npos);
        reply = session.request(RenameColorMap{0, created->colorMapId, "Warm"});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(project::findColorMap(
                    session.latestSnapshot().project, created->colorMapId)
                    ->name
            == "Warm");

        const auto removed = client.count(StudioMessageType::ObjectRemoved);
        reply = session.request(RemoveColorMap{0, created->colorMapId});
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(client.count(StudioMessageType::ObjectRemoved) == removed + 1);
        REQUIRE(session.latestSnapshot().project.colorMaps.size() == 1);
        reply = session.request(RemoveColorMap{0, created->colorMapId});
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error == "color map not found");
      }
    }
  }
}

SCENARIO("StudioServer refuses unserviceable requests with a matching reply",
    "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the refusal test");
    return;
  }

  DataRootFixture data;
  Session session(data.root);
  auto &client = session.client;

  GIVEN("a bootstrapped client")
  {
    WHEN("a project request carries its id but no other field")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(501));
      Message msg;
      msg.header.type = uint8_t(StudioMessageType::CreateShot);
      tree.write(msg.payload);
      msg.header.payload_length = uint32_t(msg.payload.size());
      client.channel->send(std::move(msg));

      THEN("the refusal is a ProjectOpReply the client can retire")
      {
        const auto reply = client.waitForReply(501);
        REQUIRE(reply);
        REQUIRE_FALSE(reply->ok);
        REQUIRE(reply->error.find("malformed CreateShot") != std::string::npos);
        REQUIRE(client.count(StudioMessageType::Error) == 0);
        REQUIRE(client.count(StudioMessageType::ProjectSnapshot) == 0);
      }
    }

    WHEN("a request of a later milestone carries an id")
    {
      SetPlaying playing;
      playing.requestId = 502;
      playing.shotId = session.server->projectContext().project().activeShotId;
      client.send(playing);

      THEN("the not-implemented refusal is a ProjectOpReply too")
      {
        const auto reply = client.waitForReply(502);
        REQUIRE(reply);
        REQUIRE_FALSE(reply->ok);
        REQUIRE(reply->error.find("SetPlaying is not implemented")
            != std::string::npos);
        REQUIRE(client.count(StudioMessageType::Error) == 0);
      }
    }

    WHEN("a project request has no readable id")
    {
      Message msg;
      msg.header.type = uint8_t(StudioMessageType::CreateShot);
      msg.payload = {std::byte{0xff}, std::byte{0x00}, std::byte{0x13}};
      msg.header.payload_length = uint32_t(msg.payload.size());
      client.channel->send(std::move(msg));

      THEN("a bare Error is all the server can send")
      {
        REQUIRE(client.waitForCount(StudioMessageType::Error, 1));
        const auto error = client.lastDecoded<Error>();
        REQUIRE(error);
        REQUIRE(error->message.find("malformed CreateShot") != std::string::npos);
        REQUIRE(client.count(StudioMessageType::ProjectOpReply) == 0);
      }
    }
  }
}

SCENARIO("StudioServer answers Remote Browse inside its Data Roots",
    "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the browse test");
    return;
  }

  DataRootFixture data;
  Session session(data.root);

  GIVEN("one data root with files, a plain directory and a project")
  {
    THEN("ListRoots names the canonical root")
    {
      const auto reply = session.request(ListRoots{});
      REQUIRE(reply.ok);
      const auto roots = results<ListRootsResult>(reply);
      REQUIRE(roots);
      REQUIRE(roots->roots
          == std::vector<std::filesystem::path>{canonicalizePath(data.root)});
    }

    THEN("ListDirectory kinds, sizes and order are right")
    {
      const auto reply = session.request(ListDirectory{0, data.root});
      REQUIRE(reply.ok);
      const auto listing = results<ListDirectoryResult>(reply);
      REQUIRE(listing);
      REQUIRE(listing->entries.size() == 4);
      REQUIRE(listing->entries[0].name == "plain");
      REQUIRE(listing->entries[0].kind == EntryKind::Directory);
      REQUIRE(listing->entries[1].name == "proj");
      REQUIRE(listing->entries[1].kind == EntryKind::ProjectDirectory);
      REQUIRE(listing->entries[2].name == "grid.obj");
      REQUIRE(listing->entries[3].name == "mesh.obj");
      const auto *mesh = findEntry(*listing, "mesh.obj");
      REQUIRE(mesh->kind == EntryKind::File);
      REQUIRE(mesh->size == std::filesystem::file_size(data.mesh));
      REQUIRE(mesh->mtimeSeconds > 0);

      const auto empty = session.request(ListDirectory{0, data.plainDir});
      REQUIRE(empty.ok);
      REQUIRE(results<ListDirectoryResult>(empty)->entries.empty());
    }

    THEN("paths outside the roots, relative paths and files are refused")
    {
      auto reply = session.request(
          ListDirectory{0, std::filesystem::temp_directory_path()});
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("Data Roots") != std::string::npos);

      reply = session.request(ListDirectory{0, "relative/dir"});
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("absolute") != std::string::npos);

      reply = session.request(ListDirectory{0, data.mesh});
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("not a directory") != std::string::npos);
      REQUIRE(session.client.count(StudioMessageType::ProjectSnapshot) == 0);
    }
  }
}

SCENARIO("StudioServer runs project tasks on its loop", "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the task test");
    return;
  }

  DataRootFixture data;
  Session session(data.root);
  auto &client = session.client;
  auto &scene = session.server->appContext().vsr.scene;
  size_t snapshots = 0;

  GIVEN("a fresh project and a mesh under the data root")
  {
    ImportStaticDataset import;
    import.name = "Tri";
    import.sourcePath = data.mesh;
    import.importerType = vsr::io::ImporterType::OBJ;
    const auto taskId = startedTaskId(session.request(import));
    const auto end = waitForTaskEnd(client, taskId);
    REQUIRE(end);

    THEN("the import completes, names its dataset and snapshots after")
    {
      REQUIRE(end->completed);
      REQUIRE(client.count(StudioMessageType::TaskProgress) >= 1);
      REQUIRE(session.waitForSnapshots(++snapshots));
      REQUIRE(client.indexOf(StudioMessageType::TaskCompleted)
          < client.indexOf(StudioMessageType::ProjectSnapshot));
      const auto project = session.latestSnapshot().project;
      REQUIRE(project.datasets.size() == 1);
      REQUIRE(project.datasets.front().id == end->text);
      REQUIRE(project.datasets.front().name == "Tri");
      REQUIRE(project.datasets.front().status == DatasetStatus::Available);
    }

    WHEN("a missing file is imported")
    {
      REQUIRE(session.waitForSnapshots(++snapshots));
      import.name = "Missing";
      import.sourcePath = data.root / "missing.obj";
      const auto failedId = startedTaskId(session.request(import));
      const auto failedEnd = waitForTaskEnd(client, failedId);

      THEN("the task fails and a snapshot still carries the failed record")
      {
        REQUIRE(failedEnd);
        REQUIRE_FALSE(failedEnd->completed);
        REQUIRE(session.waitForSnapshots(++snapshots));
        const auto project = session.latestSnapshot().project;
        REQUIRE(project.datasets.size() == 2);
        const auto *missing = findDatasetNamed(project, "Missing");
        REQUIRE(missing);
        REQUIRE(missing->status == DatasetStatus::ImportFailed);

        AND_THEN("removing it is a sync op with a snapshot")
        {
          const auto reply = session.request(RemoveDataset{0, missing->id});
          REQUIRE(reply.ok);
          REQUIRE(session.waitForSnapshots(++snapshots));
          REQUIRE(session.latestSnapshot().project.datasets.size() == 1);
        }
      }
    }

    WHEN("a path outside the roots is named")
    {
      REQUIRE(session.waitForSnapshots(++snapshots));
      const auto endings = client.count(StudioMessageType::TaskFailed)
          + client.count(StudioMessageType::TaskCompleted);
      import.sourcePath = std::filesystem::temp_directory_path() / "x.obj";
      const auto reply = session.request(import);

      THEN("the reply is an error and no task starts")
      {
        REQUIRE_FALSE(reply.ok);
        REQUIRE(reply.error.find("Data Roots") != std::string::npos);
        REQUIRE_FALSE(results<TaskStartedResult>(reply));
        REQUIRE(client.count(StudioMessageType::TaskFailed)
                + client.count(StudioMessageType::TaskCompleted)
            == endings);
        REQUIRE(client.count(StudioMessageType::ProjectSnapshot) == snapshots);
      }
    }

    WHEN("the project is saved and reopened")
    {
      REQUIRE(session.waitForSnapshots(++snapshots));
      const auto geometries = scene.numberOfObjects(ANARI_GEOMETRY);
      const auto shots = session.latestSnapshot().project.shots.size();

      auto reply = session.request(SaveProject{});
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("never been saved") != std::string::npos);

      SaveProject save;
      save.directory = std::filesystem::temp_directory_path() / "elsewhere";
      reply = session.request(save);
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("Data Roots") != std::string::npos);

      const auto saved = data.root / "saved";
      save.directory = saved;
      const auto saveId = startedTaskId(session.request(save));
      const auto saveEnd = waitForTaskEnd(client, saveId);
      REQUIRE(saveEnd);

      THEN("the save completes and the snapshot is clean")
      {
        REQUIRE(saveEnd->completed);
        REQUIRE(session.waitForSnapshots(++snapshots));
        const auto project = session.latestSnapshot().project;
        REQUIRE_FALSE(project.dirty);
        REQUIRE(project.projectDirectory == canonicalizePath(saved));
        REQUIRE(std::filesystem::exists(saved / PROJECT_MANIFEST_FILENAME));

        AND_THEN("a fresh project then OpenProject restores the datasets")
        {
          reply = session.request(NewProject{});
          REQUIRE(reply.ok);
          REQUIRE(session.waitForSnapshots(++snapshots));
          REQUIRE(session.latestSnapshot().project.datasets.empty());

          client.clear();
          snapshots = 0;
          const auto openId =
              startedTaskId(session.request(OpenProject{0, saved}));
          const auto openEnd = waitForTaskEnd(client, openId);
          REQUIRE(openEnd);
          REQUIRE(openEnd->completed);
          REQUIRE(session.waitForSnapshots(++snapshots));
          REQUIRE(client.indexOf(StudioMessageType::TransferScene)
              < client.indexOf(StudioMessageType::TaskCompleted));
          REQUIRE(client.indexOf(StudioMessageType::TaskCompleted)
              < client.indexOf(StudioMessageType::ProjectSnapshot));
          const auto reopened = session.latestSnapshot().project;
          REQUIRE(reopened.datasets.size() == 1);
          REQUIRE(reopened.datasets.front().name == "Tri");
          REQUIRE(reopened.shots.size() == shots);
          REQUIRE(reopened.projectDirectory == canonicalizePath(saved));
          REQUIRE(scene.numberOfObjects(ANARI_GEOMETRY) == geometries);

          AND_THEN("frames render after the reopen")
          {
            client.send(StartRendering{});
            REQUIRE(session.waitForFrameOf(reopened.activeShotId));
          }
        }

        AND_THEN("opening a directory without a project fails cleanly")
        {
          const auto before = client.count(StudioMessageType::ProjectSnapshot);
          const auto badId =
              startedTaskId(session.request(OpenProject{0, data.plainDir}));
          const auto badEnd = waitForTaskEnd(client, badId);
          REQUIRE(badEnd);
          REQUIRE_FALSE(badEnd->completed);
          REQUIRE(client.count(StudioMessageType::ProjectSnapshot) == before);
        }
      }
    }

    WHEN("two imports are queued and the second is cancelled at once")
    {
      REQUIRE(session.waitForSnapshots(++snapshots));
      // Task ids are allocated in order, so the second import's id is known
      // before its reply arrives; all three requests go out back to back.
      ImportStaticDataset first;
      first.requestId = session.nextRequestId++;
      first.name = "GridA";
      first.sourcePath = data.grid;
      first.importerType = vsr::io::ImporterType::OBJ;
      auto second = first;
      second.requestId = session.nextRequestId++;
      second.name = "GridB";
      CancelTask cancel;
      cancel.requestId = session.nextRequestId++;
      cancel.taskId = taskId + 2;
      client.send(first);
      client.send(second);
      client.send(cancel);

      THEN("the first runs, the second fails as cancelled")
      {
        const auto firstReply = client.waitForReply(first.requestId);
        const auto secondReply = client.waitForReply(second.requestId);
        const auto cancelReply = client.waitForReply(cancel.requestId);
        REQUIRE(firstReply);
        REQUIRE(secondReply);
        REQUIRE(cancelReply);
        REQUIRE(startedTaskId(*firstReply) == taskId + 1);
        REQUIRE(startedTaskId(*secondReply) == taskId + 2);
        REQUIRE(cancelReply->ok);

        const auto firstEnd = waitForTaskEnd(client, taskId + 1);
        const auto secondEnd = waitForTaskEnd(client, taskId + 2);
        REQUIRE(firstEnd);
        REQUIRE(firstEnd->completed);
        REQUIRE(secondEnd);
        REQUIRE_FALSE(secondEnd->completed);
        REQUIRE(secondEnd->text == "cancelled");
        REQUIRE(session.waitForSnapshots(++snapshots));
        REQUIRE(session.latestSnapshot().project.datasets.size() == 2);

        const auto late = session.request(CancelTask{0, taskId + 1});
        REQUIRE_FALSE(late.ok);
        REQUIRE(late.error.find("unknown task") != std::string::npos);
      }
    }
  }
}
