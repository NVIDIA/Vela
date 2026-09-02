// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_model
#include "Project.h"
// std
#include <string>
#include <vector>

namespace vsr::scivis_studio::client::replica {

/*
 * Read helpers over the Project Replica for the client's editors: lookups by
 * id, the strings the panels display, and name-sorted views. Every function
 * takes `const Project &` and writes nothing; the pointers they return point
 * into that Project and die with the next Project Snapshot, so editors keep
 * ids, not pointers, across frames.
 *
 * Example:
 *   const Project *project = connection.project();
 *   if (const Shot *shot = replica::activeShot(*project))
 *     ImGui::Text("%s", replica::lightRigLabel(*project, shot->lightRigId)
 *         .c_str());
 *   for (const Dataset *d : replica::sortedDatasets(*project))
 *     ImGui::Text("%s  %s", d->name.c_str(), replica::datasetStatusText(*d));
 */

// Lookups (nullptr when absent) //////////////////////////////////////////////

const Dataset *findDataset(const Project &project, const DatasetID &id);
const Shot *findShot(const Project &project, const ShotID &id);
const Shot *activeShot(const Project &project);
const LightRig *findLightRig(const Project &project, const LightRigID &id);
const CameraRig *findCameraRig(const Project &project, const CameraRigID &id);
const ColorMapRecord *findColorMap(
    const Project &project, const ColorMapID &id);

// Shots referencing the rig; the monolith's confirm-before-delete gate.
size_t lightRigUseCount(const Project &project, const LightRigID &id);
size_t cameraRigUseCount(const Project &project, const CameraRigID &id);

// Display strings ////////////////////////////////////////////////////////////

// Loaded / Unloaded / Unavailable / Importing / Import Failed
// (dataset::displayStatus).
const char *datasetStatusText(const Dataset &dataset);
const char *datasetSourceKindText(const Dataset &dataset);
const char *datasetResidencyText(const Dataset &dataset);
// The directory, or "{unsaved}" for a project never saved.
std::string projectDirectoryText(const Project &project);
// The entity's name; "<none>" for an empty id, "<missing: id>" for an id the
// replica does not hold.
std::string datasetLabel(const Project &project, const DatasetID &id);
std::string shotLabel(const Project &project, const ShotID &id);
std::string lightRigLabel(const Project &project, const LightRigID &id);
std::string cameraRigLabel(const Project &project, const CameraRigID &id);
std::string colorMapLabel(const Project &project, const ColorMapID &id);

// Sorted views ///////////////////////////////////////////////////////////////

// By name, case-insensitively, ties broken by id; the collections
// themselves keep the server's order.
std::vector<const Dataset *> sortedDatasets(const Project &project);
std::vector<const Shot *> sortedShots(const Project &project);
std::vector<const LightRig *> sortedLightRigs(const Project &project);
std::vector<const CameraRig *> sortedCameraRigs(const Project &project);
std::vector<const ColorMapRecord *> sortedColorMaps(const Project &project);

} // namespace vsr::scivis_studio::client::replica
