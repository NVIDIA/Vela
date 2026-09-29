// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "ViewportMessages.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_rendering
#include "vsr/rendering/pick/PickRequest.h"
#include "vsr/rendering/pipeline/ImagePipeline.h"
// vsr_core
#include "vsr/core/VSRMath.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <atomic>
#include <cstdint>
#include <optional>

namespace vsr::scivis_studio::server {

// The fovy a perspective camera object renders with when it sets none.
constexpr float DEFAULT_CAMERA_FOVY = vsr::math::radians(40.f);

// The shot camera's projection, including its explicit aspect when set.
// Unsupported camera subtypes have no view.
std::optional<vsr::rendering::CameraView> readCameraView(
    const vsr::scene::Object &camera);
std::optional<SceneObjectRef> sceneObjectRef(
    const vsr::rendering::PickHit &hit);

/*
 * The server's Viewport Pass suite: the monolith Viewport's id-driven passes
 * (VisualizeAOVPass, PrimitiveOutlineRenderPass, OutlineRenderPass,
 * BoxOutlineRenderPass), added after the ANARI Image Source and before the
 * copy-out sink. Enabled passes declare their channel demand to the pipeline;
 * unsupported AOVs and primitive outlines stay off.
 *
 * Picks are one-shot: armPick() stores a request and takePick() executes it
 * against the source without changing the displayed frame or its channels.
 * Wire pixels count from the top-left; takePick() converts to ANARI row order.
 *
 * Loop thread only, like the pipeline it lives in.
 *
 * Example:
 *   ViewportPasses passes;
 *   passes.setup(pipeline, scenePass, device);
 *   passes.apply(settings);                 // a latched ViewportSettings
 *   passes.setOutline(identity, scene);     // a latched SetOutline
 *   passes.updateWorldBounds(bounds, camera); // before each render
 *   pipeline.render();
 */
struct ViewportPasses
{
  // Adds the suite to `pipeline`; `scenePass` is its source, and the caller
  // adds the copy-out sink afterwards.
  void setup(vsr::rendering::ImagePipeline &pipeline,
      vsr::rendering::AnariSceneRenderPass *scenePass,
      anari::Device device);
  // Forgets the pass pointers; the pipeline owns and frees them.
  void teardown();

  // Settings and outline //

  void apply(const protocol::ViewportSettings &settings);
  // A surface or volume of `scene` is outlined; absent or anything else
  // clears the outline.
  void setOutline(const std::optional<SceneObjectRef> &identity,
      const vsr::scene::Scene &scene);

  const protocol::ViewportSettings &settings() const;
  // The packed id the OutlineRenderPass draws, ~0u when no outline shows
  // (nothing selected or highlightSelection off).
  uint32_t outlineId() const;
  bool needIDs() const;
  bool primitiveIdSupported() const;
  // Whether the enabled display passes request a supported objectId channel.
  bool idChannelEnabled() const;

  // Per frame //

  // When showWorldBounds is on: the world's bounds (the server queries them
  // once per frame, for the box and for the frame header both) and the view
  // of `camera` (a perspective or orthographic camera object; anything else,
  // or empty bounds, hides the box). Call before every render.
  void updateWorldBounds(
      const vsr::math::box3 &bounds, const vsr::scene::Object *camera);

  // Picking //

  // Arms a request at frame pixel (x, y), top-left origin; coordinates outside
  // the frame are clamped before converting to ANARI row order.
  void armPick(int x, int y);
  // Executes the armed request; empty when none was armed or on a miss.
  std::optional<vsr::rendering::PickHit> takePick();

 private:
  void updateIdChannelFlag();
  bool sourceSupports(vsr::rendering::ImageChannels channels) const;
  bool doPrimitiveOutline() const;

  vsr::rendering::AnariSceneRenderPass *m_scenePass{nullptr};
  vsr::rendering::VisualizeAOVPass *m_aovPass{nullptr};
  vsr::rendering::PrimitiveOutlineRenderPass *m_primitiveOutlinePass{nullptr};
  vsr::rendering::OutlineRenderPass *m_outlinePass{nullptr};
  vsr::rendering::BoxOutlineRenderPass *m_boundsPass{nullptr};

  protocol::ViewportSettings m_settings;
  uint32_t m_outlineIdentity{~0u}; // packed, regardless of highlightSelection
  bool m_primitiveIdSupported{false};
  // Written on the loop thread with the pass; atomic because the server
  // tests poll it while frames stream, the one read of this object that
  // does not wait for the loop to pause.
  std::atomic<bool> m_idChannelEnabled{false};

  bool m_pickArmed{false};
  vsr::math::int2 m_pickPixel{0, 0};
  std::optional<vsr::rendering::CameraView> m_view;
};

} // namespace vsr::scivis_studio::server
