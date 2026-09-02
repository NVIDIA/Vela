// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DatasetEditor.h"
#include "ReplicaView.h"
#include "UICommon.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <algorithm>

namespace vsr::scivis_studio::client {

using namespace protocol;

namespace {

constexpr const char *REVIEW_POPUP = "Review Discovered Datasets";
constexpr const char *REMOVE_POPUP = "Remove Dataset?";
const std::vector<std::string> ARCHIVE_EXTENSIONS = {".vsr", ".tsd"};

// Why an Unload is refused, or null when it is allowed.
const char *unloadBlockedReason(const Dataset &dataset)
{
  if (dataset.dirty) {
    return "The dataset has unsaved changes. Save the project first; "
           "unloading never discards changes.";
  }
  if (dataset.status == DatasetStatus::Importing)
    return "The dataset is still importing.";
  if (dataset.persistedName.empty()) {
    return "The dataset has never been saved, so there is no asset to load "
           "it back from.";
  }
  return nullptr;
}

} // namespace

DatasetEditor::DatasetEditor(
    vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Dataset Editor"), m_browse(app, context)
{}

DatasetEditor::~DatasetEditor() = default;

void DatasetEditor::onProjectReplaced()
{
  m_nameStale = true;
}

// Selection //////////////////////////////////////////////////////////////////

const Dataset *DatasetEditor::resolveSelection(const Project &project)
{
  if (project.datasets.empty()) {
    m_selected.clear();
    return nullptr;
  }
  const Dataset *dataset = replica::findDataset(project, m_selected);
  if (!dataset) {
    dataset = &project.datasets.front();
    m_selected = dataset->id;
  }
  return dataset;
}

// Requests ///////////////////////////////////////////////////////////////////

// The hint stats the asset file server-side; once a second is plenty.
void DatasetEditor::refreshAvailability(const Dataset &dataset)
{
  if (dataset.residency != DatasetResidency::Unloaded || !canSend()
      || pending(m_pendingRefresh))
    return;
  const double now = ImGui::GetTime();
  if (m_availabilityDataset == dataset.id
      && now - m_lastAvailabilityCheck < 1.0)
    return;
  m_availabilityDataset = dataset.id;
  m_lastAvailabilityCheck = now;
  m_pendingRefresh =
      ops().refreshDatasetAvailability(dataset.id, errorReporter());
}

// UI /////////////////////////////////////////////////////////////////////////

void DatasetEditor::buildEditorUI(const Project &project)
{
  buildUI_toolbar(project);

  const Dataset *dataset = resolveSelection(project);
  if (!dataset) {
    ImGui::TextDisabled("No datasets");
    return;
  }

  const std::string preview = dataset->name;
  if (ImGui::BeginCombo("Dataset", preview.c_str())) {
    for (const auto &candidate : project.datasets) {
      const bool selected = candidate.id == m_selected;
      ImGui::PushID(candidate.id.c_str());
      if (ImGui::Selectable(candidate.name.c_str(), selected))
        m_selected = candidate.id;
      if (selected)
        ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  dataset = replica::findDataset(project, m_selected);
  if (!dataset)
    return;

  refreshAvailability(*dataset);
  buildUI_nameField(*dataset);
  buildUI_details(*dataset);
  buildUI_actions(project, *dataset);
}

void DatasetEditor::buildUI_toolbar(const Project &project)
{
  ImGui::BeginDisabled(pending(m_pendingOp));
  if (ImGui::Button("Load Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::OpenFile;
    request.title = "Load Dataset Archive";
    request.extensions = ARCHIVE_EXTENSIONS;
    request.startDirectory = project.projectDirectory;
    request.onAccept = [this](const std::vector<std::filesystem::path> &paths) {
      m_pendingOp = ops().loadDatasetArchive(paths.front(),
          [this](const ProjectOpReply &reply,
              const std::optional<TaskStartedResult> &) {
            if (!reply.ok)
              reportError(reply.error);
          });
    };
    m_browse.open(std::move(request));
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::BeginDisabled(pending(m_pendingDiscover));
  if (ImGui::Button("Discover...")) {
    m_pendingDiscover = ops().discoverDatasetCandidates(
        [this](const ProjectOpReply &reply,
            const std::optional<DiscoverDatasetCandidatesResult> &result) {
          if (!reply.ok) {
            reportError(reply.error);
            return;
          }
          m_candidates = result ? result->candidates
                                : std::vector<DatasetCandidateEntry>{};
          m_candidateSelected.assign(m_candidates.size(), 1);
          m_candidateNames.clear();
          for (const auto &candidate : m_candidates)
            m_candidateNames.push_back(candidate.proposedName);
          m_openReview = true;
        });
  }
  ImGui::EndDisabled();
  vsr::ui::tooltipForPreviousItem(
      "Find Dataset Archives in the project directory that the project does"
      " not list yet");
}

// Unloaded datasets are read-only as assets (the asset stores the name), so
// the field is plain text while Unloaded.
void DatasetEditor::buildUI_nameField(const Dataset &dataset)
{
  if (dataset.residency == DatasetResidency::Unloaded) {
    ImGui::Text("Name: %s", dataset.name.c_str());
    return;
  }

  const bool refresh = m_nameStale && !ImGui::IsAnyItemActive()
      && !pending(m_pendingRename);
  if (m_nameBufferDataset != dataset.id || refresh) {
    m_nameBufferDataset = dataset.id;
    m_nameBuffer = dataset.name;
    m_nameStale = false;
    if (m_nameBufferDataset != dataset.id)
      m_nameError.clear();
  }

  ImGui::BeginDisabled(pending(m_pendingRename));
  const bool submitted = ImGui::InputText(
      "Name", &m_nameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
  const bool commit = submitted || ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();

  if (commit && m_nameBuffer != dataset.name) {
    const DatasetID id = dataset.id;
    m_pendingRename = ops().renameDataset(
        id, m_nameBuffer, [this, id](const ProjectOpReply &reply) {
          if (reply.ok) {
            m_nameError.clear();
          } else {
            m_nameError = reply.error;
            if (m_nameBufferDataset == id)
              m_nameStale = true; // reject: back to the replica's name
          }
        });
  } else if (commit) {
    m_nameError.clear();
  }
  ui::errorText(m_nameError);
}

void DatasetEditor::buildUI_details(const Dataset &dataset)
{
  const bool unloaded = dataset.residency == DatasetResidency::Unloaded;
  ImGui::Text("ID: %s", dataset.id.c_str());
  ImGui::Text("Status: %s", replica::datasetStatusText(dataset));
  ImGui::Text("Residency: %s", replica::datasetResidencyText(dataset));
  ImGui::Text("Source kind: %s", replica::datasetSourceKindText(dataset));
  ImGui::Text("Importer: %s", dataset.importerType.c_str());
  ImGui::TextWrapped("Source: %s", dataset.source.sourcePath.c_str());
  if (dataset.sourceKind == DatasetSourceKind::FileAnimation) {
    ImGui::Text("Frames: %zu", dataset.sourceFiles.size());
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (dataset.sourceFiles.size() <= 12)
      flags |= ImGuiTreeNodeFlags_DefaultOpen;
    const auto label =
        "Source Files (" + std::to_string(dataset.sourceFiles.size()) + ")";
    if (ImGui::TreeNodeEx(label.c_str(), flags)) {
      for (size_t i = 0; i < dataset.sourceFiles.size(); ++i) {
        const auto &sourceFile = dataset.sourceFiles[i];
        const std::filesystem::path path(sourceFile.path);
        const auto row = std::to_string(i) + "  " + path.filename().string();
        ImGui::TextUnformatted(row.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", sourceFile.path.c_str());
      }
      ImGui::TreePop();
    }
  }
  if (!unloaded) {
    ImGui::Text("Root: %s/%zu",
        dataset.rootNode.layerName.c_str(),
        dataset.rootNode.nodeIndex);
  }
}

void DatasetEditor::buildUI_actions(
    const Project &project, const Dataset &dataset)
{
  const bool unloaded = dataset.residency == DatasetResidency::Unloaded;
  const DatasetID id = dataset.id;
  auto taskReply = [this](const ProjectOpReply &reply,
                       const std::optional<TaskStartedResult> &) {
    if (!reply.ok)
      reportError(reply.error);
  };

  ImGui::BeginDisabled(pending(m_pendingOp));

  ImGui::BeginDisabled(unloaded || dataset.status != DatasetStatus::Available);
  if (ImGui::Button("Save Archive...")) {
    BrowseRequest request;
    request.mode = BrowseMode::SaveFile;
    request.title = "Save Dataset Archive";
    request.extensions = ARCHIVE_EXTENSIONS;
    request.startDirectory = project.projectDirectory;
    request.defaultName = dataset.name + ".vsr";
    request.onAccept =
        [this, id, taskReply](const std::vector<std::filesystem::path> &paths) {
          m_pendingOp = ops().saveDatasetArchive(
              id, ui::withVsrExtension(paths.front()), taskReply);
        };
    m_browse.open(std::move(request));
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::BeginDisabled(unloaded
      || dataset.sourceKind != DatasetSourceKind::Static
      || dataset.source.sourcePath.empty());
  if (ImGui::Button("Reimport"))
    m_pendingOp = ops().reimportDataset(id, taskReply);
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (unloaded) {
    if (ImGui::Button("Load"))
      m_pendingOp = ops().loadDataset(id, taskReply);
  } else {
    const char *blocked = unloadBlockedReason(dataset);
    ImGui::BeginDisabled(blocked != nullptr);
    if (ImGui::Button("Unload"))
      m_pendingOp = ops().unloadDataset(id, errorReporter());
    ImGui::EndDisabled();
    if (blocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", blocked);
  }

  ImGui::SameLine();
  if (ImGui::Button("Remove...")) {
    m_datasetToRemove = id;
    m_keepRemovedAsset = false;
    ImGui::OpenPopup(REMOVE_POPUP);
  }

  ImGui::EndDisabled();
}

// Popups /////////////////////////////////////////////////////////////////////

void DatasetEditor::buildPopups(const Project &project)
{
  if (m_openReview) {
    ImGui::OpenPopup(REVIEW_POPUP);
    m_openReview = false;
  }
  buildUI_discoveryReview();
  buildUI_removeConfirmation(project);
  m_browse.renderUI();
}

void DatasetEditor::buildUI_discoveryReview()
{
  ImGui::SetNextWindowSize(ImVec2(760.f, 0.f), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(
          REVIEW_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextWrapped(
      "Select the Dataset Archives to incorporate into this project.");
  if (m_candidates.empty())
    ImGui::TextDisabled("No unlisted Dataset Archives were found.");

  for (size_t i = 0; i < m_candidates.size(); ++i) {
    ImGui::PushID(int(i));
    bool selected = m_candidateSelected[i] != 0;
    if (ImGui::Checkbox("##selected", &selected))
      m_candidateSelected[i] = selected;
    ImGui::SameLine();
    ImGui::TextUnformatted(m_candidates[i].file.filename().string().c_str());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", m_candidates[i].file.generic_string().c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.f);
    ImGui::InputText("##name", &m_candidateNames[i]);
    ImGui::PopID();
  }

  auto close = [this] {
    m_candidates.clear();
    m_candidateSelected.clear();
    m_candidateNames.clear();
    ImGui::CloseCurrentPopup();
  };

  ImGui::BeginDisabled(!canSend() || m_candidates.empty());
  if (ImGui::Button("Incorporate Selected")) {
    for (size_t i = 0; i < m_candidates.size(); ++i) {
      if (!m_candidateSelected[i])
        continue;
      const auto file = m_candidates[i].file.filename().string();
      ops().incorporateDatasetCandidate(m_candidates[i].file,
          m_candidates[i].proposedName,
          m_candidateNames[i],
          [this, file](const ProjectOpReply &reply,
              const std::optional<TaskStartedResult> &) {
            if (!reply.ok)
              reportError(file + ": " + reply.error);
          });
    }
    close();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
    close();
  ImGui::EndPopup();
}

void DatasetEditor::buildUI_removeConfirmation(const Project &project)
{
  if (!ImGui::BeginPopupModal(
          REMOVE_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  const Dataset *dataset = replica::findDataset(project, m_datasetToRemove);
  ImGui::TextWrapped("Remove '%s' from the inventory and every shot?",
      dataset ? dataset->name.c_str() : m_datasetToRemove.c_str());
  ImGui::Checkbox("Keep managed asset file", &m_keepRemovedAsset);

  ImGui::BeginDisabled(!canSend());
  if (ImGui::Button("Remove")) {
    m_pendingOp = ops().removeDataset(
        m_datasetToRemove, m_keepRemovedAsset, errorReporter());
    m_datasetToRemove.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_datasetToRemove.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

} // namespace vsr::scivis_studio::client
