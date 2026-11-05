// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>
#include <vector>

namespace vsr::io {

/*
 * One thing an ANARI Capture held that its Import did not put in the scene.
 * `path` is spelled the way the SDK's capture diff spells it, from the frame
 * down (`world.instance[0].group.surface[2].geometry.vertex.position`), so a
 * diff line and a report entry name the same value.
 */
struct AnariCaptureDropped
{
  std::string path;
  std::string reason;
};

/*
 * What one ANARI Capture Import found besides scene content. Values here are
 * reported, never applied: the renderer and frame belong to whatever device
 * the application ran, not to the one viewing the import.
 *
 * Example:
 *   auto r = import_ANARI_CAPTURE(scene, animMgr, "frame0000.capture");
 *   if (!r.read)
 *     logError("%s", r.error.c_str());
 *   for (auto &d : r.dropped)
 *     logWarning("%s: %s", d.path.c_str(), d.reason.c_str());
 */
struct AnariCaptureImportReport
{
  // False when the file could not be used at all (unreadable, not a capture,
  // an unknown format version, no frame); then nothing was imported and
  // `error` says why.
  bool read{false};
  std::string error;

  int formatVersion{0};
  std::vector<std::string> extensions; // the device block, as written

  // The `calls` block (format version 2), as "<function> <n>" lines after
  // "total <n>": API calls the application had made to the device when the
  // capture was taken. Empty for a version 1 capture.
  std::vector<std::string> callCounts;

  // The frame's renderer, its parameters as "name TYPE value" lines.
  std::string rendererSubtype;
  std::vector<std::string> rendererParameters;

  // The frame's own parameters other than camera, renderer and world, as
  // "name TYPE value" lines.
  std::vector<std::string> frameParameters;

  std::vector<AnariCaptureDropped> dropped;

  // The capture's `warnings` block, verbatim: the capture device's spec
  // check, kept apart from what this Import could not represent.
  std::vector<std::string> captureWarnings;
};

// Writes a report to the log: a status summary, then one warning per dropped
// value and per capture warning.
void logAnariCaptureImportReport(
    const char *filename, const AnariCaptureImportReport &report);

} // namespace vsr::io
