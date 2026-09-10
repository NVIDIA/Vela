// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "StudioServerTestHelpers.h"
#include "TestDirectories.h"
#include "catch.hpp"
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
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::protocol;
using vsr::network::Message;
using namespace std::chrono_literals;

namespace {

constexpr auto RENDER_TIMEOUT = 30s;
const std::string LAYOUT_MARKER = "[Window][RenderShotTest]";

// A Data Root holding a triangle mesh and room for the saved project.
struct RenderFixture
{
  RenderFixture();

  ScopedFixtureDirectory scratch{"vsr_studio_server_render_shot_"};
  const std::filesystem::path &root{scratch.path};
  std::filesystem::path mesh;
  std::filesystem::path projectDir;
};

RenderFixture::RenderFixture()
{
  mesh = root / "mesh.obj";
  writeTriangleObj(mesh);
  projectDir = root / "proj";
}

// A started server (on a fresh project unless `projectDir` names one) with
// one bootstrapped client, every wait bounded by RENDER_TIMEOUT.
struct RenderSession : ServerSession
{
  RenderSession(const std::filesystem::path &dataRoot,
      const std::filesystem::path &projectDir = {});

  // Imports the mesh, sizes the active shot to `frameCount` frames of
  // `width` x `height` with one sample, saves the project to `projectDir`
  // and clears the log.
  ShotID prepareSavedShot(const std::filesystem::path &projectDir,
      int frameCount,
      uint32_t width = 32,
      uint32_t height = 24);
};

ServerOptions renderOptions(const std::filesystem::path &dataRoot,
    const std::filesystem::path &projectDir)
{
  auto options = testServerOptions({dataRoot});
  options.projectDirectory = projectDir;
  return options;
}

RenderSession::RenderSession(const std::filesystem::path &dataRoot,
    const std::filesystem::path &projectDir)
    : ServerSession(renderOptions(dataRoot, projectDir), {}, RENDER_TIMEOUT)
{}

std::vector<TaskProgress> progressOf(TestClient &client, uint64_t taskId)
{
  std::vector<TaskProgress> events;
  for (const auto &msg : client.messages()) {
    if (auto progress = decode<TaskProgress>(msg);
        progress && progress->taskId == taskId)
      events.push_back(*progress);
  }
  return events;
}

size_t indexOfFrom(TestClient &client, StudioMessageType type, size_t from)
{
  const auto messages = client.messages();
  for (size_t i = from; i < messages.size(); ++i)
    if (messages[i].header.type == uint8_t(type))
      return i;
  return SIZE_MAX;
}

size_t indexOfTaskEnd(TestClient &client, uint64_t taskId)
{
  const auto messages = client.messages();
  for (size_t i = 0; i < messages.size(); ++i) {
    if (auto completed = decode<TaskCompleted>(messages[i]);
        completed && completed->taskId == taskId)
      return i;
    if (auto failed = decode<TaskFailed>(messages[i]);
        failed && failed->taskId == taskId)
      return i;
  }
  return SIZE_MAX;
}

bool anyOfTypeBetween(
    TestClient &client, StudioMessageType type, size_t first, size_t last)
{
  const auto messages = client.messages();
  for (size_t i = first; i < last && i < messages.size(); ++i)
    if (messages[i].header.type == uint8_t(type))
      return true;
  return false;
}

// The frame the latest Frame message carries, or -1 without one.
int latestFrame(TestClient &client)
{
  const auto msg = client.last(StudioMessageType::Frame);
  if (msg.header.type != uint8_t(StudioMessageType::Frame))
    return -1;
  const auto view = decodeFrame(msg);
  return view ? int(view->header.frame) : -1;
}

size_t countPNGs(const std::filesystem::path &directory)
{
  std::error_code ec;
  if (!std::filesystem::is_directory(directory, ec))
    return 0;
  size_t n = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory))
    n += entry.path().extension() == ".png";
  return n;
}

