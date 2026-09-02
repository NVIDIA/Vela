// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// anari
#include <anari/anari_cpp.hpp>

namespace vsr::rendering {

/*
 * ImagePass that drives a single ANARI Frame with a configurable camera,
 * renderer, and world; optionally captures auxiliary AOV buffers
 * (depth, normals, albedo, object/primitive/instance IDs).
 *
 * Example:
 *   auto *pass = pipeline.emplace_back<AnariSceneRenderPass>(device);
 *   pass->setCamera(cam); pass->setRenderer(rend); pass->setWorld(world);
 */
struct AnariSceneRenderPass : public ImagePass
{
  AnariSceneRenderPass(anari::Device d);
  ~AnariSceneRenderPass() override;
  const char *name() const override;

  void setCamera(anari::Camera c);
  void setRenderer(anari::Renderer r);
  void setWorld(anari::World w);
  void setColorFormat(anari::DataType t);
  void setEnableIDs(bool on);
  void setEnablePrimitiveId(bool on);
  void setEnableInstanceId(bool on);
  void setEnableAlbedo(bool on);
  void setEnableNormals(bool on);
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

 private:
  void updateSize() override;
  void updateCameraAspect();
  void restartFrame();
  void render(ImageBuffers &b, int stageId) override;
  void copyFrameData();
  void composite(ImageBuffers &b, int stageId);
  void cleanup();

  ImageBuffers m_buffers;

  bool m_firstFrame{true};
  bool m_deviceSupportsCUDAFrames{false};
  bool m_enableIDs{false};
  bool m_enablePrimitiveId{false};
  bool m_enableInstanceId{false};
  bool m_enableAlbedo{false};
  bool m_enableNormals{false};
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
