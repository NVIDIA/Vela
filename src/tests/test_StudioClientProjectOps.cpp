// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioFakeServer.h"
#include "StudioRemoteTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_client_core
#include "ArchiveRenameFollowUp.h"
#include "ProjectOps.h"
#include "ReplicaView.h"
#include "ServerConnection.h"
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
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::client;
using vsr::network::Message;
using namespace std::chrono_literals;

namespace {

// A request the fake server has received, with its id, ready for a scripted
// reply.
struct SeenRequest
{
  StudioMessageType type{};
  uint64_t requestId{0};
  Message raw;
};

// Reads the requestId every project request carries as its first field
// without knowing the concrete payload type.
std::optional<uint64_t> requestIdOf(const Message &msg)
{
  vsr::core::DataTree tree;
  if (!msg.payload.empty() && !tree.read(msg.payload))
    return {};
  uint64_t id = 0;
  if (!readChild(tree.root(), "requestId", id))
    return {};
  return id;
}

struct Fixture
{
  Fixture(ConnectionTimings timings = fastTimings());
  ~Fixture();

  void connect();
  bool waitConnectedAndBootstrapped(int expectedBootstraps = 1);
  // Polls until the server has seen `n` requests of any type.
  bool waitForRequests(size_t n);
  std::vector<SeenRequest> requests();
  ProjectOps &ops();

  vsr::scene::Scene source;
  vsr::scene::Scene mirror;
  FakeStudioServer server;
  ServerConnection connection;
  int bootstraps{0};
  int projectReplaced{0};
  std::mutex mutex;
  std::vector<SeenRequest> seen;
};

Fixture::Fixture(ConnectionTimings timings) : connection(&mirror, timings)
{
  populateFakeScene(source);
  server.bootstrap = makeFakeBootstrap(source);
  server.onRequest = [this](const Message &msg) {
    SeenRequest request;
    request.type = StudioMessageType(msg.header.type);
    request.requestId = requestIdOf(msg).value_or(0);
    request.raw = msg;
    std::lock_guard lock(mutex);
    seen.push_back(std::move(request));
  };
  connection.onBootstrapComplete = [this]() { bootstraps++; };
  connection.onProjectReplaced = [this]() { projectReplaced++; };
}

Fixture::~Fixture()
{
  // The server's IO thread appends to `seen` from onRequest (a Disconnect
  // the test sent last still counts), so it is joined before the members
  // it writes to go away.
  server.channel->stop();
}

void Fixture::connect()
{
  connection.connect("127.0.0.1", short(server.port()));
}

bool Fixture::waitConnectedAndBootstrapped(int expectedBootstraps)
{
  return pollUntil(connection, [&] {
    return connection.state() == ConnectionState::Connected
        && bootstraps == expectedBootstraps;
  });
}

bool Fixture::waitForRequests(size_t n)
{
  return pollUntil(connection, [&] {
    std::lock_guard lock(mutex);
    return seen.size() >= n;
  });
}

std::vector<SeenRequest> Fixture::requests()
{
  std::lock_guard lock(mutex);
  return seen;
}

ProjectOps &Fixture::ops()
{
  return connection.projectOps();
}

// Collects the replies one callback receives and the thread it ran on.
struct Recorded
{
  std::vector<ProjectOpReply> replies;
  std::vector<std::thread::id> threads;

  ReplyCallback recorder();
  size_t count() const;
};

ReplyCallback Recorded::recorder()
{
  return [this](const ProjectOpReply &reply) {
    replies.push_back(reply);
    threads.push_back(std::this_thread::get_id());
  };
}

size_t Recorded::count() const
{
  return replies.size();
}

} // namespace

SCENARIO("ProjectOps matches replies to requests by id", "[StudioClient]")
{
  GIVEN("a connected client with two requests in flight")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    Recorded first, second;
    const auto h1 = f.ops().send(NewProject{}, first.recorder());
    RenameDataset rename;
    rename.datasetId = "dataset_0001";
    rename.newName = "pressure";
    const auto h2 = f.ops().send(rename, second.recorder());
    REQUIRE(h1.valid());
    REQUIRE(h2.valid());
    REQUIRE(h1.requestId != h2.requestId);
    REQUIRE(f.ops().pendingCount() == 2);
    REQUIRE(f.ops().pending(h1));
    REQUIRE(f.ops().pending(h2));

    REQUIRE(f.waitForRequests(2));
    const auto seen = f.requests();
    REQUIRE(seen[0].type == StudioMessageType::NewProject);
    REQUIRE(seen[0].requestId == h1.requestId);
    REQUIRE(seen[1].type == StudioMessageType::RenameDataset);
    REQUIRE(seen[1].requestId == h2.requestId);
    const auto decodedRename = decode<RenameDataset>(seen[1].raw);
    REQUIRE(decodedRename);
    REQUIRE(decodedRename->newName == "pressure");

    WHEN("the server answers the second request first, with an error")
    {
      f.server.send(encode(makeErrorReply(h2.requestId, "dataset not found")));

      THEN("only the second callback runs, on the polling thread")
      {
        REQUIRE(pollUntil(f.connection, [&] { return second.count() == 1; }));
        REQUIRE(first.count() == 0);
        REQUIRE_FALSE(second.replies[0].ok);
        REQUIRE(second.replies[0].error == "dataset not found");
        REQUIRE(second.replies[0].requestId == h2.requestId);
        REQUIRE(second.threads[0] == std::this_thread::get_id());
        REQUIRE(f.ops().pendingCount() == 1);
        REQUIRE(f.ops().pending(h1));
        REQUIRE_FALSE(f.ops().pending(h2));

        AND_THEN("the first callback runs once its reply arrives, once")
        {
          f.server.send(encode(makeOkReply(h1.requestId)));
          REQUIRE(pollUntil(f.connection, [&] { return first.count() == 1; }));
          REQUIRE(first.replies[0].ok);
          REQUIRE(first.replies[0].requestId == h1.requestId);
          REQUIRE(f.ops().pendingCount() == 0);

          // A second reply to the same id is noise, not a second callback.
          f.server.send(encode(makeOkReply(h1.requestId)));
          pollFor(f.connection, 50ms);
          REQUIRE(first.count() == 1);
          REQUIRE(second.count() == 1);
        }
      }
    }

    WHEN("a reply sits in the inbound queue and the UI has not polled")
    {
      f.server.send(encode(makeOkReply(h1.requestId)));
      std::this_thread::sleep_for(50ms);

      THEN("the callback waits for poll()")
      {
        REQUIRE(first.count() == 0);
        REQUIRE(pollUntil(f.connection, [&] { return first.count() == 1; }));
      }
    }

    WHEN("a callback is forgotten before its reply")
    {
      f.ops().forget(h1);
      f.server.send(encode(makeOkReply(h1.requestId)));

      THEN("the reply retires the request silently")
      {
        REQUIRE(pollUntil(f.connection, [&] { return !f.ops().pending(h1); }));
        REQUIRE(first.count() == 0);
      }
    }
  }
}

