// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "StudioServerTestHelpers.h"
#include "StudioTestClientTestHelpers.h"
#include "TestDirectories.h"
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "TestSession.h"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Project.h"
#include "Shot.h"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::test_client;
using namespace std::chrono_literals;

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
    // The studio layer gets one transform node so the script has a node to
    // address; a fresh project has none of its own.
    size_t transformNode = VSR_INVALID_INDEX;
    auto server = std::make_unique<RunningServer>(
        tempRootServerOptions(), [&](StudioServer &s) {
          auto &scene = s.appContext().vsr.scene;
          auto *layer = scene.layer("studio");
          REQUIRE(layer);
          transformNode =
              scene
                  .insertChildTransformNode(
                      layer->root(), vsr::math::IDENTITY_MAT4, "test transform")
                  .index();
        });
    REQUIRE(server->started);
    REQUIRE(transformNode != VSR_INVALID_INDEX);
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
    // Under the server's Data Root (the temp directory), gone with the test.
    ScopedFixtureDirectory frames("vsrStudioTestClient-");
    const auto ppm = (frames.path / "frame.ppm").string();

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
          "set-node-transform studio " + std::to_string(transformNode)
          + " 2 0 0 0 0 2 0 0 0 0 2 0 5 6 7 1\n"
          "send-raw 255\n"
          "expect-error \"unknown message type 255\"\n"
          "send-raw 0\n"
          "expect-error \"unknown message type 0\"\n"
          "ping\n"
          "send-raw 60 0a0b 0c\n"
          "expect-error \"malformed RenderShot payload\"\n"
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
            r, "EVT Error message=\"malformed RenderShot payload"));
        REQUIRE(hasLine(r, "OK expect-error \"malformed RenderShot payload\""));
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
      }

      THEN("the edits reached the server and the shutdown ended its run()")
      {
        REQUIRE(waitFor([&] { return server->finished(); }));
        auto &scene = server->scene();
        auto *cam = scene.getObject(ANARI_CAMERA, shot->camera.objectIndex);
        REQUIRE(cam);
        REQUIRE(cam->parameterValueAs<float>("fovy") == 0.9f);
        REQUIRE(cam->parameter("note") == nullptr);
        REQUIRE(cam->parameterValueAs<int>("count") == -7);
        auto *layer = scene.layer("studio");
        REQUIRE(layer);
        auto node = layer->at(transformNode);
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
          keepGoing());

      THEN("the Error FAILs the command in flight and the script")
      {
        REQUIRE_FALSE(result.ok);
        const auto fails = failLines(result.records);
        REQUIRE(fails.size() == 1);
        REQUIRE(fails[0].rfind("FAIL sleep 300: server answered Error"
                               " \"malformed RenderShot payload",
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
      REQUIRE(server->finished());

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
          // The server comes back while reconnect is already being refused:
          // the session is not the restarter's to watch, so it waits out a
          // span the retry cadence (50 ms, doubling to 200 ms) is well
          // inside.
          std::thread restarter([&] {
            settle(500ms);
            server =
                std::make_unique<RunningServer>(tempRootServerOptions(port));
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
          REQUIRE(waitFor([&] { return server->finished(); }));
        }
      }
    }
  }
}
