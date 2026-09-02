// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_client_core
#include "ProjectOps.h"
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "FrameCodec.h"
#include "FrameMessages.h"
#include "ProjectOpReply.h"
#include "TaskMessages.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/UpdateDelegate.hpp"
// std
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::client;
using namespace vsr::scivis_studio::server;
using namespace std::chrono_literals;

namespace {

// A helide bootstrap under ctest parallelism must never trip the liveness
// timers, and a real server takes longer than the fake one to come back.
ConnectionTimings e2eTimings()
{
  return fastTimings(200ms, 2s, 30s);
}

// The end-to-end waits include a real bootstrap and a server restart.
constexpr std::chrono::milliseconds E2E_TIMEOUT = 10s;

// Every object type the Scene database holds; arrays ride the Structural
// Mirror as descriptors, so they count too.
size_t totalObjects(const vsr::scene::Scene &scene)
{
  size_t n = 0;
  for (auto type : {ANARI_ARRAY,
           ANARI_SURFACE,
           ANARI_GEOMETRY,
           ANARI_MATERIAL,
           ANARI_SAMPLER,
           ANARI_VOLUME,
           ANARI_SPATIAL_FIELD,
           ANARI_LIGHT,
           ANARI_CAMERA,
           ANARI_RENDERER})
    n += scene.numberOfObjects(type);
  return n;
}

// Counts the structural mutations a server push would inflict on the mirror:
// an echoed edit comes back as ObjectAdded or a TransferScene, both of which
// land here.
struct StructureCounter : public vsr::scene::EmptyUpdateDelegate
{
  void signalObjectAdded(const vsr::scene::Object *) override;
  void signalObjectRemoved(const vsr::scene::Object *) override;
  void signalRemoveAllObjects() override;
  void signalLayerAdded(const vsr::scene::Layer *) override;
  void signalLayerStructureUpdated(const vsr::scene::Layer *) override;

  int mutations{0};
};

void StructureCounter::signalObjectAdded(const vsr::scene::Object *)
{
  mutations++;
}

void StructureCounter::signalObjectRemoved(const vsr::scene::Object *)
{
  mutations++;
}

void StructureCounter::signalRemoveAllObjects()
{
  mutations++;
}

void StructureCounter::signalLayerAdded(const vsr::scene::Layer *)
{
  mutations++;
}

void StructureCounter::signalLayerStructureUpdated(const vsr::scene::Layer *)
{
  mutations++;
}

// The client core on a mirror that counts structural mutations.
struct Client
{
  Client();
  ~Client();

  bool waitConnectedAndBootstrapped(int expectedBootstraps);
  // Polls until a Frame is taken; false on timeout.
  bool waitForFrame(vsr::network::Message &frame);
  // The replica and the mirror agree with the given server after a bootstrap.
  void requireMirrorsServer(RunningServer &server);

  vsr::scene::Scene mirror;
  StructureCounter *counter{nullptr};
  ServerConnection connection;
  int bootstraps{0};
  std::vector<std::string> errors;
};

Client::Client() : connection(&mirror, e2eTimings())
{
  counter = mirror.updateDelegate().emplace<StructureCounter>();
  connection.onBootstrapComplete = [this]() { bootstraps++; };
  connection.onServerError = [this](
                                 const std::string &m) { errors.push_back(m); };
}

Client::~Client()
{
  mirror.updateDelegate().erase(counter);
}

bool Client::waitConnectedAndBootstrapped(int expectedBootstraps)
{
  return pollUntil(
      connection,
      [&] {
        return connection.state() == ConnectionState::Connected
            && bootstraps == expectedBootstraps;
      },
      E2E_TIMEOUT);
}

bool Client::waitForFrame(vsr::network::Message &frame)
{
  return pollUntil(
      connection,
      [&] { return connection.takeLatestFrame(frame); },
      E2E_TIMEOUT);
}

void Client::requireMirrorsServer(RunningServer &server)
{
  REQUIRE(connection.project() != nullptr);
  REQUIRE(connection.project()->activeShotId == server.project().activeShotId);
  REQUIRE(totalObjects(mirror) == totalObjects(server.scene()));
  REQUIRE(mirror.numberOfLayers() == server.scene().numberOfLayers());
}

// A directory under the server's Data Root (the temp directory) for the
// project the session saves and reopens; gone with the test.
struct ProjectScratch
{
  ProjectScratch();
  ~ProjectScratch();

