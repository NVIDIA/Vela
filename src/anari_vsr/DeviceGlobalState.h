// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_app
#include "vsr/app/Context.h"
// vsr_core
#include "vsr/core/ObjectVersion.hpp"
// helium
#include <helium/BaseGlobalDeviceState.h>
#include <helium/helium_math.h>

namespace vsr_device {

using VSRAny = vsr::core::Any;

struct DeviceGlobalState : public helium::BaseGlobalDeviceState
{
  DeviceGlobalState(anari::Device d);

  bool usingExternalScene() const;

  vsr::scene::Scene *scene{nullptr};
  vsr::scene::Scene localScene;
  vsr::app::ANARIDeviceManager anari;

  anari::Device device{nullptr};
  vsr::core::Token deviceName;

  struct ObjectUpdates
  {
    vsr::core::ObjectVersion instancing{};
  } objectUpdates;

  int cameraCount{0};
  int surfaceCount{0};
  int geometryCount{0};
  int materialCount{0};
  int samplerCount{0};
  int volumeCount{0};
  int fieldCount{0};
  int lightCount{0};
  int rendererCount{0};
  int worldCount{0};
};

// Helper functions/macros ////////////////////////////////////////////////////

inline DeviceGlobalState *asDeviceState(helium::BaseGlobalDeviceState *s)
{
  return (DeviceGlobalState *)s;
}

#define VSR_DEVICE_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)              \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define VSR_DEVICE_ANARI_TYPEFOR_DEFINITION(type)                              \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace vsr_device
