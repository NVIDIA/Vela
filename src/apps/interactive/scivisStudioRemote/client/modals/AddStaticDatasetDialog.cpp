// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AddStaticDatasetDialog.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <algorithm>
#include <array>
#include <optional>

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

struct DatasetSourceChoice
{
  const char *name;
  // No importer: the source is a VSR archive rather than a foreign format.
  std::optional<vsr::io::ImporterType> importer;
  // A VSR Layer Subtree Archive rather than a Dataset Archive. Only
  // meaningful when 'importer' is unset.
  bool subtree = false;
};

// The monolith's table, verbatim: the user picks the importer explicitly.
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

// Advisory: the browse dialog greys files outside these; archives are .vsr.
std::vector<std::string> browseExtensions(const DatasetSourceChoice &choice)
{
  if (!choice.importer)
    return {".vsr", ".tsd"};
  return {};
}

std::vector<DatasetID> datasetIds(const Project &project)
{
  std::vector<DatasetID> ids;
  ids.reserve(project.datasets.size());
  for (const auto &dataset : project.datasets)
    ids.push_back(dataset.id);
  return ids;
}

} // namespace

AddStaticDatasetDialog::AddStaticDatasetDialog(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : Modal(app, "Add Static Dataset"), m_context(context), m_browse(app, context)
{}

AddStaticDatasetDialog::~AddStaticDatasetDialog() = default;

void AddStaticDatasetDialog::reset()
{
  m_name.clear();
  m_sourcePath.clear();
  m_error.clear();
  m_pending = {};
}

// The archive load has landed when the replica holds exactly one dataset id
// it did not hold before; then the typed name is applied.
void AddStaticDatasetDialog::onProjectReplaced()
{
  if (!m_archiveRename)
    return;
  const Project *project = m_context->project();
  if (!project)
    return;

  if (const TaskRecord *task = m_context->ops().task(m_archiveRename->taskId)) {
    if (task->state == TaskState::Failed) {
      m_archiveRename.reset();
      return;
    }
  }

  std::vector<DatasetID> added;
  for (const auto &dataset : project->datasets) {
    const auto &before = m_archiveRename->datasetIdsBefore;
    if (std::find(before.begin(), before.end(), dataset.id) == before.end())
      added.push_back(dataset.id);
  }
  if (added.empty())
    return;
  if (added.size() == 1 && m_context->canSend()) {
    m_context->ops().renameDataset(
        added.front(), m_archiveRename->name, m_context->errorReporter());
  }
  m_archiveRename.reset();
}

void AddStaticDatasetDialog::submit()
{
  if (m_sourcePath.empty()) {
    m_error = "Enter a source path.";
    return;
  }
  const auto &choice = SOURCES[m_selectedSource];
  const std::filesystem::path sourcePath(m_sourcePath);
  const std::string name = m_name;
  const Project *project = m_context->project();

  auto onReply = [this, choice, name, project](const ProjectOpReply &reply,
                     const std::optional<TaskStartedResult> &started) {
    if (reply.requestId != m_pending.requestId)
      return;
    m_pending = {};
    if (!reply.ok) {
      m_error = reply.error;
      return;
    }
    if (!choice.importer && !choice.subtree && !name.empty() && started) {
      ArchiveRename rename;
      rename.taskId = started->taskId;
      rename.datasetIdsBefore = project ? datasetIds(*project) : std::vector<DatasetID>{};
      rename.name = name;
      m_archiveRename = std::move(rename);
    }
    reset();
    hide();
  };

  m_error.clear();
  if (choice.importer) {
    m_pending = m_context->ops().importStaticDataset(
        name, sourcePath, *choice.importer, false, onReply);
  } else if (choice.subtree) {
    m_pending = m_context->ops().importStaticDataset(
        name, sourcePath, vsr::io::ImporterType::NONE, true, onReply);
  } else {
    m_pending = m_context->ops().loadDatasetArchive(sourcePath, onReply);
  }
}

void AddStaticDatasetDialog::buildUI()
{
  const bool busy = m_pending.valid() && m_context->ops().pending(m_pending);
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
    m_browse.open(std::move(request));
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

  if (busy)
    ImGui::TextDisabled("waiting for the server...");
  ui::errorText(m_error);

  // Read before the browse draws: its own Escape hides it in the same
  // frame, which must not count as this dialog's Escape too.
  const bool browseWasVisible = m_browse.visible();
  m_browse.renderUI();

  ImGui::Spacing();
  if (ImGui::Button("Cancel")
      || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !browseWasVisible)) {
    reset();
    hide();
    return;
  }
  ImGui::SameLine();
  const char *actionLabel = choice.importer
      ? "Import"
      : (choice.subtree ? "Load Subtree" : "Load Archive");
  ImGui::BeginDisabled(busy || !m_context->canSend());
  if (ImGui::Button(actionLabel))
    submit();
  ImGui::EndDisabled();
}

} // namespace vsr::scivis_studio::client
