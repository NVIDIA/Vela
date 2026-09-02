// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "ProjectSnapshot.h"
#include "SceneEditMessages.h"
#include "SceneMessages.h"
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
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::client;
using vsr::network::Message;
namespace messages = vsr::network::messages;
using namespace std::chrono_literals;

namespace {

constexpr const char *LAYER_NAME = "extra";
constexpr const char *GEOMETRY_NAME = "bootstrapped geometry";

void populate(vsr::scene::Scene &scene)
{
  auto *layer = scene.addLayer(LAYER_NAME);
  auto geometry = scene.createObject<vsr::scene::Geometry>("sphere");
  geometry->setName(GEOMETRY_NAME);
  geometry->setParameter("radius", 0.25f);
  scene.insertChildObjectNode(layer->root(), geometry);
}

// Timings small enough to exercise every timer inside a test, generous
// enough not to flake under ctest parallelism.
ConnectionTimings fastTimings()
{
  ConnectionTimings t;
  t.pingAfterQuiet = 100ms;
  t.lossAfterSilence = 400ms;
  t.retryInitialDelay = 50ms;
  t.retryMaxDelay = 200ms;
  t.autoRetryFor = 10s;
  return t;
}

// Drives poll() until `done` holds or the deadline passes.
bool pollUntil(ServerConnection &connection,
    const std::function<bool()> &done,
    std::chrono::milliseconds timeout = 5s)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    connection.poll();
    if (done())
      return true;
    std::this_thread::sleep_for(1ms);
  }
  connection.poll();
  return done();
}

// Keeps polling for a fixed span; for asserting that nothing happens.
void pollFor(ServerConnection &connection, std::chrono::milliseconds span)
{
  const auto deadline = std::chrono::steady_clock::now() + span;
  while (std::chrono::steady_clock::now() < deadline) {
    connection.poll();
    std::this_thread::sleep_for(1ms);
  }
}

/*
 * A Studio server reduced to its session behaviour: Hello on accept, the
 * prebuilt bootstrap on the client's Hello, Pong on Ping unless silenced.
 * Everything runs on the server's IO thread; the test reads the counters.
 */
struct FakeServer
{
  FakeServer(int helloVersion = PROTOCOL_VERSION) : helloVersion(helloVersion)
  {
    channel = std::make_shared<vsr::network::NetworkServer>(0);
    channel->setConnectHandler([this]() {
      accepts++;
      Hello hello;
      hello.version = this->helloVersion;
      hello.buildInfo = "fake server";
      channel->send(encode(hello));
    });
    for (int value = 1; value < vsr::network::MESSAGE_TYPE_INVALID; ++value) {
      if (!isStudioMessageType(uint8_t(value)))
        continue;
      channel->registerHandler(
          uint8_t(value), [this](const Message &msg) { onMessage(msg); });
    }
    channel->start();
  }

  ~FakeServer()
  {
    channel->stop();
  }

  unsigned short port() const
  {
    return channel->port();
  }

  size_t count(StudioMessageType type)
  {
    std::lock_guard lock(mutex);
    size_t n = 0;
    for (const auto &msg : received)
      n += msg.header.type == uint8_t(type);
    return n;
  }

  std::vector<Message> messagesOf(StudioMessageType type)
  {
    std::lock_guard lock(mutex);
    std::vector<Message> out;
    for (const auto &msg : received)
      if (msg.header.type == uint8_t(type))
        out.push_back(msg);
    return out;
  }

  void send(Message msg)
  {
    channel->send(std::move(msg));
  }

  void sendBootstrapEnd()
  {
    send(encode(BootstrapEnd{}));
  }

  // The prebuilt bracket, then End unless it is being held back.
  void sendBootstrap()
  {
    for (const auto &m : bootstrap)
      channel->send(Message(m));
    if (!holdBootstrapEnd)
      sendBootstrapEnd();
  }

  void onMessage(const Message &msg)
  {
    {
      std::lock_guard lock(mutex);
      received.push_back(msg);
    }
    switch (StudioMessageType(msg.header.type)) {
    case StudioMessageType::Hello:
      if (!holdBootstrap)
        sendBootstrap();
      break;
    case StudioMessageType::Ping:
      if (!silent)
        channel->send(encode(Pong{}));
      break;
    default:
      break;
    }
  }

  int helloVersion{PROTOCOL_VERSION};
  std::shared_ptr<vsr::network::NetworkServer> channel;
  std::vector<Message> bootstrap; // sent on the client's Hello, in order
  std::atomic<bool> holdBootstrap{false}; // answer Hello with nothing at all
  std::atomic<bool> holdBootstrapEnd{false};
  std::atomic<bool> silent{false};
  std::atomic<int> accepts{0};
  std::mutex mutex;
  std::vector<Message> received;
};

