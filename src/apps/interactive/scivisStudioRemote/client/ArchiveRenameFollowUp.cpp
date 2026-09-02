// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ArchiveRenameFollowUp.h"
// vsr_scivis_studio_model
#include "Project.h"
// std
#include <algorithm>
#include <utility>

namespace vsr::scivis_studio::client {

std::vector<DatasetID> ArchiveRenameFollowUp::datasetIds(const Project *project)
{
  std::vector<DatasetID> ids;
  if (!project)
    return ids;
  ids.reserve(project->datasets.size());
  for (const auto &dataset : project->datasets)
    ids.push_back(dataset.id);
  return ids;
}

void ArchiveRenameFollowUp::arm(
    uint64_t taskId, std::vector<DatasetID> idsBefore, std::string name)
{
  m_pending = Pending{taskId, std::move(idsBefore), std::move(name)};
}

bool ArchiveRenameFollowUp::armed() const
{
  return m_pending.has_value();
}

void ArchiveRenameFollowUp::cancel()
{
  m_pending.reset();
}

RequestHandle ArchiveRenameFollowUp::apply(
    const Project &project, ProjectOps &ops, ReplyCallback onReply)
{
  if (!m_pending)
    return {};
  if (const TaskRecord *task = ops.task(m_pending->taskId);
      task && task->state == TaskState::Failed) {
    m_pending.reset();
    return {};
  }

  std::vector<DatasetID> added;
  const auto &before = m_pending->idsBefore;
  for (const auto &dataset : project.datasets) {
    if (std::find(before.begin(), before.end(), dataset.id) == before.end())
      added.push_back(dataset.id);
  }
  if (added.empty())
    return {}; // the load has not landed yet

  RequestHandle sent;
  if (added.size() == 1)
    sent =
        ops.renameDataset(added.front(), m_pending->name, std::move(onReply));
  m_pending.reset();
  return sent;
}

} // namespace vsr::scivis_studio::client