SCENARIO("ProjectOps retires a request a bare Error names", "[StudioClient]")
{
  GIVEN("a connected client with a shot and a dataset request in flight")
  {
    Fixture f;
    std::vector<std::string> bannerErrors;
    f.connection.onServerError = [&](const std::string &m) {
      bannerErrors.push_back(m);
    };
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    Recorded shot, rename;
    const auto hShot = f.ops().send(CreateShot{}, shot.recorder());
    RenameDataset renameReq;
    renameReq.datasetId = "dataset_0001";
    renameReq.newName = "pressure";
    const auto hRename = f.ops().send(renameReq, rename.recorder());
    REQUIRE(f.waitForRequests(2));

    WHEN("the server refuses one by type with a bare Error")
    {
      Error error;
      error.message = "malformed RenameDataset payload";
      f.server.send(encode(error));

      THEN("only that request fails, once, with the server's text")
      {
        REQUIRE(pollUntil(f.connection, [&] { return rename.count() == 1; }));
        REQUIRE_FALSE(rename.replies[0].ok);
        REQUIRE(rename.replies[0].requestId == hRename.requestId);
        REQUIRE(rename.replies[0].error == "malformed RenameDataset payload");
        REQUIRE(shot.count() == 0);
        REQUIRE(f.ops().pending(hShot));
        REQUIRE_FALSE(f.ops().pending(hRename));
        REQUIRE(bannerErrors.empty());

        AND_THEN("a late reply to it is noise")
        {
          f.server.send(encode(makeOkReply(hRename.requestId)));
          pollFor(f.connection, 50ms);
          REQUIRE(rename.count() == 1);
        }
      }
    }

    WHEN("the Error names a type nothing pending has, or a longer name")
    {
      Error unrelated;
      unrelated.message = "Pick is not implemented in this server";
      f.server.send(encode(unrelated));
      Error longer;
      longer.message = "malformed CreateShotArchive payload";
      f.server.send(encode(longer));

      THEN("both requests stay pending and the banner hears the errors")
      {
        REQUIRE(
            pollUntil(f.connection, [&] { return bannerErrors.size() == 2; }));
        REQUIRE(bannerErrors[0] == unrelated.message);
        REQUIRE(bannerErrors[1] == longer.message);
        REQUIRE(shot.count() == 0);
        REQUIRE(rename.count() == 0);
        REQUIRE(f.ops().pendingCount() == 2);
      }
    }

    WHEN("two requests of one type are pending and an Error names it")
    {
      Recorded secondShot;
      const auto hShot2 = f.ops().send(CreateShot{}, secondShot.recorder());
      REQUIRE(f.waitForRequests(3));
      Error error;
      error.message = "malformed CreateShot payload";
      f.server.send(encode(error));

      THEN("the oldest one is the one retired")
      {
        REQUIRE(pollUntil(f.connection, [&] { return shot.count() == 1; }));
        REQUIRE(shot.replies[0].requestId == hShot.requestId);
        REQUIRE(secondShot.count() == 0);
        REQUIRE(f.ops().pending(hShot2));
        REQUIRE(f.ops().pending(hRename));
      }
    }
  }
}