// BootstrapBegin, scene, layer, frame config, snapshot; End is sent
// separately so a test can hold it back.
std::vector<Message> makeBootstrap(vsr::scene::Scene &source)
{
  std::vector<Message> out;
  out.push_back(encode(BootstrapBegin{}));
  messages::TransferScene scene(&source, false);
  out.push_back(encodeSceneMessage(scene, StudioMessageType::TransferScene));
  messages::TransferLayer layer(&source, source.layer(LAYER_NAME));
  out.push_back(encodeSceneMessage(layer, StudioMessageType::TransferLayer));
  FrameConfig config;
  config.width = 640;
  config.height = 480;
  out.push_back(encode(config));
  ProjectSnapshot snapshot;
  snapshot.project.name = "fake project";
  out.push_back(encode(snapshot));
  return out;
}

Message makeFrame(int frame)
{
  FrameHeader header;
  header.width = 2;
  header.height = 1;
  header.frame = frame;
  header.shotId = "shot";
  std::vector<std::byte> pixels(2 * 1 * 4, std::byte(frame));
  return encodeFrame(header, pixels.data(), pixels.size());
}

struct Fixture
{
  Fixture(int helloVersion = PROTOCOL_VERSION,
      ConnectionTimings timings = fastTimings())
      : server(helloVersion), connection(&mirror, timings)
  {
    populate(source);
    server.bootstrap = makeBootstrap(source);
    connection.onStateChanged = [this](ConnectionState, ConnectionState to) {
      transitions.push_back(to);
    };
    connection.onBootstrapBegin = [this]() {
      bootstrapBegins++;
      mirrorPopulatedAtBegin = mirror.numberOfObjects(ANARI_GEOMETRY) != 0;
    };
    connection.onBootstrapComplete = [this]() { bootstraps++; };
    connection.onServerError = [this](const std::string &m) {
      errors.push_back(m);
    };
  }

  void connect()
  {
    connection.connect("127.0.0.1", short(server.port()));
  }

  bool waitConnectedAndBootstrapped(int expectedBootstraps = 1)
  {
    return pollUntil(connection, [&] {
      return connection.state() == ConnectionState::Connected
          && bootstraps == expectedBootstraps;
    });
  }

  bool mirrorHasGeometry() const
  {
    if (mirror.numberOfObjects(ANARI_GEOMETRY) != 1)
      return false;
    auto geometry = mirror.getObject<vsr::scene::Geometry>(0);
    return geometry && geometry->name() == GEOMETRY_NAME;
  }

  vsr::scene::Scene source;
  vsr::scene::Scene mirror;
  FakeServer server;
  ServerConnection connection;
  std::vector<ConnectionState> transitions;
  int bootstrapBegins{0};
  bool mirrorPopulatedAtBegin{false};
  int bootstraps{0};
  std::vector<std::string> errors;
};

} // namespace

