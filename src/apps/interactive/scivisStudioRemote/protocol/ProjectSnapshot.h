// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_core
#include "vsr/core/DataTree.hpp"

namespace vsr::scivis_studio::protocol {

/*
 * Whole-Project notification the server pushes after every confirmed mutation
 * (ADR 0034); the client replaces its Project Replica with `project`
 * wholesale.
 *
 * Wire shape: child "project" is projectToNode()'s Full form
 * (ProjectSerialization.h): the manifest's fields plus, inline under each
 * entity, the runtime fields the server rebuilds on open but the client
 * cannot (Dataset status and source metadata, Shot::camera, the rigs' scene
 * roots and value data, persisted names). One serializer writes and reads
 * both the manifest and this, so the replica and the on-disk schema cannot
 * drift.
 *
 * Example:
 *   channel.send(encode(ProjectSnapshot{context.project()}));
 *   ...
 *   if (auto snap = decode<ProjectSnapshot>(msg))
 *     replica = std::move(snap->project);
 */
struct ProjectSnapshot
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::ProjectSnapshot;
  vsr::scivis_studio::Project project;
};

// "project" is required and read with nodeToProject()'s Full-form policy:
// a mistyped field, unknown enum spelling or malformed camera rig is
// rejected.
void toNode(const ProjectSnapshot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ProjectSnapshot &);

} // namespace vsr::scivis_studio::protocol
