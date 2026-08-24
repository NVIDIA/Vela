// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"
#include "Array.h"
// std
#include <algorithm>

namespace vsr_device {

// Object definitions /////////////////////////////////////////////////////////

Object::Object(anari::DataType type, DeviceGlobalState *s)
    : helium::BaseObject(type, s)
{
  // no-op
}

bool Object::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  return false;
}

void Object::commitParameters()
{
  // no-op
}

void Object::finalize()
{
  // no-op
}

bool Object::isValid() const
{
  return true;
}

DeviceGlobalState *Object::deviceState() const
{
  return (DeviceGlobalState *)helium::BaseObject::m_state;
}

// VSRObject definitions //////////////////////////////////////////////////////

VSRObject::VSRObject(
    anari::DataType type, DeviceGlobalState *s, vsr::core::Token subtype)
    : Object(type, s)
{
  vsr::scene::Object *obj = nullptr;
  std::string name;
  switch (type) {
  case ANARI_CAMERA:
    obj = s->scene->createObject<vsr::scene::Camera>(subtype).data();
    name = "camera" + std::to_string(s->cameraCount++);
    break;
  case ANARI_SURFACE:
    obj = s->scene->createSurface().data();
    name = "surface" + std::to_string(s->surfaceCount++);
    break;
  case ANARI_GEOMETRY:
    obj = s->scene->createObject<vsr::scene::Geometry>(subtype).data();
    name = "geometry" + std::to_string(s->geometryCount++);
    break;
  case ANARI_MATERIAL:
    obj = s->scene->createObject<vsr::scene::Material>(subtype).data();
    name = "material" + std::to_string(s->materialCount++);
    break;
  case ANARI_SAMPLER:
    obj = s->scene->createObject<vsr::scene::Sampler>(subtype).data();
    name = "sampler" + std::to_string(s->samplerCount++);
    break;
  case ANARI_VOLUME:
    obj = s->scene->createObject<vsr::scene::Volume>(subtype).data();
    name = "volume" + std::to_string(s->volumeCount++);
    break;
  case ANARI_SPATIAL_FIELD:
    obj = s->scene->createObject<vsr::scene::SpatialField>(subtype).data();
    name = "field" + std::to_string(s->fieldCount++);
    break;
  case ANARI_LIGHT:
    obj = s->scene->createObject<vsr::scene::Light>(subtype).data();
    name = "light" + std::to_string(s->lightCount++);
    break;
  case ANARI_RENDERER:
    obj = s->scene->createRenderer(s->deviceName, subtype).get();
    name = "renderer" + std::to_string(s->rendererCount++);
    break;
  default:
    break;
  }

  if (obj)
    m_object = vsr::core::Any(obj->type(), obj->index());
  else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "failed to create equivalent VSR object for %s",
        anari::toString(type));
    return;
  }

  obj->setName(name.c_str());
}

VSRObject::~VSRObject() = default;

void VSRObject::commitParameters()
{
  auto *object = vsrObject();
  if (!object) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "no equivalent VSR object present during commit() for %s",
        anari::toString(type()));
    return;
  }
#if 0 // for now let removed parameters persist
  object->removeAllParameters();
#endif
  std::for_each(params_begin(), params_end(), [&](auto &p) {
    if (anari::isObject(p.second.type())) {
      if (anari::isArray(p.second.type())) {
        auto *arr = p.second.template getObject<Array>();
        if (arr) {
          object->setParameterObject(
              vsr::core::Token(p.first), *arr->vsrObject());
        }
      } else {
        auto *obj = p.second.template getObject<VSRObject>();
        if (obj && obj->vsrObject() != nullptr) {
          object->setParameterObject(
              vsr::core::Token(p.first), *obj->vsrObject());
        }
      }
    } else if (p.first == "name" && p.second.type() == ANARI_STRING) {
      object->setName(p.second.getString().c_str());
    } else if (p.second.type() != ANARI_UNKNOWN) {
      object->setParameter(
          vsr::core::Token(p.first), p.second.type(), p.second.data());
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "skip setting parameter '%s' of unknown type",
          p.first.c_str());
    }
  });
}

vsr::scene::Object *VSRObject::vsrObject() const
{
  return deviceState()->scene->getObject(m_object);
}

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_DEFINITION(vsr_device::VSRObject *);
