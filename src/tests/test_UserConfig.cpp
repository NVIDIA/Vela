// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/UserConfig.hpp"
// std
#include <cstdlib>
#include <optional>
#include <string>

#ifndef _WIN32

namespace {

// Sets or unsets one environment variable for the life of the object.
class ScopedEnv
{
 public:
  ScopedEnv(const char *name, const char *value) : m_name(name)
  {
    if (const char *old = std::getenv(name))
      m_old = old;
    if (value)
      setenv(name, value, 1);
    else
      unsetenv(name);
  }

  ~ScopedEnv()
  {
    if (m_old)
      setenv(m_name.c_str(), m_old->c_str(), 1);
    else
      unsetenv(m_name.c_str());
  }

 private:
  std::string m_name;
  std::optional<std::string> m_old;
};

} // namespace

SCENARIO("vsr::core::userConfigDirectory()", "[UserConfig]")
{
  ScopedEnv home("HOME", "/home/someone");

  GIVEN("No XDG_CONFIG_HOME")
  {
    ScopedEnv xdg("XDG_CONFIG_HOME", nullptr);

    THEN("The directory is ~/.config/vela")
    {
      REQUIRE(vsr::core::userConfigDirectory()
          == std::filesystem::path("/home/someone/.config/vela"));
    }
  }

  GIVEN("A relative XDG_CONFIG_HOME")
  {
    ScopedEnv xdg("XDG_CONFIG_HOME", "relative/config");

    THEN("It is ignored")
    {
      REQUIRE(vsr::core::userConfigDirectory()
          == std::filesystem::path("/home/someone/.config/vela"));
    }
  }

#ifdef __linux__
  GIVEN("An absolute XDG_CONFIG_HOME")
  {
    ScopedEnv xdg("XDG_CONFIG_HOME", "/elsewhere/config");

    THEN("The directory is under it")
    {
      REQUIRE(vsr::core::userConfigDirectory()
          == std::filesystem::path("/elsewhere/config/vela"));
    }
  }
#endif

  GIVEN("Neither HOME nor XDG_CONFIG_HOME")
  {
    ScopedEnv noHome("HOME", nullptr);
    ScopedEnv xdg("XDG_CONFIG_HOME", nullptr);

    THEN("The directory is the relative path 'vela'")
    {
      REQUIRE(
          vsr::core::userConfigDirectory() == std::filesystem::path("vela"));
    }
  }
}

#endif
