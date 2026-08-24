// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sol {
class state;
}

namespace vsr::scene {
struct Scene;
}

namespace vsr::animation {
struct AnimationManager;
}

namespace vsr::scripting {

using PrintCallback = std::function<void(const std::string &)>;

struct ExecutionResult
{
  bool success{false};
  std::string error;
  std::string output;
};

// Result of attempting to dispatch a console line to a registered terminal
// command (the `vsr.terminal.commands` table). `handled` is false when the
// line's first token is not a registered command, in which case the caller
// should evaluate the line as Lua instead.
struct ConsoleCommandResult
{
  bool handled{false};
  bool success{false};
  std::string output; // string returned by the command's run()
  std::string error;
};

class LuaContext
{
 public:
  LuaContext();
  ~LuaContext();

  LuaContext(const LuaContext &) = delete;
  LuaContext &operator=(const LuaContext &) = delete;
  LuaContext(LuaContext &&) = delete;
  LuaContext &operator=(LuaContext &&) = delete;

  ExecutionResult executeFile(const std::string &filepath);
  ExecutionResult executeString(const std::string &script);

  // If `line`'s first whitespace-delimited token names a registered command in
  // `vsr.terminal.commands`, call its `run(args)` with the remaining tokens and
  // return the result. Single-line input only; returns `handled == false`
  // otherwise so the caller can evaluate `line` as Lua.
  ConsoleCommandResult runRegisteredCommand(const std::string &line);

  // The C++-owned default help text (`vsr.terminal.defaultHelp`), shown by the
  // frontends when no `help` command is registered.
  std::string consoleDefaultHelp();

  // Scene is NOT owned by LuaContext
  void bindScene(scene::Scene *scene, const std::string &varName = "scene");

  // Scene IS owned by LuaContext
  scene::Scene *createOwnedScene(const std::string &varName = "scene");

  void bindAnimationManager(vsr::animation::AnimationManager *sa,
      const std::string &varName = "animationMgr");

  scene::Scene *boundScene() const;

  // Adds paths to Lua's package.path and executes any init.lua found in them.
  // Returns errors encountered (empty on success).
  std::vector<std::string> addScriptSearchPaths(
      const std::vector<std::string> &paths);

  // Returns search paths in priority order:
  //   1. <source>/scripts/         (dev builds with VSR_SOURCE_DIR)
  //   2. <exe>/../share/vsr/scripts/
  //   3. ~/.config/vsr/scripts/    (or %APPDATA%/vsr/scripts/ on Windows)
  //   4. VSR_LUA_PACKAGE_PATHS env var (: or ; separated)
  static std::vector<std::string> defaultSearchPaths();

  void setPrintCallback(PrintCallback callback);

  sol::state &lua();
  const sol::state &lua() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace vsr::scripting
