// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AnariSceneRenderPass.h"
#include "vsr/core/Logging.hpp"
// vsr_algorithms
#include "vsr/algorithms/cpu/clearBuffers.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/clearBuffers.hpp"
#endif
#include "vsr/algorithms/cpu/depthCompositeFrame.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/depthCompositeFrame.hpp"
#endif
// std
#include <algorithm>
#include <cstring>

namespace vsr::rendering {

// Helper functions ///////////////////////////////////////////////////////////

static bool supportsCUDAFbData(anari::Device d)
{
#ifdef ENABLE_CUDA
  bool supportsCUDA = false;
  auto list = (const char *const *)anariGetObjectInfo(
      d, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST);

  for (const char *const *i = list; *i != nullptr; ++i) {
    if (std::string(*i) == "ANARI_NV_FRAME_BUFFERS_CUDA") {
      supportsCUDA = true;
      break;
    }
  }

  return supportsCUDA;
#else
  return false;
#endif
}

// AnariSceneRenderPass definitions ///////////////////////////////////////////

AnariSceneRenderPass::AnariSceneRenderPass(anari::Device d) : m_device(d)
{
  anari::retain(d, d);
  m_frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, m_frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, m_frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, m_frame, "accumulation", true);

  m_deviceSupportsCUDAFrames = supportsCUDAFbData(d);

  if (m_deviceSupportsCUDAFrames)
    vsr::core::logStatus("[ImagePipeline] using CUDA-mapped fb channels");
  else
    vsr::core::logStatus("[ImagePipeline] using host-mapped fb channels");
}

AnariSceneRenderPass::~AnariSceneRenderPass()
{
  cleanup();

  anari::discard(m_device, m_frame);
  waitForCompletion();

  anari::release(m_device, m_frame);
  anari::release(m_device, m_camera);
  anari::release(m_device, m_renderer);
  anari::release(m_device, m_world);
  anari::release(m_device, m_device);
}

void AnariSceneRenderPass::setCamera(anari::Camera c)
{
  if (c)
    anari::retain(m_device, c);
  anari::setParameter(m_device, m_frame, "camera", c);
  anari::commitParameters(m_device, m_frame);
  anari::release(m_device, m_camera);
  m_camera = c;
  updateCameraAspect();
}

void AnariSceneRenderPass::setRenderer(anari::Renderer r)
{
  if (r)
    anari::retain(m_device, r);
  anari::setParameter(m_device, m_frame, "renderer", r);
  anari::commitParameters(m_device, m_frame);
  anari::release(m_device, m_renderer);
  m_renderer = r;
}

void AnariSceneRenderPass::setWorld(anari::World w)
{
  if (w)
    anari::retain(m_device, w);
  anari::setParameter(m_device, m_frame, "world", w);
  anari::commitParameters(m_device, m_frame);
  anari::release(m_device, m_world);
  m_world = w;
}

void AnariSceneRenderPass::setColorFormat(anari::DataType t)
{
  m_format = t;
  anari::setParameter(m_device, m_frame, "channel.color", t);
  anari::commitParameters(m_device, m_frame);
}

void AnariSceneRenderPass::setEnableDepth(bool on)
{
  if (on == m_enableDepth)
    return;

  m_enableDepth = on;

  if (on) {
    vsr::core::logInfo("[ImagePipeline] enabling depth frame channel");
    anari::setParameter(m_device, m_frame, "channel.depth", ANARI_FLOAT32);
  } else {
    vsr::core::logInfo("[ImagePipeline] disabling depth frame channel");
    anari::unsetParameter(m_device, m_frame, "channel.depth");

    // Composite() reads the staging buffer even when disabled (stageId > 0
    // chains); make it read as "background" so compositing degrades to
    // color-only.
    if (m_buffers.depth) {
      auto size = getDimensions();
      std::fill(m_buffers.depth,
          m_buffers.depth + size_t(size.x) * size_t(size.y),
          vsr::math::inf);
    }
  }

  anari::commitParameters(m_device, m_frame);

  if (on)
    restartFrame();
}

void AnariSceneRenderPass::setEnableIDs(bool on)
{
  if (on == m_enableIDs)
    return;

  m_enableIDs = on;

  if (on) {
    vsr::core::logInfo("[ImagePipeline] enabling objectId frame channel");
    anari::setParameter(m_device, m_frame, "channel.objectId", ANARI_UINT32);
  } else {
    vsr::core::logInfo("[ImagePipeline] disabling objectId frame channel");
    anari::unsetParameter(m_device, m_frame, "channel.objectId");
    auto size = getDimensions();
    const size_t totalSize = size_t(size.x) * size_t(size.y);
    std::fill(m_buffers.objectId, m_buffers.objectId + totalSize, ~0u);
  }

  anari::commitParameters(m_device, m_frame);

  if (on)
    restartFrame();
}

