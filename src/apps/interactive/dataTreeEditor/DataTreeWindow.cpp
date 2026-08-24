// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataTreeWindow.h"
// imgui
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
// std
#include <algorithm>
#include <cstdint>
#include <string>

namespace vsr::datatree_editor {

DataTreeWindow::DataTreeWindow(vsr::ui::imgui::Application *app,
    vsr::core::DataTree *tree,
    bool *dirty,
    const std::string *currentFile)
    : Window(app, "DataTree Editor"),
      m_tree(tree),
      m_dirty(dirty),
      m_currentFile(currentFile)
{}

void DataTreeWindow::buildUI()
{
  // Header: filename + dirty indicator
  if (m_currentFile && !m_currentFile->empty()) {
    ImGui::TextDisabled("%s%s", m_currentFile->c_str(), *m_dirty ? " *" : "");
  } else {
    ImGui::TextDisabled("<unsaved>%s", *m_dirty ? " *" : "");
  }

  // Toolbar: add a root-level entry
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
  if (ImGui::SmallButton("+ Add")) {
    m_addChildTarget = m_tree ? &m_tree->root() : nullptr;
    m_addChildName.clear();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Add a top-level entry to the tree");

  ImGui::Separator();

  // "Add Child" modal — must be triggered outside the table
  if (m_addChildTarget)
    ImGui::OpenPopup("Add Child##modal");

  if (ImGui::BeginPopupModal(
          "Add Child##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Name for new child node:");
    ImGui::SetNextItemWidth(280.f);
    bool pressedEnter = ImGui::InputText(
        "##childname", &m_addChildName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetItemDefaultFocus();

    bool ok = ImGui::Button("OK") || pressedEnter;
    ImGui::SameLine();
    bool cancel = ImGui::Button("Cancel");

    if (ok && !m_addChildName.empty()) {
      m_pendingOps.push_back(
          {PendingOp::AddChild, m_addChildTarget, m_addChildName});
      m_addChildName.clear();
      m_addChildTarget = nullptr;
      ImGui::CloseCurrentPopup();
    } else if (cancel || (ok && m_addChildName.empty())) {
      m_addChildName.clear();
      m_addChildTarget = nullptr;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  // Two-column scrollable table: Name | Value
  constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV
      | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
      | ImGuiTableFlags_ScrollY;

  const ImVec2 tableSize(0.f, ImGui::GetContentRegionAvail().y);

  if (!m_tree || ImGui::GetContentRegionAvail().y < 1.f)
    return;

  if (ImGui::BeginTable("##datatree", 2, tableFlags, tableSize)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableHeadersRow();

    auto &root = m_tree->root();
    root.foreach_child(
        [&](vsr::core::DataNode &child) { renderNode(child, root); });

    ImGui::EndTable();
  }

  applyPendingOps();
}

void DataTreeWindow::renderNode(
    vsr::core::DataNode &node, vsr::core::DataNode &parent)
{
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);

  ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

  if (node.isLeaf())
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

  ImGui::PushID(&node);
  bool open = ImGui::TreeNodeEx(node.name().c_str(), flags);

  // Right-click context menu
  if (ImGui::BeginPopupContextItem("##ctx")) {
    if (ImGui::MenuItem("Add child...")) {
      m_addChildTarget = &node;
      m_addChildName.clear();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete"))
      m_pendingOps.push_back({PendingOp::Delete, &parent, node.name()});
    ImGui::EndPopup();
  }

  // Value column
  ImGui::TableSetColumnIndex(1);
  renderValueEditor(node);

  ImGui::PopID();

  // Recurse into children if tree node is open
  if (open && !node.isLeaf()) {
    node.foreach_child(
        [&](vsr::core::DataNode &child) { renderNode(child, node); });
    ImGui::TreePop();
  }
}

void DataTreeWindow::renderValueEditor(vsr::core::DataNode &node)
{
  // Arrays: read-only summary
  if (node.holdsArray()) {
    anari::DataType type = ANARI_UNKNOWN;
    const void *data = nullptr;
    size_t size = 0;
    node.getValueAsArray(&type, &data, &size);
    ImGui::TextDisabled("%s[%zu]", anari::toString(type), size);
    return;
  }

  // Object references: read-only
  if (node.holdsObjectIdx()) {
    anari::DataType type = ANARI_UNKNOWN;
    size_t idx = 0;
    node.getValueAsObjectIdx(&type, &idx);
    ImGui::TextDisabled("%s @%zu", anari::toString(type), idx);
    return;
  }

  const auto &value = node.getValue();
  if (!value) {
    ImGui::TextDisabled("(empty)");
    return;
  }

  ImGui::PushItemWidth(-FLT_MIN); // fill column width

  const auto type = value.type();
  switch (type) {
  case ANARI_BOOL: {
    auto v = node.getValueAs<bool>();
    if (ImGui::Checkbox("##v", &v)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_INT8: {
    auto v = static_cast<int>(node.getValueAs<int8_t>());
    if (ImGui::InputInt("##v", &v)) {
      node.setValue(static_cast<int8_t>(v));
      *m_dirty = true;
    }
    break;
  }
  case ANARI_INT16: {
    auto v = static_cast<int>(node.getValueAs<int16_t>());
    if (ImGui::InputInt("##v", &v)) {
      node.setValue(static_cast<int16_t>(v));
      *m_dirty = true;
    }
    break;
  }
  case ANARI_INT32: {
    auto v = node.getValueAs<int32_t>();
    if (ImGui::InputInt("##v", &v)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_INT64: {
    auto v = node.getValueAs<int64_t>();
    constexpr int64_t step = 1;
    if (ImGui::InputScalar(
            "##v", ImGuiDataType_S64, &v, &step, nullptr, "%lld")) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_UINT8: {
    auto raw = node.getValueAs<uint8_t>();
    auto v = static_cast<int>(raw);
    if (ImGui::InputInt("##v", &v)) {
      node.setValue(static_cast<uint8_t>(std::clamp(v, 0, 255)));
      *m_dirty = true;
    }
    break;
  }
  case ANARI_UINT16: {
    auto raw = node.getValueAs<uint16_t>();
    auto v = static_cast<int>(raw);
    if (ImGui::InputInt("##v", &v)) {
      node.setValue(static_cast<uint16_t>(std::clamp(v, 0, 65535)));
      *m_dirty = true;
    }
    break;
  }
  case ANARI_UINT32: {
    auto v = node.getValueAs<uint32_t>();
    constexpr uint32_t step = 1u;
    if (ImGui::InputScalar("##v", ImGuiDataType_U32, &v, &step)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_UINT64: {
    auto v = node.getValueAs<uint64_t>();
    constexpr uint64_t step = 1ull;
    if (ImGui::InputScalar("##v", ImGuiDataType_U64, &v, &step)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_FLOAT32: {
    auto v = node.getValueAs<float>();
    if (ImGui::InputFloat("##v", &v)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_FLOAT64: {
    auto v = node.getValueAs<double>();
    if (ImGui::InputDouble("##v", &v)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_STRING: {
    auto s = node.getValueAs<std::string>();
    if (ImGui::InputText("##v", &s)) {
      node = s.c_str();
      *m_dirty = true;
    }
    break;
  }
  case ANARI_FLOAT32_VEC2: {
    auto v = node.getValueAs<anari::math::float2>();
    if (ImGui::InputFloat2("##v", &v.x)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_FLOAT32_VEC3: {
    auto v = node.getValueAs<anari::math::float3>();
    if (ImGui::InputFloat3("##v", &v.x)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_FLOAT32_VEC4: {
    auto v = node.getValueAs<anari::math::float4>();
    if (ImGui::InputFloat4("##v", &v.x)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_INT32_VEC2: {
    auto v = node.getValueAs<anari::math::int2>();
    if (ImGui::InputInt2("##v", &v.x)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_INT32_VEC3: {
    auto v = node.getValueAs<anari::math::int3>();
    if (ImGui::InputInt3("##v", &v.x)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  case ANARI_INT32_VEC4: {
    auto v = node.getValueAs<anari::math::int4>();
    if (ImGui::InputInt4("##v", &v.x)) {
      node = v;
      *m_dirty = true;
    }
    break;
  }
  default:
    ImGui::TextDisabled("%s", anari::toString(type));
    break;
  }

  ImGui::PopItemWidth();
}

void DataTreeWindow::applyPendingOps()
{
  for (auto &op : m_pendingOps) {
    if (op.type == PendingOp::AddChild)
      op.target->append(op.name);
    else if (op.type == PendingOp::Delete)
      op.target->remove(op.name);
    *m_dirty = true;
  }
  m_pendingOps.clear();
}

} // namespace vsr::datatree_editor
