// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// vsr_rendering
#include "vsr/rendering/pick/PickRequest.h"
// anari
#include <anari/anari_cpp.hpp>

namespace vsr::rendering {

// Whether `d` lists `extension` (an "ANARI_KHR_..." name) among its device
// extensions; what a pass asks before enabling an optional frame channel.
// False for a null device or extension.
bool deviceSupportsExtension(anari::Device d, const char *extension);

/*
 * Image Source that drives a single ANARI Frame with a configurable camera,
 * renderer, and world; optionally captures auxiliary AOV buffers
 * (depth, normals, albedo, object/primitive/instance IDs). Answers Pick
 * Requests (see vsr::rendering::pick()).
 *
 * Example:
 *   auto *pass = pipeline.setSource<AnariSceneRenderPass>(device);
 *   pass->setCamera(cam); pass->setRenderer(rend); pass->setWorld(world);
 */
struct AnariSceneRenderPass : public ImageSource, public PickableSource
{
  AnariSceneRenderPass(anari::Device d);
  ~AnariSceneRenderPass() override;
  const char *name() const override;
  ImageChannels supportedChannels() const override;

  void setCamera(anari::Camera c);
  void setRenderer(anari::Renderer r);
  void setWorld(anari::World w);
  // FLOAT32_VEC4 additionally makes HDR_COLOR available to passes.
  void setColorFormat(anari::DataType t);
  void setUseImplicitAspectRatio(bool on);

  void startFirstFrame(bool wait = false);
  void waitForCompletion();

  // Default true: render() composites the frame the previous call started and
  // starts the next, so the picture lags the scene by one call. False renders
  // synchronously: render() renders, waits and composites in one call, so the
  // buffers show the scene as it is at that call (a frame stamped with the
  // time it was rendered at, a pick against the current camera).
  void setRunAsync(bool on);

  anari::Frame getFrame() const;

  // PickableSource: one synchronous render of a temporary frame sharing this
  // source's camera, renderer and world; the display frame is untouched.
  vsr::math::uint2 pickImageSize() const override;
  std::optional<PickSample> renderPickSample(vsr::math::uint2 pixel) override;

 private:
  void updateSize() override;
  void updateChannels() override;
  // Returns true if any channel was added.
  bool setFrameChannels(ImageChannels wanted);
  void resizeStaging();
  void updateCameraAspect();
  void restartFrame();
  void render(ImageBuffers &b) override;
  void copyFrameData();
  void publish(ImageBuffers &b);

  ImageBuffers m_buffers; // staging: the latest completed ANARI frame
  ImageChannels m_stagingChannels{ImageChannels::NONE};
  ImageChannels m_frameChannels{ImageChannels::NONE}; // set on m_frame
  ImageChannels m_deviceChannels{ImageChannels::NONE};

  bool m_firstFrame{true};
  // Channels were toggled since the last render: restart the frame once at
  // the next render() instead of once per toggle.
  bool m_pendingRestart{false};
  bool m_deviceSupportsCUDAFrames{false};
  bool m_runAsync{true};
  bool m_useImplicitAspectRatio{false};

  anari::DataType m_format{ANARI_UFIXED8_RGBA_SRGB};

  anari::Device m_device{nullptr};
  anari::Camera m_camera{nullptr};
  anari::Renderer m_renderer{nullptr};
  anari::World m_world{nullptr};
  anari::Frame m_frame{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *AnariSceneRenderPass::name() const
{
  return "ANARI Scene";
}

} // namespace vsr::rendering
