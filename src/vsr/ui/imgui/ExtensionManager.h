// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef VSR_USE_LUA
namespace vsr::scripting {
class LuaContext;
}
#endif

namespace vsr {
namespace app {
struct Context;
}
namespace ui::imgui {

struct ActionEntry
{
  std::string path; // "glTF/Geometry/Box"
  std::string displayName; // "Box"
  std::function<void()> fn;
};

struct ActionMenuNode
{
  std::string name;
  bool isFolder{false};
  bool isSeparator{false};
  std::vector<ActionMenuNode> children;
  size_t actionIndex{SIZE_MAX}; // index into m_actions, SIZE_MAX = folder
};

class ExtensionManager
{
 public:
  ExtensionManager();
  ~ExtensionManager();

  void initialize(vsr::app::Context *ctx);
  void refresh();

  void addMenuAction(const std::string &path, std::function<void()> fn);
  void addSeparator(const std::string &categoryPath);
  void clearActions();

  const std::vector<ActionMenuNode> &getMenuTree();
  void executeAction(size_t actionIndex);

#ifdef VSR_USE_LUA
  scripting::LuaContext &luaContext();
#endif

  static std::vector<std::string> getSearchPaths();

 private:
  void registerViewerBindings();
  void rebuildMenuTree();

#ifdef VSR_USE_LUA
  std::unique_ptr<scripting::LuaContext> m_luaContext;
#endif
  std::vector<ActionEntry> m_actions;
  std::vector<ActionMenuNode> m_menuTree;
  bool m_menuDirty{true};
  vsr::app::Context *m_ctx{nullptr};
};

} // namespace ui::imgui
} // namespace vsr
