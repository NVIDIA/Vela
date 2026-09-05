// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tests
#include "StudioFakeServer.h"
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
// std
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

/*
 * A fake project server for the test client's suite: a FakeStudioServer for
 * the session (Hello, Ping, the message log), its own Bootstrap on the
 * client's Hello, then scripted answers to the project requests the runner's
 * commands send, kept minimal but shaped like the real ones -- replies by
 * request id, results, Server Task messages, a ProjectSnapshot after every
 * mutation. Every handler runs on the server's IO thread and answers at once,
 * so a task's completion is usually in the client's queue before the script
 * awaits it.
 *
 * Example:
 *   FakeProjectServer server;
 *   server.deferSnapshots = 1;
 *   runScript(session, "connect 127.0.0.1 " + std::to_string(server.port())
 *       + "\ncreate-shot A\ncreate-shot B\nawait-snapshot\n");
 *   REQUIRE(server.requests<CreateShot>().size() == 2);
 *
 * The surface is what a real server cannot be made to produce: a stray
 * ProjectOpReply before CreateShot's own and a stray PickReply before a
 * Pick's; snapshots held back (`deferSnapshots`) or delayed
 * (`snapshotDelay`); a SetTime that is always answered with a
 * TimeAdvanceWarning; a RequestArrayHistogram refused as not scalar; a
 * RenderShot (which needs a saved project) that reports frame 1 of
 * frameCount and then holds, refusing CreateShot with "render in progress"
 * until a CancelTask naming it ends it as TaskFailed "cancelled" with
 * framesCompleted 1; and a task-status replay at every bootstrap of the ends
 * sent since the last one, as the real server does. The happy path of the
 * command surface runs against a real server in the StudioScenario scripts.
 */
struct FakeProjectServer
{
  using Message = vsr::network::Message;
  using StudioMessageType = vsr::scivis_studio::protocol::StudioMessageType;

  FakeProjectServer();
  ~FakeProjectServer();

  uint16_t port() const;
  // Every request of type T received so far, decoded, in arrival order.
  template <typename T>
  std::vector<T> requests();

  void sendBootstrap();
  void onRequest(const Message &msg);
  void send(Message msg);
  void sendSnapshot();
  template <typename Result>
  void reply(uint64_t requestId, const Result &result);
  void replyOk(uint64_t requestId);
  void replyError(uint64_t requestId, const std::string &error);
  uint64_t startTask(uint64_t requestId);
  void endTask(Message end);
  // The TaskFailed a cancelled render ends with: RenderShotResult results.
  Message failedRender(uint64_t taskId, uint64_t framesCompleted);

  // Guards the state below: a handler runs on the IO thread, a test pokes
  // the state between two connections.
  std::mutex mutex;
  vsr::scivis_studio::Project project;
  uint64_t nextTaskId{1};
  int nextShot{2};
  int nextDataset{1};
  // The end messages of the tasks finished since the last bootstrap.
  std::vector<Message> finishedSinceBootstrap;
  // The long render, while it runs.
  std::optional<uint64_t> renderRunning;
  // Network lag, staged: the next `deferSnapshots` snapshots are held back
  // until the request after them arrives (and go out ahead of its reply,
  // as the order on the wire demands); every snapshot sent normally waits
  // `snapshotDelay` first, off the IO thread so the reply before it is not
  // held up too.
  std::atomic<int> deferSnapshots{0};
  std::chrono::milliseconds snapshotDelay{0};
  std::vector<Message> deferred;
  std::vector<std::thread> delayedSends;
  bool tearingDown{false}; // under `mutex`: no delayed send may start after

