// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "StudioServerTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
#include "FrameMessages.h"
#include "ProjectSnapshot.h"
#include "SceneEditMessages.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::protocol;
using vsr::network::Message;
using namespace std::chrono_literals;

namespace {

std::vector<std::string> argv(std::initializer_list<const char *> items)
{
  std::vector<std::string> out{"scivisStudioServer"};
  out.insert(out.end(), items.begin(), items.end());
  return out;
}

} // namespace

SCENARIO("ServerOptions parses the server command line", "[StudioServer]")
{
  ServerOptions options;
  std::string error;

  GIVEN("only a data root")
  {
    REQUIRE(
        parseServerOptions(argv({"--data-root", "/data"}), options, &error));

    THEN("everything else keeps its default")
    {
      REQUIRE(options.port == DEFAULT_PORT);
      REQUIRE(options.library.empty());
      REQUIRE(options.dataRoots == std::vector<std::filesystem::path>{"/data"});
      REQUIRE(options.projectDirectory.empty());
      REQUIRE_FALSE(options.showHelp);
    }
  }

  GIVEN("every flag, with repeated data roots")
  {
    REQUIRE(parseServerOptions(argv({"--port",
                                   "4242",
                                   "--library",
                                   "visgl",
                                   "--data-root",
                                   "/a",
                                   "--data-root",
                                   "/b",
                                   "--project",
                                   "/a/proj"}),
        options,
        &error));

    THEN("all values are recorded in order")
    {
      REQUIRE(options.port == 4242);
      REQUIRE(options.library == "visgl");
      REQUIRE(
          options.dataRoots == std::vector<std::filesystem::path>{"/a", "/b"});
      REQUIRE(options.projectDirectory == "/a/proj");
    }
  }

  GIVEN("a project but no data root")
  {
    REQUIRE(parseServerOptions(
        argv({"--project", "/projects/demo/"}), options, &error));

    THEN("the project directory's parent becomes the root")
    {
      REQUIRE(
          options.dataRoots == std::vector<std::filesystem::path>{"/projects"});
      REQUIRE(options.projectDirectory == "/projects/demo/");
    }
  }

  GIVEN("--help among other arguments")
  {
    REQUIRE(parseServerOptions(argv({"--port", "1", "-h"}), options, &error));

    THEN("showHelp is set without validating the rest")
    {
      REQUIRE(options.showHelp);
    }
  }

  GIVEN("malformed command lines")
  {
    THEN("an unknown flag is rejected by name")
    {
      REQUIRE_FALSE(parseServerOptions(
          argv({"--data-root", "/d", "--bogus"}), options, &error));
      REQUIRE(error.find("--bogus") != std::string::npos);
    }
    THEN("a missing value is rejected")
    {
      REQUIRE_FALSE(parseServerOptions(argv({"--data-root"}), options, &error));
      REQUIRE(error.find("--data-root") != std::string::npos);
    }
    THEN("a non-numeric or out-of-range port is rejected")
    {
      REQUIRE_FALSE(parseServerOptions(
          argv({"--data-root", "/d", "--port", "abc"}), options, &error));
      REQUIRE(error.find("--port") != std::string::npos);
      REQUIRE_FALSE(parseServerOptions(
          argv({"--data-root", "/d", "--port", "70000"}), options, &error));
      REQUIRE_FALSE(parseServerOptions(
          argv({"--data-root", "/d", "--port", "0"}), options, &error));
    }
    THEN("no data root and no project is rejected")
    {
      REQUIRE_FALSE(parseServerOptions(argv({}), options, &error));
      REQUIRE(error.find("--data-root") != std::string::npos);
    }
    THEN("two projects are rejected")
    {
      REQUIRE_FALSE(parseServerOptions(
          argv({"--project", "/a", "--project", "/b"}), options, &error));
    }
  }

  GIVEN("the usage text")
  {
    const auto usage = serverUsage("scivisStudioServer");

    THEN("it names every flag and the default port")
    {
      for (const char *flag :
          {"--port", "--library", "--data-root", "--project", "--help"})
        REQUIRE(usage.find(flag) != std::string::npos);
      REQUIRE(usage.find(std::to_string(DEFAULT_PORT)) != std::string::npos);
    }
  }
}

