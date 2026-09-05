// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioFakeProjectServer.h"
#include "StudioFakeServer.h"
#include "StudioRemoteTestHelpers.h"
#include "TestDirectories.h"
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "Script.h"
#include "ServerProcess.h"
#include "TestClientOptions.h"
#include "TestSession.h"
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
#include "ViewportMessages.h"
// vsr_scivis_studio_model
#include "Project.h"
#include "Shot.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::test_client;
using namespace std::chrono_literals;

namespace {

constexpr auto TEST_TIMEOUT = 10s;

std::vector<std::string> lines(const std::string &text)
{
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    out.push_back(line);
  return out;
}

bool hasLine(const std::vector<std::string> &records, const std::string &exact)
{
  for (const auto &r : records)
    if (r == exact)
      return true;
  return false;
}

bool hasLineStarting(
    const std::vector<std::string> &records, const std::string &prefix)
{
  for (const auto &r : records)
    if (r.rfind(prefix, 0) == 0)
      return true;
  return false;
}

size_t countStarting(
    const std::vector<std::string> &records, const std::string &prefix)
{
  size_t n = 0;
  for (const auto &r : records)
    n += r.rfind(prefix, 0) == 0;
  return n;
}

std::vector<std::string> failLines(const std::vector<std::string> &records)
{
  std::vector<std::string> out;
  for (const auto &r : records)
    if (r.rfind("FAIL ", 0) == 0)
      out.push_back(r);
  return out;
}

// Parses `script` and runs it through a fresh runner on `session`, returning
// run()'s verdict and the record stream.
struct RunResult
{
  bool ok{false};
  std::vector<std::string> records;
};

RunResult runScript(
    TestSession &session, const std::string &script, RunnerOptions options = {})
{
  std::vector<Command> commands;
  std::string error;
  REQUIRE(parseScript(script, commands, &error));
  std::ostringstream out;
  options.timeout = TEST_TIMEOUT;
  CommandRunner runner(&session, &out, options);
  RunResult result;
  result.ok = runner.run(commands);
  result.records = lines(out.str());
  return result;
}

} // namespace