SCENARIO("ServerConnection handshakes and bootstraps", "[StudioClient]")
{
  GIVEN("a fake server holding BootstrapEnd back")
  {
    Fixture f;
    f.server.holdBootstrapEnd = true;
    REQUIRE(f.connection.state() == ConnectionState::NeverConnected);

    WHEN("the client connects")
    {
      f.connect();

      THEN("Hello is answered and the bootstrap populates the mirror")
      {
        REQUIRE(pollUntil(f.connection, [&] {
          return f.connection.state() == ConnectionState::Connected
              && f.connection.bootstrapping() && f.mirrorHasGeometry();
        }));
        REQUIRE(f.server.count(StudioMessageType::Hello) == 1);
        const auto hellos = f.server.messagesOf(StudioMessageType::Hello);
        const auto hello = decode<Hello>(hellos.front());
        REQUIRE(hello);
        REQUIRE(hello->version == PROTOCOL_VERSION);
        REQUIRE(f.mirror.layer(LAYER_NAME) != nullptr);
        REQUIRE(f.connection.frameConfig().width == 640);
        REQUIRE(f.connection.frameConfig().height == 480);
        REQUIRE(f.connection.project() != nullptr);
        REQUIRE(f.connection.project()->name == "fake project");
        REQUIRE(f.bootstraps == 0);
        REQUIRE(f.bootstrapBegins == 1);
        REQUIRE(f.transitions
            == std::vector<ConnectionState>{ConnectionState::Connected});

        AND_THEN("an edit during the bootstrap emits nothing")
        {
          auto geometry = f.mirror.getObject<vsr::scene::Geometry>(0);
          geometry->setParameter("radius", 0.5f);
          pollFor(f.connection, 50ms);
          REQUIRE(f.server.count(StudioMessageType::SetObjectParameter) == 0);

          AND_THEN("after BootstrapEnd an edit emits one SetObjectParameter")
          {
            f.server.sendBootstrapEnd();
            REQUIRE(pollUntil(f.connection, [&] { return f.bootstraps == 1; }));
            REQUIRE_FALSE(f.connection.bootstrapping());
            REQUIRE(f.connection.state() == ConnectionState::Connected);

            geometry->setParameter("radius", 0.75f);
            REQUIRE(pollUntil(f.connection, [&] {
              return f.server.count(StudioMessageType::SetObjectParameter) == 1;
            }));
            const auto edits =
                f.server.messagesOf(StudioMessageType::SetObjectParameter);
            const auto edit = decode<SetObjectParameter>(edits.front());
            REQUIRE(edit);
            REQUIRE(edit->object.type == ANARI_GEOMETRY);
            REQUIRE(edit->object.objectIndex == geometry->index());
            REQUIRE(edit->name == "radius");
            REQUIRE(edit->value.is<float>());
            REQUIRE(edit->value.get<float>() == 0.75f);

            geometry->removeParameter("radius");
            REQUIRE(pollUntil(f.connection, [&] {
              return f.server.count(StudioMessageType::RemoveObjectParameter)
                  == 1;
            }));
            pollFor(f.connection, 20ms);
            REQUIRE(f.server.count(StudioMessageType::SetObjectParameter) == 1);
          }
        }
      }
    }
  }

  GIVEN("a fake server speaking another protocol version")
  {
    Fixture f(PROTOCOL_VERSION + 1);

    WHEN("the client connects")
    {
      f.connect();

      THEN("the attempt fails, names both versions and does not retry")
      {
        REQUIRE(pollUntil(f.connection, [&] {
          return f.connection.statusText().find("mismatch")
              != std::string::npos;
        }));
        REQUIRE(f.connection.state() == ConnectionState::NeverConnected);
        REQUIRE_FALSE(f.connection.autoRetrying());
        const auto &status = f.connection.statusText();
        REQUIRE(status.find(std::to_string(PROTOCOL_VERSION + 1))
            != std::string::npos);
        REQUIRE(
            status.find(std::to_string(PROTOCOL_VERSION)) != std::string::npos);
        REQUIRE(f.server.count(StudioMessageType::Hello) == 0);
        pollFor(f.connection, 300ms);
        REQUIRE(f.server.accepts == 1);
        REQUIRE(f.transitions.empty());
      }
    }
  }
}