void AnariSceneRenderPass::setEnablePrimitiveId(bool on)
{
  if (on == m_enablePrimitiveId)
    return;

  m_enablePrimitiveId = on;

  if (on) {
    vsr::core::logInfo("[ImagePipeline] enabling primitiveId frame channel");
    anari::setParameter(m_device, m_frame, "channel.primitiveId", ANARI_UINT32);
  } else {
    vsr::core::logInfo("[ImagePipeline] disabling primitiveId frame channel");
    anari::unsetParameter(m_device, m_frame, "channel.primitiveId");

    auto size = getDimensions();
    const size_t totalSize = size_t(size.x) * size_t(size.y);
    std::fill(m_buffers.primitiveId, m_buffers.primitiveId + totalSize, ~0u);
  }

  anari::commitParameters(m_device, m_frame);

  if (on)
    restartFrame();
}

void AnariSceneRenderPass::setEnableInstanceId(bool on)
{
  if (on == m_enableInstanceId)
    return;

  m_enableInstanceId = on;

  if (on) {
    vsr::core::logInfo("[ImagePipeline] enabling instanceId frame channel");
    anari::setParameter(m_device, m_frame, "channel.instanceId", ANARI_UINT32);
  } else {
    vsr::core::logInfo("[ImagePipeline] disabling instanceId frame channel");
    anari::unsetParameter(m_device, m_frame, "channel.instanceId");

    auto size = getDimensions();
    const size_t totalSize = size_t(size.x) * size_t(size.y);
    std::fill(m_buffers.instanceId, m_buffers.instanceId + totalSize, ~0u);
  }

  anari::commitParameters(m_device, m_frame);

  if (on)
    restartFrame();
}

void AnariSceneRenderPass::setEnableAlbedo(bool on)
{
  if (on == m_enableAlbedo)
    return;

  m_enableAlbedo = on;

  if (on) {
    vsr::core::logInfo("[ImagePipeline] enabling albedo frame channel");
    anari::setParameter(
        m_device, m_frame, "channel.albedo", ANARI_FLOAT32_VEC3);
  } else {
    vsr::core::logInfo("[ImagePipeline] disabling albedo frame channel");
    anari::unsetParameter(m_device, m_frame, "channel.albedo");
  }

  anari::commitParameters(m_device, m_frame);
}

void AnariSceneRenderPass::setEnableNormals(bool on)
{
  if (on == m_enableNormals)
    return;

  m_enableNormals = on;

  if (on) {
    vsr::core::logInfo("[ImagePipeline] enabling normal frame channel");
    anari::setParameter(
        m_device, m_frame, "channel.normal", ANARI_FLOAT32_VEC3);
  } else {
    vsr::core::logInfo("[ImagePipeline] disabling normal frame channel");
    anari::unsetParameter(m_device, m_frame, "channel.normal");
  }

  anari::commitParameters(m_device, m_frame);
}

void AnariSceneRenderPass::setUseImplicitAspectRatio(bool on)
{
  if (on == m_useImplicitAspectRatio)
    return;

  m_useImplicitAspectRatio = on;
  updateCameraAspect();
}

void AnariSceneRenderPass::startFirstFrame(bool wait)
{
  if (!m_firstFrame)
    return;
  auto dims = getDimensions();
  anari::render(m_device, m_frame);
  if (wait)
    waitForCompletion();
  m_firstFrame = false;
}

void AnariSceneRenderPass::waitForCompletion()
{
  anari::wait(m_device, m_frame);
}

void AnariSceneRenderPass::setRunAsync(bool on)
{
  m_runAsync = on;
  if (!on)
    waitForCompletion();
}

anari::Frame AnariSceneRenderPass::getFrame() const
{
  return m_frame;
}

void AnariSceneRenderPass::updateSize()
{
  cleanup();
  auto size = getDimensions();
  anari::setParameter(m_device, m_frame, "size", size);
  anari::commitParameters(m_device, m_frame);

  updateCameraAspect();

  m_depthIsInf = false; // shared 'b.depth' was reallocated
  const size_t totalSize = size_t(size.x) * size_t(size.y);
  m_buffers.color = detail::allocate<uint32_t>(totalSize);
  m_buffers.hdrColor = detail::allocate<float>(totalSize * 4);
  m_buffers.depth = detail::allocate<float>(totalSize);
  std::fill(m_buffers.depth, m_buffers.depth + totalSize, vsr::math::inf);
  m_buffers.objectId = detail::allocate<uint32_t>(totalSize);
  m_buffers.primitiveId = detail::allocate<uint32_t>(totalSize);
  m_buffers.instanceId = detail::allocate<uint32_t>(totalSize);
  m_buffers.albedo = detail::allocate<vsr::math::float3>(totalSize);
  m_buffers.normal = detail::allocate<vsr::math::float3>(totalSize);

  // Render the new size synchronously so the first frame displayed after a
  // resize is valid.
  restartFrame();
}

