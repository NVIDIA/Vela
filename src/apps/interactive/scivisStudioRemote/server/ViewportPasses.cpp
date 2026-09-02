// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ViewportPasses.h"
// vsr_scene
#include "vsr/scene/objects/Camera.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <string>

namespace vsr::scivis_studio::server {

using vsr::rendering::AOVType;

namespace {

constexpr uint32_t VOLUME_ID_BIT = 0x80000000u;
constexpr float DEFAULT_FOVY = vsr::math::radians(40.f);

// The monolith Viewport's check for the optional primitiveId frame channel.
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

// The RenderIndex's id for a surface or volume; empty for anything else.
std::optional<uint32_t> packedId(
    const SceneObjectRef &ref, const vsr::scene::Scene &scene)
{
  if (ref.type != ANARI_SURFACE && ref.type != ANARI_VOLUME)
    return {};
  if (!scene.getObject(ref.type, ref.objectIndex))
    return {};
  auto id = uint32_t(ref.objectIndex);
  if (ref.type == ANARI_VOLUME)
    id |= VOLUME_ID_BIT;
  return id;
}

} // namespace

// Setup //////////////////////////////////////////////////////////////////////

void ViewportPasses::setup(vsr::rendering::ImagePipeline &pipeline,
    vsr::rendering::AnariSceneRenderPass *scenePass,
    anari::Device device)
{
  m_device = device;
  m_scenePass = scenePass;

  m_primitiveIdSupported =
      deviceSupportsExtension(device, "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID");
  if (!m_primitiveIdSupported) {
    vsr::core::logStatus(
        "[StudioServer] device has no primitiveId channel: primitive outline"
        " and the PRIMITIVE_ID AOV stay off");
  }

  m_pickPass = pipeline.emplace_back<vsr::rendering::PickPass>();
  m_pickPass->setEnabled(false);
  m_pickPass->setPickOperation([this](vsr::rendering::ImageBuffers &b) {
    const auto size = m_pickPass->getDimensions();
    if (size.x == 0 || size.y == 0)
      return;
    const auto x = std::clamp(m_pickPixel.x, 0, int(size.x) - 1);
    const auto y = std::clamp(m_pickPixel.y, 0, int(size.y) - 1);
    // ANARI frames are stored bottom-up; the wire counts rows from the top.
    const size_t i = size_t(size.y - 1 - y) * size.x + size_t(x);
    PickSample sample;
    sample.objectId = b.objectId ? b.objectId[i] : ~0u;
    sample.depth = b.depth ? b.depth[i] : 0.f;
    m_pickSample = sample;
  });

  m_aovPass = pipeline.emplace_back<vsr::rendering::VisualizeAOVPass>();
  m_aovPass->setAOVType(AOVType::NONE);
  m_primitiveOutlinePass =
      pipeline.emplace_back<vsr::rendering::PrimitiveOutlineRenderPass>();
  m_primitiveOutlinePass->setEnabled(false);
  m_outlinePass = pipeline.emplace_back<vsr::rendering::OutlineRenderPass>();
  m_outlinePass->setOutlineId(~0u);
  m_boundsPass = pipeline.emplace_back<vsr::rendering::BoxOutlineRenderPass>();
  m_boundsPass->setEnabled(false);

  m_settings = protocol::ViewportSettings{};
  m_outlineIdentity = ~0u;
  syncChannels();
}

void ViewportPasses::teardown()
{
  m_scenePass = nullptr;
  m_pickPass = nullptr;
  m_aovPass = nullptr;
  m_primitiveOutlinePass = nullptr;
  m_outlinePass = nullptr;
  m_boundsPass = nullptr;
  m_device = nullptr;
  m_pickArmed = false;
  m_pickSample.reset();
  m_idChannelEnabled = false;
}

// Settings and outline ///////////////////////////////////////////////////////

void ViewportPasses::apply(const protocol::ViewportSettings &settings)
{
  m_settings = settings;
  if (!m_primitiveIdSupported
      && m_settings.visualizeAOV == AOVType::PRIMITIVE_ID) {
    m_settings.visualizeAOV = AOVType::NONE;
  }
  if (!m_aovPass)
    return;

  m_aovPass->setAOVType(m_settings.visualizeAOV);
  m_aovPass->setDepthRange(
      m_settings.depthVisualMinimum, m_settings.depthVisualMaximum);
  m_aovPass->setEdgeInvert(m_settings.edgeInvert);
  m_primitiveOutlinePass->setEnabled(doPrimitiveOutline());
  m_outlinePass->setOutlineId(outlineId());
  m_boundsPass->setColor(m_settings.worldBoundsColor);
  m_boundsPass->setWidth(uint32_t(std::max(1, m_settings.worldBoundsWidth)));
  if (!m_settings.showWorldBounds)
    m_boundsPass->setEnabled(false);
  syncChannels();
}

void ViewportPasses::setOutline(const std::optional<SceneObjectRef> &identity,
    const vsr::scene::Scene &scene)
{
  m_outlineIdentity = ~0u;
  if (identity) {
    if (auto id = packedId(*identity, scene)) {
      m_outlineIdentity = *id;
    } else {
      vsr::core::logWarning(
          "[StudioServer] SetOutline names no surface or volume (%s, %zu);"
          " outline cleared",
          anari::toString(identity->type),
          identity->objectIndex);
    }
  }
  if (!m_outlinePass)
    return;
  m_outlinePass->setOutlineId(outlineId());
  syncChannels();
}

const protocol::ViewportSettings &ViewportPasses::settings() const
{
  return m_settings;
}

uint32_t ViewportPasses::outlineId() const
{
  return m_settings.highlightSelection ? m_outlineIdentity : ~0u;
}

bool ViewportPasses::doPrimitiveOutline() const
{
  return m_settings.outlinePrimitives && m_primitiveIdSupported
      && m_settings.visualizeAOV == AOVType::NONE;
}

bool ViewportPasses::needIDs() const
{
  const auto aov = m_settings.visualizeAOV;
  return outlineId() != ~0u || aov == AOVType::EDGES
      || aov == AOVType::OBJECT_ID || doPrimitiveOutline();
}

bool ViewportPasses::primitiveIdSupported() const
{
  return m_primitiveIdSupported;
}

bool ViewportPasses::idChannelEnabled() const
{
  return m_idChannelEnabled;
}

void ViewportPasses::syncChannels()
{
  if (!m_scenePass)
    return;
  const auto aov = m_settings.visualizeAOV;
  m_scenePass->setEnableAlbedo(aov == AOVType::ALBEDO);
  m_scenePass->setEnableNormals(aov == AOVType::NORMAL);
  m_scenePass->setEnablePrimitiveId(m_primitiveIdSupported
      && (aov == AOVType::PRIMITIVE_ID || doPrimitiveOutline()));
  m_scenePass->setEnableInstanceId(aov == AOVType::INSTANCE_ID);
  // An armed pick keeps the ids on until it has been read out.
  m_idChannelEnabled = needIDs() || m_pickArmed;
  m_scenePass->setEnableIDs(m_idChannelEnabled);
}

// Per frame //////////////////////////////////////////////////////////////////

void ViewportPasses::updateWorldBounds(
    anari::World world, const vsr::scene::Object *camera)
{
  if (!m_boundsPass)
    return;

  const auto subtype = camera ? camera->subtype() : vsr::core::Token();
  const bool perspective = subtype == vsr::scene::tokens::camera::perspective;
  const bool orthographic = subtype == vsr::scene::tokens::camera::orthographic;
  const bool enabled =
      m_settings.showWorldBounds && world && (perspective || orthographic);
  m_boundsPass->setEnabled(enabled);
  if (!enabled)
    return;

  vsr::math::box3 bounds{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
  anariGetProperty(m_device,
      world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds,
      sizeof(bounds),
      ANARI_WAIT);
  m_boundsPass->setBox(bounds);

  const auto position = camera->parameterValueAs<vsr::math::float3>("position")
                            .value_or(vsr::math::float3(0.f, 0.f, 0.f));
  const auto direction =
      camera->parameterValueAs<vsr::math::float3>("direction")
          .value_or(vsr::math::float3(0.f, 0.f, -1.f));
  const auto up = camera->parameterValueAs<vsr::math::float3>("up").value_or(
      vsr::math::float3(0.f, 1.f, 0.f));
  if (perspective) {
    const auto fovy =
        camera->parameterValueAs<float>("fovy").value_or(DEFAULT_FOVY);
    m_boundsPass->setPerspectiveView(position, direction, up, fovy);
  } else {
    // The camera object already holds the eye on the ray-origin plane and
    // the image height (see vsr::rendering::updateCameraObject).
    const auto height = camera->parameterValueAs<float>("height").value_or(1.f);
    m_boundsPass->setOrthographicView(position, direction, up, height);
  }
}

// Picking ////////////////////////////////////////////////////////////////////

void ViewportPasses::armPick(int x, int y)
{
  if (!m_pickPass)
    return;
  m_pickPixel = vsr::math::int2(x, y);
  m_pickSample.reset();
  m_pickArmed = true;
  m_pickPass->setEnabled(true);
  syncChannels();
}

std::optional<PickSample> ViewportPasses::takePick()
{
  if (!m_pickPass)
    return {};
  auto sample = std::move(m_pickSample);
  m_pickSample.reset();
  m_pickArmed = false;
  m_pickPass->setEnabled(false);
  syncChannels();
  return sample;
}

} // namespace vsr::scivis_studio::server
