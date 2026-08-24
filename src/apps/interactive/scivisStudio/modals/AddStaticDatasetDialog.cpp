// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AddStaticDatasetDialog.h"

#include "vsr/core/Logging.hpp"
#include "vsr/ui/imgui/Application.h"

#include "imgui.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <optional>

namespace vsr::scivis_studio {

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

template <size_t N>
void copyToInputBuffer(std::array<char, N> &buffer, const std::string &value)
{
  buffer.fill('\0');
  std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
}

} // namespace

AddStaticDatasetDialog::AddStaticDatasetDialog(
    vsr::ui::imgui::Application *app, ProjectContext *projectContext)
    : Modal(app, "Add Static Dataset"), m_projectContext(projectContext)
{}

AddStaticDatasetDialog::~AddStaticDatasetDialog() = default;

void AddStaticDatasetDialog::buildUI()
{
  ImGui::InputText("Name", m_name.data(), m_name.size());

  if (!m_browsedSourcePath.empty()) {
    copyToInputBuffer(m_sourcePath, m_browsedSourcePath);
    m_browsedSourcePath.clear();
  }

  if (ImGui::Button("...##datasetSource")) {
    m_browsedSourcePath.clear();
    m_app->getFilenameFromDialog(
        m_browsedSourcePath, vsr::ui::imgui::FileDialogMode::OpenFile);
  }
  ImGui::SameLine();
  ImGui::InputText("Source Path", m_sourcePath.data(), m_sourcePath.size());

  const char *preview = SOURCES[m_selectedSource].name;
  if (ImGui::BeginCombo("Source", preview)) {
    for (int i = 0; i < static_cast<int>(SOURCES.size()); ++i) {
      const bool selected = i == m_selectedSource;
      if (ImGui::Selectable(SOURCES[i].name, selected))
        m_selectedSource = i;
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Spacing();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    hide();
    return;
  }

  ImGui::SameLine();
  const auto sourceChoice = SOURCES[m_selectedSource];
  const bool importSource = sourceChoice.importer.has_value();
  const char *actionLabel =
      importSource ? "Import" : (sourceChoice.subtree ? "Load Subtree" : "Load Archive");
  const char *progressLabel = importSource
      ? "Importing Dataset..."
      : (sourceChoice.subtree ? "Loading Layer Subtree Archive..."
                              : "Loading Dataset Archive...");
  if (ImGui::Button(actionLabel)) {
    const std::string name = m_name.data();
    const std::filesystem::path sourcePath = m_sourcePath.data();
    if (sourcePath.empty()) {
      vsr::core::logWarning("[SciVisStudio] Dataset source path is empty");
      return;
    }

    hide();
    m_app->showTaskModal(
        [ctx = m_projectContext, name, sourcePath, sourceChoice]() {
          if (!ctx)
            return;
          if (sourceChoice.importer) {
            ctx->addStaticDataset(name, sourcePath, *sourceChoice.importer);
          } else if (sourceChoice.subtree) {
            auto *dataset = ctx->addStaticDatasetFromSubtree(name, sourcePath);
            if (!dataset || dataset->status != DatasetStatus::Available) {
              vsr::core::logWarning(
                  "[SciVisStudio] Failed to load Layer Subtree Archive as a dataset");
            }
          } else {
            std::string error;
            auto *dataset = ctx->loadDatasetArchive(sourcePath, &error);
            if (!dataset) {
              vsr::core::logWarning(
                  "[SciVisStudio] Failed to load Dataset Archive: %s",
                  error.c_str());
            } else if (!name.empty()
                && !ctx->renameDataset(dataset->id, name, &error)) {
              vsr::core::logWarning(
                  "[SciVisStudio] Failed to rename loaded Dataset Archive: %s",
                  error.c_str());
            }
          }
        },
        progressLabel);
  }
}

} // namespace vsr::scivis_studio