SCENARIO("ProjectOps decodes typed results for the callback", "[StudioClient]")
{
  GIVEN("a connected client")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    WHEN("createShot is answered with a ShotCreatedResult")
    {
      std::optional<ShotCreatedResult> result;
      bool ok = false;
      int calls = 0;
      const auto handle = f.ops().createShot("Shot 2",
          [&](const ProjectOpReply &reply,
              const std::optional<ShotCreatedResult> &r) {
            calls++;
            ok = reply.ok;
            result = r;
          });
      REQUIRE(f.waitForRequests(1));
      const auto seen = f.requests();
      REQUIRE(seen[0].type == StudioMessageType::CreateShot);
      const auto request = decode<CreateShot>(seen[0].raw);
      REQUIRE(request);
      REQUIRE(request->name == "Shot 2");

      auto reply = makeOkReply(handle.requestId);
      setResults(reply, ShotCreatedResult{"shot_0002"});
      f.server.send(encode(reply));

      THEN("the callback receives the decoded shot id")
      {
        REQUIRE(pollUntil(f.connection, [&] { return calls == 1; }));
        REQUIRE(ok);
        REQUIRE(result);
        REQUIRE(result->shotId == "shot_0002");
      }
    }

    WHEN("a typed request fails")
    {
      std::optional<LightRigCreatedResult> result;
      bool ok = true;
      int calls = 0;
      const auto handle = f.ops().cloneLightRig("lightrig_0009",
          [&](const ProjectOpReply &reply,
              const std::optional<LightRigCreatedResult> &r) {
            calls++;
            ok = reply.ok;
            result = r;
          });
      REQUIRE(f.waitForRequests(1));
      f.server.send(
          encode(makeErrorReply(handle.requestId, "light rig not found")));

      THEN("the callback sees the failure and no result")
      {
        REQUIRE(pollUntil(f.connection, [&] { return calls == 1; }));
        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(result);
      }
    }

    WHEN("every typed wrapper is used once")
    {
      auto &ops = f.ops();
      const auto ignore = [](const ProjectOpReply &) {};
      const auto ignoreR = [](const ProjectOpReply &, const auto &) {};
      Shot shot;
      shot.id = "shot_0001";
      SceneNodeRef node;
      node.layerName = "studio";
      node.nodeIndex = 7;
      const std::vector<std::filesystem::path> frames{"/d/a.raw", "/d/b.raw"};
      const std::vector<std::string> list{"a.raw", "b.raw"};

      std::vector<StudioMessageType> expected;
      const auto expect = [&](StudioMessageType t, RequestHandle h) {
        REQUIRE(h.valid());
        expected.push_back(t);
      };
      // clang-format off
      expect(StudioMessageType::NewProject, ops.newProject(ignore));
      expect(StudioMessageType::OpenProject, ops.openProject("/d/p", ignoreR));
      expect(StudioMessageType::SaveProject, ops.saveProject(std::nullopt, nullptr, ignoreR));
      expect(StudioMessageType::ImportStaticDataset, ops.importStaticDataset("n", "/d/f.obj", vsr::io::ImporterType::OBJ, false, ignoreR));
      expect(StudioMessageType::ImportFileAnimationDataset, ops.importFileAnimationDataset("n", frames, vsr::io::ImporterType::VOLUME_ANIMATION, true, ignoreR));
      expect(StudioMessageType::DeclareFileAnimationDataset, ops.declareFileAnimationDataset("n", list, vsr::io::ImporterType::VOLUME_ANIMATION, true, ignoreR));
      expect(StudioMessageType::ReimportDataset, ops.reimportDataset("dataset_0001", ignoreR));
      expect(StudioMessageType::RenameDataset, ops.renameDataset("dataset_0001", "x", ignore));
      expect(StudioMessageType::RemoveDataset, ops.removeDataset("dataset_0001", true, ignore));
      expect(StudioMessageType::LoadDataset, ops.loadDataset("dataset_0001", ignoreR));
      expect(StudioMessageType::UnloadDataset, ops.unloadDataset("dataset_0001", ignore));
      expect(StudioMessageType::RefreshDatasetAvailability, ops.refreshDatasetAvailability("dataset_0001", ignore));
      expect(StudioMessageType::SaveDatasetArchive, ops.saveDatasetArchive("dataset_0001", "/d/a.vsr", ignoreR));
      expect(StudioMessageType::LoadDatasetArchive, ops.loadDatasetArchive("/d/a.vsr", ignoreR));
      expect(StudioMessageType::DiscoverDatasetCandidates, ops.discoverDatasetCandidates(ignoreR));
      expect(StudioMessageType::IncorporateDatasetCandidate, ops.incorporateDatasetCandidate("/d/c.vsr", "c", "c", ignoreR));
      expect(StudioMessageType::CreateShot, ops.createShot("s", ignoreR));
      expect(StudioMessageType::RemoveShot, ops.removeShot("shot_0001", ignore));
      expect(StudioMessageType::UpdateShot, ops.updateShot(shot, ignore));
      expect(StudioMessageType::SetActiveShot, ops.setActiveShot("shot_0001", ignore));
      expect(StudioMessageType::CreateLightRig, ops.createLightRig("l", ignoreR));
      expect(StudioMessageType::CloneLightRig, ops.cloneLightRig("lightrig_0001", ignoreR));
      expect(StudioMessageType::RemoveLightRig, ops.removeLightRig("lightrig_0001", ignore));
      expect(StudioMessageType::RenameLightRig, ops.renameLightRig("lightrig_0001", "x", ignore));
      expect(StudioMessageType::AddLightToRig, ops.addLightToRig("lightrig_0001", "point", ignoreR));
      expect(StudioMessageType::RemoveLightFromRig, ops.removeLightFromRig("lightrig_0001", node, ignore));
      expect(StudioMessageType::CreateCameraRig, ops.createCameraRig("c", ignoreR));
      expect(StudioMessageType::RemoveCameraRig, ops.removeCameraRig("camerarig_0001", ignore));
      expect(StudioMessageType::RenameCameraRig, ops.renameCameraRig("camerarig_0001", "x", ignore));
      expect(StudioMessageType::SaveCameraRigArchive, ops.saveCameraRigArchive("camerarig_0001", "/d/c.vsr", ignore));
      expect(StudioMessageType::LoadCameraRigArchive, ops.loadCameraRigArchive("/d/c.vsr", ignoreR));
      expect(StudioMessageType::SaveLightRigArchive, ops.saveLightRigArchive("lightrig_0001", "/d/l.vsr", ignore));
      expect(StudioMessageType::LoadLightRigArchive, ops.loadLightRigArchive("/d/l.vsr", ignoreR));
      expect(StudioMessageType::CreateColorMap, ops.createColorMap("m", ignoreR));
      expect(StudioMessageType::RenameColorMap, ops.renameColorMap("colormap_0001", "x", ignore));
      expect(StudioMessageType::RemoveColorMap, ops.removeColorMap("colormap_0001", ignore));
      expect(StudioMessageType::ListRoots, ops.listRoots(ignoreR));
      expect(StudioMessageType::ListDirectory, ops.listDirectory("/d", ignoreR));
      expect(StudioMessageType::CancelTask, ops.cancelTask(3, ignore));
      // clang-format on

      THEN("the server sees one request of each type, in order, with ids")
      {
        REQUIRE(f.waitForRequests(expected.size()));
        const auto seen = f.requests();
        REQUIRE(seen.size() == expected.size());
        for (size_t i = 0; i < seen.size(); ++i) {
          REQUIRE(seen[i].type == expected[i]);
          REQUIRE(seen[i].requestId != 0);
        }
        // Payload fields land where the wrappers put them.
        const auto rm = decode<RemoveDataset>(seen[8].raw);
        REQUIRE(rm);
        REQUIRE(rm->keepAssetFile);
        const auto imp = decode<ImportStaticDataset>(seen[3].raw);
        REQUIRE(imp);
        REQUIRE(imp->importerType == vsr::io::ImporterType::OBJ);
        REQUIRE(imp->sourcePath == "/d/f.obj");
        const auto light = decode<RemoveLightFromRig>(seen[25].raw);
        REQUIRE(light);
        REQUIRE(light->lightNode.layerName == "studio");
        REQUIRE(light->lightNode.nodeIndex == 7);
        const auto cancel = decode<CancelTask>(seen.back().raw);
        REQUIRE(cancel);
        REQUIRE(cancel->taskId == 3);
        REQUIRE(f.ops().pendingCount() == expected.size());
      }
    }
  }
}

