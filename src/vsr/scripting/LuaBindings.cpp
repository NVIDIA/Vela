// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scripting/LuaBindings.hpp"

#include <sol/sol.hpp>

namespace vsr::scripting {

void registerAllBindings(sol::state &lua)
{
  sol::table vsr = lua.create_named_table("vsr");
  vsr["io"] = lua.create_table();
  vsr["render"] = lua.create_table();

  // Terminal command registry: scripts populate `vsr.terminal.commands` with
  // { run = fn(args) -> string, summary = string } records; the interactive
  // frontends dispatch a line's first token here before evaluating as Lua.
  sol::table terminal = lua.create_table();
  terminal["commands"] = lua.create_table();
  // Default help describing the C++-provided exposure. Owned here so it is
  // available even without the script pack; the fallback prints it and the
  // script-registered `help` command embeds it in its overview.
  terminal["defaultHelp"] =
      "Available globals:\n"
      "  scene         The current VSR scene\n"
      "  animationMgr  Animation collection + time/frame control\n"
      "  vsr           The VSR Lua module\n"
      "\n"
      "VSR namespaces:\n"
      "  vsr.io      Importers and procedural generators\n"
      "  vsr.render  Offline rendering (loadDevice, createRenderIndex, ...)\n"
      "  vsr.viewer  Viewer integration (refresh, addMenuAction; viewer only)\n"
      "\n"
      "Example:\n"
      "  vsr.io.generateRandomSpheres(scene)\n"
      "  print(scene:numberOfObjects(vsr.GEOMETRY))\n";
  vsr["terminal"] = terminal;

  // Register bindings in order of dependency
  registerMathBindings(lua);
  registerContextBindings(lua);
  registerAnimationManagerBindings(lua);
  registerObjectBindings(lua);
  registerLayerBindings(lua);
  registerIOBindings(lua);
  registerRenderBindings(lua);
}

} // namespace vsr::scripting