SCENARIO("StudioServer runs a viewer-parity session", "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the session test");
    return;
  }

  GIVEN("a server on a fresh project and a free port")
  {
    ServerOptions options;
    options.port = 0;
    options.library = "helide";
    options.dataRoots = {std::filesystem::temp_directory_path()};
    StudioServer server(options);
    std::string error;
    REQUIRE(server.start(&error));
    REQUIRE(server.libraryName() == "helide");
    const auto port = server.port();
    REQUIRE(port != 0);

    auto &project = server.projectContext().project();
    const auto shotId = project.activeShotId;
    REQUIRE_FALSE(shotId.empty());
    const auto *shot = project::activeShot(project);
    REQUIRE(shot);
    const auto cameraIndex = shot->camera.objectIndex;
    REQUIRE(cameraIndex != VSR_INVALID_INDEX);
    const SceneObjectRef cameraRef{ANARI_CAMERA, cameraIndex};

    ServerLoop loop(&server);

    WHEN("a client connects and answers the Hello")
    {
      TestClient client;
      client.connect(port);
      REQUIRE(client.waitForCount(StudioMessageType::Hello, 1));
      const auto hello = decode<Hello>(client.last(StudioMessageType::Hello));
      REQUIRE(hello);
      REQUIRE(hello->version == PROTOCOL_VERSION);
      REQUIRE(hello->buildInfo.find("helide") != std::string::npos);
      REQUIRE(waitFor([&] {
        return server.sessionState() == SessionState::AwaitingHello;
      }));

      client.clear();
      client.send(Hello{});
      REQUIRE(client.waitForCount(StudioMessageType::BootstrapEnd, 1));

      THEN("the bootstrap arrives as one ordered bracket")
      {
        const auto msgs = client.messages();
        std::vector<StudioMessageType> types;
        for (const auto &m : msgs)
          types.push_back(StudioMessageType(m.header.type));

        REQUIRE(types.size() >= 6);
        REQUIRE(types[0] == StudioMessageType::BootstrapBegin);
        REQUIRE(types[1] == StudioMessageType::TransferScene);
        size_t i = 2;
        size_t layers = 0;
        while (
            i < types.size() && types[i] == StudioMessageType::TransferLayer) {
          ++i;
          ++layers;
        }
        REQUIRE(layers >= 1);
        REQUIRE(i + 4 == types.size());
        REQUIRE(types[i] == StudioMessageType::FrameConfig);
        REQUIRE(types[i + 1] == StudioMessageType::UIState);
        REQUIRE(types[i + 2] == StudioMessageType::ProjectSnapshot);
        REQUIRE(types[i + 3] == StudioMessageType::BootstrapEnd);

        const auto config = decode<FrameConfig>(msgs[i]);
        REQUIRE(config);
        REQUIRE(config->width == shot->renderSettings.width);
        REQUIRE(config->height == shot->renderSettings.height);

        // A fresh project carries no UI state.
        const auto uiState = decode<UIState>(msgs[i + 1]);
        REQUIRE(uiState);
        REQUIRE(uiState->tree == nullptr);

        const auto snapshot = decode<ProjectSnapshot>(msgs[i + 2]);
        REQUIRE(snapshot);
        REQUIRE(snapshot->project.activeShotId == shotId);
        REQUIRE(snapshot->project.shots.size() == 1);
        REQUIRE(
            snapshot->project.shots.front().camera.objectIndex == cameraIndex);

        vsr::scene::Scene mirror;
        vsr::network::messages::TransferScene(msgs[1], &mirror).execute();
        auto *camera = mirror.getObject(ANARI_CAMERA, cameraIndex);
        REQUIRE(camera);
        REQUIRE(camera->name() == shotId + "_camera");
        REQUIRE(mirror.layer("studio") != nullptr);
        REQUIRE(waitFor(
            [&] { return server.sessionState() == SessionState::Connected; }));

        AND_THEN("StartRendering streams frames at the requested config")
        {
          client.clear();
          SetEncodings encodings;
          encodings.supported = {FrameEncoding::Raw};
          client.send(encodings);
          SetFrameConfig frameConfig;
          frameConfig.width = 64;
          frameConfig.height = 48;
          client.send(frameConfig);
          client.send(StartRendering{});

          REQUIRE(client.waitForCount(StudioMessageType::Frame, 1));
          REQUIRE(server.sessionState() == SessionState::Rendering);
          const auto frame = decodeFrame(client.last(StudioMessageType::Frame));
          REQUIRE(frame);
          REQUIRE(frame->header.width == 64);
          REQUIRE(frame->header.height == 48);
          REQUIRE(frame->header.encoding == FrameEncoding::Raw);
          REQUIRE(frame->header.pixelFormat == PixelFormat::RGBA8_sRGB);
          REQUIRE(frame->header.shotId == shotId);
          REQUIRE(frame->header.frame == 0);
          REQUIRE(frame->size == 64 * 48 * 4);

          REQUIRE(client.waitForCount(StudioMessageType::FrameConfig, 1));
          const auto ack =
              decode<FrameConfig>(client.last(StudioMessageType::FrameConfig));
          REQUIRE(ack);
          REQUIRE(ack->width == 64);
          REQUIRE(ack->height == 48);

          AND_THEN("unknown and unimplemented messages are refused loudly")
          {
            // 0 and 255 are the type bytes the enum never assigns (255 is the
            // transport's MESSAGE_TYPE_INVALID); 200 is a gap in the middle.
            for (int outsideType : {200, 0, 255}) {
              client.clear();
              Message outside;
              outside.header.type = uint8_t(outsideType);
              client.channel->send(std::move(outside));
              REQUIRE(client.waitForCount(StudioMessageType::Error, 1));
              const auto refused =
                  decode<Error>(client.last(StudioMessageType::Error));
              REQUIRE(refused);
              REQUIRE(refused->message.find(
                          "unknown message type " + std::to_string(outsideType))
                  != std::string::npos);
            }

            client.clear();
            Message pick;
            pick.header.type = uint8_t(StudioMessageType::Pick);
            client.channel->send(std::move(pick));
            REQUIRE(client.waitForCount(StudioMessageType::Error, 1));
            const auto refused =
                decode<Error>(client.last(StudioMessageType::Error));
            REQUIRE(refused);
            REQUIRE(refused->message.find("not implemented in this server")
                != std::string::npos);
          }

          AND_THEN("StopRendering pauses and an edit reaches the server scene")
          {
            client.send(StopRendering{});
            REQUIRE(waitFor([&] {
              return server.sessionState() == SessionState::Connected;
            }));

            SetObjectParameter edit;
            edit.object = cameraRef;
            edit.name = "fovy";
            edit.value = vsr::core::Any(0.5f);
            client.send(edit);
            auto &scene = server.appContext().vsr.scene;
            REQUIRE(waitFor([&] {
              auto *camera = scene.getObject(ANARI_CAMERA, cameraIndex);
              const auto fovy = camera->parameterValueAs<float>("fovy");
              return fovy && *fovy == 0.5f;
            }));
            // Parameter edits are one-way: nothing echoes back.
            REQUIRE(client.count(StudioMessageType::ObjectAdded) == 0);
            REQUIRE(client.count(StudioMessageType::TransferScene) == 0);

            AND_THEN(
                "a replacement client greeting right after the drop is"
                " served")
            {
              // No wait for Listening: the drop, the accept and the Hello may
              // all reach the loop in one latch batch, and the accepted Hello
              // must survive the old session's teardown.
              client.channel->disconnect();
              TestClient next;
              next.connect(port);
              REQUIRE(next.waitForCount(StudioMessageType::Hello, 1));
              next.send(Hello{});
              REQUIRE(next.waitForCount(StudioMessageType::BootstrapEnd, 1));

              next.clear();
              SetFrameConfig resize;
              resize.width = 32;
              resize.height = 24;
              next.send(resize);
              REQUIRE(next.waitForCount(StudioMessageType::FrameConfig, 1));
              REQUIRE(next.count(StudioMessageType::Error) == 0);
            }

            AND_THEN("a dropped client returns the server to Listening")
            {
              client.channel->disconnect();
              REQUIRE(waitFor([&] {
                return server.sessionState() == SessionState::Listening;
              }));

              AND_THEN("a version-mismatched Hello is refused and closed")
              {
                TestClient other;
                other.connect(port);
                REQUIRE(other.waitForCount(StudioMessageType::Hello, 1));
                Hello wrong;
                wrong.version = PROTOCOL_VERSION + 1;
                other.send(wrong);
                REQUIRE(other.waitForCount(StudioMessageType::Error, 1));
                const auto refused =
                    decode<Error>(other.last(StudioMessageType::Error));
                REQUIRE(refused);
                REQUIRE(refused->message.find("version") != std::string::npos);
                REQUIRE(waitFor([&] { return other.disconnects == 1; }));
                REQUIRE(waitFor([&] {
                  return server.sessionState() == SessionState::Listening;
                }));
                REQUIRE(other.count(StudioMessageType::BootstrapBegin) == 0);

                AND_THEN("Shutdown makes run() return")
                {
                  TestClient last;
                  last.connect(port);
                  REQUIRE(last.waitForCount(StudioMessageType::Hello, 1));
                  last.send(Hello{});
                  REQUIRE(
                      last.waitForCount(StudioMessageType::BootstrapEnd, 1));
                  last.send(Shutdown{});
                  REQUIRE(waitFor([&] { return loop.finished.load(); }));
                  REQUIRE(server.sessionState() == SessionState::Shutdown);
                }
              }
            }
          }
        }
      }
    }
  }
}