SCENARIO("ProjectOps fails every pending request when the connection goes",
    "[StudioClient]")
{
  GIVEN("a connected client with two requests awaiting replies")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    Recorded a, b;
    const auto h1 = f.ops().send(NewProject{}, a.recorder());
    const auto h2 = f.ops().send(ListRoots{}, b.recorder());
    REQUIRE(f.waitForRequests(2));

    WHEN("the server falls silent until loss is declared")
    {
      f.server.silent = true;
      REQUIRE(pollUntil(f.connection,
          [&] { return f.connection.state() == ConnectionState::Lost; }));

      THEN("both callbacks ran exactly once with \"connection lost\"")
      {
        REQUIRE(a.count() == 1);
        REQUIRE(b.count() == 1);
        REQUIRE_FALSE(a.replies[0].ok);
        REQUIRE(a.replies[0].error == "connection lost");
        REQUIRE(a.replies[0].requestId == h1.requestId);
        REQUIRE_FALSE(b.replies[0].ok);
        REQUIRE(b.replies[0].error == "connection lost");
        REQUIRE(b.replies[0].requestId == h2.requestId);
        REQUIRE(f.ops().pendingCount() == 0);

        AND_THEN("a late reply after reconnecting does not run them again")
        {
          f.server.silent = false;
          REQUIRE(f.waitConnectedAndBootstrapped(2));
          f.server.send(encode(makeOkReply(h1.requestId)));
          f.server.send(encode(makeOkReply(h2.requestId)));
          pollFor(f.connection, 50ms);
          REQUIRE(a.count() == 1);
          REQUIRE(b.count() == 1);
        }
      }
    }

    WHEN("the user disconnects")
    {
      f.connection.disconnect();

      THEN("both callbacks ran once with \"connection lost\"")
      {
        REQUIRE(a.count() == 1);
        REQUIRE(b.count() == 1);
        REQUIRE(a.replies[0].error == "connection lost");
        REQUIRE(b.replies[0].error == "connection lost");
        REQUIRE(f.ops().pendingCount() == 0);
      }
    }
  }

  GIVEN("a client that is not connected")
  {
    Fixture f;
    Recorded a;

    WHEN("a request is sent anyway")
    {
      const auto handle = f.ops().send(NewProject{}, a.recorder());
      REQUIRE(handle.valid());

      THEN("the callback fails from the next poll(), not from send()")
      {
        REQUIRE(a.count() == 0);
        f.connection.poll();
        REQUIRE(a.count() == 1);
        REQUIRE_FALSE(a.replies[0].ok);
        REQUIRE(a.replies[0].error == "not connected");
        REQUIRE(a.replies[0].requestId == handle.requestId);
        REQUIRE(f.ops().pendingCount() == 0);
      }
    }
  }
}

SCENARIO(
    "ProjectOps tracks Server Tasks from start to finish", "[StudioClient]")
{
  GIVEN("a connected client that asked to open a project")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    REQUIRE(f.ops().tasks().empty());
    REQUIRE_FALSE(f.ops().tasksActive());

    std::optional<TaskStartedResult> started;
    const auto handle = f.ops().openProject("/data/run7",
        [&](const ProjectOpReply &, const std::optional<TaskStartedResult> &r) {
          started = r;
        });
    REQUIRE(f.waitForRequests(1));

    WHEN("the reply carries a TaskStartedResult")
    {
      auto reply = makeOkReply(handle.requestId);
      setResults(reply, TaskStartedResult{42});
      f.server.send(encode(reply));
      REQUIRE(pollUntil(f.connection, [&] { return started.has_value(); }));

      THEN("a Queued record labelled after the request appears")
      {
        REQUIRE(started->taskId == 42);
        REQUIRE(f.ops().tasks().size() == 1);
        const TaskRecord *task = f.ops().task(42);
        REQUIRE(task);
        REQUIRE(task->state == TaskState::Queued);
        REQUIRE(task->label == "Open project '/data/run7'");
        REQUIRE_FALSE(task->finished());
        REQUIRE(f.ops().tasksActive());

        AND_THEN("progress makes it Running and completion keeps it")
        {
          TaskProgress progress;
          progress.taskId = 42;
          progress.message = "staging";
          f.server.send(encode(progress));
          REQUIRE(pollUntil(f.connection,
              [&] { return f.ops().task(42)->state == TaskState::Running; }));
          REQUIRE(f.ops().task(42)->lastProgress.message == "staging");
          REQUIRE(f.ops().task(42)->lastProgress.total == 0);

          TaskCompleted completed;
          completed.taskId = 42;
          completed.message = "opened";
          f.server.send(encode(completed));
          REQUIRE(pollUntil(f.connection,
              [&] { return f.ops().task(42)->state == TaskState::Completed; }));
          REQUIRE(f.ops().task(42)->finished());
          REQUIRE(f.ops().task(42)->lastProgress.message == "opened");
          REQUIRE(f.ops().task(42)->error.empty());
          REQUIRE_FALSE(f.ops().tasksActive());
          REQUIRE(f.ops().tasks().size() == 1);

          f.ops().clearFinishedTasks();
          REQUIRE(f.ops().tasks().empty());
          REQUIRE(f.ops().task(42) == nullptr);
        }

        AND_THEN("a failure records the error and stays until cleared")
        {
          TaskFailed failed;
          failed.taskId = 42;
          failed.error = "project.vsr does not exist";
          f.server.send(encode(failed));
          REQUIRE(pollUntil(f.connection,
              [&] { return f.ops().task(42)->state == TaskState::Failed; }));
          REQUIRE(f.ops().task(42)->error == "project.vsr does not exist");
          REQUIRE(f.ops().task(42)->finished());

          // A straggling progress event does not resurrect it.
          TaskProgress late;
          late.taskId = 42;
          late.message = "late";
          f.server.send(encode(late));
          REQUIRE(pollUntil(f.connection, [&] {
            return f.ops().task(42)->lastProgress.message == "late";
          }));
          REQUIRE(f.ops().task(42)->state == TaskState::Failed);
        }

        AND_THEN("cancelTask sends CancelTask for that id")
        {
          Recorded cancel;
          const auto ch = f.ops().cancelTask(42, cancel.recorder());
          REQUIRE(f.waitForRequests(2));
          const auto seen = f.requests();
          REQUIRE(seen[1].type == StudioMessageType::CancelTask);
          const auto request = decode<CancelTask>(seen[1].raw);
          REQUIRE(request);
          REQUIRE(request->taskId == 42);
          REQUIRE(request->requestId == ch.requestId);

          f.server.send(encode(makeOkReply(ch.requestId)));
          TaskFailed failed;
          failed.taskId = 42;
          failed.error = "cancelled";
          f.server.send(encode(failed));
          REQUIRE(pollUntil(f.connection, [&] {
            return cancel.count() == 1
                && f.ops().task(42)->state == TaskState::Failed;
          }));
          REQUIRE(f.ops().task(42)->error == "cancelled");
        }
      }
    }

    WHEN("task events name a task this client never launched")
    {
      TaskProgress progress;
      progress.taskId = 99;
      progress.current = 2;
      progress.total = 10;
      f.server.send(encode(progress));

      THEN("a record is created for it with a generic label")
      {
        REQUIRE(pollUntil(
            f.connection, [&] { return f.ops().task(99) != nullptr; }));
        const TaskRecord *task = f.ops().task(99);
        REQUIRE(task->state == TaskState::Running);
        REQUIRE(task->label == "Task 99");
        REQUIRE(task->lastProgress.current == 2);
        REQUIRE(task->lastProgress.total == 10);
      }
    }

    WHEN("the user disconnects with a task recorded")
    {
      auto reply = makeOkReply(handle.requestId);
      setResults(reply, TaskStartedResult{42});
      f.server.send(encode(reply));
      REQUIRE(pollUntil(f.connection, [&] { return started.has_value(); }));
      f.connection.disconnect();

      THEN("the task records go with the session")
      {
        REQUIRE(f.ops().tasks().empty());
      }
    }
  }
}