ShotID RenderSession::prepareSavedShot(const std::filesystem::path &projectDir,
    int frameCount,
    uint32_t width,
    uint32_t height)
{
  ImportStaticDataset import;
  import.name = "Mesh";
  import.sourcePath = options.dataRoots.front() / "mesh.obj";
  import.importerType = vsr::io::ImporterType::OBJ;
  const auto imported = waitForTaskEnd(startedTaskId(request(import)));
  REQUIRE(imported);
  REQUIRE(imported->completed);

  REQUIRE(client.waitForCount(StudioMessageType::ProjectSnapshot, 1));
  auto snapshot = client.lastDecoded<ProjectSnapshot>();
  REQUIRE(snapshot);
  const auto *active = project::activeShot(snapshot->project);
  REQUIRE(active);
  UpdateShot update;
  update.shotId = active->id;
  update.patch.frameCount = frameCount;
  update.patch.renderSettings.width = width;
  update.patch.renderSettings.height = height;
  update.patch.renderSettings.samples = 1;
  REQUIRE(request(update).ok);

  SaveProject save;
  save.directory = projectDir;
  const auto saved = waitForTaskEnd(startedTaskId(request(save)));
  REQUIRE(saved);
  REQUIRE(saved->completed);
  client.clear();
  return active->id;
}

// Puts a UI-state node into a saved project's manifest, as the monolith
// writes one: `windows`, `layout` and `settings` beside "scivisStudio".
void writeManifestUIState(const std::filesystem::path &projectDir)
{
  const auto manifest = projectDir / "project.vsr";
  vsr::core::DataTree tree;
  REQUIRE(tree.load(manifest.string().c_str()));
  auto &root = tree.root();
  root["layout"] = LAYOUT_MARKER;
  root["windows"]["Viewport"]["viewport.scale"] = 2.f;
  root["settings"]["fontScale"] = 1.5f;
  REQUIRE(tree.save(manifest.string().c_str()));
}

// The manifest's `layout` leaf, empty when it carries none.
std::string manifestLayout(const std::filesystem::path &projectDir)
{
  vsr::core::DataTree tree;
  if (!tree.load((projectDir / "project.vsr").string().c_str()))
    return {};
  if (const auto *layout = tree.root().child("layout"))
    return layout->getValueOr<std::string>("");
  return {};
}

} // namespace

