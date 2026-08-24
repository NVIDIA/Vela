// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectContext.h"
#include "vsr/ui/imgui/windows/Window.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace vsr::ui::imgui {
struct Viewport;
}

namespace vsr::scivis_studio {

struct DatasetEditor : public vsr::ui::imgui::Window
{
  DatasetEditor(vsr::ui::imgui::Application *app,
      ProjectContext *projectContext,
      vsr::ui::imgui::Viewport *viewport);
  ~DatasetEditor() override;

  void buildUI() override;

 private:
  enum class PendingFileIO
  {
    None,
    Load,
    Save
  };

  // Shared result slot for operations that run behind the task modal
  // (Reimport, Dataset Load); only one can be pending at a time.
  struct AsyncTaskResult
  {
    std::atomic_bool complete{false};
    std::string error;
  };

  void pollPendingFileIO();
  void pollPendingAsyncTask();
  void buildDiscoveryReview();
  void buildErrorPopup();

  ProjectContext *m_projectContext{nullptr};
  vsr::ui::imgui::Viewport *m_viewport{nullptr};
  int m_selectedDataset{0};
  PendingFileIO m_pendingFileIO{PendingFileIO::None};
  std::string m_pendingFilename;
  DatasetID m_pendingSaveDataset;
  DatasetID m_pendingRemoveDataset;
  std::shared_ptr<AsyncTaskResult> m_pendingAsyncTask;
  bool m_keepRemovedAsset{false};
  DatasetID m_nameBufferDataset;
  DatasetID m_availabilityCheckDataset;
  double m_lastAvailabilityCheck{0.0};
  std::string m_nameBuffer;
  std::string m_nameError;
  std::string m_ioError;
  std::vector<DatasetCandidate> m_candidates;
  std::vector<char> m_candidateSelected;
  std::vector<std::string> m_candidateNames;
};

} // namespace vsr::scivis_studio