SCENARIO("ServerConnection watches liveness", "[StudioClient]")
{
  GIVEN("a connected client on a quiet link")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    WHEN("the link stays quiet past pingAfterQuiet")
    {
      REQUIRE(pollUntil(f.connection,
          [&] { return f.server.count(StudioMessageType::Ping) >= 1; }));

      THEN("the Pong keeps the connection alive")
      {
        pollFor(f.connection, 2 * fastTimings().lossAfterSilence);
        REQUIRE(f.connection.state() == ConnectionState::Connected);
        REQUIRE(f.server.count(StudioMessageType::Ping) >= 2);
        REQUIRE(f.server.accepts == 1);
      }
    }

    WHEN("the server goes totally silent")
    {
      f.server.silent = true;

      THEN("loss is declared, the view is frozen, and auto-retry reconnects")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.connection.state() == ConnectionState::Lost; }));
        REQUIRE(f.connection.autoRetrying());
        REQUIRE(f.mirrorHasGeometry());
        REQUIRE(f.connection.project() != nullptr);
        REQUIRE(f.connection.statusText().find("reconnecting")
            != std::string::npos);

        f.server.silent = false;
        REQUIRE(f.waitConnectedAndBootstrapped(2));
        REQUIRE(f.server.accepts == 2);
        REQUIRE(f.mirrorHasGeometry());
        // The frozen mirror was still populated when the second bootstrap
        // announced itself: the hook fires before the mirror is cleared.
        REQUIRE(f.bootstrapBegins == 2);
        REQUIRE(f.mirrorPopulatedAtBegin);
        REQUIRE(f.transitions
            == std::vector<ConnectionState>{ConnectionState::Connected,
                ConnectionState::Lost,
                ConnectionState::Connected});
      }
    }
  }

  GIVEN("a connected client whose server falls silent and comes back late")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    f.server.silent = true;
    REQUIRE(pollUntil(f.connection,
        [&] { return f.connection.state() == ConnectionState::Lost; }));
    REQUIRE(f.mirrorHasGeometry());

    WHEN("the reconnect is greeted but its bootstrap has not started")
    {
      f.server.holdBootstrap = true;
      f.server.silent = false;
      REQUIRE(pollUntil(f.connection, [&] {
        return f.connection.state() == ConnectionState::Connected
            && f.server.count(StudioMessageType::Hello) == 2;
      }));
      REQUIRE(f.bootstraps == 1);
      REQUIRE_FALSE(f.connection.bootstrapping());

      THEN("an edit to the frozen mirror emits nothing")
      {
        auto geometry = f.mirror.getObject<vsr::scene::Geometry>(0);
        REQUIRE(geometry);
        geometry->setParameter("radius", 0.9f);
        pollFor(f.connection, 50ms);
        REQUIRE(f.server.count(StudioMessageType::SetObjectParameter) == 0);

        AND_THEN("edits flow again once the bootstrap has completed")
        {
          f.server.sendBootstrap();
          REQUIRE(f.waitConnectedAndBootstrapped(2));
          auto rebuilt = f.mirror.getObject<vsr::scene::Geometry>(0);
          REQUIRE(rebuilt);
          rebuilt->setParameter("radius", 0.6f);
          REQUIRE(pollUntil(f.connection, [&] {
            return f.server.count(StudioMessageType::SetObjectParameter) == 1;
          }));
        }
      }
    }
  }

  GIVEN("a connected client whose server closes the socket")
  {
    auto timings = fastTimings();
    timings.lossAfterSilence = 3s; // loss must come from the hook, not this
    Fixture f(PROTOCOL_VERSION, timings);
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    WHEN("the server stops")
    {
      const auto closedAt = std::chrono::steady_clock::now();
      f.server.channel->stop();

      THEN("loss is declared promptly and disconnect() clears everything")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.connection.state() == ConnectionState::Lost; }));
        REQUIRE(std::chrono::steady_clock::now() - closedAt < 1s);
        REQUIRE(f.mirrorHasGeometry());

        f.connection.disconnect();
        REQUIRE(f.connection.state() == ConnectionState::Disconnected);
        REQUIRE_FALSE(f.connection.autoRetrying());
        REQUIRE(f.mirror.numberOfObjects(ANARI_GEOMETRY) == 0);
        REQUIRE(f.connection.project() == nullptr);
        pollFor(f.connection, 100ms);
        REQUIRE(f.connection.state() == ConnectionState::Disconnected);
      }
    }
  }
}

SCENARIO("ServerConnection rejects messages outside the Studio set",
    "[StudioClient]")
{
  GIVEN("a connected client")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    WHEN("the server sends the type bytes the enum never assigns")
    {
      // 0 is the sentinel, 255 the transport's MESSAGE_TYPE_INVALID.
      for (int outsideType : {0, 255}) {
        Message outside;
        outside.header.type = uint8_t(outsideType);
        f.server.send(std::move(outside));
      }

      THEN("each is answered with an Error naming the type")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.server.count(StudioMessageType::Error) == 2; }));
        const auto errors = f.server.messagesOf(StudioMessageType::Error);
        std::vector<std::string> texts;
        for (const auto &msg : errors) {
          const auto error = decode<Error>(msg);
          REQUIRE(error);
          texts.push_back(error->message);
        }
        REQUIRE(texts[0].find("unknown message type 0") != std::string::npos);
        REQUIRE(texts[1].find("unknown message type 255") != std::string::npos);
        REQUIRE(f.connection.state() == ConnectionState::Connected);
      }
    }
  }
}

SCENARIO("ServerConnection keeps only the latest frame", "[StudioClient]")
{
  GIVEN("a connected client")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    Message frame;
    REQUIRE_FALSE(f.connection.takeLatestFrame(frame));

    WHEN("two frames arrive before the UI takes one")
    {
      f.server.send(makeFrame(1));
      f.server.send(makeFrame(2));
      Error marker;
      marker.message = "marker";
      f.server.send(encode(marker));
      REQUIRE(pollUntil(f.connection, [&] { return f.errors.size() == 1; }));
      REQUIRE(f.errors.front() == "marker");

      THEN("only the second is taken, once")
      {
        REQUIRE(f.connection.takeLatestFrame(frame));
        const auto view = decodeFrame(frame);
        REQUIRE(view);
        REQUIRE(view->header.frame == 2);
        REQUIRE(view->header.shotId == "shot");
        REQUIRE_FALSE(f.connection.takeLatestFrame(frame));
      }
    }
  }
}