SCENARIO(
    "StudioServer renders a shot as an exclusive Server Task", "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the RenderShot tests");
    return;
  }

  RenderFixture data;
  RenderSession session(data.root);
  auto &client = session.client;

  GIVEN("a fresh, unsaved project")
  {
    const auto shotId = session.server->projectContext().project().activeShotId;

    THEN("RenderShot is refused until the project is saved")
    {
      const auto reply = session.request(RenderShot{0, shotId});
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("saved") != std::string::npos);
      REQUIRE(client.count(StudioMessageType::TaskProgress) == 0);
    }

    THEN("RenderShot of an unknown shot is refused")
    {
      const auto reply = session.request(RenderShot{0, "shot_nope"});
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error == "shot not found");
    }

    WHEN("a save and a render of two frames are sent back-to-back")
    {
      REQUIRE(client.waitForCount(StudioMessageType::ProjectSnapshot, 1));
      auto snapshot = client.lastDecoded<ProjectSnapshot>();
      REQUIRE(snapshot);
      UpdateShot update;
      update.shotId = project::activeShot(snapshot->project)->id;
      update.patch.frameCount = 2;
      update.patch.renderSettings.width = 32;
      update.patch.renderSettings.height = 24;
      update.patch.renderSettings.samples = 1;
      REQUIRE(session.request(update).ok);

      SaveProject save;
      save.requestId = session.nextRequestId++;
      save.directory = data.projectDir;
      RenderShot render;
      render.requestId = session.nextRequestId++;
      render.shotId = shotId;
      client.send(save);
      client.send(render);

      THEN("the render takes its turn behind the save and sees it saved")
      {
        const auto saveReply = client.waitForReply(save.requestId);
        const auto renderReply =
            client.waitForReply(render.requestId, RENDER_TIMEOUT);
        REQUIRE(saveReply);
        REQUIRE(renderReply);
        const auto saved = session.waitForTaskEnd(startedTaskId(*saveReply));
        REQUIRE(saved);
        REQUIRE(saved->completed);
        const auto rendered =
            session.waitForTaskEnd(startedTaskId(*renderReply));
        REQUIRE(rendered);
        REQUIRE(rendered->completed);
        REQUIRE(rendered->framesCompleted == 2);
        REQUIRE(countPNGs(data.projectDir / "renders" / shotId) == 2);
      }
    }
  }

  GIVEN("a saved project with a four-frame shot and frames streaming")
  {
    const auto shotId = session.prepareSavedShot(data.projectDir, 4);
    client.send(StartRendering{});
    REQUIRE(client.waitForCount(StudioMessageType::Frame, 1));

    WHEN("the client asks for the render")
    {
      RenderShot render;
      render.shotId = shotId;
      const auto reply = session.request(render);
      const auto taskId = startedTaskId(reply);
      const auto end = session.waitForTaskEnd(taskId);
      REQUIRE(end);

      THEN("a scrub sent on hearing the ending is served, not dropped")
      {
        // Only the inputs latched while the body held the loop are stale;
        // this one follows the ending and lands on the scene the render
        // left.
        client.send(SetTime{shotId, 2});
        REQUIRE(waitFor([&] { return latestFrame(client) == 2; }));
        REQUIRE(client.count(StudioMessageType::Error) == 0);
      }

      THEN("the reply, determinate progress, the ending and a snapshot arrive")
      {
        const auto replyIndex = client.indexOfReply(reply.requestId);
        REQUIRE(replyIndex != SIZE_MAX);
        const auto endIndex = indexOfTaskEnd(client, taskId);
        const auto progress = progressOf(client, taskId);
        REQUIRE(progress.size() == 4);
        for (size_t i = 0; i < progress.size(); ++i) {
          REQUIRE(progress[i].current == i + 1);
          REQUIRE(progress[i].total == 4);
          REQUIRE(progress[i].message.find("frame") != std::string::npos);
        }
        // The shot was the active one already, so the prelude changed
        // nothing and no snapshot precedes the first progress event: one
        // snapshot per revision change (the switch case is below).
        const auto firstProgress =
            indexOfFrom(client, StudioMessageType::TaskProgress, replyIndex);
        REQUIRE(firstProgress != SIZE_MAX);
        REQUIRE_FALSE(anyOfTypeBetween(client,
            StudioMessageType::ProjectSnapshot,
            replyIndex,
            firstProgress));
        REQUIRE(firstProgress < endIndex);

        REQUIRE(end->completed);
        REQUIRE(end->framesCompleted == 4);
        const auto outputDir = data.projectDir / "renders" / shotId;
        REQUIRE(std::filesystem::equivalent(
            std::filesystem::path(end->text), outputDir));
        REQUIRE(countPNGs(outputDir) == 4);

        AND_THEN("interactive frames paused during the render and resume after")
        {
          REQUIRE_FALSE(anyOfTypeBetween(
              client, StudioMessageType::Frame, replyIndex, endIndex));
          REQUIRE(waitFor([&] {
            return indexOfFrom(client, StudioMessageType::Frame, endIndex)
                != SIZE_MAX;
          }));
          // The snapshot after the task shows the Project the render left.
          const auto after =
              indexOfFrom(client, StudioMessageType::ProjectSnapshot, endIndex);
          REQUIRE(after != SIZE_MAX);
          const auto snapshot = client.lastDecoded<ProjectSnapshot>();
          REQUIRE(snapshot);
          REQUIRE(snapshot->project.activeShotId == shotId);
        }
      }
    }

    WHEN("another shot is active as the render is asked")
    {
      REQUIRE(session.request(CreateShot{0, "Two"}).ok); // becomes active
      const auto reply = session.request(RenderShot{0, shotId});
      const auto taskId = startedTaskId(reply);
      const auto end = session.waitForTaskEnd(taskId);
      REQUIRE(end);
      REQUIRE(end->completed);

      THEN("the prelude's switch is snapshotted before the first progress")
      {
        const auto replyIndex = client.indexOfReply(reply.requestId);
        REQUIRE(replyIndex != SIZE_MAX);
        const auto snapshotIndex =
            indexOfFrom(client, StudioMessageType::ProjectSnapshot, replyIndex);
        const auto firstProgress =
            indexOfFrom(client, StudioMessageType::TaskProgress, replyIndex);
        REQUIRE(snapshotIndex < firstProgress);
        const auto switched =
            decode<ProjectSnapshot>(client.messages()[snapshotIndex]);
        REQUIRE(switched);
        REQUIRE(switched->project.activeShotId == shotId);
      }
    }
  }

  GIVEN("a saved project with a long shot")
  {
    // Large enough that helide takes a good second over 200 frames: the
    // cases below act while the render is running.
    const auto shotId =
        session.prepareSavedShot(data.projectDir, 200, 256, 192);

    WHEN("a render runs and requests arrive while a second is queued")
    {
      const auto first = startedTaskId(session.request(RenderShot{0, shotId}));
      REQUIRE(client.waitForCount(StudioMessageType::TaskProgress, 1));

      // All three land in the latch while the first body holds the loop and
      // are dispatched together once it returns: the second render queues,
      // the mutation meets it, the browse passes.
      RenderShot second;
      second.requestId = session.nextRequestId++;
      second.shotId = shotId;
      CreateShot create;
      create.requestId = session.nextRequestId++;
      create.name = "Refused";
      ListRoots roots;
      roots.requestId = session.nextRequestId++;
      client.send(second);
      client.send(create);
      client.send(roots);

      const auto secondReply =
          client.waitForReply(second.requestId, RENDER_TIMEOUT);
      const auto createReply =
          client.waitForReply(create.requestId, RENDER_TIMEOUT);
      const auto rootsReply =
          client.waitForReply(roots.requestId, RENDER_TIMEOUT);
      REQUIRE(secondReply);
      REQUIRE(createReply);
      REQUIRE(rootsReply);

      THEN("the mutation is refused, the browse served, the first completes")
      {
        const auto secondId = startedTaskId(*secondReply);
        REQUIRE_FALSE(createReply->ok);
        REQUIRE(createReply->error == "render in progress");
        REQUIRE(rootsReply->ok);
        REQUIRE(results<ListRootsResult>(*rootsReply));

        const auto firstEnd = session.waitForTaskEnd(first);
        REQUIRE(firstEnd);
        REQUIRE(firstEnd->completed);
        REQUIRE(firstEnd->framesCompleted == 200);

        // Cancel the second wherever it stands: still queued or running.
        const auto cancel = session.request(CancelTask{0, secondId});
        REQUIRE(cancel.ok);
        const auto secondEnd = session.waitForTaskEnd(secondId);
        REQUIRE(secondEnd);
        REQUIRE_FALSE(secondEnd->completed);
        REQUIRE(secondEnd->text == "cancelled");
        REQUIRE(secondEnd->framesCompleted < 200);
        REQUIRE(session.request(CreateShot{0, "Allowed"}).ok);
      }
    }

    WHEN("the running render is cancelled")
    {
      const auto taskId = startedTaskId(session.request(RenderShot{0, shotId}));
      REQUIRE(client.waitForCount(StudioMessageType::TaskProgress, 1));
      const auto cancel = session.request(CancelTask{0, taskId});

      THEN("it stops at the next frame with the count of frames written")
      {
        REQUIRE(cancel.ok);
        const auto end = session.waitForTaskEnd(taskId);
        REQUIRE(end);
        REQUIRE_FALSE(end->completed);
        REQUIRE(end->text == "cancelled");
        REQUIRE(end->framesCompleted >= 1);
        REQUIRE(end->framesCompleted < 200);
        const auto outputDir = data.projectDir / "renders" / shotId;
        REQUIRE(countPNGs(outputDir) == end->framesCompleted);
        // The cancel reply came after the body returned, so after the ending.
        REQUIRE(client.indexOfReply(cancel.requestId)
            > indexOfTaskEnd(client, taskId));

        const auto again = session.request(CancelTask{0, taskId});
        REQUIRE(again.ok); // acknowledged once more, nothing changes
      }
    }

    WHEN("the client disconnects mid-render and another connects")
    {
      const auto taskId = startedTaskId(session.request(RenderShot{0, shotId}));
      REQUIRE(client.waitForCount(StudioMessageType::TaskProgress, 1));
      client.channel->disconnect();

      TestClient other;
      bootstrapClient(other, session.port(), RENDER_TIMEOUT);

      THEN("the bootstrap replays the finished render before the snapshot")
      {
        const auto begin = other.indexOf(StudioMessageType::BootstrapBegin);
        const auto config =
            indexOfFrom(other, StudioMessageType::FrameConfig, begin);
        const auto snapshot =
            indexOfFrom(other, StudioMessageType::ProjectSnapshot, begin);
        // Every ending since the last bootstrap is replayed (the import and
        // the save among them); the render's is what matters here.
        const auto completed = other.indexOfCompletedFrom(taskId, config);
        REQUIRE(config < completed);
        REQUIRE(completed < snapshot);
        const auto ending = decode<TaskCompleted>(other.messages()[completed]);
        REQUIRE(ending);
        REQUIRE(framesCompletedOf(*ending) == 200);
        REQUIRE(countPNGs(data.projectDir / "renders" / shotId) == 200);
      }
    }
  }

  GIVEN("a saved project with a small shot")
  {
    const auto shotId = session.prepareSavedShot(data.projectDir, 4);

    WHEN("the client leaves right after the render is accepted")
    {
      const auto taskId = startedTaskId(session.request(RenderShot{0, shotId}));
      client.channel->disconnect();
      REQUIRE(waitFor(
          [&] {
            return session.server->sessionState() == SessionState::Listening;
          },
          RENDER_TIMEOUT));

      TestClient other;
      bootstrapClient(other, session.port(), RENDER_TIMEOUT);

      THEN("the render ran anyway and the next bootstrap tells how it ended")
      {
        const auto begin = other.indexOf(StudioMessageType::BootstrapBegin);
        const auto config =
            indexOfFrom(other, StudioMessageType::FrameConfig, begin);
        const auto snapshot =
            indexOfFrom(other, StudioMessageType::ProjectSnapshot, begin);
        const auto completed = other.indexOfCompletedFrom(taskId, config);
        REQUIRE(config < completed);
        REQUIRE(completed < snapshot);
        const auto ending = decode<TaskCompleted>(other.messages()[completed]);
        REQUIRE(ending);
        REQUIRE(framesCompletedOf(*ending) == 4);
        REQUIRE(countPNGs(data.projectDir / "renders" / shotId) == 4);

        AND_THEN("a later bootstrap does not repeat it")
        {
          other.channel->disconnect();
          REQUIRE(waitFor([&] {
            return session.server->sessionState() == SessionState::Listening;
          }));
          TestClient third;
          bootstrapClient(third, session.port(), RENDER_TIMEOUT);
          REQUIRE(third.count(StudioMessageType::TaskCompleted) == 0);
        }
      }
    }
  }
}

