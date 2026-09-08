// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AddStaticDatasetDialog.h"
// vsr_scivis_studio_modals
#include "modals/ModalUI.h"
// imgui
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
// std
#include <array>

namespace vsr::scivis_studio::modals {

namespace {

struct DatasetSourceChoice
{
  const char *name;
  // No importer: the source is a VSR archive rather than a foreign format.
  std::optional<vsr::io::ImporterType> importer;
  // A VSR Layer Subtree Archive (as saved from vsrViewer's LayerTree) rather
  // than a Dataset Archive. Only meaningful when 'importer' is unset.
  bool subtree = false;
};

// The user picks the importer explicitly; nothing is inferred.
constexpr std::array<DatasetSourceChoice, 28> SOURCES = {{
    {"AGX", vsr::io::ImporterType::AGX},
    {"ASSIMP", vsr::io::ImporterType::ASSIMP},
    {"ASSIMP_FLAT", vsr::io::ImporterType::ASSIMP_FLAT},
    {"AXYZ", vsr::io::ImporterType::AXYZ},
    {"DLAF", vsr::io::ImporterType::DLAF},
    {"E57XYZ", vsr::io::ImporterType::E57XYZ},
    {"ENSIGHT", vsr::io::ImporterType::ENSIGHT},
    {"GLTF", vsr::io::ImporterType::GLTF},
    {"HDRI", vsr::io::ImporterType::HDRI},
    {"HSMESH", vsr::io::ImporterType::HSMESH},
    {"NBODY", vsr::io::ImporterType::NBODY},
    {"OBJ", vsr::io::ImporterType::OBJ},
    {"PDB", vsr::io::ImporterType::PDB},
    {"PLY", vsr::io::ImporterType::PLY},
    {"POINTSBIN_MULTIFILE", vsr::io::ImporterType::POINTSBIN_MULTIFILE},
    {"PT", vsr::io::ImporterType::PT},
    {"SILO", vsr::io::ImporterType::SILO},
    {"SMESH", vsr::io::ImporterType::SMESH},
    {"SWC", vsr::io::ImporterType::SWC},
    {"TRK", vsr::io::ImporterType::TRK},
    {"USD", vsr::io::ImporterType::USD},
    {"USD_MTLX", vsr::io::ImporterType::USD_MTLX},
    {"VTP", vsr::io::ImporterType::VTP},
    {"VTU", vsr::io::ImporterType::VTU},
    {"XYZDP", vsr::io::ImporterType::XYZDP},
    {"VOLUME", vsr::io::ImporterType::VOLUME},
    {"VSR Dataset Archive", std::nullopt},
    {"VSR Layer Subtree Archive", std::nullopt, true},
}};

// Advisory: a browse greys files outside these; archives only.
std::vector<std::string> browseExtensions(const DatasetSourceChoice &choice)
{
  if (!choice.importer)
    return archiveExtensions();
  return {};
}

} // namespace

AddStaticDatasetDialog::AddStaticDatasetDialog(vsr::ui::imgui::Application *app,
    std::unique_ptr<BrowseProvider> browse,
    std::unique_ptr<Action> action)
    : Modal(app, "Add Static Dataset"),
      m_browse(std::move(browse)),
      m_action(std::move(action))
{}

AddStaticDatasetDialog::~AddStaticDatasetDialog() = default;

void AddStaticDatasetDialog::reset()
{
  m_name.clear();
  m_sourcePath.clear();
  m_error.clear();
  m_action->reset();
}

void AddStaticDatasetDialog::submit()
{
  if (m_sourcePath.empty()) {
    m_error = "Enter a source path.";
    return;
  }

  const auto &choice = SOURCES[m_selectedSource];
  Request request;
  request.name = m_name;
  request.sourcePath = m_sourcePath;
  request.importer = choice.importer;
  request.subtree = choice.subtree;

  m_error.clear();
  m_action->submit(request, [this](bool ok, const std::string &error) {
    if (!ok) {
      m_error = error;
      return;
    }
    reset();
    hide();
  });
}

void AddStaticDatasetDialog::buildUI()
{
  const bool busy = m_action->busy();
  const auto &choice = SOURCES[m_selectedSource];

  ImGui::BeginDisabled(busy);
  ImGui::SetNextItemWidth(420.f);
  ImGui::InputText("Name", &m_name);

  if (ImGui::Button("Browse...##datasetSource")) {
    BrowseRequest request;
    request.mode = BrowseMode::OpenFile;
    request.title = "Choose the dataset source file";
    request.extensions = browseExtensions(choice);
    request.onAccept = [this](const std::vector<std::filesystem::path> &paths) {
      if (!paths.empty())
        m_sourcePath = paths.front().generic_string();
    };
    m_browse->browse(std::move(request));
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(420.f);
  ImGui::InputText("Source Path", &m_sourcePath);

  ImGui::SetNextItemWidth(420.f);
  if (ImGui::BeginCombo("Source", choice.name)) {
    for (int i = 0; i < int(SOURCES.size()); ++i) {
      const bool selected = i == m_selectedSource;
      if (ImGui::Selectable(SOURCES[i].name, selected))
        m_selectedSource = i;
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();

  const char *actionLabel = choice.importer
      ? "Import"
      : (choice.subtree ? "Load Subtree" : "Load Archive");
  switch (modalFooter(*m_browse, *m_action, m_error, actionLabel)) {
  case ModalChoice::Cancelled:
    reset();
    hide();
    break;
  case ModalChoice::Submitted:
    submit();
    break;
  case ModalChoice::None:
    break;
  }
}

} // namespace vsr::scivis_studio::modals
