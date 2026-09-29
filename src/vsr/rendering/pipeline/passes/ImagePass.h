// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"
#include "vsr/core/VSRMath.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/passes/detail/ComputeStream.h"

namespace vsr::rendering {

/*
 * POD struct holding pointers to all per-pixel output buffers (color, depth,
 * object/primitive/instance IDs, albedo, normals, hdrColor and
 * exposure setting) shared across the stages of an Image Pipeline.
 */
struct ImageBuffers
{
  uint32_t *color{nullptr};
  float *hdrColor{nullptr};
  float exposure{0.f};
  float *depth{nullptr};
  uint32_t *objectId{nullptr};
  uint32_t *primitiveId{nullptr};
  uint32_t *instanceId{nullptr};
  vsr::math::float3 *albedo{nullptr};
  vsr::math::float3 *normal{nullptr};
  detail::ComputeStream stream{};
};

enum class ImageStageRole
{
  SOURCE,
  PASS,
  SINK
};

const char *toString(ImageStageRole role);

/*
 * Common base of every stage of an Image Pipeline: a named, independently
 * enable-able unit that the owning pipeline keeps sized to its output.
 * Concrete stages derive from exactly one of ImageSource, ImagePass or
 * ImageSink, which fix their role and the shape of their render hook.
 */
struct ImageStage
{
  ImageStage();
  virtual ~ImageStage();

  VSR_NOT_COPYABLE(ImageStage)
  VSR_NOT_MOVEABLE(ImageStage)

  bool isEnabled() const;
  vsr::math::uint2 dimensions() const;
  virtual const char *name() const;
  virtual ImageStageRole role() const = 0;

  void setEnabled(bool enabled);

 protected:
  virtual void updateSize();

 private:
  void setDimensions(uint32_t width, uint32_t height);

  vsr::math::uint2 m_size{0, 0};
  bool m_enabled{true};

  friend struct ImagePipeline;
};

/*
 * The single stage of an Image Pipeline that produces the frame's pixels. It
 * must write every pixel of the color buffer it is handed; everything after
 * it refines or consumes those pixels.
 *
 * Example:
 *   struct MySource : ImageSource {
 *     void render(ImageBuffers &b) override { ... }
 *   };
 *   pipeline.setSource<MySource>();
 */
struct ImageSource : public ImageStage
{
  ImageStageRole role() const override;

 protected:
  virtual void render(ImageBuffers &b) = 0;

  friend struct ImagePipeline;
};

/*
 * A stage that reads and modifies the pixels produced by the Image Source
 * (tone mapping, outlines, overlays). Passes run in insertion order.
 *
 * Example:
 *   struct MyPass : ImagePass {
 *     void render(ImageBuffers &b) override { ... }
 *   };
 *   pipeline.addPass<MyPass>();
 */
struct ImagePass : public ImageStage
{
  ImageStageRole role() const override;

 protected:
  virtual void render(ImageBuffers &b) = 0;

  friend struct ImagePipeline;
};

/*
 * A terminal stage that hands the finished pixels to a consumer outside the
 * pipeline (display texture, network stream). Sinks never modify the image.
 *
 * Example:
 *   struct MySink : ImageSink {
 *     void render(const ImageBuffers &b) override { ... }
 *   };
 *   pipeline.addSink<MySink>();
 */
struct ImageSink : public ImageStage
{
  ImageStageRole role() const override;

 protected:
  virtual void render(const ImageBuffers &b) = 0;

  friend struct ImagePipeline;
};

// Utility functions //////////////////////////////////////////////////////////

namespace detail {

void *allocate_(size_t numBytes);
void free_(void *ptr);
void memcpy_(void *dst, const void *src, size_t numBytes);
void convertFloatColorBuffer_(
    ComputeStream stream, const float *v, uint8_t *out, size_t totalSize);

template <typename T>
inline void copy(T *dst, const T *src, size_t numElements)
{
  detail::memcpy_(dst, src, sizeof(T) * numElements);
}

template <typename T>
inline T *allocate(size_t numElements)
{
  return (T *)detail::allocate_(numElements * sizeof(T));
}

template <typename T>
inline void free(T *ptr)
{
  detail::free_(ptr);
}

} // namespace detail

} // namespace vsr::rendering
