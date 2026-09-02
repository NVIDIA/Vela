// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_test_client_core
#include "CommandRunner.h"
#include "Script.h"
#include "TestClientOptions.h"
#include "TestSession.h"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
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

// A StudioServer on its own loop thread. Stopping it is what the client sees
// as the server going away: run() tears the listening socket down.
struct RunningServer
{
  explicit RunningServer(int port);
  ~RunningServer();

  void stop();
  unsigned short port() const;
  vsr::scene::Scene &scene();

  std::unique_ptr<StudioServer> server;
  bool started{false};
  std::string startError;
  // A transform node the tests can address with set-node-transform; a fresh
  // project has none of its own.
  size_t transformNode{VSR_INVALID_INDEX};
  std::atomic<bool> finished{false};
  std::thread thread;
};

RunningServer::RunningServer(int port)
{
  ServerOptions options;
  options.port = port;
  options.library = "helide";
  options.dataRoots = {std::filesystem::temp_directory_path()};
  server = std::make_unique<StudioServer>(options);
  started = server->start(&startError);
  if (!started)
    return;
  // Before run(): start() is the last thing on this thread that may touch the
  // scene.
  auto &s = scene();
  if (auto *layer = s.layer("studio")) {
    auto node = s.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "test transform");
    transformNode = node.index();
  }
  thread = std::thread([this] {
    server->run();
    finished.store(true);
  });
}

RunningServer::~RunningServer()
{
  stop();
}

void RunningServer::stop()
{
  if (!thread.joinable())
    return;
  server->requestShutdown();
  thread.join();
}

unsigned short RunningServer::port() const
{
  return server->port();
}

vsr::scene::Scene &RunningServer::scene()
{
  return server->appContext().vsr.scene;
}

// A fake server on a bare NetworkServer that misbehaves in one scripted way,
// so the client's failure paths run without a StudioServer.
struct ScriptedServer
{
  enum class Behaviour
  {
    MismatchedHello, // a Hello of the wrong version, nothing else
    HelloOnly, // the right Hello, then silence: no Bootstrap ever comes
    RefuseOnHello, // answers the client's Hello with an Error and closes
    SilentAfterBootstrap // an empty Bootstrap, then nothing, Pings included
  };

  explicit ScriptedServer(Behaviour behaviour);
  ~ScriptedServer();

  unsigned short port() const;

  std::shared_ptr<vsr::network::NetworkServer> channel;
  std::atomic<size_t> pingsReceived{0};
  // RefuseOnHello: the Error is flushed and the socket closed off the IO
  // thread, the way StudioServer's farewell works.
  std::atomic<bool> refusing{false};
  vsr::network::MessageFuture farewell;
  std::thread closer;
};

ScriptedServer::ScriptedServer(Behaviour behaviour)
{
  channel = std::make_shared<vsr::network::NetworkServer>(0);
  channel->setConnectHandler([this, behaviour]() {
    Hello hello;
    hello.version = behaviour == Behaviour::MismatchedHello
        ? PROTOCOL_VERSION + 1
        : PROTOCOL_VERSION;
    hello.buildInfo = "scripted server";
    channel->send(encode(hello));
    if (behaviour == Behaviour::SilentAfterBootstrap) {
      channel->send(encode(BootstrapBegin{}));
      channel->send(encode(BootstrapEnd{}));
    }
  });
  channel->registerHandler(uint8_t(StudioMessageType::Ping),
      [this](const vsr::network::Message &) { ++pingsReceived; });
  if (behaviour == Behaviour::RefuseOnHello) {
    channel->registerHandler(uint8_t(StudioMessageType::Hello),
        [this](const vsr::network::Message &) {
          Error error;
          error.message = "the scripted server refuses";
          farewell = channel->send(encode(error));
          refusing.store(true);
        });
    closer = std::thread([this] {
      if (!waitFor([&] { return refusing.load(); }))
        return;
      farewell.wait_for(1s);
      channel->restart();
    });
  }
  channel->start();
}

ScriptedServer::~ScriptedServer()
{
  refusing.store(true);
  if (closer.joinable())
    closer.join();
  channel->stop();
}

