// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioFakeProjectServer.h"
#include "StudioTestClientTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "TestSession.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "PlaybackMessages.h"
#include "ProjectRequests.h"
#include "ShotRigRequests.h"
#include "TaskMessages.h"
#include "ViewportMessages.h"
// std
#include <chrono>
#include <string>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::test_client;
using namespace std::chrono_literals;

SCENARIO("the test client drives project ops against a fake server",
    "[StudioTestClient]")
{
  GIVEN("a fake project server and a session")
  {
    FakeProjectServer server;
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
          "expect-fail await-task\n"
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
          "assert var.lastTaskMessage == dataset_0002\n"
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
        REQUIRE(hasLine(r, "OK expect-fail await-task"));
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

      THEN("the expanded ids and a patch of the edited fields reached the wire")
      {
        const auto removes = server.requests<RemoveShot>();
        REQUIRE(removes.size() == 2);
        REQUIRE(removes[0].shotId == "shot_9999");
        REQUIRE(removes[1].shotId == "shot_0002");
        const auto updates = server.requests<UpdateShot>();
        REQUIRE(updates.size() == 1);
        REQUIRE(updates[0].shotId == "shot_0002");
        const auto &patch = updates[0].patch;
        REQUIRE(patch.name == "Intro Cut");
        REQUIRE(patch.frameCount == 10);
        REQUIRE(patch.fps == 30.f);
        REQUIRE(patch.loop == false);
        REQUIRE_FALSE(patch.currentFrame); // not named, so not in the patch
        REQUIRE_FALSE(patch.lightRigId);
        REQUIRE_FALSE(patch.renderSettings.width);
        REQUIRE(patch.datasetBindings.size() == 1);
        REQUIRE(patch.datasetBindings[0].datasetId == "dataset_0009");
        REQUIRE(patch.datasetBindings[0].enabled);
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

    WHEN("the server answers a pick nobody sent and refuses a scrub")
    {
      // A real server answers only the picks it was asked; the fake sends a
      // stray PickReply first, and warns of every scrub instead of serving it.
      const auto result = runScript(session,
          "connect " + endpoint + "\n"
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
          "set-time active 99\n"
          "await-warning\n"
          "assert warnings.received == 1\n"
          "assert lastWarning contains load\n"
          "assert errors.received == 0\n"
          "disconnect\n");
      for (const auto &f : failLines(result.records))
        WARN(f);
      REQUIRE(result.ok);

      THEN("the stray reply is an event the wait looks past")
      {
        const auto &r = result.records;
        REQUIRE(countStarting(r,
                    "EVT PickReply requestId=777 hit=false"
                    " worldPosition=\"0 0 0\" objectType=none objectIndex=none")
            == 2);
        REQUIRE(hasLine(r,
            "EVT PickReply requestId=1 hit=false worldPosition=\"0 0 0\""
            " objectType=none objectIndex=none"));
        REQUIRE(hasLine(r,
            "EVT PickReply requestId=2 hit=true worldPosition=\"0.5 0.25 -1\""
            " objectType=surface objectIndex=4"));
        REQUIRE(hasLine(r,
            "EVT TimeAdvanceWarning shotId=shot_0001 frame=99"
            " message=\"frame 99 failed to load\""));
        const auto picks = server.requests<Pick>();
        REQUIRE(picks.size() == 2);
        REQUIRE(picks[0].x == 0);
        REQUIRE(picks[0].y == 0);
        REQUIRE(picks[1].x == 10);
        REQUIRE(picks[1].y == 5);
        const auto times = server.requests<SetTime>();
        REQUIRE(times.size() == 1);
        REQUIRE(times[0].shotId == "shot_0001");
        REQUIRE(times[0].frame == 99);
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
          keepGoing());

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
        REQUIRE(fails[8].find("usage: viewport-settings") != std::string::npos);
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
        REQUIRE(
            fails[16].find("no pick has been answered") != std::string::npos);
        // A refused histogram request leaves no histogram to assert on.
        REQUIRE(fails[17].find("no histogram has been answered")
            != std::string::npos);
        REQUIRE(
            fails[18].find("unknown shot field 'bogus'") != std::string::npos);
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
    WHEN("a render is refused, holds, refuses an edit and is cancelled")
    {
      const std::string script =
          "connect " + endpoint + "\n"
          "assert uiState.present == false\n"
          // An unsaved project cannot render; the refusal is a reply.
          "expect-fail render-shot active\n"
          "assert lastReplyError contains saved\n"
          "expect-fail render-shot shot_9999\n"
          "save-project /data/p1\n"
          "await-task\n"
          "await-snapshot\n"
          "assert task.last.state == Completed\n"
          "update-shot active frameCount=400\n"
          "await-snapshot\n"
          // The render: the active-shot snapshot, one frame of progress, then
          // it holds; a mutating request meeting it is refused.
          "no-wait render-shot active\n"
          "await-reply\n"
          "assert var.lastTaskId == 2\n"
          "await-task-progress\n"
          "assert task.last.state == Running\n"
          "assert task.last.current == 1\n"
          "assert task.last.total == 400\n"
          "no-wait expect-fail create-shot Late\n"
          "cancel-task $lastTaskId\n"
          "expect-fail await-reply\n"
          "assert lastReplyError contains progress\n"
          "expect-fail await-task $lastTaskId\n"
          "assert task.last.state == Failed\n"
          "assert task.last.message == cancelled\n"
          "assert task.last.framesCompleted >= 1\n"
          "assert tasks.failed == 1\n"
          // A task that ends without ever reporting progress FAILs the wait.
          "open-project /data/p1\n"
          "await-task\n"
          "await-task-progress timeout=100\n"
          // The replay: every task that ended since the last bootstrap, the
          // ends heard live before the disconnect counted again.
          "disconnect\n"
          "reconnect\n"
          "assert tasks.replayed == 3\n"
          "assert task.2.state == Failed\n"
          "assert task.2.framesCompleted == 1\n"
          "assert task.3.state == Completed\n"
          "assert tasks.completed == 4\n"
          "assert tasks.failed == 2\n"
          // Nothing ended since: an empty replay, and a disconnect forgets.
          "disconnect\n"
          "reconnect\n"
          "assert tasks.replayed == 0\n"
          "assert task.2.state == Failed\n"
          "disconnect\n";
      const auto result = runScript(session, script, keepGoing());
      for (const auto &f : failLines(result.records))
        WARN(f);

      THEN(
          "the records show the refusals, the progress, the end and the replay")
      {
        const auto &r = result.records;
        const auto fails = failLines(r);
        // The two FAILs written as such: a task that ended without ever
        // reporting progress, and a record a disconnect forgot.
        REQUIRE(fails.size() == 2);
        REQUIRE(fails[0]
            == "FAIL await-task-progress timeout=100: task 3 ended (Completed)"
               " before reporting progress");
        REQUIRE(fails[1]
            == "FAIL assert task.2.state == Failed: task.2.state: nothing"
               " has been heard of task 2");
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=1 ok=false error=\"project must be"
            " saved before rendering\""));
        REQUIRE(hasLine(r, "OK assert lastReplyError contains saved"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=2 ok=false error=\"shot not found\""));
        REQUIRE(hasLine(r,
            "EVT TaskProgress taskId=2 current=1 total=400 message=\"frame\""));
        REQUIRE(hasLine(r,
            "EVT TaskFailed taskId=2 error=\"cancelled\" framesCompleted=1"));
        REQUIRE(hasLine(r, "OK expect-fail await-reply"));
        REQUIRE(hasLine(r, "OK assert lastReplyError contains progress"));
        REQUIRE(hasLine(r, "EVT UIState present=false children=0"));
        REQUIRE(hasLine(r, "OK assert tasks.replayed == 3"));
        REQUIRE(hasLine(r, "OK assert tasks.completed == 4"));
        REQUIRE(hasLine(r, "OK assert tasks.replayed == 0"));
      }

      THEN("the wire carries the resolved shot ids and the cancel")
      {
        const auto renders = server.requests<RenderShot>();
        REQUIRE(renders.size() == 3);
        REQUIRE(renders[0].shotId == "shot_0001");
        REQUIRE(renders[1].shotId == "shot_9999");
        REQUIRE(renders[2].shotId == "shot_0001");
        const auto cancels = server.requests<CancelTask>();
        REQUIRE(cancels.size() == 1);
        REQUIRE(cancels[0].taskId == 2);
      }
    }

    WHEN("the link drops while a task of this client is still open")
    {
      const auto first = runScript(session,
          "connect " + endpoint + "\n"
          "save-project /data/p1\n"
          "await-task\n"
          "update-shot active frameCount=400\n"
          "await-snapshot\n"
          "render-shot active\n"
          "await-task-progress\n"
          "assert task.last.state == Running\n");
      REQUIRE(first.ok);
      // The server drops the socket without a word, as a crash would.
      server.fake.dropConnection();
      // A fresh runner: the render is task 2, not $lastTaskId.
      const auto lost = runScript(session,
          "await-lost\n"
          "assert task.2.state == Running\n"
          "reconnect\n"
          "assert task.2.state == Failed\n"
          "assert task.2.message == \"connection lost\"\n"
          "assert tasks.failed == 0\n"
          // The save ended since the last bootstrap, so its end is replayed
          // (and counted again); the render is still open on the server, so
          // nothing of it is.
          "assert tasks.replayed == 1\n"
          "assert tasks.completed == 2\n"
          "assert task.1.state == Completed\n"
          "disconnect\n");
      for (const auto &f : failLines(lost.records))
        WARN(f);

      THEN("the bootstrap fails the open record without counting a message")
      {
        REQUIRE(lost.ok);
        REQUIRE(countStarting(lost.records, "EVT TaskCompleted taskId=1") == 1);
        REQUIRE(countStarting(lost.records, "EVT TaskFailed") == 0);
      }
    }

    WHEN("a restarted server reuses the id of a task that finished")
    {
      const auto first = runScript(session,
          "connect " + endpoint + "\n"
          "save-project /data/p1\n"
          "await-task\n"
          "assert task.1.state == Completed\n"
          "update-shot active frameCount=400\n"
          "await-snapshot\n");
      REQUIRE(first.ok);
      // A kill and restart: the socket drops and the new process counts task
      // ids from 1 again.
      server.fake.dropConnection();
      server.nextTaskId = 1;
      server.renderRunning.reset();
      const auto reused = runScript(session,
          "await-lost\n"
          "reconnect\n"
          "render-shot active\n"
          "assert var.lastTaskId == 1\n"
          "await-task-progress\n"
          "assert task.1.state == Running\n"
          "assert task.1.message == \"\"\n"
          "cancel-task 1\n"
          "expect-fail await-task 1\n"
          "assert task.1.state == Failed\n"
          "assert task.1.message == \"cancelled\"\n"
          "disconnect\n");
      for (const auto &f : failLines(reused.records))
        WARN(f);

      THEN("the old record starts over with the new task")
      {
        REQUIRE(reused.ok);
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
          keepGoing());

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
