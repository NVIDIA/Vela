// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AnariSceneRenderPass.h"
#include "vsr/core/Logging.hpp"
// vsr_algorithms
#include "vsr/algorithms/cpu/clearBuffers.hpp"
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include "vsr/algorithms/cuda/clearBuffers.hpp"
#endif
// std
#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>

namespace vsr::rendering {

bool deviceSupportsExtension(anari::Device d, const char *extension)
{
  if (!d || !extension)
    return false;

  auto list = (const char *const *)anariGetObjectInfo(
      d, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST);
  if (!list)
    return false;

  for (const char *const *i = list; *i != nullptr; ++i) {
    if (std::string(*i) == extension)
      return true;
  }
  return false;
}

// Helper functions ///////////////////////////////////////////////////////////

static bool deviceHasExtension(anari::Device d, const char *extension)
{
  auto list = (const char *const *)anariGetObjectInfo(
      d, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST);
  if (!list)
    return false;

  for (const char *const *i = list; *i != nullptr; ++i) {
    if (std::strcmp(*i, extension) == 0)
      return true;
  }
  return false;
}

static bool supportsCUDAFbData(anari::Device d)
{
#ifdef ENABLE_CUDA
  return deviceHasExtension(d, "ANARI_NV_FRAME_BUFFERS_CUDA");
#else
  return false;
#endif
}

// Auxiliary channels the device can produce; depth is core ANARI.
static ImageChannels deviceChannels(anari::Device d)
{
  struct
  {
    const char *extension;
    ImageChannels channel;
  } constexpr optional[] = {
      {"ANARI_KHR_FRAME_CHANNEL_OBJECT_ID", ImageChannels::OBJECT_ID},
      {"ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID", ImageChannels::PRIMITIVE_ID},
      {"ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID", ImageChannels::INSTANCE_ID},
      {"ANARI_KHR_FRAME_CHANNEL_ALBEDO", ImageChannels::ALBEDO},
      {"ANARI_KHR_FRAME_CHANNEL_NORMAL", ImageChannels::NORMAL},
  };

  ImageChannels channels = ImageChannels::DEPTH;
  for (const auto &o : optional) {
    if (deviceHasExtension(d, o.extension))
      channels |= o.channel;
  }
  return channels;
}

// ANARI frame parameter and element type backing each auxiliary channel.
struct FrameChannel
{
  ImageChannels channel;
  const char *parameter;
  anari::DataType type;
};

static constexpr FrameChannel FRAME_CHANNELS[] = {
    {ImageChannels::DEPTH, "channel.depth", ANARI_FLOAT32},
    {ImageChannels::OBJECT_ID, "channel.objectId", ANARI_UINT32},
    {ImageChannels::PRIMITIVE_ID, "channel.primitiveId", ANARI_UINT32},
    {ImageChannels::INSTANCE_ID, "channel.instanceId", ANARI_UINT32},
    {ImageChannels::ALBEDO, "channel.albedo", ANARI_FLOAT32_VEC3},
    {ImageChannels::NORMAL, "channel.normal", ANARI_FLOAT32_VEC3},
};

// AnariSceneRenderPass definitions ///////////////////////////////////////////

AnariSceneRenderPass::AnariSceneRenderPass(anari::Device d) : m_device(d)
{
  anari::retain(d, d);
  m_frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, m_frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, m_frame, "accumulation", true);
  // Placeholder size so every intermediate commit/flush finalizes validly
  // (devices warn/skip on sizeless frames, e.g. helide "invalid frame
  // dimensions"). Tiny but non-degenerate, so a warm-up render at this size
  // still exercises the device's full render path. Overwritten by the first
  // updateSize().
  anari::setParameter(d, m_frame, "size", vsr::math::uint2(64, 64));

  m_deviceSupportsCUDAFrames = supportsCUDAFbData(d);
  m_deviceChannels = deviceChannels(d);

  if (m_deviceSupportsCUDAFrames)
    vsr::core::logStatus("[ImagePipeline] using CUDA-mapped fb channels");
  else
    vsr::core::logStatus("[ImagePipeline] using host-mapped fb channels");
}