unsigned short ScriptedServer::port() const
{
  return channel->port();
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
    THEN("an unknown flag and a bad port are rejected by name")
    {
      REQUIRE_FALSE(parseTestClientOptions(argv({"--bogus"}), options, &error));
      REQUIRE(error.find("--bogus") != std::string::npos);
      REQUIRE_FALSE(
          parseTestClientOptions(argv({"--port", "70000"}), options, &error));
      REQUIRE(error.find("--port") != std::string::npos);
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
               "--quiet-events"})
        REQUIRE(usage.find(flag) != std::string::npos);
      for (const auto &name : CommandRunner::assertNames())
        REQUIRE(usage.find(name) != std::string::npos);
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
        "assert lastError == \"\"\n",
        [] {
          RunnerOptions o;
          o.keepGoing = true;
          return o;
        }());

    THEN("--keep-going runs everything, records each FAIL and still fails")
    {
      REQUIRE_FALSE(result.ok);
      REQUIRE(result.records.size() == 16);
      const auto fails = failLines(result.records);
      REQUIRE(fails.size() == 11);
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
    ScriptedServer server(ScriptedServer::Behaviour::MismatchedHello);
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
              + " buildInfo=\"scripted server\"");
      REQUIRE(result.records[1].rfind("FAIL connect", 0) == 0);
      REQUIRE(result.records[1].find("protocol version mismatch")
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
    ScriptedServer server(ScriptedServer::Behaviour::HelloOnly);
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
    auto server = std::make_unique<RunningServer>(0);
    REQUIRE(server->started);
    REQUIRE(server->transformNode != VSR_INVALID_INDEX);
    const auto port = server->port();
    const auto endpoint = "127.0.0.1 " + std::to_string(port);

    const auto &project = server->server->projectContext().project();
    const auto *shot = project::activeShot(project);
    REQUIRE(shot);
    const auto cameraIndex = std::to_string(shot->camera.objectIndex);
    const auto camera = "camera " + cameraIndex;
    const auto cameraParam = "param.camera." + cameraIndex + ".";
    const auto ppm = (std::filesystem::temp_directory_path()
        / ("vsrStudioTestClient-" + std::to_string(port) + ".ppm"))
                         .string();

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
          "ping\n"
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
          "set-param " + camera + " tint float32_vec4 1 0 0 1\n"
          "assert " + cameraParam + "tint != \"0 0 0 0\"\n"
          "remove-param " + camera + " note\n"
          "set-node-transform studio " + std::to_string(server->transformNode)
          + " 2 0 0 0 0 2 0 0 0 0 2 0 5 6 7 1\n"
          "send-raw 255\n"
          "expect-error \"unknown message type 255\"\n"
          "send-raw 0\n"
          "expect-error \"unknown message type 0\"\n"
          "send-raw 20 0a0b 0c\n"
          "expect-error \"not implemented\"\n"
          "assert errors.received == 3\n"
          "assert lastError contains NewProject\n"
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
        REQUIRE(hasLine(r, "EVT Pong"));
        REQUIRE(hasLine(r, "EVT FrameConfig width=32 height=24"));
        REQUIRE(countStarting(r,
                    "EVT Frame width=32 height=24 encoding=Raw"
                    " pixelFormat=RGBA8_sRGB shotId="
                        + project.activeShotId + " frame=0 bytes=3072")
            >= 3); // two awaited, one from dump-frame, maybe more in flight
        REQUIRE(
            hasLineStarting(r, "EVT Object type=camera index=" + cameraIndex));
        REQUIRE(hasLineStarting(r, "EVT Object type=renderer"));
        REQUIRE(hasLineStarting(r, "EVT Layer index=0 name=\"studio\""));
        REQUIRE(hasLineStarting(r,
            "EVT Project name=\"" + project.name + "\" activeShot="
                + project.activeShotId + " shots=1 datasets=0"));
        REQUIRE(hasLine(r, "EVT Error message=\"unknown message type 255\""));
        REQUIRE(hasLine(r, "EVT Error message=\"unknown message type 0\""));
        REQUIRE(hasLineStarting(
            r, "EVT Error message=\"NewProject is not implemented"));
        REQUIRE(hasLine(r, "OK expect-error \"not implemented\""));
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
        std::filesystem::remove(ppm);
      }

      THEN("the edits reached the server and the shutdown ended its run()")
      {
        REQUIRE(waitFor([&] { return server->finished.load(); }));
        auto &scene = server->scene();
        auto *cam = scene.getObject(ANARI_CAMERA, shot->camera.objectIndex);
        REQUIRE(cam);
        REQUIRE(cam->parameterValueAs<float>("fovy") == 0.9f);
        REQUIRE(cam->parameter("note") == nullptr);
        REQUIRE(cam->parameterValueAs<int>("count") == -7);
        auto *layer = scene.layer("studio");
        REQUIRE(layer);
        auto node = layer->at(server->transformNode);
        REQUIRE(node);
        const auto xfm = (*node)->getTransform();
        REQUIRE(xfm[0][0] == 2.f);
        REQUIRE(xfm[3][0] == 5.f);
        REQUIRE(xfm[3][2] == 7.f);
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
      REQUIRE(server->finished.load());

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

        AND_THEN("reconnect bootstraps from a restarted server")
        {
          server = std::make_unique<RunningServer>(int(port));
          REQUIRE(server->started);
          REQUIRE(server->port() == port);

          const auto back = runScript(session,
              "reconnect\n"
              "assert state == Connected\n"
              "assert scene.objects > 0\n"
              "ping\n"
              "shutdown\n"
              "assert state == Disconnected\n"
              "reconnect\n");
          REQUIRE_FALSE(back.ok);
          REQUIRE(hasLine(back.records, "OK reconnect"));
          REQUIRE(hasLine(back.records, "OK assert state == Connected"));
          REQUIRE(hasLine(back.records, "OK ping"));
          REQUIRE(hasLine(back.records, "OK shutdown"));
          REQUIRE(hasLine(back.records, "OK assert state == Disconnected"));
          REQUIRE(back.records.back().rfind("FAIL reconnect", 0) == 0);
          REQUIRE(waitFor([&] { return server->finished.load(); }));
        }
      }
    }
  }
}