namespace {

// The names of a layer's nodes by forest index; empty slots stay empty.
std::vector<std::string> namesByIndex(const vsr::scene::Layer &layer)
{
  std::vector<std::string> names(layer.capacity());
  for (size_t i = 0; i < layer.capacity(); ++i) {
    if (auto node = layer.at(i))
      names[i] = (*node)->name();
  }
  return names;
}

// Where a node falls in the layer's traversal order.
size_t traversalPosition(
    const vsr::scene::Layer &layer, vsr::scene::LayerNodeRef target)
{
  size_t position = 0;
  size_t found = VSR_INVALID_INDEX;
  layer.traverse_const(
      layer.root(), [&](const vsr::scene::LayerNode &node, int) {
        if (node.index() == target.index())
          found = position;
        ++position;
        return true;
      });
  return found;
}

// Rebuild a mirror from the scene and layer transfers of one bootstrap.
void applySceneTransfers(
    vsr::scene::Scene &mirror, const std::vector<Message> &msgs)
{
  for (const auto &msg : msgs) {
    switch (StudioMessageType(msg.header.type)) {
    case StudioMessageType::TransferScene:
      vsr::network::messages::TransferScene(msg, &mirror).execute();
      break;
    case StudioMessageType::TransferLayer:
      vsr::network::messages::TransferLayer(msg, &mirror).execute();
      break;
    default:
      break;
    }
  }
}

} // namespace