SCENARIO("ServerConnection applies snapshots outside the bootstrap",
    "[StudioClient]")
{
  GIVEN("a connected, bootstrapped client")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    REQUIRE(f.projectReplaced == 1); // the bootstrap's own snapshot
    REQUIRE(f.connection.project()->name == "fake project");

    WHEN("the server pushes a ProjectSnapshot")
    {
      ProjectSnapshot snapshot;
      snapshot.project.name = "renamed project";
      Shot shot;
      shot.id = "shot_0002";
      shot.name = "Shot 2";
      snapshot.project.shots.push_back(shot);
      snapshot.project.activeShotId = "shot_0002";
      f.server.send(encode(snapshot));

      THEN("the replica is replaced and onProjectReplaced fires once")
      {
        REQUIRE(
            pollUntil(f.connection, [&] { return f.projectReplaced == 2; }));
        const Project *project = f.connection.project();
        REQUIRE(project);
        REQUIRE(project->name == "renamed project");
        REQUIRE(replica::activeShot(*project));
        REQUIRE(replica::activeShot(*project)->id == "shot_0002");
        REQUIRE_FALSE(f.connection.bootstrapping());
        REQUIRE(f.bootstraps == 1);
      }
    }

    WHEN("the server bootstraps again with a task record still open")
    {
      TaskProgress progress;
      progress.taskId = 5;
      progress.message = "importing";
      f.server.send(encode(progress));
      REQUIRE(
          pollUntil(f.connection, [&] { return f.ops().tasks().size() == 1; }));
      REQUIRE(f.ops().tasksActive());

      f.server.sendBootstrap();

      THEN("the record goes with the mirror: nothing of it survives")
      {
        REQUIRE(f.waitConnectedAndBootstrapped(2));
        REQUIRE(f.ops().tasks().empty());
        REQUIRE_FALSE(f.ops().tasksActive());
      }
    }

    WHEN("the server pushes a TimeAdvanceWarning")
    {
      REQUIRE_FALSE(f.connection.lastTimeAdvanceWarning());
      TimeAdvanceWarning warning;
      warning.shotId = "shot_0001";
      warning.frame = 17;
      warning.message = "frame 17 failed to load";
      f.server.send(encode(warning));

      THEN("the latest warning is kept until cleared")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.connection.lastTimeAdvanceWarning().has_value(); }));
        REQUIRE(f.connection.lastTimeAdvanceWarning()->frame == 17);
        REQUIRE(f.connection.lastTimeAdvanceWarning()->shotId == "shot_0001");
        f.connection.clearTimeAdvanceWarning();
        REQUIRE_FALSE(f.connection.lastTimeAdvanceWarning());
      }
    }

    WHEN("the server pushes a UIState")
    {
      REQUIRE_FALSE(f.connection.uiState());
      UIState state;
      state.tree = makeSubtree();
      state.tree->root()["layout"] = std::string("ini");
      f.server.send(encode(state));

      THEN("the tree is held for the UI")
      {
        REQUIRE(pollUntil(
            f.connection, [&] { return f.connection.uiState() != nullptr; }));
        REQUIRE(f.connection.uiState()->root().child("layout"));
      }
    }
  }
}

SCENARIO("ArchiveRenameFollowUp names the dataset an archive load adds",
    "[StudioClient]")
{
  GIVEN("a connected client that submits a LoadDatasetArchive with a name")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    REQUIRE(f.connection.project()->datasets.empty());

    // What the dialog does on submit: copy the ids, send, arm on the reply.
    ArchiveRenameFollowUp follow;
    const auto idsBefore =
        ArchiveRenameFollowUp::datasetIds(f.connection.project());
    f.ops().loadDatasetArchive("/data/a.vsr",
        [&](const ProjectOpReply &reply,
            const std::optional<TaskStartedResult> &started) {
          if (reply.ok && started)
            follow.arm(started->taskId, idsBefore, "Typed");
        });
    REQUIRE(f.waitForRequests(1));
    const auto load = f.requests().front();
    REQUIRE(load.type == StudioMessageType::LoadDatasetArchive);

    WHEN("a snapshot replaces the replica before the reply arrives")
    {
      ProjectSnapshot unrelated;
      unrelated.project.name = "fake project";
      f.server.send(encode(unrelated));
      REQUIRE(pollUntil(f.connection, [&] { return f.projectReplaced == 2; }));

      auto reply = makeOkReply(load.requestId);
      setResults(reply, TaskStartedResult{3});
      f.server.send(encode(reply));
      REQUIRE(pollUntil(f.connection, [&] { return follow.armed(); }));

      AND_WHEN("the snapshot after the load shows one new dataset")
      {
        ProjectSnapshot loaded;
        loaded.project.name = "fake project";
        Dataset dataset;
        dataset.id = "dataset_0001";
        dataset.name = "as stored in the archive";
        loaded.project.datasets.push_back(dataset);
        f.server.send(encode(loaded));
        REQUIRE(
            pollUntil(f.connection, [&] { return f.projectReplaced == 3; }));

        const auto sent =
            follow.apply(*f.connection.project(), f.ops(), [](const auto &) {});

        THEN("RenameDataset goes out for that id with the typed name")
        {
          REQUIRE(sent.valid());
          REQUIRE_FALSE(follow.armed());
          REQUIRE(f.waitForRequests(2));
          const auto rename = f.requests().back();
          REQUIRE(rename.type == StudioMessageType::RenameDataset);
          const auto decoded = decode<RenameDataset>(rename.raw);
          REQUIRE(decoded);
          REQUIRE(decoded->datasetId == "dataset_0001");
          REQUIRE(decoded->newName == "Typed");
        }
      }

      AND_WHEN("the task fails instead")
      {
        TaskFailed failed;
        failed.taskId = 3;
        failed.error = "no such archive";
        f.server.send(encode(failed));
        REQUIRE(pollUntil(f.connection, [&] {
          const auto *task = f.ops().task(3);
          return task && task->state == TaskState::Failed;
        }));
        const auto sent =
            follow.apply(*f.connection.project(), f.ops(), [](const auto &) {});

        THEN("the follow-up is dropped and nothing is sent")
        {
          REQUIRE_FALSE(sent.valid());
          REQUIRE_FALSE(follow.armed());
          REQUIRE(f.requests().size() == 1);
        }
      }
    }
  }
}

