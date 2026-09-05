// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioFakeServer.h"
#include "StudioTestClientTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "TestSession.h"
// vsr_scivis_studio_protocol
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// std
#include <chrono>
#include <string>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::test_client;
using namespace std::chrono_literals;

SCENARIO(
    "the command runner fails cleanly without a server", "[StudioTestClient]")
{
  TestSession session;

  GIVEN("a port nobody listens on")
  {
    unsigned short closedPort = 0;
    {
      vsr::network::NetworkServer probe(0);
      closedPort = probe.port();
    }
    const auto result = runScript(session,
        "connect 127.0.0.1 " + std::to_string(closedPort) + "\n"
        "assert state == NeverConnected\n");

    THEN("connect FAILs with the socket error and the state is untouched")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 1);
      REQUIRE(result.records[0].rfind("FAIL connect", 0) == 0);
      REQUIRE(result.records[0].find("connect failed") != std::string::npos);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }

  GIVEN("commands that need a connection or a frame")
  {
    const auto result = runScript(session,
        "ping\n"
        "set-frame-config 8 8\n"
        "set-param camera 0 fovy float32 1\n"
        "save-frame /nonexistent/frame.ppm\n"
        "dump-frame\n"
        "dump-project\n"
        "assert frame.width == 8\n"
        "assert project.shots == 1\n"
        "assert nonsense == 1\n"
        "assert state ~= Connected\n"
        "frobnicate\n"
        "assert state == NeverConnected\n"
        "assert scene.objects >= 0\n"
        "assert frames.received == 0\n"
        "assert errors.received == 0\n"
        "assert lastError == \"\"\n"
        "sleep 9223372036854775807\n"
        "set-param camera 0 n uint8 300\n"
        "set-param camera 0 n int8 -129\n"
        "set-param camera 0 n uint32 -1\n"
        "set-param camera 0 n int64 -9223372036854775808\n"
        "send-raw 20 -1\n"
        "send-raw 20 +f\n",
        keepGoing());

    THEN("--keep-going runs everything, records each FAIL and still fails")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 23);
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 18);
      REQUIRE(fails[0].find("not connected") != std::string::npos);
      REQUIRE(fails[3].find("no frame") != std::string::npos);
      REQUIRE(fails[5].find("no Project Replica") != std::string::npos);
      REQUIRE(fails[8].find("unknown value 'nonsense'") != std::string::npos);
      REQUIRE(
          fails[8].find("param.<type>.<index>.<name>") != std::string::npos);
      REQUIRE(fails[9].find("unknown operator") != std::string::npos);
      REQUIRE(fails[10].find("unknown command") != std::string::npos);
      REQUIRE(hasLine(result.records, "OK assert state == NeverConnected"));
      REQUIRE(hasLine(result.records, "OK assert lastError == \"\""));
    }

    THEN("values a component cannot hold are rejected, not wrapped")
    {
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 18);
      REQUIRE(fails[11].find("usage: sleep") != std::string::npos);
      REQUIRE(
          fails[12].find("not a uint8 component: 300") != std::string::npos);
      REQUIRE(
          fails[13].find("not a int8 component: -129") != std::string::npos);
      REQUIRE(
          fails[14].find("not a uint32 component: -1") != std::string::npos);
      // int64's minimum fits; only the missing connection stops it.
      REQUIRE(fails[15].find("not connected") != std::string::npos);
      REQUIRE(fails[16].find("not a hex byte: -1") != std::string::npos);
      REQUIRE(fails[17].find("not a hex byte: +f") != std::string::npos);
    }
  }

  GIVEN("the same failing script without --keep-going")
  {
    const auto result = runScript(session, "ping\nassert state == Lost\n");
    THEN("the first FAIL ends the run")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 1);
    }
  }
}

SCENARIO("the test client refuses a server speaking another protocol version",
    "[StudioTestClient]")
{
  GIVEN("a server whose Hello carries the wrong version")
  {
    FakeStudioServer server(PROTOCOL_VERSION + 1);
    TestSession session;
    const auto result = runScript(session,
        "connect 127.0.0.1 " + std::to_string(server.port()) + "\n"
        "assert state == NeverConnected\n");

    THEN("connect FAILs naming the mismatch after printing the Hello")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 2);
      REQUIRE(result.records[0]
          == "EVT Hello version=" + std::to_string(PROTOCOL_VERSION + 1)
              + " buildInfo=\"fake server\"");
      REQUIRE(result.records[1].rfind("FAIL connect", 0) == 0);
      REQUIRE(result.records[1].find("protocol version mismatch")
          != std::string::npos);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }

  GIVEN("a server that answers the client's Hello with an Error and closes")
  {
    FakeStudioServer server;
    server.holdBootstrap = true;
    server.onHello = [&] {
      Error error;
      error.message = "the scripted server refuses";
      server.farewell(encode(error));
    };
    TestSession session;
    const auto result = runScript(
        session, "connect 127.0.0.1 " + std::to_string(server.port()) + "\n");

    THEN("the Error is heard before the close: connect FAILs as refused")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(hasLine(
          result.records, "EVT Error message=\"the scripted server refuses\""));
      REQUIRE(result.records.back().rfind("FAIL connect", 0) == 0);
      REQUIRE(result.records.back().find(
                  "server refused: the scripted server refuses")
          != std::string::npos);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }
}

