// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/core/ObjectPool.hpp"
#include "vsr/core/Token.hpp"
// anari
#include <anari/anari_cpp.hpp>

namespace vsr::scene {

struct Array;
struct Object;
struct Scene;

/*
 * Per-device cache of live ANARI object handles mirroring the Scene's object
 * pools; handles are created on demand and released when the corresponding
 * VSR object is removed.
 *
 * Example:
 *   AnariHandleCache cache(scene, deviceToken, anariDevice);
 *   auto h = cache.getHandle(ANARI_GEOMETRY, geom->index(), true);
 *   cache.releaseHandle(ANARI_GEOMETRY, geom->index());
 */
struct AnariHandleCache
{
  AnariHandleCache(Scene &scene, vsr::core::Token deviceName, anari::Device d);
  ~AnariHandleCache();
  anari::Object getHandle(
      anari::DataType type, size_t index, bool createIfNotPresent);
  anari::Object getHandle(const Object *o, bool createIfNotPresent);
  void insertEmptyHandle(anari::DataType type);
  void releaseHandle(anari::DataType type, size_t index);
  void releaseHandle(const Object *o);
  void removeHandle(anari::DataType type, size_t index);
  void removeHandle(const Object *o);
  void clear();
  bool supportsCUDA() const;
  void updateObjectArrayData(const Array *a); // for arrays-of-arrays

  vsr::core::ObjectPool<anari::Surface> surface;
  vsr::core::ObjectPool<anari::Geometry> geometry;
  vsr::core::ObjectPool<anari::Material> material;
  vsr::core::ObjectPool<anari::Sampler> sampler;
  vsr::core::ObjectPool<anari::Volume> volume;
  vsr::core::ObjectPool<anari::SpatialField> field;
  vsr::core::ObjectPool<anari::Light> light;
  vsr::core::ObjectPool<anari::Array> array;
  vsr::core::ObjectPool<anari::Renderer> renderer;
  vsr::core::ObjectPool<anari::Camera> camera;

  anari::Device device{nullptr};
  vsr::core::Token deviceName;

  VSR_NOT_COPYABLE(AnariHandleCache)
  VSR_NOT_MOVEABLE(AnariHandleCache)

 private:
  void replaceHandle(anari::Object o, anari::DataType type, size_t i);
  anari::Object readHandle(anari::DataType type, size_t i) const;

  Scene *m_scene{nullptr};
  bool m_supportsCUDA{false};
};

} // namespace vsr::scene