  // The session behaviour (Hello, Ping, the message log) and the socket.
  // Last, so its IO thread is stopped before the state its handlers read
  // goes away.
  FakeStudioServer fake;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline FakeProjectServer::FakeProjectServer()
{
  using namespace vsr::scivis_studio;

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

  // The bootstrap is this server's own (UI state, task replay), built per
  // Hello rather than prerecorded.
  fake.holdBootstrap = true;
  fake.onHello = [this]() { sendBootstrap(); };
  fake.onRequest = [this](const Message &msg) { onRequest(msg); };
}

inline FakeProjectServer::~FakeProjectServer()
{
  // The flag and the swap share the lock with the handlers, so a snapshot
  // delayed from the IO thread either queued its thread before this or finds
  // the teardown under way and goes out at once.
  std::vector<std::thread> pending;
  {
    std::lock_guard lock(mutex);
    tearingDown = true;
    pending.swap(delayedSends);
  }
  for (auto &thread : pending)
    thread.join();
}

inline uint16_t FakeProjectServer::port() const
{
  return fake.port();
}

template <typename T>
std::vector<T> FakeProjectServer::requests()
{
  std::vector<T> out;
  for (const auto &msg : fake.messagesOf(T::MESSAGE_TYPE))
    if (auto decoded = vsr::scivis_studio::protocol::decode<T>(msg))
      out.push_back(std::move(*decoded));
  return out;
}

inline void FakeProjectServer::send(Message msg)
{
  fake.send(std::move(msg));
}

inline void FakeProjectServer::sendSnapshot()
{
  using vsr::scivis_studio::protocol::encode;
  using vsr::scivis_studio::protocol::ProjectSnapshot;

  if (deferSnapshots > 0) {
    --deferSnapshots;
    deferred.push_back(encode(ProjectSnapshot{project}));
    return;
  }
  if (snapshotDelay.count() > 0 && !tearingDown) {
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
inline void FakeProjectServer::reply(uint64_t requestId, const Result &result)
{
  using namespace vsr::scivis_studio::protocol;

  auto r = makeOkReply(requestId);
  setResults(r, result);
  send(encode(r));
}

inline void FakeProjectServer::replyOk(uint64_t requestId)
{
  using namespace vsr::scivis_studio::protocol;

  send(encode(makeOkReply(requestId)));
}

inline void FakeProjectServer::replyError(
    uint64_t requestId, const std::string &error)
{
  using namespace vsr::scivis_studio::protocol;

  send(encode(makeErrorReply(requestId, error)));
}

inline uint64_t FakeProjectServer::startTask(uint64_t requestId)
{
  const auto taskId = nextTaskId++;
  reply(requestId, vsr::scivis_studio::protocol::TaskStartedResult{taskId});
  return taskId;
}

inline void FakeProjectServer::endTask(Message end)
{
  finishedSinceBootstrap.push_back(end);
  send(std::move(end));
}

inline vsr::network::Message FakeProjectServer::failedRender(
    uint64_t taskId, uint64_t framesCompleted)
{
  using namespace vsr::scivis_studio::protocol;

  TaskFailed failed;
  failed.taskId = taskId;
  failed.error = "cancelled";
  setResults(failed, RenderShotResult{framesCompleted});
  return encode(failed);
}

inline void FakeProjectServer::sendBootstrap()
{
  using namespace vsr::scivis_studio::protocol;

  std::lock_guard lock(mutex);
  send(encode(BootstrapBegin{}));
  FrameConfig config;
  config.width = 640;
  config.height = 480;
  send(encode(config));
  send(encode(UIState{})); // every bootstrap carries one, null here
  // The task-status replay: what ended while nobody was listening, or
  // since the last bootstrap.
  for (auto &end : finishedSinceBootstrap)
    send(std::move(end));
  finishedSinceBootstrap.clear();
  send(encode(ProjectSnapshot{project})); // never staged: the bootstrap's
  send(encode(BootstrapEnd{}));
}

inline void FakeProjectServer::onRequest(const Message &msg)
{
  using namespace vsr::scivis_studio;
  using namespace vsr::scivis_studio::protocol;

  std::lock_guard lock(mutex);
  const auto type = messageType(msg);
  if (!type)
    return;

  // A request arrived: whatever earlier snapshots were held back go first.
  for (auto &held : deferred)
    send(std::move(held));
  deferred.clear();

  switch (*type) {
  case StudioMessageType::CreateShot: {
    const auto req = *decode<CreateShot>(msg);
    if (renderRunning) {
      replyError(req.requestId, "render in progress");
      return;
    }
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
    auto *shot = project::findShot(project, req.shotId);
    if (!shot) {
      replyError(req.requestId, "shot not found");
      return;
    }
    shot::applyPatch(*shot, req.patch);
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
    endTask(encode(completed));
    sendSnapshot();
    return;
  }
  case StudioMessageType::OpenProject: {
    const auto req = *decode<OpenProject>(msg);
    const auto taskId = startTask(req.requestId);
    if (req.directory != project.projectDirectory
        || project.projectDirectory.empty()) {
      TaskFailed failed;
      failed.taskId = taskId;
      failed.error = "project directory does not exist";
      endTask(encode(failed));
      return;
    }
    // The saved project again: its UI state goes out before the end.
    project.dirty = false;
    send(encode(UIState{})); // the opened project's UI state, none here
    TaskCompleted completed;
    completed.taskId = taskId;
    endTask(encode(completed));
    sendSnapshot();
    return;
  }
  case StudioMessageType::RenderShot: {
    const auto req = *decode<RenderShot>(msg);
    const auto *shot = project::findShot(project, req.shotId);
    if (!shot) {
      replyError(req.requestId, "shot not found");
      return;
    }
    if (project.projectDirectory.empty()) {
      replyError(req.requestId, "project must be saved before rendering");
      return;
    }
    if (renderRunning) {
      replyError(req.requestId, "render in progress");
      return;
    }
    project.activeShotId = shot->id;
    const auto taskId = startTask(req.requestId);
    sendSnapshot(); // the active-shot change, before the task runs
    // One frame of progress, then it holds for a CancelTask.
    TaskProgress progress;
    progress.taskId = taskId;
    progress.current = 1;
    progress.total = uint64_t(shot->frameCount);
    progress.message = "frame";
    send(encode(progress));
    renderRunning = taskId;
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
    if (renderRunning && *renderRunning == req.taskId) {
      // Cooperative, at the next frame: ok once the body has returned.
      replyOk(req.requestId);
      endTask(failedRender(req.taskId, 1));
      renderRunning.reset();
      sendSnapshot();
      return;
    }
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

  case StudioMessageType::SetTime: {
    const auto req = *decode<SetTime>(msg);
    TimeAdvanceWarning warning;
    warning.shotId = req.shotId;
    warning.frame = req.frame;
    warning.message = "frame " + std::to_string(req.frame) + " failed to load";
    send(encode(warning));
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
  case StudioMessageType::RequestArrayHistogram: {
    const auto req = *decode<RequestArrayHistogram>(msg);
    replyError(req.requestId,
        "array " + std::to_string(req.array.objectIndex)
            + " element type ANARI_FLOAT32_VEC3 is not scalar");
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
