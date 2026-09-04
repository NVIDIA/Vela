// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioFakeServer.h"
#include "StudioRemoteTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "SceneEditMessages.h"
#include "SceneMessages.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/messages/TransferScene.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <chrono>
#include <string>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::client;
using vsr::network::Message;
namespace messages = vsr::network::messages;
using namespace std::chrono_literals;

namespace {

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
      ConnectionTimings timings = fastTimings());

  void connect();
  bool waitConnectedAndBootstrapped(int expectedBootstraps = 1);
  bool mirrorHasGeometry() const;

  vsr::scene::Scene source;
  vsr::scene::Scene mirror;
  FakeStudioServer server;
  ServerConnection connection;
  std::vector<ConnectionState> transitions;
  int mirrorReplaces{0};
  bool mirrorPopulatedAtReplace{false};
  int bootstraps{0};
  std::vector<std::string> errors;
};

Fixture::Fixture(int helloVersion, ConnectionTimings timings)
    : server(helloVersion), connection(&mirror, timings)
{
  populateFakeScene(source);
  server.bootstrap = makeFakeBootstrap(source);
  connection.onStateChanged = [this](ConnectionState, ConnectionState to) {
    transitions.push_back(to);
  };
  connection.onMirrorReplaceBegin = [this]() {
    mirrorReplaces++;
    mirrorPopulatedAtReplace = mirror.numberOfObjects(ANARI_GEOMETRY) != 0;
  };
  connection.onBootstrapComplete = [this]() { bootstraps++; };
  connection.onServerError = [this](
                                 const std::string &m) { errors.push_back(m); };
}

void Fixture::connect()
{
  connection.connect("127.0.0.1", server.port());
}

bool Fixture::waitConnectedAndBootstrapped(int expectedBootstraps)
{
  return pollUntil(connection, [&] {
    return connection.state() == ConnectionState::Connected
        && bootstraps == expectedBootstraps;
  });
}

bool Fixture::mirrorHasGeometry() const
{
  if (mirror.numberOfObjects(ANARI_GEOMETRY) != 1)
    return false;
  auto geometry = mirror.getObject<vsr::scene::Geometry>(0);
  return geometry && geometry->name() == FAKE_GEOMETRY_NAME;
}

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
        // The snapshot is the last message before the held-back End; once it
        // is in, everything before it has been applied.
        REQUIRE(pollUntil(f.connection, [&] {
          return f.connection.state() == ConnectionState::Connected
              && f.connection.bootstrapping() && f.mirrorHasGeometry()
              && f.connection.project() != nullptr;
        }));
        REQUIRE(f.server.count(StudioMessageType::Hello) == 1);
        const auto hellos = f.server.messagesOf(StudioMessageType::Hello);
        const auto hello = decode<Hello>(hellos.front());
        REQUIRE(hello);
        REQUIRE(hello->version == PROTOCOL_VERSION);
        REQUIRE(f.mirror.layer(FAKE_LAYER_NAME) != nullptr);
        REQUIRE(f.connection.frameConfig().width == 640);
        REQUIRE(f.connection.frameConfig().height == 480);
        REQUIRE(f.connection.project() != nullptr);
        REQUIRE(f.connection.project()->name == "fake project");
        REQUIRE(f.bootstraps == 0);
        REQUIRE(f.mirrorReplaces == 1);
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
        REQUIRE(f.mirrorReplaces == 2);
        REQUIRE(f.mirrorPopulatedAtReplace);
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
      // Connected is not populated: the replica is the old session's.
      REQUIRE_FALSE(f.connection.bootstrapped());
      REQUIRE(f.connection.project() != nullptr);

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
          REQUIRE(f.connection.bootstrapped());
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

  GIVEN("a connected client whose server comes back speaking another version")
  {
    auto timings = fastTimings();
    timings.lossAfterSilence = 3s; // loss must come from the hook, not this
    Fixture f(PROTOCOL_VERSION, timings);
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    // The server drops the socket but keeps listening, like a restart.
    f.server.channel->restart();
    f.server.helloVersion = PROTOCOL_VERSION + 1;
    REQUIRE(pollUntil(f.connection,
        [&] { return f.connection.state() == ConnectionState::Lost; }));
    REQUIRE(f.mirrorHasGeometry());

    WHEN("the retry is greeted with a mismatched Hello")
    {
      THEN("the client is Disconnected with the mismatch as its status")
      {
        REQUIRE(pollUntil(f.connection, [&] {
          return f.connection.state() == ConnectionState::Disconnected;
        }));
        REQUIRE(
            f.connection.statusText().find("mismatch") != std::string::npos);
        REQUIRE_FALSE(f.connection.autoRetrying());
        REQUIRE(f.mirror.numberOfObjects(ANARI_GEOMETRY) == 0);
        REQUIRE(f.connection.project() == nullptr);
        const int accepts = f.server.accepts;
        pollFor(f.connection, 300ms);
        REQUIRE(f.connection.state() == ConnectionState::Disconnected);
        REQUIRE(f.server.accepts == accepts); // no retry loop
        REQUIRE(f.transitions
            == std::vector<ConnectionState>{ConnectionState::Connected,
                ConnectionState::Lost,
                ConnectionState::Disconnected});
      }
    }
  }

  GIVEN("a client whose server drops it and returns with a slow bootstrap")
  {
    auto timings = fastTimings();
    timings.lossAfterSilence = 3s; // loss must come from the hook, not this
    Fixture f(PROTOCOL_VERSION, timings);
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    f.server.holdBootstrapEnd = true;
    f.server.channel->restart();
    REQUIRE(pollUntil(f.connection,
        [&] { return f.connection.state() == ConnectionState::Lost; }));

    WHEN("the reconnect's bootstrap is cut short by a close")
    {
      REQUIRE(pollUntil(f.connection, [&] {
        return f.connection.bootstrapping() && f.mirrorHasGeometry();
      }));
      REQUIRE(f.bootstraps == 1);
      f.server.channel->stop();

      THEN(
          "loss leaves an empty mirror, no bootstrap in progress, and the"
          " previous replica")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.connection.state() == ConnectionState::Lost; }));
        REQUIRE_FALSE(f.connection.bootstrapping());
        REQUIRE(f.mirror.numberOfObjects(ANARI_GEOMETRY) == 0);
        REQUIRE(f.connection.project() != nullptr);
        REQUIRE(f.bootstraps == 1);
        // The cut-short bracket announced its replacement once; the loss
        // announced the emptying once more.
        REQUIRE(f.mirrorReplaces == 3);
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

