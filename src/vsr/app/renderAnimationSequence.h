// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/Any.hpp"
#include "vsr/core/ObjectPool.hpp"
#include "vsr/core/Token.hpp"
#include "vsr/core/VSRMath.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/passes/VisualizeAOVPass.h"
// std
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scene {
struct Object;
struct Scene;
} // namespace vsr::scene

namespace vsr::app {

struct Context;

struct OfflineRenderFrame
{
  int frameIndex{0};
  std::string destination;
};

/*
 * Authoritative assignment applied after animation evaluation, in list order.
 * Capture a live Scene object's type/index at construction, not its address:
 * pool growth (including generated Camera creation) must not invalidate a job.
 * This borrows the Scene and slot, without owning or retaining the object.
 * Types must match device metadata. Duplicates are last-wins; original values
 * and absence are restored.
 */
struct OfflineParameterOverride
{
  OfflineParameterOverride() = default;
  OfflineParameterOverride(
      vsr::scene::Object *object, vsr::core::Token name, vsr::core::Any value);

  vsr::scene::Object *object() const;

  vsr::core::Token name;
  vsr::core::Any value;

 private:
  vsr::scene::Scene *m_scene{nullptr};
  vsr::core::Any m_object;
};

struct OfflineCameraParameter
{
  vsr::core::Token name;
  vsr::core::Any value;
};

/*
 * Resolved synchronous request; renderer and override targets are borrowed, not
 * owned. Keep the Context, its Scene, renderer and override targets alive until
 * renderOfflineJob() returns or throws, including during callbacks. Callbacks
 * must not delete referenced objects or otherwise invalidate these borrows.
 * Overrides remain reusable across engine-created Camera pool growth. While
 * retaining a job for reuse, do not delete/replace its targets, defragment
 * their Scene slots or destroy the Scene. Construct overrides only from live
 * objects; obtain fresh object addresses through object() or Scene refs after
 * allocation.
 */
struct OfflineRenderJob
{
  std::vector<OfflineRenderFrame> frames;
  size_t cameraIndex{vsr::core::INVALID_INDEX};
  bool generateCamera{false};
  // Required for generation, retained unchanged when frames are partitioned.
  // Fit at this global frame, never at the first rank-local assigned frame.
  int globalFirstSelectedFrame{-1};
  // Saved assignments first, explicit assignments last; radians for fovy.
  // These target the selected (including generated) Camera after animation.
  std::vector<OfflineCameraParameter> cameraParameters;
  std::optional<vsr::math::float3> cameraLookAt;
  uint32_t width{1024};
  uint32_t height{768};
  uint32_t samples{128};
  std::string deviceIdentifier{"helide"};
  const vsr::scene::Object *renderer{nullptr};
  vsr::rendering::AOVType aovType{vsr::rendering::AOVType::NONE};
  float depthMin{0.f};
  float depthMax{1.f};
  bool edgeInvert{false};
  std::vector<OfflineParameterOverride> overrides;
};

enum class OfflineRenderStatus
{
  Completed,
  Cancelled,
  Failed
};

struct OfflineRenderResult
{
  OfflineRenderStatus status{OfflineRenderStatus::Failed};
  size_t completedFrames{0};
  int frameIndex{-1};
  std::string destination;
  std::string diagnostic;
};

struct OfflineRenderProgress
{
  int frameIndex;
  size_t position;
  size_t totalFrames;
};

// Called before each frame; false cancels without writing that frame.
using OfflineRenderCallback =
    std::function<bool(const OfflineRenderProgress &)>;

// No camera/device fallback, output naming, overwrite policy or MPI scheduling.
// Expected setup/device/buffer/write errors return Failed. Unexpected
// exceptions (including callbacks and restoration notifications) propagate to
// the caller's application boundary. Caller runtime clock and exact authored
// override entry state (including absence, type, enabled state and metadata)
// are restored on all exits, even when generated-Camera preparation evaluates
// animation. Non-overridden animation is evaluated at the restored clock;
// arbitrary observer side effects are not rolled back. Output aspect is set on
// the isolated ANARI camera.
OfflineRenderResult renderOfflineJob(Context &ctx,
    const OfflineRenderJob &job,
    OfflineRenderCallback preFrameCallback = {});

using RenderSequenceCallback =
    std::function<bool(int frameIndex, int numFrames)>;

// UI sequence adapter: retains legacy naming, selection and overwrite policy.
// Returns the engine outcome; configuration errors also return Failed.
// Unexpected exceptions propagate just as in renderOfflineJob().
OfflineRenderResult renderAnimationSequence(Context &ctx,
    const std::string &outputDir,
    const std::string &filePrefix,
    RenderSequenceCallback preFrameCallback = {});

} // namespace vsr::app
