// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Instance.h"
// std
#include <vector>
// vsr_rendering
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"

namespace vsr_device {

struct World : public Object
{
  World(DeviceGlobalState *s);
  ~World() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  void updateLayer();

  const vsr::rendering::RenderIndexAllLayers *getRenderIndex() const;
  vsr::rendering::RenderIndexAllLayers *getRenderIndex();

 private:
  vsr::scene::Layer *layer() const;

  void updateValidObjects();

  helium::ChangeObserverPtr<helium::ObjectArray> m_zeroSurfaceData;
  helium::ChangeObserverPtr<helium::ObjectArray> m_zeroVolumeData;
  helium::ChangeObserverPtr<helium::ObjectArray> m_zeroLightData;
  helium::ChangeObserverPtr<helium::ObjectArray> m_instanceData;

  std::vector<Instance *> m_instances;
  std::vector<VSRObject *> m_zeroSurfaces;
  std::vector<VSRObject *> m_zeroVolumes;
  std::vector<VSRObject *> m_zeroLights;

  vsr::core::Token m_layerName;
  vsr::rendering::RenderIndexAllLayers *m_renderIndex{nullptr};

  vsr::core::ObjectVersion m_instancingUpdated{};
};

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_SPECIALIZATION(vsr_device::World *, ANARI_WORLD);
