// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ImagePipeline.h"
// std
#include <chrono>
#include <cstring>
#include <limits>

namespace vsr::rendering {

ImagePipeline::ImagePipeline()
{
#ifdef ENABLE_CUDA
  cudaStreamCreate(&m_buffers.stream);
#endif
}

ImagePipeline::ImagePipeline(int width, int height) : ImagePipeline()
{
  setDimensions(width, height);
}

ImagePipeline::~ImagePipeline()
{
  cleanup();
#ifdef ENABLE_CUDA
  cudaStreamDestroy(m_buffers.stream);
#endif
}

const uint32_t *ImagePipeline::getColorBuffer() const
{
  return m_buffers.color;
}

const std::vector<ImagePipeline::PassTiming> &ImagePipeline::getPassTimings()
    const
{
  return m_passTimings;
}

ImageSource *ImagePipeline::source() const
{
  return m_source.get();
}

bool ImagePipeline::empty() const
{
  return !m_source && m_passes.empty() && m_sinks.empty();
}

void ImagePipeline::setDimensions(uint32_t width, uint32_t height)
{
  if (m_size.x == width && m_size.y == height)
    return;
  m_size.x = width;
  m_size.y = height;

  cleanup();

  const size_t totalSize = size_t(width) * size_t(height);
  if (totalSize == 0)
    return;

  m_buffers.color = detail::allocate<uint32_t>(totalSize);
  m_buffers.hdrColor = detail::allocate<float>(totalSize * 4);
  m_buffers.depth = detail::allocate<float>(totalSize);
  m_buffers.instanceId = detail::allocate<uint32_t>(totalSize);
  m_buffers.objectId = detail::allocate<uint32_t>(totalSize);
  m_buffers.primitiveId = detail::allocate<uint32_t>(totalSize);
  m_buffers.albedo = detail::allocate<vsr::math::float3>(totalSize);
  m_buffers.normal = detail::allocate<vsr::math::float3>(totalSize);

  if (m_source)
    m_source->setDimensions(width, height);
  for (auto &p : m_passes)
    p->setDimensions(width, height);
  for (auto &s : m_sinks)
    s->setDimensions(width, height);
}

void ImagePipeline::render()
{
  m_passTimings.clear();
  if (!m_source || !m_source->isEnabled() || !m_buffers.color)
    return;

  using clock = std::chrono::steady_clock;
  auto elapsed = [](clock::time_point start) {
    return std::chrono::duration<float, std::milli>(clock::now() - start)
        .count();
  };

  m_buffers.exposure = 0.f;

  auto start = clock::now();
  m_source->render(m_buffers);
  timeStage(*m_source, elapsed(start));

  for (auto &p : m_passes) {
    if (!p->isEnabled())
      continue;
    start = clock::now();
    p->render(m_buffers);
    timeStage(*p, elapsed(start));
  }

  const ImageBuffers &finished = m_buffers;
  for (auto &s : m_sinks) {
    if (!s->isEnabled())
      continue;
    start = clock::now();
    s->render(finished);
    timeStage(*s, elapsed(start));
  }
}

void ImagePipeline::clear()
{
  m_sinks.clear();
  m_passes.clear();
  m_source.reset();
  m_passTimings.clear();
  setDimensions(0, 0);
}

void ImagePipeline::sizeStage(ImageStage &s) const
{
  if (m_size.x != 0 && m_size.y != 0)
    s.setDimensions(m_size.x, m_size.y);
}

void ImagePipeline::setSourceImpl(std::unique_ptr<ImageSource> s)
{
  sizeStage(*s);
  m_source = std::move(s);
}

void ImagePipeline::timeStage(ImageStage &s, float milliseconds)
{
  m_passTimings.push_back({s.name(), s.role(), milliseconds});
}

void ImagePipeline::cleanup()
{
  detail::free(m_buffers.color);
  detail::free(m_buffers.hdrColor);
  detail::free(m_buffers.depth);
  detail::free(m_buffers.objectId);
  detail::free(m_buffers.primitiveId);
  detail::free(m_buffers.instanceId);
  detail::free(m_buffers.albedo);
  detail::free(m_buffers.normal);

  // nullify all buffers, retaining the associated CUDA streams.
  auto stream = m_buffers.stream;
  m_buffers = {};
  m_buffers.stream = stream;
}

} // namespace vsr::rendering