void AnariSceneRenderPass::updateCameraAspect()
{
  auto size = getDimensions();
  if (!m_camera || size.y == 0)
    return;

  if (m_useImplicitAspectRatio)
    anari::unsetParameter(m_device, m_camera, "aspect");
  else
    anari::setParameter(m_device, m_camera, "aspect", size.x / float(size.y));

  anari::commitParameters(m_device, m_camera);
}

void AnariSceneRenderPass::restartFrame()
{
  if (!m_device || m_firstFrame)
    return;
  anari::discard(m_device, m_frame);
  waitForCompletion();
  anari::render(m_device, m_frame);
  waitForCompletion();
}

void AnariSceneRenderPass::render(ImageBuffers &b, int stageId)
{
  m_buffers.stream = b.stream;

  startFirstFrame(false);

  if (!m_runAsync)
    waitForCompletion();

  if (anari::isReady(m_device, m_frame)) {
    copyFrameData();
    anari::render(m_device, m_frame);
  }

  if (!m_firstFrame)
    composite(b, stageId);
  else {
    const auto size = getDimensions();
    const uint32_t totalPixels = uint32_t(size.x) * uint32_t(size.y);
#ifdef VSR_ALGORITHMS_HAS_CUDA
    if (b.stream)
      vsr::algorithms::cuda::fill(b.stream, b.color, totalPixels, 0);
#else
    vsr::algorithms::cpu::fill(b.color, totalPixels, 0);
#endif
  }
}

void AnariSceneRenderPass::copyFrameData()
{
  const char *colorChannel =
      m_deviceSupportsCUDAFrames ? "channel.colorCUDA" : "channel.color";
  const char *depthChannel =
      m_deviceSupportsCUDAFrames ? "channel.depthCUDA" : "channel.depth";
  const char *objectIdChannel =
      m_deviceSupportsCUDAFrames ? "channel.objectIdCUDA" : "channel.objectId";
  const char *primitiveIdChannel = m_deviceSupportsCUDAFrames
      ? "channel.primitiveIdCUDA"
      : "channel.primitiveId";
  const char *instanceIdChannel = m_deviceSupportsCUDAFrames
      ? "channel.instanceIdCUDA"
      : "channel.instanceId";
  const char *albedoChannel =
      m_deviceSupportsCUDAFrames ? "channel.albedoCUDA" : "channel.albedo";
  const char *normalChannel =
      m_deviceSupportsCUDAFrames ? "channel.normalCUDA" : "channel.normal";

  auto color = anari::map<void>(m_device, m_frame, colorChannel);
  anari::MappedFrameData<float> depth{};
  if (m_enableDepth)
    depth = anari::map<float>(m_device, m_frame, depthChannel);

  const vsr::math::uint2 size(getDimensions());
  const size_t totalSize = size.x * size.y;

  // All channels this frame requested must have mapped successfully. Note
  // depth is only mapped when requested, so a null depth is fine unless
  // m_enableDepth asked for it.
  const bool sizeMatches =
      totalSize > 0 && size.x == color.width && size.y == color.height;
  const bool colorMapped =
      color.data != nullptr && color.pixelType != ANARI_UNKNOWN;
  const bool depthMappedIfRequested = !m_enableDepth || depth.data != nullptr;

  const bool valid = sizeMatches && colorMapped && depthMappedIfRequested;
  if (!valid) {
    anari::unmap(m_device, m_frame, colorChannel);
    if (m_enableDepth)
      anari::unmap(m_device, m_frame, depthChannel);
    return;
  }

  if (color.pixelType == ANARI_FLOAT32_VEC4) {
    detail::copy(m_buffers.hdrColor, (float *)color.data, totalSize * 4);
    detail::convertFloatColorBuffer_(m_buffers.stream,
        (const float *)color.data,
        (uint8_t *)m_buffers.color,
        totalSize * 4);
  } else
    detail::copy(m_buffers.color, (uint32_t *)color.data, totalSize);

  if (m_enableDepth)
    detail::copy(m_buffers.depth, depth.data, totalSize);
  if (m_enableIDs) {
    auto objectId = anari::map<uint32_t>(m_device, m_frame, objectIdChannel);
    if (objectId.data)
      detail::copy(m_buffers.objectId, objectId.data, totalSize);
  }
  if (m_enablePrimitiveId) {
    auto primitiveId =
        anari::map<uint32_t>(m_device, m_frame, primitiveIdChannel);
    if (primitiveId.data)
      detail::copy(m_buffers.primitiveId, primitiveId.data, totalSize);
  }
  if (m_enableInstanceId) {
    auto instanceId =
        anari::map<uint32_t>(m_device, m_frame, instanceIdChannel);
    if (instanceId.data)
      detail::copy(m_buffers.instanceId, instanceId.data, totalSize);
  }
  if (m_enableAlbedo) {
    auto albedo =
        anari::map<vsr::math::float3>(m_device, m_frame, albedoChannel);
    if (albedo.data)
      detail::copy(m_buffers.albedo, albedo.data, totalSize);
  }
  if (m_enableNormals) {
    auto normal =
        anari::map<vsr::math::float3>(m_device, m_frame, normalChannel);
    if (normal.data)
      detail::copy(m_buffers.normal, normal.data, totalSize);
  }

  anari::unmap(m_device, m_frame, colorChannel);
  if (m_enableDepth)
    anari::unmap(m_device, m_frame, depthChannel);
  if (m_enableIDs)
    anari::unmap(m_device, m_frame, objectIdChannel);
  if (m_enablePrimitiveId)
    anari::unmap(m_device, m_frame, primitiveIdChannel);
  if (m_enableInstanceId)
    anari::unmap(m_device, m_frame, instanceIdChannel);
  if (m_enableAlbedo)
    anari::unmap(m_device, m_frame, albedoChannel);
  if (m_enableNormals)
    anari::unmap(m_device, m_frame, normalChannel);
}

