// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ImagePass.h"
// vsr_algorithms
#include "vsr/algorithms/cpu/convertColorBuffer.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/convertColorBuffer.hpp"
#endif
// std
#include <algorithm>
#include <cstring>

#if USE_CUDA
#include "cuda_runtime.h"
#endif

namespace vsr::rendering {

const char *toString(ImageStageRole role)
{
  switch (role) {
  case ImageStageRole::SOURCE:
    return "source";
  case ImageStageRole::PASS:
    return "pass";
  case ImageStageRole::SINK:
    return "sink";
  }
  return "unknown";
}

// ImageStage definitions /////////////////////////////////////////////////////

ImageStage::ImageStage() = default;

ImageStage::~ImageStage() = default;

bool ImageStage::isEnabled() const
{
  return m_enabled;
}

vsr::math::uint2 ImageStage::dimensions() const
{
  return m_size;
}

const char *ImageStage::name() const
{
  return "ImageStage";
}

void ImageStage::setEnabled(bool enabled)
{
  m_enabled = enabled;
}

void ImageStage::updateSize()
{
  // no-op
}

void ImageStage::setDimensions(uint32_t width, uint32_t height)
{
  if (m_size.x == width && m_size.y == height)
    return;

  m_size.x = width;
  m_size.y = height;

  updateSize();
}

// Role definitions ///////////////////////////////////////////////////////////

ImageStageRole ImageSource::role() const
{
  return ImageStageRole::SOURCE;
}

ImageStageRole ImagePass::role() const
{
  return ImageStageRole::PASS;
}

ImageStageRole ImageSink::role() const
{
  return ImageStageRole::SINK;
}

// Utility functions //////////////////////////////////////////////////////////

namespace detail {

void *allocate_(size_t numBytes)
{
#ifdef ENABLE_CUDA
  void *ptr = nullptr;
  cudaMallocManaged(&ptr, numBytes);
  return ptr;
#else
  return std::malloc(numBytes);
#endif
}

void free_(void *ptr)
{
#ifdef ENABLE_CUDA
  cudaFree(ptr);
#else
  std::free(ptr);
#endif
}

void memcpy_(void *dst, const void *src, size_t numBytes)
{
#ifdef ENABLE_CUDA
  cudaMemcpy(dst, src, numBytes, cudaMemcpyDefault);
#else
  std::memcpy(dst, src, numBytes);
#endif
}

void convertFloatColorBuffer_(
    ComputeStream stream, const float *v, uint8_t *out, size_t totalSize)
{
#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (stream) {
    vsr::algorithms::cuda::convertFloatToUint8(stream, v, out, totalSize);
    return;
  }
#endif
  vsr::algorithms::cpu::convertFloatToUint8(v, out, totalSize);
}

} // namespace detail

} // namespace vsr::rendering
