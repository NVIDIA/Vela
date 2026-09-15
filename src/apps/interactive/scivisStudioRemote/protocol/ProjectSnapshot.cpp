// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectSnapshot.h"
// vsr_scivis_studio_model
#include "ProjectSerialization.h"

namespace vsr::scivis_studio::protocol {

void toNode(const ProjectSnapshot &s, vsr::core::DataNode &n)
{
  projectToNode(s.project, n["project"], ProjectForm::Full);
}

bool fromNode(const vsr::core::DataNode &n, ProjectSnapshot &s)
{
  const auto *project = n.child("project");
  return project && nodeToProject(*project, s.project, ProjectForm::Full);
}

} // namespace vsr::scivis_studio::protocol
