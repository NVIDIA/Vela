// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/scene/Layer.hpp"
// vsr_rendering
#include "vsr/rendering/index/RenderIndexFilterFcn.hpp"
// std
#include <algorithm>
#include <anari/anari_cpp.hpp>
#include <cstdint>
#include <iterator>
#include <map>
#include <stack>
#include <tuple>

namespace vsr::rendering {

enum RenderInclusionMask
{
  SURFACES = 1 << 0,
  VOLUMES = 1 << 1,
  LIGHTS = 1 << 2,
  ALL = ((1 << 3) - 1)
};

constexpr uint8_t objectMask_none()
{
  return 0;
}

constexpr uint8_t objectMask_all()
{
  return RenderInclusionMask::ALL;
}

constexpr uint8_t objectMask_surfacesAndVolumes()
{
  return RenderInclusionMask::SURFACES | RenderInclusionMask::VOLUMES;
}

constexpr uint8_t objectMask_lights()
{
  return RenderInclusionMask::LIGHTS;
}

///////////////////////////////////////////////////////////////////////////////

/*
 * LayerVisitor that traverses a Layer forest and accumulates ANARI Surface,
 * Volume, and Light handles into ANARI Instance objects, filtered by an
 * optional inclusion mask and a per-object filter function.
 *
 * Transforms that place the very same objects share one ANARI Group, since a
 * device builds its acceleration structure per Group (ADR 0048). Reuse spans
 * one traversal: Groups are not shared between layers.
 *
 * Example:
 *   std::vector<anari::Instance> instances;
 *   RenderToAnariObjectsVisitor v(device, cache, &instances, objectMask_all());
 *   layer.traverse_const(layer.root(), v);
 */
struct RenderToAnariObjectsVisitor : public vsr::scene::LayerVisitor
{
  RenderToAnariObjectsVisitor(anari::Device d,
      vsr::scene::AnariHandleCache &oc,
      std::vector<anari::Instance> *instances,
      uint8_t inclusionMask = objectMask_all(),
      RenderIndexFilterFcn *f = nullptr);
  ~RenderToAnariObjectsVisitor();

  bool preChildren_const(const vsr::scene::LayerNode &n, int level) override;
  void postChildren_const(const vsr::scene::LayerNode &n, int level) override;

  // How many distinct Groups the traversal has made so far.
  size_t groupCount() const;

 private:
  bool isIncludedAfterFiltering(const vsr::scene::LayerNode &n) const;
  anari::Instance createInstanceFromTop();

  struct GroupedObjects
  {
    std::vector<anari::Surface> surfaces;
    std::vector<anari::Volume> volumes;
    std::vector<anari::Light> lights;

    bool operator<(const GroupedObjects &o) const
    {
      return std::tie(surfaces, volumes, lights)
          < std::tie(o.surfaces, o.volumes, o.lights);
    }
  };

  anari::Group groupFor(const GroupedObjects &objects);

