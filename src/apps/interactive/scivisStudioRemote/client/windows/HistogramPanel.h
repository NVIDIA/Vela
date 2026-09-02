// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "EditorWindow.h"
// vsr_scivis_studio_protocol
#include "ViewportMessages.h"
// std
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

/*
 * Histogram of one of the first selected object's arrays, computed by the
 * server (RequestArrayHistogram): the client has no array data, only the
 * Structural Mirror's array parameters and their identities. The combo lists
 * the array-typed parameters of the selected object and, for a volume, of
 * the spatial field its "value" names; Refresh asks for the chosen array
 * with the bin count; the reply is plotted, a refusal (vector element type,
 * CUDA or proxy array) is shown as the server's own text.
 *
 * Example:
 *   auto *histogram = new HistogramPanel(this, &m_editorContext);
 *   windows.emplace_back(histogram);
 */
struct HistogramPanel : public EditorWindow
{
  HistogramPanel(vsr::ui::imgui::Application *app, EditorContext *context);
  ~HistogramPanel() override;

 private:
  struct ArrayChoice
  {
    std::string label;
    SceneObjectRef ref;
  };

  void buildEditorUI(const Project &project) override;
  std::vector<ArrayChoice> arrayChoices(const vsr::scene::Object &object) const;
  void refresh(const ArrayChoice &choice);

  int m_choice{0};
  int m_binCount{64};
  // Whose arrays the combo listed last; a new selection resets the choice.
  SceneObjectRef m_listedFor;

  RequestHandle m_pending;
  std::string m_error;
  std::string m_resultLabel;
  std::optional<protocol::ArrayHistogramResult> m_result;
  std::vector<float> m_plot; // bins as floats for ImGui
};

} // namespace vsr::scivis_studio::client