SCENARIO("StudioServer applies SetNodeTransform to the addressed node",
    "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the node transform test");
    return;
  }

  GIVEN("a started server whose studio layer is sparse")
  {
    ServerOptions options;
    options.port = 0;
    options.library = "helide";
    options.dataRoots = {std::filesystem::temp_directory_path()};
    StudioServer server(options);
    std::string error;
    REQUIRE(server.start(&error));

    // Before run(): start() is the last thing on this thread that may touch
    // the scene. Two transform nodes bracket a removed one so the addressed
    // node's forest index is not its traversal position.
    auto &scene = server.appContext().vsr.scene;
    auto *layer = scene.layer("studio");
    REQUIRE(layer != nullptr);
    auto first = scene.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "first");
    auto doomed = scene.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "doomed");
    auto target = scene.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "target");
    scene.removeNode(doomed);
    const size_t targetIndex = target.index();
    const size_t firstIndex = first.index();
    REQUIRE(traversalPosition(*layer, target) != targetIndex);
    const auto serverNames = namesByIndex(*layer);
    const SceneNodeRef targetRef{"studio", targetIndex};

    ServerLoop loop(&server);
    TestClient client;
    client.connect(server.port());
    REQUIRE(client.waitForCount(StudioMessageType::Hello, 1));
    client.send(Hello{});
    REQUIRE(client.waitForCount(StudioMessageType::BootstrapEnd, 1));
    REQUIRE(waitFor(
        [&] { return server.sessionState() == SessionState::Connected; }));

    WHEN("the bootstrap is applied to a mirror")
    {
      vsr::scene::Scene mirror;
      applySceneTransfers(mirror, client.messages());

      THEN("the mirror names every studio node by the server's index")
      {
        const auto *mirrorLayer = mirror.layer("studio");
        REQUIRE(mirrorLayer != nullptr);
        REQUIRE(namesByIndex(*mirrorLayer) == serverNames);
        auto mirrored = mirrorLayer->at(targetRef.nodeIndex);
        REQUIRE(mirrored);
        REQUIRE((*mirrored)->name() == "target");
      }
    }

    WHEN("the client sets the transform of the addressed node")
    {
      client.clear();
      SetNodeTransform edit;
      edit.node = targetRef;
      edit.transform = vsr::math::mat4{{2.f, 0.f, 0.f, 0.f},
          {0.f, 2.f, 0.f, 0.f},
          {0.f, 0.f, 2.f, 0.f},
          {5.f, 6.f, 7.f, 1.f}};
      client.send(edit);

      THEN("that node, and no other, takes the transform on the server")
      {
        REQUIRE(waitFor([&] {
          return (*layer->at(targetIndex))->getTransform() == edit.transform;
        }));
        REQUIRE((*layer->at(firstIndex))->getTransform()
            == vsr::math::IDENTITY_MAT4);
        REQUIRE(client.count(StudioMessageType::Error) == 0);
        // Origin-based echo suppression: the client's own edit is not pushed
        // back as a layer transfer.
        REQUIRE(client.count(StudioMessageType::TransferLayer) == 0);
      }
    }
  }
}
