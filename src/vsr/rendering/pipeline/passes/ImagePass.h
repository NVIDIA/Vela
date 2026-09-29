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
 * Optional per-pixel channels an Image Source can produce in addition to the
 * always-present 8-bit color. Image Passes declare the channels they read;
 * the pipeline forwards the union to its source and backs exactly those.
 */
enum class ImageChannels : uint32_t
{
  NONE = 0,
  DEPTH = 1u << 0,
  OBJECT_ID = 1u << 1,
  PRIMITIVE_ID = 1u << 2,
  INSTANCE_ID = 1u << 3,
  ALBEDO = 1u << 4,
  NORMAL = 1u << 5,
  HDR_COLOR = 1u << 6
};

constexpr ImageChannels operator|(ImageChannels a, ImageChannels b);
constexpr ImageChannels operator&(ImageChannels a, ImageChannels b);
constexpr ImageChannels operator^(ImageChannels a, ImageChannels b);
constexpr ImageChannels &operator|=(ImageChannels &a, ImageChannels b);
// True when every channel of 'subset' is in 'set'.
constexpr bool hasChannels(ImageChannels set, ImageChannels subset);

/*
 * Per-pixel buffers shared across the stages of an Image Pipeline. 'color' is
 * always backed; every other buffer is null unless its channel was requested
 * by an enabled pass and is supported by the source. Channel buffers start
 * out as "background": infinite depth, ~0u IDs, zero elsewhere.
 */
struct ImageBuffers
{
  uint32_t *color{nullptr};
  float *hdrColor{nullptr};
  float *depth{nullptr};
  uint32_t *objectId{nullptr};
  uint32_t *primitiveId{nullptr};
  uint32_t *instanceId{nullptr};
  vsr::math::float3 *albedo{nullptr};
  vsr::math::float3 *normal{nullptr};
  detail::ComputeStream stream{};
};

/*
 * Per-frame scalars passed between Image Passes; reset before every frame.
 */
struct FrameState
{
  float exposure{0.f}; // auto exposure in EV, written by AutoExposurePass
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
  // The auxiliary channels this source can produce right now.
  virtual ImageChannels supportedChannels() const;
  // The channels the pipeline asked for; always within supportedChannels().
  ImageChannels channels() const;

 protected:
  // Called after channels() changed; start or stop producing channels here.
  virtual void updateChannels();
  virtual void render(ImageBuffers &b) = 0;

 private:
  void setChannels(ImageChannels channels);

  ImageChannels m_channels{ImageChannels::NONE};

  friend struct ImagePipeline;
};

/*
 * A stage that reads and modifies the pixels produced by the Image Source
 * (tone mapping, outlines, overlays). Passes run in insertion order.
 *
 * Example:
 *   struct MyPass : ImagePass {
 *     ImageChannels requiredChannels() const override { return DEPTH; }
 *     void render(ImageBuffers &b, FrameState &f) override { ... }
 *   };
 *   pipeline.addPass<MyPass>();
 */
struct ImagePass : public ImageStage
{
  ImageStageRole role() const override;
  // The channels this pass reads in its current configuration. A requested
  // channel may still be null in render() when the source cannot produce it.
  virtual ImageChannels requiredChannels() const;

 protected:
  virtual void render(ImageBuffers &b, FrameState &frame) = 0;

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

// Backs 'b' with color plus exactly 'channels' for 'numPixels' pixels.
// Buffers of channels already in 'current' are kept as-is; newly backed ones
// are filled with background values. Returns the new current channel set.
ImageChannels updateImageBuffers(ImageBuffers &b,
    size_t numPixels,
    ImageChannels current,
    ImageChannels channels);
// Frees every buffer of 'b', keeping its stream.
void freeImageBuffers(ImageBuffers &b);

} // namespace detail

// Inlined definitions ////////////////////////////////////////////////////////

constexpr ImageChannels operator|(ImageChannels a, ImageChannels b)
{
  return ImageChannels(uint32_t(a) | uint32_t(b));
}

constexpr ImageChannels operator&(ImageChannels a, ImageChannels b)
{
  return ImageChannels(uint32_t(a) & uint32_t(b));
}

constexpr ImageChannels operator^(ImageChannels a, ImageChannels b)
{
  return ImageChannels(uint32_t(a) ^ uint32_t(b));
}

constexpr ImageChannels &operator|=(ImageChannels &a, ImageChannels b)
{
  a = a | b;
  return a;
}

constexpr bool hasChannels(ImageChannels set, ImageChannels subset)
{
  return (set & subset) == subset;
}

} // namespace vsr::rendering