SCENARIO("ProjectOps decodes Remote Browse results", "[StudioClient]")
{
  GIVEN("a connected client")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    WHEN("listRoots is answered")
    {
      std::optional<ListRootsResult> roots;
      int calls = 0;
      const auto handle = f.ops().listRoots(
          [&](const ProjectOpReply &, const std::optional<ListRootsResult> &r) {
            calls++;
            roots = r;
          });
      REQUIRE(f.waitForRequests(1));
      ListRootsResult result;
      result.roots = {"/data", "/scratch/runs"};
      auto reply = makeOkReply(handle.requestId);
      setResults(reply, result);
      f.server.send(encode(reply));

      THEN("the roots arrive typed and in order")
      {
        REQUIRE(pollUntil(f.connection, [&] { return calls == 1; }));
        REQUIRE(roots);
        REQUIRE(roots->roots.size() == 2);
        REQUIRE(roots->roots[0] == "/data");
        REQUIRE(roots->roots[1] == "/scratch/runs");
      }
    }

    WHEN("listDirectory is answered")
    {
      std::optional<ListDirectoryResult> listing;
      int calls = 0;
      const auto handle = f.ops().listDirectory("/data",
          [&](const ProjectOpReply &,
              const std::optional<ListDirectoryResult> &r) {
            calls++;
            listing = r;
          });
      REQUIRE(f.waitForRequests(1));
      const auto seen = f.requests();
      const auto request = decode<ListDirectory>(seen[0].raw);
      REQUIRE(request);
      REQUIRE(request->directory == "/data");

      ListDirectoryResult result;
      DirectoryEntry project;
      project.name = "run7";
      project.kind = EntryKind::ProjectDirectory;
      project.mtimeSeconds = 1700000000;
      DirectoryEntry dir;
      dir.name = "raw";
      dir.kind = EntryKind::Directory;
      DirectoryEntry file;
      file.name = "field.raw";
      file.kind = EntryKind::File;
      file.size = 4096;
      result.entries = {project, dir, file};
      auto reply = makeOkReply(handle.requestId);
      setResults(reply, result);
      f.server.send(encode(reply));

      THEN("the entries arrive typed and in order")
      {
        REQUIRE(pollUntil(f.connection, [&] { return calls == 1; }));
        REQUIRE(listing);
        REQUIRE(listing->entries.size() == 3);
        REQUIRE(listing->entries[0].name == "run7");
        REQUIRE(listing->entries[0].kind == EntryKind::ProjectDirectory);
        REQUIRE(listing->entries[0].mtimeSeconds == 1700000000);
        REQUIRE(listing->entries[1].kind == EntryKind::Directory);
        REQUIRE(listing->entries[2].kind == EntryKind::File);
        REQUIRE(listing->entries[2].size == 4096);
      }
    }

    WHEN("listDirectory is refused")
    {
      std::optional<ListDirectoryResult> listing;
      std::string error;
      int calls = 0;
      const auto handle = f.ops().listDirectory("/etc",
          [&](const ProjectOpReply &reply,
              const std::optional<ListDirectoryResult> &r) {
            calls++;
            error = reply.error;
            listing = r;
          });
      REQUIRE(f.waitForRequests(1));
      f.server.send(encode(makeErrorReply(
          handle.requestId, "path is outside the server's Data Roots")));

      THEN("the error reaches the callback without a listing")
      {
        REQUIRE(pollUntil(f.connection, [&] { return calls == 1; }));
        REQUIRE_FALSE(listing);
        REQUIRE(error == "path is outside the server's Data Roots");
      }
    }
  }
}

