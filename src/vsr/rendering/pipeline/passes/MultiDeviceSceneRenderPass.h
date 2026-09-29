// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include <vsr/rendering/pipeline/passes/ImagePass.h>
// anari
#include <anari/anari_cpp.hpp>
// std
#include <functional>

namespace vsr::rendering {

/*
 * Image Source that drives one ANARI Frame per device in parallel and
 * composites their results into a single color buffer; mirrors
 * AnariSceneRenderPass but spans multiple ANARI devices.
 *
 * Example:
 *   auto *pass = pipeline.setSource<MultiDeviceSceneRenderPass>(devices);
 *   pass->setCamera(0, cam); pass->setWorld(0, world);
 */
struct MultiDeviceSceneRenderPass : public ImageSource
{
  MultiDeviceSceneRenderPass(const std::vector<anari::Device> &devices);
  ~MultiDeviceSceneRenderPass() override;
  ImageChannels supportedChannels() const override;

  size_t numDevices() const;

  void setCamera(size_t i, anari::Camera c);
  void setRenderer(size_t i, anari::Renderer r);
  void setWorld(size_t i, anari::World w);
  void setColorFormat(anari::DataType t);

  // default' true' -- if 'false', then anari::wait() on each pass
  void setRunAsync(bool on);

  anari::Frame getFrame(size_t i = 0) const;

 private:
  void foreach_frame(
      const std::function<void(anari::Device, anari::Frame)> &func) const;

  void updateSize() override;
  void render(ImageBuffers &b) override;
  void copyFrameData();
  void publish(ImageBuffers &b);
  void cleanup();

  ImageBuffers m_buffers;

  bool m_firstFrame{true};
  bool m_runAsync{true};

  std::vector<anari::Device> m_devices;
  std::vector<anari::Frame> m_frames;
};

} // namespace vsr::rendering
