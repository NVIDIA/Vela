// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ViewportPasses.h"
// vsr_scene
#include "vsr/scene/objects/Camera.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <cmath>

namespace vsr::scivis_studio::server {

using vsr::rendering::AOVType;

namespace {

// The RenderIndex's id for a surface or volume; empty for anything else.
std::optional<uint32_t> packedId(
    const SceneObjectRef &ref, const vsr::scene::Scene &scene)
{
  if (ref.type != ANARI_SURFACE && ref.type != ANARI_VOLUME)
    return {};
  if (!scene.getObject(ref.type, ref.objectIndex))
    return {};
  return vsr::scene::encodeObjectId(ref.type, ref.objectIndex);
}

} // namespace

// Camera view and picks //////////////////////////////////////////////////////

std::optional<vsr::rendering::CameraView> readCameraView(
    const vsr::scene::Object &camera)
{
  using vsr::math::float3;
  using vsr::rendering::CameraView;
  const auto position =
      camera.parameterValueAs<float3>("position").value_or(float3(0.f));
  const auto direction = camera.parameterValueAs<float3>("direction")
                             .value_or(float3(0.f, 0.f, -1.f));
  const auto up =
      camera.parameterValueAs<float3>("up").value_or(float3(0.f, 1.f, 0.f));
  CameraView view;
  if (camera.subtype() == vsr::scene::tokens::camera::perspective) {
    view = CameraView::perspective(position,
        direction,
        up,
        camera.parameterValueAs<float>("fovy").value_or(DEFAULT_CAMERA_FOVY));
  } else if (camera.subtype() == vsr::scene::tokens::camera::orthographic) {
    view = CameraView::orthographic(position,
        direction,
        up,
        camera.parameterValueAs<float>("height").value_or(1.f));
  } else
    return {};
  view.aspect = camera.parameterValueAs<float>("aspect").value_or(0.f);
  return view;
}

std::optional<SceneObjectRef> sceneObjectRef(const vsr::rendering::PickHit &hit)
{
  if (!hit.object)
    return {};
  SceneObjectRef ref;
  ref.type = hit.object->type;
  ref.objectIndex = hit.object->index;
  return ref;
}

// Setup //////////////////////////////////////////////////////////////////////

void ViewportPasses::setup(vsr::rendering::ImagePipeline &pipeline,
    vsr::rendering::AnariSceneRenderPass *scenePass,
    anari::Device device)
{
  m_scenePass = scenePass;

  m_primitiveIdSupported = vsr::rendering::deviceSupportsExtension(
      device, "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID");
  if (!m_primitiveIdSupported) {
    vsr::core::logStatus(
        "[StudioServer] device has no primitiveId channel: primitive outline"
        " and the PRIMITIVE_ID AOV stay off");
  }

  m_aovPass = pipeline.addPass<vsr::rendering::VisualizeAOVPass>();
  m_aovPass->setAOVType(AOVType::NONE);
  m_primitiveOutlinePass =
      pipeline.addPass<vsr::rendering::PrimitiveOutlineRenderPass>();
  m_primitiveOutlinePass->setEnabled(false);
  m_outlinePass = pipeline.addPass<vsr::rendering::OutlineRenderPass>();
  m_outlinePass->setOutlineId(~0u);
  m_boundsPass = pipeline.addPass<vsr::rendering::BoxOutlineRenderPass>();
  m_boundsPass->setEnabled(false);

  m_settings = protocol::ViewportSettings{};
  m_outlineIdentity = ~0u;
  updateIdChannelFlag();
}

void ViewportPasses::teardown()
{
  m_scenePass = nullptr;
  m_aovPass = nullptr;
  m_primitiveOutlinePass = nullptr;
  m_outlinePass = nullptr;
  m_boundsPass = nullptr;
  m_pickArmed = false;
  m_view.reset();
  m_idChannelEnabled = false;
}

// Settings and outline ///////////////////////////////////////////////////////

void ViewportPasses::apply(const protocol::ViewportSettings &settings)
{
  m_settings = settings;
  if (!sourceSupports(
          vsr::rendering::requiredChannels(m_settings.visualizeAOV))) {
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
  updateIdChannelFlag();
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
  updateIdChannelFlag();
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

bool ViewportPasses::sourceSupports(
    vsr::rendering::ImageChannels channels) const
{
  return m_scenePass
      && vsr::rendering::hasChannels(
          m_scenePass->supportedChannels(), channels);
}

void ViewportPasses::updateIdChannelFlag()
{
  m_idChannelEnabled =
      needIDs() && sourceSupports(vsr::rendering::ImageChannels::OBJECT_ID);
}

// Per frame //////////////////////////////////////////////////////////////////

void ViewportPasses::updateWorldBounds(
    const vsr::math::box3 &bounds, const vsr::scene::Object *camera)
{
  m_view = camera ? readCameraView(*camera) : std::nullopt;
  if (!m_boundsPass)
    return;

  const bool haveBounds = bounds.lower.x <= bounds.upper.x
      && bounds.lower.y <= bounds.upper.y && bounds.lower.z <= bounds.upper.z;
  const bool enabled = m_settings.showWorldBounds && haveBounds && m_view;
  m_boundsPass->setEnabled(enabled);
  m_boundsPass->setView(m_view);
  if (enabled)
    m_boundsPass->setBox(bounds);
}

// Picking ////////////////////////////////////////////////////////////////////

void ViewportPasses::armPick(int x, int y)
{
  if (!m_scenePass)
    return;
  m_pickPixel = vsr::math::int2(x, y);
  m_pickArmed = true;
}

std::optional<vsr::rendering::PickHit> ViewportPasses::takePick()
{
  if (!m_pickArmed || !m_scenePass)
    return {};
  m_pickArmed = false;
  const auto size = m_scenePass->pickImageSize();
  if (size.x == 0 || size.y == 0)
    return {};
  const auto x = std::clamp(m_pickPixel.x, 0, int(size.x) - 1);
  const auto y = std::clamp(m_pickPixel.y, 0, int(size.y) - 1);
  vsr::rendering::PickRequest request;
  request.pixel = vsr::math::uint2(uint32_t(x), size.y - 1 - uint32_t(y));
  request.view = m_view;
  return vsr::rendering::pick(*m_scenePass, request);
}

} // namespace vsr::scivis_studio::server