  anari::Device m_device{nullptr};
  vsr::scene::AnariHandleCache *m_cache{nullptr};
  std::vector<anari::Instance> *m_instances{nullptr};
  std::stack<GroupedObjects> m_objects;
  std::map<GroupedObjects, anari::Group> m_groups;
  uint8_t m_mask{objectMask_none()};
  RenderIndexFilterFcn *m_filter{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline RenderToAnariObjectsVisitor::RenderToAnariObjectsVisitor(anari::Device d,
    vsr::scene::AnariHandleCache &oc,
    std::vector<anari::Instance> *instances,
    uint8_t mask,
    RenderIndexFilterFcn *f)
    : m_device(d),
      m_cache(&oc),
      m_instances(instances),
      m_mask(mask),
      m_filter(f)
{
  anari::retain(d, d);
  m_objects.emplace();
}

inline RenderToAnariObjectsVisitor::~RenderToAnariObjectsVisitor()
{
  // The Instances made from each Group hold it from here on.
  for (auto &entry : m_groups)
    anari::release(m_device, entry.second);
  anari::release(m_device, m_device);
}

inline size_t RenderToAnariObjectsVisitor::groupCount() const
{
  return m_groups.size();
}

inline bool RenderToAnariObjectsVisitor::preChildren_const(
    const vsr::scene::LayerNode &n, int level)
{
  if (!n->isEnabled())
    return false;

  auto &current = m_objects.top();

  const bool included = isIncludedAfterFiltering(n);

  auto type = n->type();
  switch (type) {
  case ANARI_SURFACE:
    if (m_mask & RenderInclusionMask::SURFACES) {
      size_t i = n->getObjectIndex();
      if (auto h = m_cache->getHandle(type, i, true); h != nullptr && included)
        current.surfaces.push_back((anari::Surface)h);
    }
    break;
  case ANARI_VOLUME:
    if (m_mask & RenderInclusionMask::VOLUMES) {
      size_t i = n->getObjectIndex();
      if (auto h = m_cache->getHandle(type, i, true); h != nullptr && included)
        current.volumes.push_back((anari::Volume)h);
    }
    break;
  case ANARI_LIGHT:
    if (m_mask & RenderInclusionMask::LIGHTS) {
      size_t i = n->getObjectIndex();
      if (auto h = m_cache->getHandle(type, i, true); h != nullptr)
        current.lights.push_back((anari::Light)h);
    }
    break;
  case ANARI_FLOAT32_MAT4:
    m_objects.emplace();
    break;
  case ANARI_ARRAY1D: {
    if (auto *a = n->getTransformArray(); a)
      m_objects.emplace();
  }
  default:
    break;
  }

  return true;
}

inline void RenderToAnariObjectsVisitor::postChildren_const(
    const vsr::scene::LayerNode &n, int level)
{
  if (!n->isEnabled())
    return;

  switch (n->type()) {
  case ANARI_ARRAY1D: {
    if (auto *a = n->getTransformArray(); !a)
      break;
  }
  // intentionally fallthrough...
  case ANARI_FLOAT32_MAT4: {
    if (auto inst = createInstanceFromTop()) {
      for (auto &p : n->getInstanceParameters()) {
        if (!p.second.holdsObject())
          continue;
        auto objType = p.second.type();
        auto objHandle =
            m_cache->getHandle(objType, p.second.getAsObjectIndex(), true);
        anari::setParameter(
            m_device, inst, p.first.c_str(), objType, &objHandle);
      }
      anari::commitParameters(m_device, inst);
    }
    m_objects.pop();
    break;
  }
  default:
    // no-op
    break;
  }
}

inline bool RenderToAnariObjectsVisitor::isIncludedAfterFiltering(
    const vsr::scene::LayerNode &n) const
{
  if (!m_filter)
    return true;

  auto type = n->type();
  if (!anari::isObject(type))
    return false;

  return (*m_filter)(n->getObject());
}

inline anari::Instance RenderToAnariObjectsVisitor::createInstanceFromTop()
{
  auto &current = m_objects.top();
  if (current.surfaces.empty() && current.volumes.empty()
      && current.lights.empty())
    return {};

  auto group = groupFor(current);

  current.surfaces.clear();
  current.volumes.clear();
  current.lights.clear();

  auto instance = anari::newObject<anari::Instance>(m_device, "transform");
  anari::setParameter(m_device, instance, "group", group);
  anari::commitParameters(m_device, instance);
  m_instances->push_back(instance);

  return instance;
}

inline anari::Group RenderToAnariObjectsVisitor::groupFor(
    const GroupedObjects &objects)
{
  if (auto found = m_groups.find(objects); found != m_groups.end())
    return found->second;

  auto group = anari::newObject<anari::Group>(m_device);

  if (!objects.surfaces.empty()) {
    anari::setParameterArray1D(m_device,
        group,
        "surface",
        objects.surfaces.data(),
        objects.surfaces.size());
  }

  if (!objects.volumes.empty()) {
    anari::setParameterArray1D(m_device,
        group,
        "volume",
        objects.volumes.data(),
        objects.volumes.size());
  }

  if (!objects.lights.empty()) {
    anari::setParameterArray1D(
        m_device, group, "light", objects.lights.data(), objects.lights.size());
  }

  anari::commitParameters(m_device, group);

  m_groups.emplace(objects, group);
  return group;
}

} // namespace vsr::rendering