SCENARIO("ReplicaView reads the Project Replica", "[StudioClient]")
{
  GIVEN("a project with datasets, shots, rigs and color maps")
  {
    Project project;
    Dataset d1;
    d1.id = "dataset_0001";
    d1.name = "pressure";
    d1.status = DatasetStatus::Available;
    d1.residency = DatasetResidency::Unloaded;
    d1.sourceKind = DatasetSourceKind::FileAnimation;
    Dataset d2;
    d2.id = "dataset_0002";
    d2.name = "Density";
    d2.status = DatasetStatus::ImportFailed;
    project.datasets = {d1, d2};

    Shot s1;
    s1.id = "shot_0001";
    s1.name = "b shot";
    s1.lightRigId = "lightrig_0001";
    s1.cameraRigId = "camerarig_0001";
    Shot s2;
    s2.id = "shot_0002";
    s2.name = "A shot";
    s2.lightRigId = "lightrig_0001";
    s2.cameraRigId = "camerarig_0002"; // not in the project
    project.shots = {s1, s2};
    project.activeShotId = "shot_0002";

    LightRig rig;
    rig.id = "lightrig_0001";
    rig.name = "Default";
    project.lightRigs = {rig};
    CameraRig cam;
    cam.id = "camerarig_0001";
    cam.name = "Orbit";
    project.cameraRigs = {cam};
    ColorMapRecord map;
    map.id = "colormap_0001";
    map.name = "viridis";
    project.colorMaps = {map};

    THEN("lookups find entities by id and the active shot")
    {
      REQUIRE(replica::findDataset(project, "dataset_0002")
          == &project.datasets[1]);
      REQUIRE(replica::findDataset(project, "nope") == nullptr);
      REQUIRE(replica::findShot(project, "shot_0001") == &project.shots[0]);
      REQUIRE(replica::activeShot(project) == &project.shots[1]);
      REQUIRE(replica::findLightRig(project, "lightrig_0001")
          == &project.lightRigs[0]);
      REQUIRE(replica::findCameraRig(project, "camerarig_0001")
          == &project.cameraRigs[0]);
      REQUIRE(replica::findColorMap(project, "colormap_0001")
          == &project.colorMaps[0]);
      REQUIRE(replica::findColorMap(project, "colormap_0002") == nullptr);
      REQUIRE(replica::lightRigUseCount(project, "lightrig_0001") == 2);
      REQUIRE(replica::cameraRigUseCount(project, "camerarig_0001") == 1);
      REQUIRE(replica::cameraRigUseCount(project, "camerarig_0002") == 1);
    }

    THEN("display strings reuse the model's names and mark gaps")
    {
      REQUIRE(std::string(replica::datasetStatusText(d1)) == "Unloaded");
      REQUIRE(std::string(replica::datasetStatusText(d2)) == "Import Failed");
      REQUIRE(std::string(replica::datasetSourceKindText(d1))
          == dataset::toString(DatasetSourceKind::FileAnimation));
      REQUIRE(std::string(replica::datasetResidencyText(d1))
          == dataset::toString(DatasetResidency::Unloaded));
      REQUIRE(replica::projectDirectoryText(project) == "{unsaved}");
      project.projectDirectory = "/data/run7";
      REQUIRE(replica::projectDirectoryText(project) == "/data/run7");
      REQUIRE(replica::lightRigLabel(project, "lightrig_0001") == "Default");
      REQUIRE(replica::lightRigLabel(project, "") == "<none>");
      REQUIRE(replica::cameraRigLabel(project, "camerarig_0002")
          == "<missing: camerarig_0002>");
      REQUIRE(replica::datasetLabel(project, "dataset_0001") == "pressure");
      REQUIRE(replica::shotLabel(project, "shot_0002") == "A shot");
      REQUIRE(replica::colorMapLabel(project, "colormap_0001") == "viridis");
    }

    THEN("sorted views order by name case-insensitively")
    {
      const auto datasets = replica::sortedDatasets(project);
      REQUIRE(datasets.size() == 2);
      REQUIRE(datasets[0]->name == "Density");
      REQUIRE(datasets[1]->name == "pressure");
      const auto shots = replica::sortedShots(project);
      REQUIRE(shots[0]->id == "shot_0002");
      REQUIRE(shots[1]->id == "shot_0001");
      REQUIRE(replica::sortedLightRigs(project).size() == 1);
      REQUIRE(replica::sortedCameraRigs(project).size() == 1);
      REQUIRE(replica::sortedColorMaps(project).size() == 1);
      // The collections themselves are untouched.
      REQUIRE(project.datasets[0].id == "dataset_0001");
      REQUIRE(project.shots[0].id == "shot_0001");
    }
  }
}

SCENARIO("ProjectOps matches PickReply messages to picks by id",
    "[StudioClient]")
{
  GIVEN("a connected client with two picks in flight")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    std::vector<std::optional<PickReply>> first, second;
    const auto h1 = f.ops().pick(10, 20, [&](const auto &r) {
      first.push_back(r);
    });
    const auto h2 = f.ops().pick(30, 40, [&](const auto &r) {
      second.push_back(r);
    });
    REQUIRE(h1.valid());
    REQUIRE(h2.valid());
    REQUIRE(h1.requestId != h2.requestId);
    REQUIRE(f.ops().pendingCount() == 2);
    REQUIRE(f.ops().pending(h1));
    REQUIRE(f.ops().pending(h2));

    REQUIRE(f.waitForRequests(2));
    const auto seen = f.requests();
    REQUIRE(seen[0].type == StudioMessageType::Pick);
    REQUIRE(seen[0].requestId == h1.requestId);
    REQUIRE(seen[1].type == StudioMessageType::Pick);
    const auto decodedPick = decode<Pick>(seen[1].raw);
    REQUIRE(decodedPick);
    REQUIRE(decodedPick->x == 30);
    REQUIRE(decodedPick->y == 40);

    WHEN("the server answers the second pick first, with a hit")
    {
      PickReply reply;
      reply.requestId = h2.requestId;
      reply.hit = true;
      reply.worldPosition = {1.f, 2.f, 3.f};
      reply.objectIdentity = SceneObjectRef{ANARI_VOLUME, 7};
      f.server.send(encode(reply));

      THEN("only the second callback runs, with the decoded reply")
      {
        REQUIRE(pollUntil(f.connection, [&] { return second.size() == 1; }));
        REQUIRE(first.empty());
        REQUIRE(second[0].has_value());
        REQUIRE(second[0]->hit);
        REQUIRE(second[0]->worldPosition.y == 2.f);
        REQUIRE(second[0]->objectIdentity);
        REQUIRE(second[0]->objectIdentity->type == ANARI_VOLUME);
        REQUIRE(second[0]->objectIdentity->objectIndex == 7);
        REQUIRE(f.ops().pending(h1));
        REQUIRE_FALSE(f.ops().pending(h2));

        AND_THEN("a background reply retires the first pick once")
        {
          PickReply miss;
          miss.requestId = h1.requestId;
          f.server.send(encode(miss));
          REQUIRE(pollUntil(f.connection, [&] { return first.size() == 1; }));
          REQUIRE(first[0].has_value());
          REQUIRE_FALSE(first[0]->hit);
          REQUIRE_FALSE(first[0]->objectIdentity);
          REQUIRE(f.ops().pendingCount() == 0);

          f.server.send(encode(miss));
          pollFor(f.connection, 50ms);
          REQUIRE(first.size() == 1);
        }
      }
    }

    WHEN("the user disconnects")
    {
      f.connection.disconnect();

      THEN("both picks fail once with an absent reply")
      {
        REQUIRE(first.size() == 1);
        REQUIRE(second.size() == 1);
        REQUIRE_FALSE(first[0].has_value());
        REQUIRE_FALSE(second[0].has_value());
        REQUIRE(f.ops().pendingCount() == 0);
      }
    }
  }

  GIVEN("a client that is not connected")
  {
    Fixture f;
    std::vector<std::optional<PickReply>> replies;

    WHEN("a pick is sent anyway")
    {
      const auto handle = f.ops().pick(1, 1, [&](const auto &r) {
        replies.push_back(r);
      });
      REQUIRE(handle.valid());

      THEN("the callback fails from the next poll(), not from pick()")
      {
        REQUIRE(replies.empty());
        f.connection.poll();
        REQUIRE(replies.size() == 1);
        REQUIRE_FALSE(replies[0].has_value());
        REQUIRE(f.ops().pendingCount() == 0);
      }
    }
  }
}

