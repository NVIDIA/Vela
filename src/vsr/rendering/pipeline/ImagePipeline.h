// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "passes/AnariAxesRenderPass.h"
#include "passes/AnariSceneRenderPass.h"
#include "passes/BoxOutlineRenderPass.h"
#if ENABLE_SDL
#include "passes/CopyToSDLTexturePass.h"
#endif
#include "passes/CopyFromColorBufferPass.hpp"
#include "passes/ExternalFrameSource.h"
#include "passes/MultiDeviceSceneRenderPass.h"
#include "passes/OutlineRenderPass.h"
#include "passes/PickPass.h"
#include "passes/PrimitiveOutlineRenderPass.h"
#include "passes/SaveToFilePass.h"
#include "passes/VisualizeAOVPass.h"
// std
#include <memory>
#include <type_traits>
#include <vector>

namespace vsr::rendering {

/*
 * One Image Source, followed by ordered Image Passes, followed by Image Sinks,
 * all sharing one set of per-pixel buffers. Each render() runs the source,
 * then every enabled pass in insertion order, then every enabled sink. With
 * no source, or a disabled one, render() does nothing and the previous image
 * is kept.
 *
 * Stage pointers returned by setSource/addPass/addSink are owned by the
 * pipeline and stay valid until clear(), or, for the source, until it is
 * replaced by another setSource() call.
 *
 * Example:
 *   ImagePipeline pipeline(1920, 1080);
 *   auto *scene = pipeline.setSource<AnariSceneRenderPass>(device);
 *   pipeline.addPass<ToneMapPass>();
 *   pipeline.render();
 *   auto *pixels = pipeline.getColorBuffer();
 */
struct ImagePipeline final
{
  struct PassTiming
  {
    const char *name{nullptr};
    ImageStageRole role{ImageStageRole::PASS};
    float milliseconds{0.f};
  };

  ImagePipeline();
  ImagePipeline(int width, int height);
  ~ImagePipeline();

  VSR_NOT_COPYABLE(ImagePipeline)
  VSR_NOT_MOVEABLE(ImagePipeline)

  const uint32_t *getColorBuffer() const;
  const std::vector<PassTiming> &getPassTimings() const;
  ImageSource *source() const;
  bool empty() const;

  void setDimensions(uint32_t width, uint32_t height);
  void render();
  void clear();

  template <typename T, typename... Args>
  T *setSource(Args &&...args);
  template <typename T, typename... Args>
  T *addPass(Args &&...args);
  template <typename T, typename... Args>
  T *addSink(Args &&...args);

 private:
  void cleanup();
  void sizeStage(ImageStage &s) const;
  void setSourceImpl(std::unique_ptr<ImageSource> s);
  void timeStage(ImageStage &s, float milliseconds);

  std::unique_ptr<ImageSource> m_source;
  std::vector<std::unique_ptr<ImagePass>> m_passes;
  std::vector<std::unique_ptr<ImageSink>> m_sinks;
  std::vector<PassTiming> m_passTimings;
  ImageBuffers m_buffers;
  vsr::math::uint2 m_size{0, 0};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T, typename... Args>
inline T *ImagePipeline::setSource(Args &&...args)
{
  static_assert(std::is_base_of_v<ImageSource, T>,
      "ImagePipeline::setSource() requires an ImageSource");
  auto s = std::make_unique<T>(std::forward<Args>(args)...);
  T *p = s.get();
  setSourceImpl(std::move(s));
  return p;
}

template <typename T, typename... Args>
inline T *ImagePipeline::addPass(Args &&...args)
{
  static_assert(std::is_base_of_v<ImagePass, T>,
      "ImagePipeline::addPass() requires an ImagePass");
  auto s = std::make_unique<T>(std::forward<Args>(args)...);
  T *p = s.get();
  sizeStage(*p);
  m_passes.push_back(std::move(s));
  return p;
}

template <typename T, typename... Args>
inline T *ImagePipeline::addSink(Args &&...args)
{
  static_assert(std::is_base_of_v<ImageSink, T>,
      "ImagePipeline::addSink() requires an ImageSink");
  auto s = std::make_unique<T>(std::forward<Args>(args)...);
  T *p = s.get();
  sizeStage(*p);
  m_sinks.push_back(std::move(s));
  return p;
}

} // namespace vsr::rendering
