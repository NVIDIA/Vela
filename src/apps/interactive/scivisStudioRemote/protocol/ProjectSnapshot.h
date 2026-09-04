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
 * Wire shape: child "project" is exactly what projectToNode() writes (the
 * manifest form, reused so the replica and the on-disk schema cannot drift),
 * plus a sibling child "runtime". The manifest deliberately omits fields the
 * server rebuilds on open but the client cannot: nodeToProject() forces
 * Dataset::status to Unavailable and Dataset::dirty to false, and never reads
 * Dataset::{sourceKind, importerType, source, sourceFiles, rootNode,
 * declared, pendingExtraction, pendingSourceListMigration, persistedName},
 * Shot::camera, LightRig::{rootNode, persistedName} or CameraRig::{current,
 * keyframes, persistedName}. "runtime" carries exactly those, keyed by entity
 * id (ADR 0025) so a receiver never relies on list position:
 *
 *   runtime/datasets/<DatasetID>/{status, sourceKind, importerType,
 *       source/{sourcePath, importerSettings/<key>}, sourceFiles/<i>/{path,
 *       resolvedPath}, rootNode, dirty, declared, pendingExtraction,
 *       pendingSourceListMigration, persistedName}
 *   runtime/shots/<ShotID>/camera
 *   runtime/lightRigs/<LightRigID>/{rootNode, persistedName}
 *   runtime/cameraRigs/<CameraRigID>/{rig (cameraRigToNode), persistedName}
 *
 * ProjectSerialization is untouched: its readers stay manifest readers, and a
 * snapshot missing "runtime" (or an entity missing its entry) still decodes
 * with the manifest defaults.
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

/*
 * Opaque UI-state tree (windows/layout/settings) the server stores with the
 * project and hands back during bootstrap without ever inspecting it. Null
 * `tree` means the project carries no UI state.
 */
struct UIState
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::UIState;
  SubtreePtr tree;
};

// "project" is required; "runtime" and each of its entries are optional and a
// mistyped runtime field is rejected.
void toNode(const ProjectSnapshot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ProjectSnapshot &);

// The tree travels under child "tree"; absent reads back as null.
// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, UIState &u)
{
  v.subtree("tree", u.tree);
}

} // namespace vsr::scivis_studio::protocol
