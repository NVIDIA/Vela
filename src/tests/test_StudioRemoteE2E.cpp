// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
#include "FrameMessages.h"
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
#include <string>
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

} // namespace

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