SCENARIO("ServerConnection takes the loss reason from the server's farewell",
    "[StudioClient]")
{
  GIVEN("a connected client whose link will not go quiet on its own")
  {
    auto timings = fastTimings();
    timings.lossAfterSilence = 10s; // the loss must come from the close
    Fixture f(PROTOCOL_VERSION, timings);
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());

    WHEN("the server says Disconnect{reason} and closes long after")
    {
      Disconnect farewell;
      farewell.reason = "replaced by another client";
      f.server.send(encode(farewell));
      // Well past the two seconds the old Error-then-close heuristic gave.
      pollFor(f.connection, 2200ms);
      REQUIRE(f.connection.state() == ConnectionState::Connected);
      f.server.channel->restart();

      THEN("the loss names the farewell's reason and no Error was involved")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.connection.state() == ConnectionState::Lost; }));
        REQUIRE(f.connection.statusText().find("replaced by another client")
            != std::string::npos);
        REQUIRE(f.errors.empty());
        REQUIRE(f.connection.autoRetrying());
      }
    }

    WHEN("the server sends a bare Error and then closes")
    {
      Error error;
      error.message = "something else entirely";
      f.server.send(encode(error));
      REQUIRE(pollUntil(f.connection, [&] { return !f.errors.empty(); }));
      f.server.channel->restart();

      THEN("the Error was a toast; the loss reason is the socket's")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.connection.state() == ConnectionState::Lost; }));
        REQUIRE(
            f.errors == std::vector<std::string>{"something else entirely"});
        REQUIRE(f.connection.statusText().find("something else entirely")
            == std::string::npos);
      }
    }
  }
}

SCENARIO("ServerConnection announces a mid-session scene replacement",
    "[StudioClient]")
{
  GIVEN("a connected, bootstrapped client")
  {
    Fixture f;
    f.connect();
    REQUIRE(f.waitConnectedAndBootstrapped());
    REQUIRE(f.mirrorReplaces == 1);

    WHEN("the server pushes a whole TransferScene outside a bootstrap")
    {
      auto second = f.source.createObject<vsr::scene::Geometry>("sphere");
      second->setName("second geometry");
      messages::TransferScene resend(&f.source, false);
      f.server.send(
          encodeSceneMessage<StudioMessageType::TransferScene>(resend));

      THEN(
          "the hook fires while the old mirror still stands, then it is"
          " replaced")
      {
        REQUIRE(pollUntil(f.connection,
            [&] { return f.mirror.numberOfObjects(ANARI_GEOMETRY) == 2; }));
        REQUIRE(f.mirrorReplaces == 2);
        REQUIRE(f.mirrorPopulatedAtReplace);
        REQUIRE(f.bootstraps == 1);
        REQUIRE_FALSE(f.connection.bootstrapping());

        AND_THEN("edits still flow afterwards")
        {
          auto geometry = f.mirror.getObject<vsr::scene::Geometry>(1);
          REQUIRE(geometry);
          geometry->setParameter("radius", 0.3f);
          REQUIRE(pollUntil(f.connection, [&] {
            return f.server.count(StudioMessageType::SetObjectParameter) == 1;
          }));
        }
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
