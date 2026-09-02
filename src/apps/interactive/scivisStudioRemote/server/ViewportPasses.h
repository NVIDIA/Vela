// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "ViewportMessages.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// vsr_core
#include "vsr/core/VSRMath.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <cstdint>
#include <optional>

namespace vsr::scivis_studio::server {

// What one serviced Pick read out of the id and depth buffers. objectId is
// the RenderIndex's packed id (pool index, volume bit 0x80000000), ~0u on
// background; depth is the ANARI ray distance at that pixel.
struct PickSample
{
  uint32_t objectId{~0u};
  float depth{0.f};
};

/*
 * The server's Viewport Pass suite: the monolith Viewport's id-driven passes
 * (VisualizeAOVPass, PrimitiveOutlineRenderPass, OutlineRenderPass,
 * BoxOutlineRenderPass) plus the PickPass, appended to the ImagePipeline
 * right after the AnariSceneRenderPass and before the copy-out pass, in the
 * monolith's order. It keeps the last applied ViewportSettings and outline
 * identity and derives from them which ANARI frame channels the scene pass
 * must enable, so `needIDs` is recomputed whenever either changes: an
 * outline, the EDGES/OBJECT_ID AOVs and the primitive outline need
 * objectId; PRIMITIVE_ID and the primitive outline also need primitiveId,
 * which the device may not offer (queried once at setup; unsupported means
 * both stay silently off).
 *
 * Picks are one-shot: armPick() enables the id channel and the PickPass for
 * the next render, takePick() returns what that render read and restores the
 * id channel to what the settings need. Pixel convention: x right, y down
 * from the top-left corner of the frame; the pass converts to ANARI's
 * bottom-up buffer.
 *
 * Loop thread only, like the pipeline it lives in.
 *
 * Example:
 *   ViewportPasses passes;
 *   passes.setup(pipeline, scenePass, device);
 *   passes.apply(settings);                 // a latched ViewportSettings
 *   passes.setOutline(identity, scene);     // a latched SetOutline
 *   passes.updateWorldBounds(world, camera); // before each render
 *   pipeline.render();
 */
struct ViewportPasses
{
  // Appends the suite to `pipeline`; `scenePass` must be the pass appended
  // just before, and the copy-out pass is appended by the caller after.
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
  // Whether the scene pass currently renders the objectId channel.
  bool idChannelEnabled() const;

  // Per frame //

  // When showWorldBounds is on: the world's bounds and the view of `camera`
  // (a perspective or orthographic camera object; anything else hides the
  // box). Call before every render.
  void updateWorldBounds(anari::World world, const vsr::scene::Object *camera);

  // Picking //

  // Arms the PickPass for the next render at frame pixel (x, y), top-left
  // origin; coordinates outside the frame are clamped to its edge.
  void armPick(int x, int y);
  // What the render since armPick() read; empty when none was armed or the
  // pipeline did not run. Restores the id channel to what needIDs() says.
  std::optional<PickSample> takePick();

 private:
  void syncChannels();
  bool doPrimitiveOutline() const;

  anari::Device m_device{nullptr};
  vsr::rendering::AnariSceneRenderPass *m_scenePass{nullptr};
  vsr::rendering::PickPass *m_pickPass{nullptr};
  vsr::rendering::VisualizeAOVPass *m_aovPass{nullptr};
  vsr::rendering::PrimitiveOutlineRenderPass *m_primitiveOutlinePass{nullptr};
  vsr::rendering::OutlineRenderPass *m_outlinePass{nullptr};
  vsr::rendering::BoxOutlineRenderPass *m_boundsPass{nullptr};

  protocol::ViewportSettings m_settings;
  uint32_t m_outlineIdentity{~0u}; // packed, regardless of highlightSelection
  bool m_primitiveIdSupported{false};
  bool m_idChannelEnabled{false};

  bool m_pickArmed{false};
  vsr::math::int2 m_pickPixel{0, 0};
  std::optional<PickSample> m_pickSample;
};

} // namespace vsr::scivis_studio::server