void AnariSceneRenderPass::composite(ImageBuffers &b, int stageId)
{
  const bool firstPass = stageId == 0;
  const vsr::math::uint2 size(getDimensions());
  const size_t totalSize = size.x * size.y;
  const float inf = vsr::math::inf;

  if (firstPass) {
    detail::copy(b.color, m_buffers.color, totalSize);
    if (m_format == ANARI_FLOAT32_VEC4)
      detail::copy(b.hdrColor, m_buffers.hdrColor, totalSize * 4);
    if (m_enableDepth) {
      detail::copy(b.depth, m_buffers.depth, totalSize);
      m_depthIsInf = false;
    } else if (!m_depthIsInf) {
      // No depth produced this frame: make the shared buffer read as
      // "background" so depth-testing passes (box outline, stageId > 0
      // compositing) fall back instead of using stale/garbage values.
      const uint32_t totalPixels = uint32_t(totalSize);
#ifdef VSR_ALGORITHMS_HAS_CUDA
      if (b.stream)
        vsr::algorithms::cuda::fill(b.stream, b.depth, totalPixels, inf);
      else
#endif
        vsr::algorithms::cpu::fill(b.depth, totalPixels, inf);
      m_depthIsInf = true;
    }
    detail::copy(b.objectId, m_buffers.objectId, totalSize);
    if (m_enablePrimitiveId)
      detail::copy(b.primitiveId, m_buffers.primitiveId, totalSize);
    if (m_enableInstanceId)
      detail::copy(b.instanceId, m_buffers.instanceId, totalSize);
    if (m_enableAlbedo)
      detail::copy(b.albedo, m_buffers.albedo, totalSize);
    if (m_enableNormals)
      detail::copy(b.normal, m_buffers.normal, totalSize);
  } else {
    const uint32_t totalPixels = uint32_t(size.x) * uint32_t(size.y);
#ifdef VSR_ALGORITHMS_HAS_CUDA
    if (b.stream) {
      vsr::algorithms::cuda::depthCompositeFrame(b.stream,
          b.color,
          b.depth,
          b.objectId,
          m_buffers.color,
          m_buffers.depth,
          m_buffers.objectId,
          totalPixels,
          firstPass);
      return;
    }
#endif
    vsr::algorithms::cpu::depthCompositeFrame(b.color,
        b.depth,
        b.objectId,
        m_buffers.color,
        m_buffers.depth,
        m_buffers.objectId,
        totalPixels,
        firstPass);
  }
}

void AnariSceneRenderPass::cleanup()
{
  detail::free(m_buffers.color);
  detail::free(m_buffers.hdrColor);
  detail::free(m_buffers.depth);
  detail::free(m_buffers.objectId);
  detail::free(m_buffers.primitiveId);
  detail::free(m_buffers.instanceId);
  detail::free(m_buffers.albedo);
  detail::free(m_buffers.normal);
}

} // namespace vsr::rendering