SCENARIO("ServerConnection carries playback and viewport messages",
    "[StudioClient]")
{
  GIVEN("a connected client")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    WHEN("setPlaying is answered ok")
    {
      Recorded recorded;
      const auto handle =
          f.ops().setPlaying("shot_0001", true, recorded.recorder());
      REQUIRE(f.waitForRequests(1));
      const auto seen = f.requests();
      REQUIRE(seen[0].type == StudioMessageType::SetPlaying);
      const auto request = decode<SetPlaying>(seen[0].raw);
      REQUIRE(request);
      REQUIRE(request->shotId == "shot_0001");
      REQUIRE(request->playing);
      REQUIRE(request->requestId == handle.requestId);

      f.server.send(encode(makeOkReply(handle.requestId)));

      THEN("the callback sees the reply")
      {
        REQUIRE(
            pollUntil(f.connection, [&] { return recorded.count() == 1; }));
        REQUIRE(recorded.replies[0].ok);
      }
    }

    WHEN("requestArrayHistogram is answered with bins")
    {
      std::optional<ArrayHistogramResult> result;
      int calls = 0;
      const auto handle = f.ops().requestArrayHistogram(
          SceneObjectRef{ANARI_ARRAY1D, 3},
          16,
          [&](const ProjectOpReply &reply,
              const std::optional<ArrayHistogramResult> &r) {
            calls++;
            if (reply.ok)
              result = r;
          });
      REQUIRE(f.waitForRequests(1));
      const auto request =
          decode<RequestArrayHistogram>(f.requests()[0].raw);
      REQUIRE(request);
      REQUIRE(request->array.type == ANARI_ARRAY1D);
      REQUIRE(request->array.objectIndex == 3);
      REQUIRE(request->binCount == 16);

      ArrayHistogramResult histogram;
      histogram.bins = {1, 2, 3};
      histogram.minValue = -1.f;
      histogram.maxValue = 4.f;
      auto reply = makeOkReply(handle.requestId);
      setResults(reply, histogram);
      f.server.send(encode(reply));

      THEN("the callback receives the decoded histogram")
      {
        REQUIRE(pollUntil(f.connection, [&] { return calls == 1; }));
        REQUIRE(result);
        REQUIRE(result->bins == std::vector<uint64_t>{1, 2, 3});
        REQUIRE(result->minValue == -1.f);
        REQUIRE(result->maxValue == 4.f);
      }
    }

    WHEN("the optimistic time and viewport messages are sent")
    {
      f.connection.setTime("shot_0001", 42);
      f.connection.setOutline(SceneObjectRef{ANARI_SURFACE, 5});
      f.connection.setOutline(std::nullopt);
      ViewportSettings settings;
      settings.visualizeAOV = vsr::rendering::AOVType::DEPTH;
      settings.depthVisualMaximum = 12.f;
      settings.showWorldBounds = true;
      f.connection.setViewportSettings(settings);

      THEN("the server receives each with its fields")
      {
        REQUIRE(f.waitForRequests(4));
        const auto seen = f.requests();
        REQUIRE(seen[0].type == StudioMessageType::SetTime);
        const auto time = decode<SetTime>(seen[0].raw);
        REQUIRE(time);
        REQUIRE(time->shotId == "shot_0001");
        REQUIRE(time->frame == 42);
        REQUIRE(seen[1].type == StudioMessageType::SetOutline);
        const auto outline = decode<SetOutline>(seen[1].raw);
        REQUIRE(outline);
        REQUIRE(outline->objectIdentity);
        REQUIRE(outline->objectIdentity->objectIndex == 5);
        const auto cleared = decode<SetOutline>(seen[2].raw);
        REQUIRE(cleared);
        REQUIRE_FALSE(cleared->objectIdentity);
        REQUIRE(seen[3].type == StudioMessageType::ViewportSettings);
        const auto received = decode<ViewportSettings>(seen[3].raw);
        REQUIRE(received);
        REQUIRE(received->visualizeAOV == vsr::rendering::AOVType::DEPTH);
        REQUIRE(received->depthVisualMaximum == 12.f);
        REQUIRE(received->showWorldBounds);
        REQUIRE(received->highlightSelection); // default travelled too
      }
    }

    WHEN("a TimeAdvanceWarning arrives")
    {
      std::vector<TimeAdvanceWarning> warnings;
      f.connection.onTimeAdvanceWarning = [&](const TimeAdvanceWarning &w) {
        warnings.push_back(w);
      };
      TimeAdvanceWarning warning;
      warning.shotId = "shot_0001";
      warning.frame = 12;
      warning.message = "file missing";
      f.server.send(encode(warning));

      THEN("the callback fires and the newest warning is kept until cleared")
      {
        REQUIRE(pollUntil(f.connection, [&] { return warnings.size() == 1; }));
        REQUIRE(warnings[0].frame == 12);
        REQUIRE(warnings[0].message == "file missing");
        REQUIRE(f.connection.lastTimeAdvanceWarning());
        REQUIRE(f.connection.lastTimeAdvanceWarning()->frame == 12);
        f.connection.clearTimeAdvanceWarning();
        REQUIRE_FALSE(f.connection.lastTimeAdvanceWarning());
      }
    }

    WHEN("a Frame is taken")
    {
      REQUIRE_FALSE(f.connection.lastFrameHeader());
      FrameHeader header;
      header.width = 2;
      header.height = 1;
      header.shotId = "shot_0001";
      header.frame = 9;
      const std::vector<std::byte> pixels(2 * 1 * 4, std::byte{0});
      f.server.send(encodeFrame(header, pixels.data(), pixels.size()));

      THEN("its header is the last frame header")
      {
        Message frame;
        REQUIRE(pollUntil(f.connection,
            [&] { return f.connection.takeLatestFrame(frame); }));
        REQUIRE(f.connection.lastFrameHeader());
        REQUIRE(f.connection.lastFrameHeader()->frame == 9);
        REQUIRE(f.connection.lastFrameHeader()->shotId == "shot_0001");

        f.connection.disconnect();
        REQUIRE_FALSE(f.connection.lastFrameHeader());
      }
    }
  }
}