SCENARIO(
    "StudioServer preserves the UI state a project carries", "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the UI state tests");
    return;
  }

  RenderFixture data;

  GIVEN("a saved project whose manifest carries a UI-state tree")
  {
    {
      RenderSession session(data.root);
      session.prepareSavedShot(data.projectDir, 4);
    }
    writeManifestUIState(data.projectDir);
    REQUIRE(manifestLayout(data.projectDir) == LAYOUT_MARKER);

    WHEN("a server opens it (--project) and saves it again")
    {
      RenderSession session(data.root, data.projectDir);
      const auto saved =
          session.waitForTaskEnd(startedTaskId(session.request(SaveProject{})));
      REQUIRE(saved);
      REQUIRE(saved->completed);

      THEN("the manifest still carries it, though no client ever saw it")
      {
        REQUIRE(manifestLayout(data.projectDir) == LAYOUT_MARKER);
      }
    }

    WHEN("a client opens it and saves it again")
    {
      RenderSession session(data.root);
      const auto opened = session.waitForTaskEnd(
          startedTaskId(session.request(OpenProject{0, data.projectDir})));
      REQUIRE(opened);
      REQUIRE(opened->completed);
      const auto saved =
          session.waitForTaskEnd(startedTaskId(session.request(SaveProject{})));
      REQUIRE(saved);
      REQUIRE(saved->completed);

      THEN("the tree the open read is written back unchanged")
      {
        REQUIRE(manifestLayout(data.projectDir) == LAYOUT_MARKER);
      }
    }
  }
}
