// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectSnapshot.h"
// vsr_scivis_studio_model
#include "CameraRig.h"
#include "ProjectSerialization.h"
// std
#include <optional>
#include <string>

namespace vsr::scivis_studio::protocol {

namespace {

using vsr::core::DataNode;

// Strict enum parsers: dataset::*FromString() silently fall back to a default
// on unknown text, which would hide a corrupt snapshot. Accept exactly the
// spellings toString() emits.
std::optional<DatasetStatus> statusFromString(const std::string &s)
{
  for (auto v : {DatasetStatus::Available,
           DatasetStatus::Unavailable,
           DatasetStatus::Importing,
           DatasetStatus::ImportFailed}) {
    if (s == dataset::toString(v))
      return v;
  }
  return {};
}

std::optional<DatasetSourceKind> sourceKindFromString(const std::string &s)
{
  for (auto v : {DatasetSourceKind::Static,
           DatasetSourceKind::FileAnimation,
           DatasetSourceKind::Live}) {
    if (s == dataset::toString(v))
      return v;
  }
  return {};
}

// Dataset runtime fields ////////////////////////////////////////////////////

void sourceFileToNode(const DatasetSourceFile &f, DataNode &n)
{
  writeChild(n, "path", f.path);
  writeChild(n, "resolvedPath", f.resolvedPath);
}

// path is required; resolvedPath defaults to "".
bool nodeToSourceFile(const DataNode &n, DatasetSourceFile &f)
{
  if (!readChild(n, "path", f.path))
    return false;
  f.resolvedPath = readChildOr(n, "resolvedPath", std::string());
  return true;
}

void datasetRuntimeToNode(const Dataset &d, DataNode &n)
{
  writeChild(n, "status", std::string(dataset::toString(d.status)));
  writeChild(n, "sourceKind", std::string(dataset::toString(d.sourceKind)));
  writeChild(n, "importerType", d.importerType);

  auto &source = n["source"];
  writeChild(source, "sourcePath", d.source.sourcePath);
  if (d.source.importerSettings.size() > 0) {
    auto &settings = source["importerSettings"];
    for (const auto &kv : d.source.importerSettings)
      writeChild(settings, kv.first.c_str(), kv.second);
  }

  writeNodeList(n, "sourceFiles", d.sourceFiles, sourceFileToNode);

  writeChildNode(n, "rootNode", d.rootNode);
  writeChild(n, "dirty", d.dirty);
  writeChild(n, "declared", d.declared);
  writeChild(n, "pendingExtraction", d.pendingExtraction);
  writeChild(n, "pendingSourceListMigration", d.pendingSourceListMigration);
  writeChild(n, "persistedName", d.persistedName);
}

bool nodeToDatasetRuntime(const DataNode &n, Dataset &d)
{
  if (!readOptionalEnumChild(n, "status", d.status, statusFromString)
      || !readOptionalEnumChild(
          n, "sourceKind", d.sourceKind, sourceKindFromString))
    return false;
  d.importerType = readChildOr(n, "importerType", d.importerType);

  if (const auto *source = n.child("source")) {
    d.source.sourcePath =
        readChildOr(*source, "sourcePath", d.source.sourcePath);
    if (const auto *settings = source->child("importerSettings")) {
      bool ok = true;
      settings->foreach_child_const([&](const DataNode &kv) {
        if (!kv.getValue().is<std::string>()) {
          ok = false;
          return;
        }
        d.source.importerSettings.set(kv.name(), kv.getValueAs<std::string>());
      });
      if (!ok)
        return false;
    }
  }

  if (hasChild(n, "sourceFiles")
      && !readNodeList(n, "sourceFiles", d.sourceFiles, nodeToSourceFile))
    return false;

  if (!readOptionalChildNode(n, "rootNode", d.rootNode))
    return false;
  d.dirty = readChildOr(n, "dirty", d.dirty);
  d.declared = readChildOr(n, "declared", d.declared);
  d.pendingExtraction =
      readChildOr(n, "pendingExtraction", d.pendingExtraction);
  d.pendingSourceListMigration = readChildOr(
      n, "pendingSourceListMigration", d.pendingSourceListMigration);
  d.persistedName = readChildOr(n, "persistedName", d.persistedName);
  return true;
}

// Whole sidecar /////////////////////////////////////////////////////////////

void runtimeToNode(const Project &p, DataNode &n)
{
  auto &datasets = n["datasets"];
  for (const auto &d : p.datasets)
    datasetRuntimeToNode(d, datasets[d.id]);

  auto &shots = n["shots"];
  for (const auto &s : p.shots)
    writeChildNode(shots[s.id], "camera", s.camera);

  auto &lightRigs = n["lightRigs"];
  for (const auto &r : p.lightRigs) {
    auto &rn = lightRigs[r.id];
    writeChildNode(rn, "rootNode", r.rootNode);
    writeChild(rn, "persistedName", r.persistedName);
  }

  auto &cameraRigs = n["cameraRigs"];
  for (const auto &r : p.cameraRigs) {
    auto &rn = cameraRigs[r.id];
    camera_rig::cameraRigToNode(r, rn["rig"]);
    writeChild(rn, "persistedName", r.persistedName);
  }
}

// `n` is mutable only because nodeToCameraRig() takes a non-const node; the
// caller owns it as a scratch copy of the received tree.
bool nodeToRuntime(DataNode &n, Project &p)
{
  if (const auto *datasets = n.child("datasets")) {
    for (auto &d : p.datasets) {
      const auto *dn = datasets->child(d.id);
      if (dn && !nodeToDatasetRuntime(*dn, d))
        return false;
    }
  }

  if (const auto *shots = n.child("shots")) {
    for (auto &s : p.shots) {
      const auto *sn = shots->child(s.id);
      if (sn && !readOptionalChildNode(*sn, "camera", s.camera))
        return false;
    }
  }

  if (const auto *lightRigs = n.child("lightRigs")) {
    for (auto &r : p.lightRigs) {
      const auto *rn = lightRigs->child(r.id);
      if (!rn)
        continue;
      if (!readOptionalChildNode(*rn, "rootNode", r.rootNode))
        return false;
      r.persistedName = readChildOr(*rn, "persistedName", r.persistedName);
    }
  }

  if (auto *cameraRigs = n.child("cameraRigs")) {
    for (auto &r : p.cameraRigs) {
      auto *rn = cameraRigs->child(r.id);
      if (!rn)
        continue;
      if (auto *rig = rn->child("rig"))
        camera_rig::nodeToCameraRig(*rig, r);
      r.persistedName = readChildOr(*rn, "persistedName", r.persistedName);
    }
  }

  return true;
}

} // namespace

void toNode(const ProjectSnapshot &s, DataNode &n)
{
  projectToNode(s.project, n["project"]);
  runtimeToNode(s.project, n["runtime"]);
}

bool fromNode(const DataNode &n, ProjectSnapshot &s)
{
  if (!hasChild(n, "project"))
    return false;
  // nodeToProject()/nodeToCameraRig() take a non-const node (and create
  // children as a side effect), so they run on a private copy.
  vsr::core::DataTree scratch;
  scratch.root() = n;
  Project project;
  if (!nodeToProject(scratch.root()["project"], project))
    return false;
  if (auto *runtime = scratch.root().child("runtime")) {
    if (!nodeToRuntime(*runtime, project))
      return false;
  }
  s.project = std::move(project);
  return true;
}

void toNode(const UIState &u, DataNode &n)
{
  writeSubtree(n, "tree", u.tree);
}

bool fromNode(const DataNode &n, UIState &u)
{
  u.tree = readSubtree(n, "tree");
  return true;
}

} // namespace vsr::scivis_studio::protocol
