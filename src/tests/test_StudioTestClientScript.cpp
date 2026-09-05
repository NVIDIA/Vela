// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioTestClientTestHelpers.h"
#include "TestDirectories.h"
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "Script.h"
#include "ServerProcess.h"
#include "TestClientOptions.h"
#include "TestSession.h"
// vsr_scivis_studio_protocol
#include "StudioProtocol.h"
// std
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio::test_client;
using namespace std::chrono_literals;

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
