// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "HistogramPanel.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
// imgui
#include <imgui.h>
// std
#include <algorithm>

namespace vsr::scivis_studio::client {

namespace {

constexpr int MAX_BINS = 4096; // the server's clamp

bool sameRef(const SceneObjectRef &a, const SceneObjectRef &b)
{
  return a.type == b.type && a.objectIndex == b.objectIndex;
}

std::string objectLabel(const vsr::scene::Object &object)
{
  std::string label = object.name();
  if (label.empty())
    label = object.subtype().str();
  return label + " [" + anari::toString(object.type()) + " "
      + std::to_string(object.index()) + "]";
}

// The array parameters of `object`, labelled `prefix + name`.
void appendArrayParameters(const vsr::scene::Object &object,
    const std::string &prefix,
    std::vector<std::pair<std::string, SceneObjectRef>> &out)
{
  const size_t count = object.numParameters();
  for (size_t i = 0; i < count; ++i) {
    const auto &param = object.parameterAt(i);
    const auto &value = param.value();
    if (!anari::isArray(value.type()))
      continue;
    const size_t index = value.getAsObjectIndex();
    if (index == VSR_INVALID_INDEX)
      continue;
    std::string label = prefix + param.name().str() + "  ("
        + anari::toString(value.type()) + " " + std::to_string(index) + ")";
    out.emplace_back(std::move(label), SceneObjectRef{value.type(), index});
  }
}

} // namespace

HistogramPanel::HistogramPanel(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Histogram")
{}

HistogramPanel::~HistogramPanel() = default;

std::vector<HistogramPanel::ArrayChoice> HistogramPanel::arrayChoices(
    const vsr::scene::Object &object) const
{
  std::vector<std::pair<std::string, SceneObjectRef>> found;
  appendArrayParameters(object, "", found);

  // One level down: a volume's spatial field carries the data array.
  const auto &scene = appContext()->vsr.scene;
  const size_t count = object.numParameters();
  for (size_t i = 0; i < count; ++i) {
    const auto &param = object.parameterAt(i);
    const auto &value = param.value();
    if (value.type() != ANARI_SPATIAL_FIELD && value.type() != ANARI_GEOMETRY)
      continue;
    const auto *child = scene.getObject(value.type(), value.getAsObjectIndex());
    if (child)
      appendArrayParameters(*child, param.name().str() + "/", found);
  }

  std::vector<ArrayChoice> choices;
  for (auto &[label, ref] : found)
    choices.push_back(ArrayChoice{std::move(label), ref});
  return choices;
}

void HistogramPanel::refresh(const ArrayChoice &choice)
{
  if (!canSend())
    return;
  m_binCount = std::clamp(m_binCount, 1, MAX_BINS);
  const std::string label = choice.label;
  m_pending = ops().requestArrayHistogram(choice.ref,
      uint32_t(m_binCount),
      [this, label](const protocol::ProjectOpReply &reply,
          const std::optional<protocol::ArrayHistogramResult> &result) {
        if (!reply.ok) {
          m_error = reply.error;
          return;
        }
        if (!result) {
          m_error = "the reply carried no histogram";
          return;
        }
        m_error.clear();
        m_result = *result;
        m_resultLabel = label;
        m_plot.assign(result->bins.begin(), result->bins.end());
      });
}

void HistogramPanel::buildEditorUI(const Project &)
{
  auto *ctx = appContext();
  const auto node = ctx->getFirstSelected();
  const vsr::scene::Object *object =
      node.valid() ? (*node)->getObject() : nullptr;

  if (!object) {
    ImGui::TextDisabled("Select an object in the Layers window");
  } else {
    ImGui::TextUnformatted(objectLabel(*object).c_str());
    const SceneObjectRef current{object->type(), object->index()};
    if (!sameRef(current, m_listedFor)) {
      m_listedFor = current;
      m_choice = 0;
    }

    const auto choices = arrayChoices(*object);
    if (choices.empty()) {
      ImGui::TextDisabled("The selected object has no array parameters");
    } else {
      m_choice = std::clamp(m_choice, 0, int(choices.size()) - 1);
      if (ImGui::BeginCombo("Array", choices[m_choice].label.c_str())) {
        for (int i = 0; i < int(choices.size()); ++i) {
          const bool selected = i == m_choice;
          if (ImGui::Selectable(choices[i].label.c_str(), selected))
            m_choice = i;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::SetNextItemWidth(120.f);
      if (ImGui::InputInt("Bins", &m_binCount, 1, 16))
        m_binCount = std::clamp(m_binCount, 1, MAX_BINS);
      ImGui::SameLine();
      ImGui::BeginDisabled(pending(m_pending));
      if (ImGui::Button(pending(m_pending) ? "Waiting..." : "Refresh"))
        refresh(choices[m_choice]);
      ImGui::EndDisabled();
    }
  }

  ImGui::Separator();

  if (!m_error.empty())
    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", m_error.c_str());

  if (!m_result) {
    if (m_error.empty())
      ImGui::TextDisabled("No histogram yet");
    return;
  }

  ImGui::TextUnformatted(m_resultLabel.c_str());
  ImGui::Text("range: %g .. %g   bins: %zu",
      m_result->minValue,
      m_result->maxValue,
      m_plot.size());
  const float maxCount =
      m_plot.empty() ? 0.f : *std::max_element(m_plot.begin(), m_plot.end());
  const ImVec2 size(
      ImGui::GetContentRegionAvail().x, std::max(80.f, ImGui::GetContentRegionAvail().y));
  ImGui::PlotHistogram("##histogram",
      m_plot.data(),
      int(m_plot.size()),
      0,
      nullptr,
      0.f,
      maxCount,
      size);
}

} // namespace vsr::scivis_studio::client