SCENARIO("the test client parses its script language", "[StudioTestClient]")
{
  GIVEN("a script with comments, quotes and ; separators")
  {
    const std::string script =
        "# a comment line\n"
        "\n"
        "connect 127.0.0.1 4242   # trailing comment\n"
        "set-param camera 0 note string \"hello world\"; ping\n"
        "  assert lastError contains \"a ; b # c\"  \n"
        "\r\n"
        "await-frame 2 timeout=250\n";
    std::vector<Command> commands;
    std::string error;
    REQUIRE(parseScript(script, commands, &error));

    THEN("every command carries its name, arguments, line and text")
    {
      REQUIRE(commands.size() == 5);

      REQUIRE(commands[0].name == "connect");
      REQUIRE(
          commands[0].args == std::vector<std::string>{"127.0.0.1", "4242"});
      REQUIRE(commands[0].lineNumber == 3);
      REQUIRE(commands[0].text == "connect 127.0.0.1 4242");

      REQUIRE(commands[1].name == "set-param");
      REQUIRE(commands[1].args
          == std::vector<std::string>{
              "camera", "0", "note", "string", "hello world"});
      REQUIRE(commands[1].lineNumber == 4);
      REQUIRE(
          commands[1].text == "set-param camera 0 note string \"hello world\"");

      REQUIRE(commands[2].name == "ping");
      REQUIRE(commands[2].args.empty());
      REQUIRE(commands[2].lineNumber == 4);

      REQUIRE(commands[3].name == "assert");
      REQUIRE(commands[3].args
          == std::vector<std::string>{"lastError", "contains", "a ; b # c"});
      REQUIRE(commands[3].lineNumber == 5);

      REQUIRE(commands[4].name == "await-frame");
      REQUIRE(commands[4].lineNumber == 7);
    }

    THEN("a trailing timeout=<ms> is split off, once")
    {
      auto &await = commands[4];
      const auto timeout = takeTimeoutSuffix(await, &error);
      REQUIRE(timeout);
      REQUIRE(*timeout == 250ms);
      REQUIRE(await.args == std::vector<std::string>{"2"});
      REQUIRE_FALSE(takeTimeoutSuffix(await, &error));
      REQUIRE(error.empty());
      REQUIRE(await.args == std::vector<std::string>{"2"});
    }
  }

  GIVEN("-e style input")
  {
    std::vector<Command> commands;
    REQUIRE(parseScript("connect; start-rendering ;await-frame 3;", commands));
    THEN("the ; pieces are separate commands on the same line")
    {
      REQUIRE(commands.size() == 3);
      REQUIRE(commands[0].name == "connect");
      REQUIRE(commands[1].name == "start-rendering");
      REQUIRE(commands[2].name == "await-frame");
      REQUIRE(commands[2].args == std::vector<std::string>{"3"});
      for (const auto &c : commands)
        REQUIRE(c.lineNumber == 1);
    }
  }

  GIVEN("malformed input")
  {
    std::vector<Command> commands;
    std::string error;
    THEN("an unterminated quote names its line")
    {
      REQUIRE_FALSE(
          parseScript("ping\nassert a == \"open\n", commands, &error));
      REQUIRE(error.find("line 2") != std::string::npos);
      REQUIRE(commands.size() == 1);
    }
    THEN("a malformed timeout is an error, not an argument")
    {
      Command c;
      c.name = "ping";
      c.args = {"timeout=soon"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
      REQUIRE(error.find("timeout=soon") != std::string::npos);
    }
    THEN("a timeout no deadline can hold is an error too")
    {
      Command c;
      c.name = "ping";
      c.args = {"timeout=99999999999999999999"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
      REQUIRE(error.find("malformed timeout") != std::string::npos);
      c.args = {"timeout=9223372036854775807"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
      c.args = {"timeout=-5"};
      REQUIRE_FALSE(takeTimeoutSuffix(c, &error));
    }
  }

  GIVEN("$name variables in arguments")
  {
    std::vector<Command> commands;
    REQUIRE(parseScript(
        "update-shot $lastShotId name=$title path=$root/x $ 5$ $$root",
        commands));
    auto &command = commands[0];
    const auto lookup =
        [](const std::string &name) -> std::optional<std::string> {
      if (name == "lastShotId")
        return "shot_0002";
      if (name == "title")
        return "Intro";
      if (name == "root")
        return "/data";
      return {};
    };
    std::string error;

    THEN("every name expands, alone or inside a token; a bare $ stays")
    {
      REQUIRE(expandVariables(command, lookup, &error));
      REQUIRE(command.args
          == std::vector<std::string>{
              "shot_0002", "name=Intro", "path=/data/x", "$", "5$", "$/data"});
      REQUIRE(command.name == "update-shot");
    }
    THEN("an unknown variable is an error that leaves the arguments alone")
    {
      command.args = {"$lastShotId", "$nosuch"};
      REQUIRE_FALSE(expandVariables(command, lookup, &error));
      REQUIRE(error == "unknown variable $nosuch");
      REQUIRE(
          command.args == std::vector<std::string>{"$lastShotId", "$nosuch"});
    }
  }

  GIVEN("integer arguments")
  {
    long long signedValue = 0;
    unsigned long long unsignedValue = 0;
    std::chrono::milliseconds ms;
    THEN("whole decimal numbers that fit parse, nothing else does")
    {
      REQUIRE(parseInteger("-42", signedValue));
      REQUIRE(signedValue == -42);
      REQUIRE(parseNonNegative("18446744073709551615", unsignedValue));
      REQUIRE(unsignedValue == 18446744073709551615ull);
      REQUIRE_FALSE(parseInteger("", signedValue));
      REQUIRE_FALSE(parseInteger("12x", signedValue));
      REQUIRE_FALSE(parseInteger(" 12", signedValue));
      REQUIRE_FALSE(parseInteger("99999999999999999999", signedValue));
      REQUIRE_FALSE(parseNonNegative("-1", unsignedValue));
      REQUIRE_FALSE(parseNonNegative("+1", unsignedValue));
      REQUIRE(parseMilliseconds("0", ms));
      REQUIRE(parseMilliseconds("86400000", ms));
      REQUIRE(ms == 24h);
      REQUIRE_FALSE(parseMilliseconds("9223372036854775807", ms));
    }
  }
}

SCENARIO("the test client parses its command line", "[StudioTestClient]")
{
  TestClientOptions options;
  std::string error;
  const auto argv = [](std::initializer_list<const char *> items) {
    std::vector<std::string> out{"scivisStudioTestClient"};
    out.insert(out.end(), items.begin(), items.end());
    return out;
  };

  GIVEN("every flag")
  {
    REQUIRE(parseTestClientOptions(argv({"--host",
                                       "10.0.0.1",
                                       "--port",
                                       "4242",
                                       "-e",
                                       "connect; ping",
                                       "-e",
                                       "shutdown",
                                       "--timeout",
                                       "750",
                                       "--keep-going",
                                       "--quiet-events"}),
        options,
        &error));
    THEN("all values are recorded")
    {
      REQUIRE(options.runner.host == "10.0.0.1");
      REQUIRE(options.runner.port == 4242);
      REQUIRE(options.inlineScripts
          == std::vector<std::string>{"connect; ping", "shutdown"});
      REQUIRE(options.runner.timeout == 750ms);
      REQUIRE(options.runner.keepGoing);
      REQUIRE(options.runner.quietEvents);
      REQUIRE(options.scriptPath.empty());
    }
  }

  GIVEN("defaults and malformed input")
  {
    THEN("nothing given means stdin and the default endpoint")
    {
      REQUIRE(parseTestClientOptions(argv({}), options, &error));
      REQUIRE(options.runner.host == "127.0.0.1");
      REQUIRE(options.runner.port == DEFAULT_PORT);
      REQUIRE(options.runner.timeout == 5000ms);
      REQUIRE(options.scriptPath.empty());
      REQUIRE(options.inlineScripts.empty());
    }
    THEN("--script and -e are exclusive")
    {
      REQUIRE_FALSE(parseTestClientOptions(
          argv({"--script", "a.studio", "-e", "ping"}), options, &error));
    }
    THEN("--spawn-server takes the rest of the line as the server's")
    {
      REQUIRE(parseTestClientOptions(argv({"--script",
                                         "a.studio",
                                         "--require-device",
                                         "--spawn-server",
                                         "/bin/server",
                                         "--library",
                                         "helide",
                                         "--timeout",
                                         "7"}),
          options,
          &error));
      REQUIRE(options.spawnServer
          == std::vector<std::string>{
              "/bin/server", "--library", "helide", "--timeout", "7"});
      REQUIRE(options.requireDevice);
      REQUIRE(options.runner.timeout == 5000ms);
    }
    THEN(
        "--spawn-server needs a binary, owns the port, and --require-device"
        " needs it")
    {
      REQUIRE_FALSE(
          parseTestClientOptions(argv({"--spawn-server"}), options, &error));
      REQUIRE(error.find("--spawn-server") != std::string::npos);
      REQUIRE_FALSE(parseTestClientOptions(
          argv({"--port", "4242", "--spawn-server", "/bin/server"}),
          options,
          &error));
      REQUIRE(error.find("--port") != std::string::npos);
      REQUIRE_FALSE(
          parseTestClientOptions(argv({"--require-device"}), options, &error));
      REQUIRE(error.find("--require-device") != std::string::npos);
    }
    THEN("an unknown flag and a bad port are rejected by name")
    {
      REQUIRE_FALSE(parseTestClientOptions(argv({"--bogus"}), options, &error));
      REQUIRE(error.find("--bogus") != std::string::npos);
      REQUIRE_FALSE(
          parseTestClientOptions(argv({"--port", "70000"}), options, &error));
      REQUIRE(error.find("--port") != std::string::npos);
    }
    THEN("a --timeout no deadline can hold is rejected by name")
    {
      REQUIRE_FALSE(parseTestClientOptions(
          argv({"--timeout", "99999999999999999999"}), options, &error));
      REQUIRE(error.find("--timeout") != std::string::npos);
      REQUIRE_FALSE(parseTestClientOptions(
          argv({"--timeout", "9223372036854775807"}), options, &error));
    }
    THEN("the usage names every flag and every assert value")
    {
      const auto usage = testClientUsage("scivisStudioTestClient");
      for (const char *flag : {"--host",
               "--port",
               "--script",
               "-e",
               "--timeout",
               "--keep-going",
               "--quiet-events",
               "--spawn-server",
               "--require-device"})
        REQUIRE(usage.find(flag) != std::string::npos);
      for (const auto &spec : CommandRunner::namedValues())
        REQUIRE(usage.find(spec.name) != std::string::npos);
      for (const auto &spec : CommandRunner::commands())
        REQUIRE(usage.find(std::string("  ") + spec.name) != std::string::npos);
    }
  }
}

SCENARIO("the test client owns a server it spawned", "[StudioTestClient]")
{
  // A stand-in for scivisStudioServer: /bin/sh printing what the real one
  // prints. ServerProcess appends `--port N --data-root DIR`, which land in
  // the shell's positional parameters and are echoed back as a check.
  ScopedFixtureDirectory scratch("vsrStudioServerProcess-");
  const auto &work = scratch.path;
  std::filesystem::create_directories(work / "data");
  const auto log = work / "server.log";
  const auto fakeServer = [&](const char *script) {
    return ServerProcess({"/bin/sh", "-c", script, "fake"}, work / "data", log);
  };
  std::string error;

  GIVEN("a server that listens")
  {
    auto server = fakeServer(
        "echo \"[StudioServer] args: $*\";"
        " echo \"[StudioServer] Listening on port 4321\"; exec sleep 30");
    REQUIRE(server.start(&error));

    THEN("its Listening line gives the port and the log shows the arguments")
    {
      REQUIRE(server.awaitListening(TEST_TIMEOUT, &error)
          == ServerProcess::Start::Listening);
      REQUIRE(server.port() == 4321);
      REQUIRE(server.running());
      REQUIRE(server.log().find(
                  "args: --port 0 --data-root " + (work / "data").string())
          != std::string::npos);

      AND_THEN("a kill ends it, and a restart asks for the same port")
      {
        server.kill();
        REQUIRE_FALSE(server.running());
        REQUIRE(server.start(&error));
        REQUIRE(server.awaitListening(TEST_TIMEOUT, &error)
            == ServerProcess::Start::Listening);
        REQUIRE(server.log().find("args: --port 4321 --data-root ")
            != std::string::npos);
        server.stop();
        REQUIRE_FALSE(server.running());
      }
    }
  }

  GIVEN("a server that loads no device")
  {
    auto server = fakeServer(
        "echo \"[StudioServer] no ANARI device could be loaded (requested"
        " 'helide')\"; exit 1");
    REQUIRE(server.start(&error));

    THEN("the start is NoDevice, not a failure")
    {
      REQUIRE(server.awaitListening(TEST_TIMEOUT, &error)
          == ServerProcess::Start::NoDevice);
      REQUIRE_FALSE(server.running());
    }
  }

  GIVEN("a server that exits before listening")
  {
    auto server = fakeServer("echo \"[StudioServer] cannot listen\"; exit 3");
    REQUIRE(server.start(&error));

    THEN("the start Failed with the exit status in the reason")
    {
      REQUIRE(server.awaitListening(TEST_TIMEOUT, &error)
          == ServerProcess::Start::Failed);
      REQUIRE(error.find("exit status 3") != std::string::npos);
    }
  }

  GIVEN("a binary that cannot be spawned")
  {
    ServerProcess server({work / "no-such-server"}, work / "data", log);

    THEN("start says so")
    {
      REQUIRE_FALSE(server.start(&error));
      REQUIRE(error.find("no-such-server") != std::string::npos);
    }
  }

  GIVEN("a runner without a spawned server")
  {
    TestSession session;
    std::ostringstream out;
    std::vector<Command> commands;
    REQUIRE(parseScript(
        "copy-fixture x.obj; kill-server; await-server", commands, &error));
    RunnerOptions keepGoing;
    keepGoing.keepGoing = true;
    CommandRunner tolerant(&session, &out, keepGoing);

    THEN("the server commands FAIL naming --spawn-server")
    {
      REQUIRE_FALSE(tolerant.run(commands));
      const auto records = lines(out.str());
      REQUIRE(records.size() == 3);
      for (const auto &record : records) {
        INFO(record);
        REQUIRE(record.rfind("FAIL ", 0) == 0);
        REQUIRE(record.find("--spawn-server") != std::string::npos);
      }
    }
  }
}

SCENARIO("the command table is the one source of the command vocabulary",
    "[StudioTestClient]")
{
  GIVEN("the runner's command table")
  {
    const auto &commands = CommandRunner::commands();

    THEN("it is sorted by name, with one consistent row per command")
    {
      REQUIRE(commands.size() > 70);
      for (size_t i = 0; i < commands.size(); ++i) {
        const auto &spec = commands[i];
        INFO(spec.name);
        if (i > 0)
          REQUIRE(std::string(commands[i - 1].name) < spec.name);
        REQUIRE(spec.usage != nullptr);
        REQUIRE(std::string(spec.summary).size() > 0);
        REQUIRE(spec.minArgs >= 0);
        REQUIRE((spec.maxArgs == -1 || spec.maxArgs >= spec.minArgs));
        REQUIRE(spec.run.fn);
        REQUIRE(CommandRunner::findCommand(spec.name) == &spec);
      }
      REQUIRE(CommandRunner::findCommand("no-such-command") == nullptr);
    }

    THEN(
        "the README's command and assert-value tables are the ones"
        " --markdown prints")
    {
      const auto readme = std::filesystem::path(__FILE__).parent_path() / ".."
          / "apps" / "interactive" / "scivisStudioRemote" / "test_client"
          / "README.md";
      std::ifstream file(readme);
      REQUIRE(file);
      const std::string text((std::istreambuf_iterator<char>(file)), {});
      const auto table = testClientCommandTable();
      REQUIRE(table.find("| command | kind | does |") == 0);
      REQUIRE(text.find(table) != std::string::npos);
      const auto values = testClientAssertValueTable();
      REQUIRE(values.find("| value | is |") == 0);
      REQUIRE(text.find(values) != std::string::npos);
    }
  }

  GIVEN("the runner's value table")
  {
    const auto &values = CommandRunner::namedValues();

    THEN("every row has a distinct name, a summary and a resolver")
    {
      REQUIRE(values.size() > 30);
      for (size_t i = 0; i < values.size(); ++i) {
        const auto &spec = values[i];
        INFO(spec.name);
        REQUIRE(std::string(spec.name).size() > 0);
        REQUIRE(spec.summary.size() > 0);
        REQUIRE(spec.resolve != nullptr);
        for (size_t j = 0; j < i; ++j)
          REQUIRE(std::string(values[j].name) != spec.name);
      }
    }
  }
}

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
        [] {
          RunnerOptions o;
          o.keepGoing = true;
          return o;
        }());

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
        [] {
          RunnerOptions o;
          o.keepGoing = true;
          return o;
        }());

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
        [] {
          RunnerOptions o;
          o.keepGoing = true;
          return o;
        }());
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

    WHEN("a script runs the playback, pick, viewport and histogram commands")
    {
      const std::string script =
          "connect " + endpoint + "\n"
          "assert shot.active.playing == false\n"
          "assert shot.active.frameCount == @shot.shot_0001.frameCount\n"
          // The SetPlaying's snapshot and the auto-stop's arrive together;
          // each await-snapshot consumes one.
          "set-playing active on\n"
          "await-snapshot\n"
          "await-snapshot\n"
          "assert snapshots.received == 3\n"
          "assert shot.active.playing == false\n"
          "assert shot.shot_0001.currentFrame == 5\n"
          "expect-fail set-playing shot_9999 on\n"
          "assert lastReplyError contains active\n"
          "assert replies.failed == 1\n"
          // Frames 0, 1, 2, 0: three advances, the wrap is not a step.
          "start-rendering\n"
          "await-frame-advance 3\n"
          "assert frames.advanced == 3\n"
          "assert frames.maxStep == 1\n"
          "assert frame.frame == 0\n"
          "set-time active 7\n"
          "await-frame-at 7\n"
          "assert frame.frame == 7\n"
          "assert frames.advanced == 4\n"
          // A forward scrub is a forward step; only backward ones are not.
          "assert frames.maxStep == 7\n"
          "set-time shot_0001 99\n"
          "await-warning\n"
          "assert warnings.received == 1\n"
          "assert lastWarning contains load\n"
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
          "set-outline $lastPickType $lastPickIndex\n"
          "set-outline volume:2\n"
          "set-outline none\n"
          "set-outline\n"
          "viewport-settings visualizeAOV=depth depthVisualMinimum=0 depthVisualMaximum=10\n"
          "viewport-settings showWorldBounds=on worldBoundsWidth=2 worldBoundsColor=1,0,0,1\n"
          "viewport-settings\n"
          "request-array-histogram array 0 3\n"
          "assert histogram.bins == 3\n"
          "assert histogram.total == 6\n"
          "assert histogram.min == 0\n"
          "assert histogram.max == 1\n"
          "expect-fail request-array-histogram array:5 16\n"
          "assert lastReplyError contains scalar\n"
          "assert errors.received == 0\n"
          "disconnect\n";
      const auto result = runScript(session, script);
      for (const auto &f : failLines(result.records))
        WARN(f);
      REQUIRE(result.ok);

      THEN("the records show the snapshot's time, the frames and the replies")
      {
        const auto &r = result.records;
        REQUIRE(hasLine(r,
            "EVT ProjectSnapshot activeShot=shot_0001 shots=1 datasets=0"
            " lightRigs=1 cameraRigs=1 colorMaps=0 dirty=true playing=true"
            " currentFrame=0"));
        REQUIRE(hasLine(r,
            "EVT ProjectSnapshot activeShot=shot_0001 shots=1 datasets=0"
            " lightRigs=1 cameraRigs=1 colorMaps=0 dirty=true playing=false"
            " currentFrame=5"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=2 ok=false error=\"shot 'shot_9999'"
            " is not the active shot\""));
        REQUIRE(countStarting(r,
                    "EVT Frame width=2 height=2 encoding=Raw"
                    " pixelFormat=RGBA8_sRGB shotId=shot_0001 frame=0 bytes=16")
            == 2);
        REQUIRE(hasLine(r,
            "EVT Frame width=2 height=2 encoding=Raw pixelFormat=RGBA8_sRGB"
            " shotId=shot_0001 frame=7 bytes=16"));
        REQUIRE(hasLine(r,
            "EVT TimeAdvanceWarning shotId=shot_0001 frame=99"
            " message=\"frame 99 failed to load\""));
        REQUIRE(countStarting(r,
                    "EVT PickReply requestId=777 hit=false"
                    " worldPosition=\"0 0 0\" objectType=none objectIndex=none")
            == 2);
        REQUIRE(hasLine(r,
            "EVT PickReply requestId=3 hit=false worldPosition=\"0 0 0\""
            " objectType=none objectIndex=none"));
        REQUIRE(hasLine(r,
            "EVT PickReply requestId=4 hit=true worldPosition=\"0.5 0.25 -1\""
            " objectType=surface objectIndex=4"));
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=5 ok=true error=\"\" bins=3 min=0"
            " max=1 nonFinite=0"));
        REQUIRE(hasLine(r, "OK set-outline $lastPickType $lastPickIndex"));
      }

      THEN("the wire carries the resolved ids, pixels and composed settings")
      {
        const auto playing = server.requests<SetPlaying>();
        REQUIRE(playing.size() == 2);
        REQUIRE(playing[0].shotId == "shot_0001");
        REQUIRE(playing[0].playing);
        REQUIRE(playing[1].shotId == "shot_9999");
        const auto times = server.requests<SetTime>();
        REQUIRE(times.size() == 2);
        REQUIRE(times[0].shotId == "shot_0001");
        REQUIRE(times[0].frame == 7);
        REQUIRE(times[1].frame == 99);
        const auto picks = server.requests<Pick>();
        REQUIRE(picks.size() == 2);
        REQUIRE(picks[0].x == 0);
        REQUIRE(picks[0].y == 0);
        REQUIRE(picks[1].x == 10);
        REQUIRE(picks[1].y == 5);
        const auto outlines = server.requests<SetOutline>();
        REQUIRE(outlines.size() == 4);
        REQUIRE(outlines[0].objectIdentity);
        REQUIRE(outlines[0].objectIdentity->type == ANARI_SURFACE);
        REQUIRE(outlines[0].objectIdentity->objectIndex == 4);
        REQUIRE(outlines[1].objectIdentity);
        REQUIRE(outlines[1].objectIdentity->type == ANARI_VOLUME);
        REQUIRE(outlines[1].objectIdentity->objectIndex == 2);
        REQUIRE_FALSE(outlines[2].objectIdentity);
        REQUIRE_FALSE(outlines[3].objectIdentity);
        const auto settings = server.requests<ViewportSettings>();
        REQUIRE(settings.size() == 3);
        REQUIRE(settings[0].visualizeAOV == vsr::rendering::AOVType::DEPTH);
        REQUIRE(settings[0].depthVisualMaximum == 10.f);
        REQUIRE_FALSE(settings[0].showWorldBounds);
        REQUIRE(settings[0].highlightSelection); // the default, sent whole
        REQUIRE(settings[1].visualizeAOV == vsr::rendering::AOVType::DEPTH);
        REQUIRE(settings[1].depthVisualMaximum == 10.f);
        REQUIRE(settings[1].showWorldBounds);
        REQUIRE(settings[1].worldBoundsWidth == 2);
        REQUIRE(settings[1].worldBoundsColor.x == 1.f);
        REQUIRE(settings[1].worldBoundsColor.y == 0.f);
        REQUIRE(settings[1].worldBoundsColor.w == 1.f);
        REQUIRE(settings[2].showWorldBounds);
        REQUIRE(settings[2].visualizeAOV == vsr::rendering::AOVType::DEPTH);
        const auto histograms = server.requests<RequestArrayHistogram>();
        REQUIRE(histograms.size() == 2);
        REQUIRE(histograms[0].array.type == ANARI_ARRAY);
        REQUIRE(histograms[0].array.objectIndex == 0);
        REQUIRE(histograms[0].binCount == 3);
        REQUIRE(histograms[1].array.objectIndex == 5);
        REQUIRE(histograms[1].binCount == 16);
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
          [] {
            RunnerOptions o;
            o.keepGoing = true;
            return o;
          }());

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

    WHEN("a script renders a shot, cancels a long render and reads UI state")
    {
      const std::string script =
          "connect " + endpoint + "\n"
          "assert uiState.present == false\n"
          // An unsaved project cannot render; the refusal is a reply.
          "expect-fail render-shot active\n"
          "assert lastReplyError contains saved\n"
          "expect-fail render-shot shot_9999\n"
          "set-ui-state layout=abc theme=dark\n"
          "set-ui-state theme=light\n"
          "save-project /data/p1\n"
          "await-task\n"
          "await-snapshot\n"
          "assert task.last.state == Completed\n"
          "update-shot active frameCount=3\n"
          "await-snapshot\n"
          // The render: the active-shot snapshot, three determinate steps,
          // the end with the frame count and the output directory.
          "render-shot active\n"
          "assert var.lastTaskId == 2\n"
          "await-task\n"
          "await-snapshot\n"
          "assert task.last.state == Completed\n"
          "assert task.last.framesCompleted == 3\n"
          "assert task.last.current == 3\n"
          "assert task.last.total == 3\n"
          "assert task.2.message == /data/p1/renders/shot_0001\n"
          "assert tasks.completed == 2\n"
          "assert tasks.replayed == 0\n"
          // The long render: progress, a refused edit, the cancel.
          "update-shot active frameCount=400\n"
          "await-snapshot\n"
          "no-wait render-shot active\n"
          "await-reply\n"
          "assert var.lastTaskId == 3\n"
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
          // The UI state round trip: the saved tree comes back with the
          // OpenProject, and with the bootstrap of a fresh connection.
          "open-project /data/p1\n"
          "await-task\n"
          "await-task-progress timeout=100\n"
          "assert uiState.present == true\n"
          "assert uiState.layout == abc\n"
          "assert uiState.theme == light\n"
          "dump-ui-state\n"
          // A save that sends no tree leaves the retained one alone.
          "set-ui-state none\n"
          "save-project /data/p1\n"
          "await-task\n"
          "await-snapshot\n"
          "disconnect\n"
          "assert uiState.present == false\n"
          "reconnect\n"
          "assert uiState.theme == light\n"
          // The replay: every task that ended since the last bootstrap, the
          // ends heard live before the disconnect counted again.
          "assert tasks.replayed == 5\n"
          "assert task.3.state == Failed\n"
          "assert task.3.framesCompleted == 1\n"
          "assert task.2.framesCompleted == 3\n"
          "assert task.5.state == Completed\n"
          "assert tasks.completed == 8\n"
          "assert tasks.failed == 2\n"
          // Nothing ended since: an empty replay, and a disconnect forgets.
          "disconnect\n"
          "reconnect\n"
          "assert tasks.replayed == 0\n"
          "assert task.2.state == Completed\n"
          "disconnect\n";
      const auto result = runScript(session, script, [] {
        RunnerOptions o;
        o.keepGoing = true;
        return o;
      }());
      for (const auto &f : failLines(result.records))
        WARN(f);

      THEN("the records show the progress, the ends and the UI state")
      {
        const auto &r = result.records;
        const auto fails = failLines(r);
        // The two FAILs written as such: a task that ended without ever
        // reporting progress, and a record a disconnect forgot.
        REQUIRE(fails.size() == 2);
        REQUIRE(fails[0]
            == "FAIL await-task-progress timeout=100: task 4 ended (Completed)"
               " before reporting progress");
        REQUIRE(fails[1]
            == "FAIL assert task.2.state == Completed: task.2.state: nothing"
               " has been heard of task 2");
        REQUIRE(hasLine(r,
            "EVT ProjectOpReply requestId=1 ok=false error=\"project must be"
            " saved before rendering\""));
        REQUIRE(hasLine(r, "OK assert lastReplyError contains saved"));
        REQUIRE(hasLine(r,
            "EVT TaskProgress taskId=2 current=1 total=3 message=\"frame\""));
        REQUIRE(hasLine(r,
            "EVT TaskProgress taskId=2 current=3 total=3 message=\"frame\""));
        REQUIRE(hasLine(r,
            "EVT TaskCompleted taskId=2 message=\"/data/p1/renders/shot_0001\""
            " framesCompleted=3"));
        REQUIRE(hasLine(r,
            "EVT TaskProgress taskId=3 current=1 total=400 message=\"frame\""));
        REQUIRE(hasLine(r,
            "EVT TaskFailed taskId=3 error=\"cancelled\" framesCompleted=1"));
        REQUIRE(hasLine(r, "OK expect-fail await-reply"));
        REQUIRE(hasLine(r, "OK assert lastReplyError contains progress"));
        REQUIRE(hasLine(r, "EVT UIState present=false children=0"));
        REQUIRE(hasLine(r, "EVT UIState present=true children=1"));
        REQUIRE(hasLine(r,
            "EVT UIStateEntry path=\"windows/layout\""
            " value=\"abc\""));
        REQUIRE(hasLine(r,
            "EVT UIStateEntry path=\"windows/theme\""
            " value=\"light\""));
        REQUIRE(hasLine(r, "OK assert tasks.replayed == 5"));
        REQUIRE(hasLine(r, "OK assert tasks.completed == 8"));
        REQUIRE(hasLine(r, "OK assert tasks.replayed == 0"));
        REQUIRE(hasLine(r, "OK assert uiState.present == false"));
      }

      THEN("the wire carries the resolved shot ids and the UI state tree")
      {
        const auto renders = server.requests<RenderShot>();
        REQUIRE(renders.size() == 4);
        REQUIRE(renders[0].shotId == "shot_0001");
        REQUIRE(renders[1].shotId == "shot_9999");
        REQUIRE(renders[3].shotId == "shot_0001");
        const auto saves = server.requests<SaveProject>();
        REQUIRE(saves.size() == 2);
        REQUIRE_FALSE(saves[1].uiState);
        REQUIRE(saves[0].uiState);
        const auto *windows = saves[0].uiState->root().child("windows");
        REQUIRE(windows);
        REQUIRE(windows->numChildren() == 2);
        REQUIRE(windows->child("layout")->getValueAs<std::string>() == "abc");
        REQUIRE(windows->child("theme")->getValueAs<std::string>() == "light");
        REQUIRE(saves[0].uiState->root().child("layout") == nullptr);
        const auto cancels = server.requests<CancelTask>();
        REQUIRE(cancels.size() == 1);
        REQUIRE(cancels[0].taskId == 3);
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
          [] {
            RunnerOptions o;
            o.keepGoing = true;
            return o;
          }());

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
          [] {
            RunnerOptions o;
            o.keepGoing = true;
            return o;
          }());

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