  std::filesystem::path base;
  std::filesystem::path saved;
};

ProjectScratch::ProjectScratch()
{
  static int counter = 0;
  base = std::filesystem::temp_directory_path()
      / ("vsr_studio_e2e_" + std::to_string(++counter));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  saved = base / "saved";
}

ProjectScratch::~ProjectScratch()
{
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
}

// Polls until the frame header names `shotId`; false on timeout.
bool waitForFrameOf(Client &client, const ShotID &shotId)
{
  return pollUntil(
      client.connection,
      [&] {
        vsr::network::Message msg;
        if (!client.connection.takeLatestFrame(msg))
          return false;
        const auto frame = decodeFrame(msg);
        return frame && frame->header.shotId == shotId;
      },
      E2E_TIMEOUT);
}

// Stops rendering and waits until the server loop is idle, so the server
// scene may be read.
void pauseServer(Client &client, RunningServer &server)
{
  client.connection.stopRendering();
  REQUIRE(pollUntil(
      client.connection,
      [&] { return server.server->sessionState() == SessionState::Connected; },
      E2E_TIMEOUT));
}

// The states a task record passed through, one entry per change, as seen
// from the polls that drove the wait. `taskId` is the caller's slot the reply
// callback fills, so the wait starts before the reply is in.
std::vector<TaskState> waitForTaskEnd(Client &client, const uint64_t &taskId)
{
  std::vector<TaskState> states;
  pollUntil(
      client.connection,
      [&] {
        if (taskId == 0)
          return false;
        const auto *task = client.connection.projectOps().task(taskId);
        if (!task)
          return false;
        if (states.empty() || states.back() != task->state)
          states.push_back(task->state);
        return task->finished();
      },
      E2E_TIMEOUT);
  return states;
}

} // namespace

