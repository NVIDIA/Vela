// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
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
#include "vsr/network/messages/TransferScene.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
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

bool waitFor(
    const std::function<bool()> &done, std::chrono::milliseconds timeout = 5s)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (done())
      return true;
    std::this_thread::sleep_for(1ms);
  }
  return done();
}

// The session test needs a real device; absent builds skip rather than fail.
bool helideAvailable()
{
  auto library = anari::loadLibrary("helide",
      [](const void *,
          ANARIDevice,
          ANARIObject,
          anari::DataType,
          ANARIStatusSeverity,
          ANARIStatusCode,
          const char *) {});
  if (!library)
    return false;
  anari::unloadLibrary(library);
  return true;
}

// A raw NetworkClient that records every Studio message in arrival order.
struct TestClient
{
  TestClient()
  {
    channel = std::make_shared<vsr::network::NetworkClient>();
    for (int value = 1; value < vsr::network::MESSAGE_TYPE_INVALID; ++value) {
      if (!isStudioMessageType(uint8_t(value)))
        continue;
      channel->registerHandler(uint8_t(value), [this](const Message &msg) {
        std::lock_guard lock(mutex);
        received.push_back(msg);
      });
    }
    channel->setDisconnectHandler(
        [this](const boost::system::error_code &) { disconnects++; });
  }

  ~TestClient()
  {
    channel->disconnect();
  }

  void connect(unsigned short port)
  {
    channel->connect("127.0.0.1", short(port));
  }

  template <typename T>
  void send(const T &payload)
  {
    channel->send(encode(payload));
  }

  size_t count(StudioMessageType type)
  {
    std::lock_guard lock(mutex);
    size_t n = 0;
    for (const auto &msg : received)
      n += msg.header.type == uint8_t(type);
    return n;
  }

  bool waitForCount(StudioMessageType type, size_t n)
  {
    return waitFor([&] { return count(type) >= n; });
  }

  std::vector<Message> messages()
  {
    std::lock_guard lock(mutex);
    return received;
  }

  // The last message of a type, or an invalid Message.
  Message last(StudioMessageType type)
  {
    std::lock_guard lock(mutex);
    for (auto it = received.rbegin(); it != received.rend(); ++it)
      if (it->header.type == uint8_t(type))
        return *it;
    return {};
  }

  void clear()
  {
    std::lock_guard lock(mutex);
    received.clear();
  }

  std::shared_ptr<vsr::network::NetworkClient> channel;
  std::mutex mutex;
  std::vector<Message> received;
  std::atomic<int> disconnects{0};
};

// Runs the server loop on its own thread and always brings it down, so a
// failed REQUIRE never leaves a joinable thread behind.
struct ServerLoop
{
  explicit ServerLoop(StudioServer &server)
      : server(server), thread([this, &server] {
          server.run();
          finished.store(true);
        })
  {}

  ~ServerLoop()
  {
    server.requestShutdown();
    thread.join();
  }

  StudioServer &server;
  std::atomic<bool> finished{false};
  std::thread thread;
};

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

    ServerLoop loop(server);

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
        REQUIRE(i + 3 == types.size());
        REQUIRE(types[i] == StudioMessageType::FrameConfig);
        REQUIRE(types[i + 1] == StudioMessageType::ProjectSnapshot);
        REQUIRE(types[i + 2] == StudioMessageType::BootstrapEnd);

        const auto config = decode<FrameConfig>(msgs[i]);
        REQUIRE(config);
        REQUIRE(config->width == shot->renderSettings.width);
        REQUIRE(config->height == shot->renderSettings.height);

        const auto snapshot = decode<ProjectSnapshot>(msgs[i + 1]);
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
            client.clear();
            Message outside;
            outside.header.type = 200;
            client.channel->send(std::move(outside));
            REQUIRE(client.waitForCount(StudioMessageType::Error, 1));
            auto refused = decode<Error>(client.last(StudioMessageType::Error));
            REQUIRE(refused);
            REQUIRE(refused->message.find("unknown message type 200")
                != std::string::npos);

            client.clear();
            Message newProject;
            newProject.header.type = uint8_t(StudioMessageType::NewProject);
            client.channel->send(std::move(newProject));
            REQUIRE(client.waitForCount(StudioMessageType::Error, 1));
            refused = decode<Error>(client.last(StudioMessageType::Error));
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
