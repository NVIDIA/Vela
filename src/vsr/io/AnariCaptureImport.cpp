// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/AnariCaptureImport.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"

namespace vsr::io {

void logAnariCaptureImportReport(
    const char *filename, const AnariCaptureImportReport &report)
{
  using namespace vsr::core;

  if (!report.read) {
    logError("[import_ANARI_CAPTURE] %s: %s", filename, report.error.c_str());
    return;
  }
  logStatus(
      "[import_ANARI_CAPTURE] %s: ANARI Capture v%d, %zu dropped, %zu "
      "capture warnings",
      filename,
      report.formatVersion,
      report.dropped.size(),
      report.captureWarnings.size());
  if (!report.callCounts.empty()) {
    logStatus("[import_ANARI_CAPTURE] API calls before the capture: %s",
        report.callCounts.front().c_str());
  }
  if (!report.rendererSubtype.empty()) {
    logStatus("[import_ANARI_CAPTURE] renderer \"%s\" (not applied)",
        report.rendererSubtype.c_str());
    for (auto &p : report.rendererParameters)
      logStatus("[import_ANARI_CAPTURE]   %s", p.c_str());
  }
  for (auto &p : report.frameParameters)
    logStatus("[import_ANARI_CAPTURE] frame %s (not applied)", p.c_str());
  for (auto &d : report.dropped) {
    logWarning("[import_ANARI_CAPTURE] dropped %s: %s",
        d.path.c_str(),
        d.reason.c_str());
  }
  for (auto &w : report.captureWarnings)
    logWarning("[ANARI Capture warning] %s", w.c_str());
}

} // namespace vsr::io