AnariSceneRenderPass::~AnariSceneRenderPass()
{
  detail::freeImageBuffers(m_buffers);

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

ImageChannels AnariSceneRenderPass::supportedChannels() const
{
  // HDR color exists only when the frame itself renders float color.
  const ImageChannels hdr = m_format == ANARI_FLOAT32_VEC4
      ? ImageChannels::HDR_COLOR
      : ImageChannels::NONE;
  return m_deviceChannels | hdr;
}

void AnariSceneRenderPass::updateChannels()
{
  const bool added = setFrameChannels(channels());
  resizeStaging();

  // A frame already in flight lacks the added channels: restart once, at the
  // next render(), so they are produced before being read.
  if (added)
    m_pendingRestart = true;
}

bool AnariSceneRenderPass::setFrameChannels(ImageChannels wanted)
{
  bool changed = false;
  bool added = false;
  for (const auto &c : FRAME_CHANNELS) {
    const bool want = hasChannels(wanted, c.channel);
    if (want == hasChannels(m_frameChannels, c.channel))
      continue;
    changed = true;
    added |= want;
    vsr::core::logInfo("[ImagePipeline] %s frame %s",
        want ? "enabling" : "disabling",
        c.parameter);
    if (want)
      anari::setParameter(m_device, m_frame, c.parameter, c.type);
    else
      anari::unsetParameter(m_device, m_frame, c.parameter);
  }
  if (changed)
    anari::commitParameters(m_device, m_frame);

  m_frameChannels = wanted;
  return added;
}

vsr::math::uint2 AnariSceneRenderPass::pickImageSize() const
{
  return dimensions();
}

std::optional<PickSample> AnariSceneRenderPass::renderPickSample(
    vsr::math::uint2 pixel)
{
  const auto size = dimensions();
  if (!m_device || !m_camera || !m_renderer || !m_world || size.x == 0
      || size.y == 0)
    return {};

  // A separate frame sharing the display's camera, renderer and world sees
  // the same image, so the display frame keeps its channels, accumulation and
  // any render in flight. Created per pick: picks are rare, and a full-size
  // frame with depth and ID channels is too large to keep around.
  // Per ANARI this leaves the display frame's accumulation alone; some devices
  // (VisRTX, OSPRay, Barney, Visionaray as of 2026-09) still reset or perturb
  // other frames on an unrelated frame commit -- a device bug, not handled
  // here.
  auto frame = anari::newObject<anari::Frame>(m_device);
  anari::setParameter(m_device, frame, "size", size);
  anari::setParameter(m_device, frame, "camera", m_camera);
  anari::setParameter(m_device, frame, "renderer", m_renderer);
  anari::setParameter(m_device, frame, "world", m_world);
  anari::setParameter(m_device, frame, "accumulation", false);
  // Enable only the channels the query reads; no color output is needed.
  constexpr ImageChannels PICK_CHANNELS = ImageChannels::DEPTH
      | ImageChannels::OBJECT_ID | ImageChannels::INSTANCE_ID
      | ImageChannels::PRIMITIVE_ID;
  const ImageChannels channels = PICK_CHANNELS & m_deviceChannels;
  for (const auto &c : FRAME_CHANNELS) {
    if (hasChannels(channels, c.channel))
      anari::setParameter(m_device, frame, c.parameter, c.type);
  }
  anari::commitParameters(m_device, frame);

  anari::render(m_device, frame);
  anari::wait(m_device, frame);

  // Host-mapped channels: only one pixel is read.
  const size_t i = size_t(pixel.y) * size.x + pixel.x;
  auto read = [&](const char *channel, auto &out) {
    using T = std::remove_reference_t<decltype(out)>;
    auto mapped = anari::map<T>(m_device, frame, channel);
    const bool ok =
        mapped.data && mapped.width == size.x && mapped.height == size.y;
    if (ok)
      out = mapped.data[i];
    anari::unmap(m_device, frame, channel);
  };

  PickSample sample;
  if (hasChannels(channels, ImageChannels::DEPTH))
    read("channel.depth", sample.depth);
  if (hasChannels(channels, ImageChannels::OBJECT_ID))
    read("channel.objectId", sample.objectId);
  if (hasChannels(channels, ImageChannels::INSTANCE_ID))
    read("channel.instanceId", sample.instanceId);
  if (hasChannels(channels, ImageChannels::PRIMITIVE_ID))
    read("channel.primitiveId", sample.primitiveId);

  anari::release(m_device, frame);
  return sample;
}

void AnariSceneRenderPass::resizeStaging()
{
  const auto size = dimensions();
  m_stagingChannels = detail::updateImageBuffers(m_buffers,
      size_t(size.x) * size_t(size.y),
      m_stagingChannels,
      m_frameChannels);
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
  auto dims = dimensions();
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
  auto size = dimensions();
  anari::setParameter(m_device, m_frame, "size", size);
  anari::commitParameters(m_device, m_frame);

  updateCameraAspect();

  detail::freeImageBuffers(m_buffers);
  m_stagingChannels = ImageChannels::NONE;
  resizeStaging();

  // Render the new size synchronously so the first frame displayed after a
  // resize is valid.
  restartFrame();
}

void AnariSceneRenderPass::updateCameraAspect()
{
  auto size = dimensions();
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
  // Any deferred restart request is fulfilled (or subsumed by the still
  // pending first render) here.
  m_pendingRestart = false;

  if (!m_device || m_firstFrame)
    return;
  anari::discard(m_device, m_frame);
  waitForCompletion();
  anari::render(m_device, m_frame);
  waitForCompletion();
}

void AnariSceneRenderPass::render(ImageBuffers &b)
{
  m_buffers.stream = b.stream;

  // Restart the frame at most once per render, no matter how many channel
  // toggles happened since the last one -- each toggle only commits and
  // defers its restart here, so no "first frame" with requested-but-
  // unmapped channels is ever rendered twice in a row.
  if (m_pendingRestart)
    restartFrame();

  // Asynchronous: the picture composited is the frame the previous call
  // started, so the first call has nothing to show yet. Synchronous: this
  // call's own render of the current scene state is what is composited, so
  // the caller's picture and its state agree.
  const bool nothingToShow = m_runAsync && m_firstFrame;
  if (m_runAsync) {
    startFirstFrame(false);
  } else {
    if (!m_firstFrame)
      waitForCompletion();
    anari::render(m_device, m_frame);
    waitForCompletion();
    m_firstFrame = false;
  }

  if (anari::isReady(m_device, m_frame)) {
    copyFrameData();
    // Only the asynchronous mode keeps a render in flight between calls.
    if (m_runAsync)
      anari::render(m_device, m_frame);
  }

  if (!nothingToShow)
    publish(b);
  else {
    const auto size = dimensions();
    const uint32_t totalPixels = uint32_t(size.x) * uint32_t(size.y);
#ifdef VSR_ALGORITHMS_HAS_CUDA
    if (b.stream)
      vsr::algorithms::cuda::fill(b.stream, b.color, totalPixels, 0);
    else
#endif
      vsr::algorithms::cpu::fill(b.color, totalPixels, 0);
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

  const auto has = [&](ImageChannels c) {
    return hasChannels(m_frameChannels, c);
  };
  const bool enableDepth = has(ImageChannels::DEPTH);

  auto color = anari::map<void>(m_device, m_frame, colorChannel);
  anari::MappedFrameData<float> depth{};
  if (enableDepth)
    depth = anari::map<float>(m_device, m_frame, depthChannel);

  const vsr::math::uint2 size(dimensions());
  const size_t totalSize = size.x * size.y;

  // All channels this frame requested must have mapped successfully. Note
  // depth is only mapped when requested, so a null depth is fine unless
  // depth was requested.
  const bool sizeMatches =
      totalSize > 0 && size.x == color.width && size.y == color.height;
  const bool colorMapped =
      color.data != nullptr && color.pixelType != ANARI_UNKNOWN;
  const bool depthMappedIfRequested = !enableDepth || depth.data != nullptr;

  const bool valid = sizeMatches && colorMapped && depthMappedIfRequested;
  if (!valid) {
    anari::unmap(m_device, m_frame, colorChannel);
    if (enableDepth)
      anari::unmap(m_device, m_frame, depthChannel);
    return;
  }

  if (color.pixelType == ANARI_FLOAT32_VEC4) {
    if (m_buffers.hdrColor)
      detail::copy(m_buffers.hdrColor, (float *)color.data, totalSize * 4);
    detail::convertFloatColorBuffer_(m_buffers.stream,
        (const float *)color.data,
        (uint8_t *)m_buffers.color,
        totalSize * 4);
  } else
    detail::copy(m_buffers.color, (uint32_t *)color.data, totalSize);

  if (enableDepth)
    detail::copy(m_buffers.depth, depth.data, totalSize);
  // A requested channel that fails to map this frame reads as background
  // rather than keeping an earlier frame's values.
  const uint32_t totalPixels = uint32_t(totalSize);
  auto fillIds = [&](uint32_t *buf) {
#ifdef VSR_ALGORITHMS_HAS_CUDA
    if (m_buffers.stream) {
      vsr::algorithms::cuda::fill(m_buffers.stream, buf, totalPixels, ~0u);
      return;
    }
#endif
    vsr::algorithms::cpu::fill(buf, totalPixels, ~0u);
  };
  auto fillZero3 = [&](vsr::math::float3 *buf) {
    auto *f = reinterpret_cast<float *>(buf);
#ifdef VSR_ALGORITHMS_HAS_CUDA
    if (m_buffers.stream) {
      vsr::algorithms::cuda::fill(m_buffers.stream, f, totalPixels * 3, 0.f);
      return;
    }
#endif
    vsr::algorithms::cpu::fill(f, totalPixels * 3, 0.f);
  };
  auto copyOr = [&](auto *dst, const auto &mapped, auto &&background) {
    if (mapped.data)
      detail::copy(dst, mapped.data, totalSize);
    else
      background(dst);
  };

  if (has(ImageChannels::OBJECT_ID)) {
    copyOr(m_buffers.objectId,
        anari::map<uint32_t>(m_device, m_frame, objectIdChannel),
        fillIds);
  }
  if (has(ImageChannels::PRIMITIVE_ID)) {
    copyOr(m_buffers.primitiveId,
        anari::map<uint32_t>(m_device, m_frame, primitiveIdChannel),
        fillIds);
  }
  if (has(ImageChannels::INSTANCE_ID)) {
    copyOr(m_buffers.instanceId,
        anari::map<uint32_t>(m_device, m_frame, instanceIdChannel),
        fillIds);
  }
  if (has(ImageChannels::ALBEDO)) {
    copyOr(m_buffers.albedo,
        anari::map<vsr::math::float3>(m_device, m_frame, albedoChannel),
        fillZero3);
  }
  if (has(ImageChannels::NORMAL)) {
    copyOr(m_buffers.normal,
        anari::map<vsr::math::float3>(m_device, m_frame, normalChannel),
        fillZero3);
  }

  anari::unmap(m_device, m_frame, colorChannel);
  if (enableDepth)
    anari::unmap(m_device, m_frame, depthChannel);
  if (has(ImageChannels::OBJECT_ID))
    anari::unmap(m_device, m_frame, objectIdChannel);
  if (has(ImageChannels::PRIMITIVE_ID))
    anari::unmap(m_device, m_frame, primitiveIdChannel);
  if (has(ImageChannels::INSTANCE_ID))
    anari::unmap(m_device, m_frame, instanceIdChannel);
  if (has(ImageChannels::ALBEDO))
    anari::unmap(m_device, m_frame, albedoChannel);
  if (has(ImageChannels::NORMAL))
    anari::unmap(m_device, m_frame, normalChannel);
}

void AnariSceneRenderPass::publish(ImageBuffers &b)
{
  const vsr::math::uint2 size(dimensions());
  const size_t totalSize = size.x * size.y;

  // The pipeline backs exactly channels(), which the staging mirrors.
  detail::copy(b.color, m_buffers.color, totalSize);
  if (b.hdrColor && m_buffers.hdrColor)
    detail::copy(b.hdrColor, m_buffers.hdrColor, totalSize * 4);
  if (b.depth && m_buffers.depth)
    detail::copy(b.depth, m_buffers.depth, totalSize);
  if (b.objectId && m_buffers.objectId)
    detail::copy(b.objectId, m_buffers.objectId, totalSize);
  if (b.primitiveId && m_buffers.primitiveId)
    detail::copy(b.primitiveId, m_buffers.primitiveId, totalSize);
  if (b.instanceId && m_buffers.instanceId)
    detail::copy(b.instanceId, m_buffers.instanceId, totalSize);
  if (b.albedo && m_buffers.albedo)
    detail::copy(b.albedo, m_buffers.albedo, totalSize);
  if (b.normal && m_buffers.normal)
    detail::copy(b.normal, m_buffers.normal, totalSize);
}

} // namespace vsr::rendering
