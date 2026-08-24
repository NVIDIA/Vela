## UI Library (`vsr_ui_imgui`)

`vsr_ui_imgui` currently provides an ImGui-based UI toolkit centered on
`vsr::ui::imgui::Application`.

### High-Level Concepts

- `vsr::ui::imgui::Application` extends `anari_viewer::Application` with VSR
  app context, scene-aware menus, task queue integration, and session
  save/load support.
- Reusable windows and modals for common workflows:
  viewport(s), log, layer tree, object editor, database editor, timeline,
  camera poses, import/export dialogs, offline rendering dialog, and others.
- `ExtensionManager` builds hierarchical action menus and supports runtime
  extension actions (including Lua-driven actions when `VSR_USE_LUA=ON`).
- `vsr_ui_imgui.h` contains low-level object/parameter editor UI builders for
  custom windows.

### Why Use This Library

- You want to build a full interactive VSR viewer/editor quickly.
- You need production-ready scene inspection/editing widgets instead of writing
  each ImGui panel from scratch.
- You want a common UI framework shared across standalone viewer, MPI viewer,
  network client, and interactive demos.

### Build Notes

- Enable with `-DVSR_BUILD_UI_LIBRARY=ON`.
- Lua-related menu extension features are active when `-DVSR_USE_LUA=ON`.
