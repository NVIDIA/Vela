// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// helium
#include <helium/array/ObjectArray.h>

namespace vsr_device {

struct Group : public Object
{
  Group(DeviceGlobalState *s);
  virtual ~Group() = default;

  void commitParameters() override;
  void finalize() override;

  const std::vector<VSRObject *> &surfaces() const;
  const std::vector<VSRObject *> &volumes() const;
  const std::vector<VSRObject *> &lights() const;

  void addObjectsToLayer(vsr::scene::LayerNodeRef parent) const;

 private:
  helium::ChangeObserverPtr<helium::ObjectArray> m_surfaceData;
  helium::ChangeObserverPtr<helium::ObjectArray> m_volumeData;
  helium::ChangeObserverPtr<helium::ObjectArray> m_lightData;

  std::vector<VSRObject *> m_surfaces;
  std::vector<VSRObject *> m_volumes;
  std::vector<VSRObject *> m_lights;
};

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_SPECIALIZATION(vsr_device::Group *, ANARI_GROUP);
