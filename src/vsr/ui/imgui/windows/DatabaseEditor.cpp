// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DatabaseEditor.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// vsr_app
#include "vsr/app/Context.h"

namespace vsr::ui::imgui {

DatabaseEditor::DatabaseEditor(Application *app, const char *name)
    : Window(app, name)
{}

void DatabaseEditor::buildUI()
{
  ImGui::BeginDisabled(!appContext()->vsr.sceneLoadComplete);

  auto buildUI_objectSection = [&](const auto &ctxList,
                                   const char *headerText) {
    if (ctxList.empty())
      return;
    ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
    if (ImGui::CollapsingHeader(headerText, ImGuiTreeNodeFlags_None)) {
      vsr::core::foreach_item_const(ctxList, [&](auto *o) {
        if (!o)
          return;

        ImGui::Separator();

        ImGui::PushID(o);
        ImGui::BeginDisabled(o->totalUseCount() > 0);
        const bool doDelete = ImGui::Button("delete");
        ImGui::EndDisabled();
        if (doDelete)
          appContext()->vsr.scene.removeObject(o);
        else
          vsr::ui::buildUI_object(*o, appContext()->vsr.scene, true);
        ImGui::PopID();
      });
    }
  };

  const auto &db = appContext()->vsr.scene.objectDB();

  buildUI_objectSection(db.camera, "Cameras");
  buildUI_objectSection(db.renderer, "Renderers");
  buildUI_objectSection(db.light, "Lights");
  buildUI_objectSection(db.sampler, "Samplers");
  buildUI_objectSection(db.material, "Materials");
  buildUI_objectSection(db.geometry, "Geometries");
  buildUI_objectSection(db.surface, "Surfaces");
  buildUI_objectSection(db.field, "Spatial Fields");
  buildUI_objectSection(db.volume, "Volumes");
  buildUI_objectSection(db.array, "Arrays");

  ImGui::EndDisabled();
}

} // namespace vsr::ui::imgui