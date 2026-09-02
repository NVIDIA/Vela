// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client_core
#include "ProjectOps.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// std
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio {
struct Project;
}

namespace vsr::scivis_studio::client {

/*
 * Applies the name the user typed to the dataset a LoadDatasetArchive task
 * creates. The protocol carries no name on that request (the archive stores
 * its own), so the caller notes which dataset ids the Project Replica held
 * when the load was submitted and, once a later snapshot shows exactly one
 * id that was not among them, sends RenameDataset for it. The ids are copied
 * up front: every ProjectSnapshot replaces the replica wholesale, so nothing
 * may hold a pointer into it across a poll.
 *
 * Example:
 *   const auto before = ArchiveRenameFollowUp::datasetIds(context.project());
 *   ops.loadDatasetArchive(file, [=](const ProjectOpReply &, const auto &s) {
 *     if (s)
 *       m_rename.arm(s->taskId, before, typedName);
 *   });
 *   ...
 *   void onProjectReplaced() { m_rename.apply(*project, ops, onReply); }
 */
struct ArchiveRenameFollowUp
{
  // The ids `project` holds; empty for a null replica.
  static std::vector<DatasetID> datasetIds(const Project *project);

  // Waits for `taskId` to add one dataset to `idsBefore`, then names it.
  void arm(uint64_t taskId, std::vector<DatasetID> idsBefore, std::string name);
  bool armed() const;
  void cancel();

  // Called on each snapshot. Disarms without a word when the task failed;
  // sends RenameDataset (answered through `onReply`) and disarms once exactly
  // one new dataset appeared -- more than one means the archive was not the
  // only thing added, and guessing would rename the wrong one. Returns the
  // handle of the request sent, invalid when none went out.
  RequestHandle apply(
      const Project &project, ProjectOps &ops, ReplyCallback onReply);

 private:
  struct Pending
  {
    uint64_t taskId{0};
    std::vector<DatasetID> idsBefore;
    std::string name;
  };
  std::optional<Pending> m_pending;
};

} // namespace vsr::scivis_studio::client
