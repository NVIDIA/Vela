// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"
// vsr_io
#include "vsr/io/UsdImport.hpp"
// usd
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>
// std
#include <memory>
#include <string>
#include <vector>

namespace vsr::io::usd {

/*
 * A Stage held open together with the resolution chain that turns it into the
 * scene VSR reads, and the Time Code both are currently evaluated at. One
 * Session is shared by the Import that created it and by every animation
 * binding that Import produced, so a scrub resolves through exactly the chain
 * the Import converted from.
 *
 * setTime() is the one place SetTime/ApplyPendingUpdates happen, and it does
 * them once per distinct Time Code no matter how many bindings ask for it.
 * The only Import Options a Session depends on are its Variant Selections,
 * which change what the Stage composes to; two Imports of one file with the
 * same selections share one Session whatever their other options (ADR 0045).
 *
 * Example:
 *   auto session = acquireUsdSession("/data/sim.usd");
 *   session->setTime(session->timeCodeAt(0.5f));
 *   auto prim = session->sceneIndex()->GetPrim(primPath);
 */
struct UsdStageSession
{
  VSR_NOT_COPYABLE(UsdStageSession)
  VSR_NOT_MOVEABLE(UsdStageSession)

  // Use acquireUsdSession(); this is public only so the registry can build a
  // Session with make_shared. `key` is what the registry filed it under, which
  // the Session needs in order to take itself back out again.
  UsdStageSession(std::string key,
      std::string filePath,
      UsdVariantSelections variantSelections,
      pxr::UsdStageRefPtr stage);
  ~UsdStageSession();

  const std::string &filePath() const;
  const UsdVariantSelections &variantSelections() const;
  const pxr::UsdStageRefPtr &stage() const;

  // What making the Variant Selections did; see UsdImportReport's fields of
  // the same names. Empty when the Session was opened without selections.
  size_t variantSelectedPrims() const;
  const std::vector<std::string> &unavailableVariants() const;
  const pxr::HdSceneIndexBaseRefPtr &sceneIndex() const;

  // The Stage's own clock.
  double startTimeCode() const;
  double endTimeCode() const;
  double timeCodesPerSecond() const;

  // Whether the Stage authored a time-code range of its own. When it did not,
  // the range is whatever noteAuthoredSampleTimes() has been told about.
  bool hasAuthoredTimeRange() const;

  // Widen the fallback range with times authored on one attribute. Does
  // nothing when the Stage authored a range, which is the authority. Without
  // this a Stage that has time samples but no `startTimeCode`/`endTimeCode`
  // would map every animation time onto one Time Code and never move.
  void noteAuthoredSampleTimes(const std::vector<double> &times);

  // Normalized animation time onto the Stage's clock. USD evaluates
  // continuously at the result, so no snapping to an authored sample happens.
  pxr::UsdTimeCode timeCodeAt(float t) const;

  pxr::UsdTimeCode currentTime() const;
  void setTime(pxr::UsdTimeCode time);

 private:
  void selectVariants();

  std::string m_key;
  std::string m_filePath;
  UsdVariantSelections m_variantSelections;
  size_t m_variantSelectedPrims{0};
  std::vector<std::string> m_unavailableVariants;
  pxr::UsdStageRefPtr m_stage;
  pxr::UsdImagingStageSceneIndexRefPtr m_stageSceneIndex;
  pxr::HdSceneIndexBaseRefPtr m_sceneIndex;
  pxr::UsdTimeCode m_currentTime{pxr::UsdTimeCode::EarliestTime()};
  double m_startTimeCode{0.0};
  double m_endTimeCode{0.0};
  bool m_authoredTimeRange{false};
  bool m_sawSampleTimes{false};
};

// Open `filePath` with `variantSelections` made, or join the Session already
// open on that pair. Sessions are keyed by absolute path and selections in a
// process-wide registry that holds them weakly: the last holder to let go
// closes the Stage. Returns null if the Stage cannot open.
std::shared_ptr<UsdStageSession> acquireUsdSession(const std::string &filePath,
    const UsdVariantSelections &variantSelections = {});

} // namespace vsr::io::usd