SCENARIO("scivisStudioServer and the client core run the project layer",
    "[StudioRemote]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the project layer test");
    return;
  }

  GIVEN("a bootstrapped client core on a fresh server")
  {
    ProjectScratch scratch;
    auto server = std::make_unique<RunningServer>(0);
    REQUIRE(server->started);
    Client client;
    int replaced = 0;
    client.connection.onProjectReplaced = [&] { replaced++; };
    client.connection.connect("127.0.0.1", short(server->port()));
    REQUIRE(client.waitConnectedAndBootstrapped(1));
    REQUIRE(replaced == 1); // the bootstrap's snapshot
    auto &ops = client.connection.projectOps();

    THEN("the fresh project starts clean")
    {
      REQUIRE(client.connection.project());
      REQUIRE_FALSE(client.connection.project()->dirty);
    }

    WHEN("NewProject is requested")
    {
      std::vector<ProjectOpReply> replies;
      ops.newProject([&](const ProjectOpReply &r) { replies.push_back(r); });
      REQUIRE(pollUntil(
          client.connection, [&] { return replies.size() == 1; }, E2E_TIMEOUT));
      REQUIRE(replies[0].ok);

      THEN("the replica is replaced by a clean one-shot project")
      {
        REQUIRE(pollUntil(
            client.connection, [&] { return replaced == 2; }, E2E_TIMEOUT));
        const Project *project = client.connection.project();
        REQUIRE(project);
        REQUIRE(project->shots.size() == 1);
        REQUIRE_FALSE(project->dirty);
        REQUIRE(project->projectDirectory.empty());
        client.requireMirrorsServer(*server);
        const ShotID firstShot = project->shots.front().id;

        AND_THEN(
            "CreateShot with no name yields a numbered shot in the replica")
        {
          std::optional<ShotID> createdId;
          ops.createShot({},
              [&](const ProjectOpReply &r,
                  const std::optional<ShotCreatedResult> &result) {
                REQUIRE(r.ok);
                REQUIRE(result);
                createdId = result->shotId;
              });
          REQUIRE(pollUntil(
              client.connection,
              [&] {
                return createdId && client.connection.project()
                    && client.connection.project()->shots.size() == 2;
              },
              E2E_TIMEOUT));
          project = client.connection.project();
          const auto *created = project::findShot(*project, *createdId);
          REQUIRE(created);
          REQUIRE(created->name == "Shot 2");
          REQUIRE(*createdId != firstShot);
          REQUIRE(project->activeShotId == *createdId);
          REQUIRE(project->dirty);

          AND_THEN("SetActiveShot changes the shot the frames name")
          {
            client.connection.setFrameConfig(32, 24);
            client.connection.startRendering();
            REQUIRE(waitForFrameOf(client, *createdId));

            std::vector<ProjectOpReply> activeReplies;
            ops.setActiveShot(firstShot,
                [&](const ProjectOpReply &r) { activeReplies.push_back(r); });
            REQUIRE(waitForFrameOf(client, firstShot));
            REQUIRE(activeReplies.size() == 1);
            REQUIRE(activeReplies[0].ok);
            REQUIRE(pollUntil(
                client.connection,
                [&] {
                  return client.connection.project()->activeShotId == firstShot;
                },
                E2E_TIMEOUT));
            pauseServer(client, *server);

            AND_THEN("SaveProject runs as a task and leaves the replica clean")
            {
              uint64_t taskId = 0;
              bool queuedAtReply = false;
              ops.saveProject(scratch.saved,
                  nullptr,
                  [&](const ProjectOpReply &r,
                      const std::optional<TaskStartedResult> &started) {
                    REQUIRE(r.ok);
                    REQUIRE(started);
                    taskId = started->taskId;
                    const auto *task = ops.task(taskId);
                    queuedAtReply = task && task->state == TaskState::Queued;
                  });
              const auto states = waitForTaskEnd(client, taskId);
              REQUIRE(taskId != 0);
              REQUIRE(queuedAtReply);
              // A save is quick enough that one poll may drain progress and
              // completion together; the states seen still only advance, and
              // the "writing" phase the Running state carried is kept.
              REQUIRE_FALSE(states.empty());
              REQUIRE(std::is_sorted(states.begin(), states.end()));
              REQUIRE(states.back() == TaskState::Completed);
              const TaskRecord *saveTask = ops.task(taskId);
              REQUIRE(saveTask);
              REQUIRE(saveTask->lastProgress.message == "writing");
              REQUIRE(saveTask->error.empty());
              REQUIRE(
                  saveTask->label.find("Save project as") != std::string::npos);
              REQUIRE_FALSE(ops.tasksActive());

              REQUIRE(pollUntil(
                  client.connection,
                  [&] {
                    const auto *p = client.connection.project();
                    return p && p->projectDirectory == scratch.saved;
                  },
                  E2E_TIMEOUT));
              REQUIRE_FALSE(client.connection.project()->dirty);
              REQUIRE(std::filesystem::is_directory(scratch.saved));

              AND_THEN("ListDirectory marks the saved project directory")
              {
                std::optional<ListDirectoryResult> listing;
                std::vector<ProjectOpReply> listReplies;
                ops.listDirectory(scratch.base,
                    [&](const ProjectOpReply &r,
                        const std::optional<ListDirectoryResult> &result) {
                      listReplies.push_back(r);
                      listing = result;
                    });
                REQUIRE(pollUntil(
                    client.connection,
                    [&] { return listReplies.size() == 1; },
                    E2E_TIMEOUT));
                REQUIRE(listReplies[0].ok);
                REQUIRE(listing);
                const auto entry = std::find_if(listing->entries.begin(),
                    listing->entries.end(),
                    [](const DirectoryEntry &e) { return e.name == "saved"; });
                REQUIRE(entry != listing->entries.end());
                REQUIRE(entry->kind == EntryKind::ProjectDirectory);

                AND_THEN("OpenProject of it rebuilds mirror and replica alike")
                {
                  const auto objectsBefore = totalObjects(client.mirror);
                  uint64_t openTask = 0;
                  ops.openProject(scratch.saved,
                      [&](const ProjectOpReply &r,
                          const std::optional<TaskStartedResult> &started) {
                        REQUIRE(r.ok);
                        REQUIRE(started);
                        openTask = started->taskId;
                      });
                  const auto openStates = waitForTaskEnd(client, openTask);
                  REQUIRE(openTask != 0);
                  REQUIRE_FALSE(openStates.empty());
                  REQUIRE(openStates.back() == TaskState::Completed);
                  REQUIRE(pollUntil(
                      client.connection,
                      [&] {
                        const auto *p = client.connection.project();
                        return p && p->projectDirectory == scratch.saved
                            && p->shots.size() == 2 && !p->dirty;
                      },
                      E2E_TIMEOUT));
                  client.requireMirrorsServer(*server);
                  REQUIRE(totalObjects(client.mirror) == objectsBefore);
                  REQUIRE(
                      client.connection.project()->activeShotId == firstShot);
                  REQUIRE(client.errors.empty());

                  AND_THEN("losing the server fails a pending request once")
                  {
                    std::vector<ProjectOpReply> lostReplies;
                    server->stop();
                    REQUIRE(server->finished.load());
                    // The client has not polled since: the request goes out
                    // on a link it still believes in.
                    const auto handle =
                        ops.newProject([&](const ProjectOpReply &r) {
                          lostReplies.push_back(r);
                        });
                    REQUIRE(handle.valid());
                    REQUIRE(ops.pending(handle));
                    REQUIRE(pollUntil(
                        client.connection,
                        [&] {
                          return client.connection.state()
                              == ConnectionState::Lost;
                        },
                        E2E_TIMEOUT));
                    REQUIRE(lostReplies.size() == 1);
                    REQUIRE_FALSE(lostReplies[0].ok);
                    REQUIRE(lostReplies[0].error == "connection lost");
                    REQUIRE(lostReplies[0].requestId == handle.requestId);
                    REQUIRE_FALSE(ops.pending(handle));
                    pollFor(client.connection, 200ms);
                    REQUIRE(lostReplies.size() == 1);
                    // The frozen replica and task records survive the loss.
                    REQUIRE(client.connection.project() != nullptr);
                    REQUIRE(ops.task(taskId) != nullptr);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

SCENARIO("scivisStudioServer and the client core run a session end to end",
    "[StudioRemote]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the end-to-end test");
    return;
  }

  GIVEN("a server on a free port and a client on a mirror scene")
  {
    auto server = std::make_unique<RunningServer>(0);
    REQUIRE(server->started);
    const auto port = server->port();
    REQUIRE(port != 0);

    Client client;
    REQUIRE(client.connection.state() == ConnectionState::NeverConnected);

    WHEN("the client connects")
    {
      client.connection.connect("127.0.0.1", short(port));
      REQUIRE(client.waitConnectedAndBootstrapped(1));

      THEN("the bootstrap leaves mirror and replica equal to the server's")
      {
        REQUIRE_FALSE(client.connection.bootstrapping());
        client.requireMirrorsServer(*server);
        REQUIRE(client.errors.empty());
        const auto *shot = project::activeShot(server->project());
        REQUIRE(shot);
        REQUIRE(client.connection.frameConfig().width
            == shot->renderSettings.width);
        REQUIRE(client.connection.frameConfig().height
            == shot->renderSettings.height);

        AND_THEN("negotiated frames stream at the requested size")
        {
          const auto &supported = supportedFrameEncodings();
          const bool jpeg =
              std::find(
                  supported.begin(), supported.end(), FrameEncoding::TurboJpeg)
              != supported.end();
          std::vector<FrameEncoding> preferred;
          if (jpeg)
            preferred.push_back(FrameEncoding::TurboJpeg);
          preferred.push_back(FrameEncoding::Raw);
          const auto expected = preferred.front();

          client.connection.setEncodings(preferred);
          client.connection.setFrameConfig(64, 48);
          client.connection.startRendering();

          vsr::network::Message frameMessage;
          REQUIRE(client.waitForFrame(frameMessage));
          const auto frame = decodeFrame(frameMessage);
          REQUIRE(frame);
          REQUIRE(frame->header.width == 64);
          REQUIRE(frame->header.height == 48);
          REQUIRE(frame->header.encoding == expected);
          REQUIRE(frame->header.pixelFormat == PixelFormat::RGBA8_sRGB);
          REQUIRE(frame->header.shotId == server->project().activeShotId);
          std::vector<uint8_t> pixels;
          REQUIRE(decodeFramePixels(*frame, pixels));
          REQUIRE(pixels.size() == 64 * 48 * 4);

          // The FrameConfig ack follows the size change.
          REQUIRE(pollUntil(
              client.connection,
              [&] {
                return client.connection.frameConfig().width == 64
                    && client.connection.frameConfig().height == 48;
              },
              E2E_TIMEOUT));

          AND_THEN("a mirror camera edit reaches the server without an echo")
          {
            // Reading the server scene is only safe while it is not
            // rendering.
            client.connection.stopRendering();
            REQUIRE(pollUntil(
                client.connection,
                [&] {
                  return server->server->sessionState()
                      == SessionState::Connected;
                },
                E2E_TIMEOUT));

            const auto cameraIndex = shot->camera.objectIndex;
            REQUIRE(cameraIndex != VSR_INVALID_INDEX);
            auto *mirrorCamera =
                client.mirror.getObject(ANARI_CAMERA, cameraIndex);
            REQUIRE(mirrorCamera);
            client.counter->mutations = 0;
            mirrorCamera->setParameter("fovy", 0.5f);

            auto &scene = server->scene();
            REQUIRE(pollUntil(
                client.connection,
                [&] {
                  auto *camera = scene.getObject(ANARI_CAMERA, cameraIndex);
                  const auto fovy = camera->parameterValueAs<float>("fovy");
                  return fovy && *fovy == 0.5f;
                },
                E2E_TIMEOUT));

            pollFor(client.connection, 200ms);
            REQUIRE(client.counter->mutations == 0);
            REQUIRE(mirrorCamera->parameterValueAs<float>("fovy") == 0.5f);
            REQUIRE(client.errors.empty());

            AND_THEN("losing the server freezes the client, which reconnects")
            {
              const auto objectsBefore = totalObjects(client.mirror);
              server->stop();
              REQUIRE(server->finished.load());
              REQUIRE(pollUntil(
                  client.connection,
                  [&] {
                    return client.connection.state() == ConnectionState::Lost;
                  },
                  E2E_TIMEOUT));
              REQUIRE(client.connection.autoRetrying());
              REQUIRE(totalObjects(client.mirror) == objectsBefore);
              REQUIRE(client.connection.project() != nullptr);

              server = std::make_unique<RunningServer>(int(port));
              REQUIRE(server->started);
              REQUIRE(server->port() == port);
              REQUIRE(client.waitConnectedAndBootstrapped(2));
              client.requireMirrorsServer(*server);
              // The new server never saw the edit: the fresh bootstrap wins.
              auto *rebuilt =
                  client.mirror.getObject(ANARI_CAMERA, cameraIndex);
              REQUIRE(rebuilt);
              REQUIRE(rebuilt->parameterValueAs<float>("fovy") != 0.5f);

              AND_THEN("shutdownServer() returns the server's run()")
              {
                client.connection.shutdownServer();
                REQUIRE(
                    client.connection.state() == ConnectionState::Disconnected);
                REQUIRE(client.connection.project() == nullptr);
                REQUIRE(totalObjects(client.mirror) == 0);

                const auto deadline = std::chrono::steady_clock::now() + 10s;
                while (!server->finished.load()
                    && std::chrono::steady_clock::now() < deadline)
                  std::this_thread::sleep_for(1ms);
                REQUIRE(server->finished.load());
                REQUIRE(
                    server->server->sessionState() == SessionState::Shutdown);
              }
            }
          }
        }
      }
    }
  }
}
