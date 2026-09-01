// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
// std
#include <filesystem>
#include <string>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::server;

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