SCENARIO("the test client stays unconnected until the Bootstrap completes",
    "[StudioTestClient]")
{
  GIVEN("a server that says Hello and never bootstraps")
  {
    FakeStudioServer server;
    server.holdBootstrap = true;
    TestSession session;
    const auto endpoint = "127.0.0.1 " + std::to_string(server.port());
    const auto result = runScript(session,
        "connect " + endpoint + " timeout=300\n"
        "assert state == NeverConnected\n"
        "ping\n"
        "connect " + endpoint + " timeout=300\n"
        "assert state == NeverConnected\n",
        keepGoing());

    THEN(
        "the deadline FAILs connect, the state is untouched, and connect"
        " may be tried again")
    {
      REQUIRE_FALSE(result.ok);
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 3);
      REQUIRE(fails[0].rfind("FAIL connect", 0) == 0);
      REQUIRE(fails[0].find("no complete Bootstrap") != std::string::npos);
      REQUIRE(
          fails[1].find("not connected (NeverConnected)") != std::string::npos);
      REQUIRE(fails[2].rfind("FAIL connect", 0) == 0);
      REQUIRE(fails[2].find("no complete Bootstrap") != std::string::npos);
      REQUIRE(countStarting(result.records, "EVT Hello") == 2);
      REQUIRE(countStarting(result.records, "OK assert state == NeverConnected")
          == 2);
      REQUIRE(session.state() == test_client::SessionState::NeverConnected);
    }
  }
}

SCENARIO("the test client takes the loss reason from the server's farewell",
    "[StudioTestClient]")
{
  GIVEN("a server that bootstraps, says Disconnect{reason} and closes")
  {
    FakeStudioServer server;
    server.bootstrap = {encode(BootstrapBegin{})}; // empty, but complete
    server.onHello = [&] {
      // The close waits a little so the client's connect has settled
      // before the loss lands.
      Disconnect goodbye;
      goodbye.reason = "replaced by another client";
      server.farewell(encode(goodbye), 300ms);
    };
    TestSession session;
    const auto result = runScript(session,
        "connect 127.0.0.1 " + std::to_string(server.port()) + "\n"
        "await-lost timeout=3000\n"
        "assert state == Lost\n");

    THEN("the farewell is an event and its reason is why the link was lost")
    {
      REQUIRE(result.ok);
      REQUIRE(hasLine(result.records,
          "EVT Disconnect reason=\"replaced by another client\""));
      REQUIRE(session.state() == test_client::SessionState::Lost);
      REQUIRE(session.failure() == "replaced by another client");
    }
  }
}

SCENARIO("the test client's liveness timers end a wait on a silent server",
    "[StudioTestClient]")
{
  GIVEN(
      "a server that bootstraps and then never speaks again, and a session"
      " with fast timings")
  {
    FakeStudioServer server;
    server.bootstrap = {encode(BootstrapBegin{})}; // empty, but complete
    server.silent = true;
    SessionTimings timings;
    timings.pingAfterQuiet = 100ms;
    timings.lossAfterSilence = 400ms;
    TestSession session(timings);

    const auto started = std::chrono::steady_clock::now();
    const auto result = runScript(session,
        "connect 127.0.0.1 " + std::to_string(server.port()) + "\n"
        "assert state == Connected\n"
        "ping\n"
        "expect-pong timeout=8000\n"
        "assert state == Lost\n",
        keepGoing());
    const auto elapsed = std::chrono::steady_clock::now() - started;

    THEN(
        "the session pings, declares the loss, and the wait FAILs at once"
        " naming it")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(hasLine(result.records, "OK assert state == Connected"));
      REQUIRE(hasLine(result.records, "OK ping"));
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 1);
      REQUIRE(fails[0]
          == "FAIL expect-pong timeout=8000: connection lost while waiting"
             " for Pong: no traffic from server for 400 ms");
      REQUIRE(hasLine(result.records, "OK assert state == Lost"));
      REQUIRE(elapsed < 4s);
      // The script's Ping and at least one liveness Ping after the quiet.
      REQUIRE(server.count(StudioMessageType::Ping) >= 2);
    }
  }
}
