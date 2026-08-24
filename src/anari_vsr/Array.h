// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DeviceGlobalState.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/array/Array3D.h"
#include "helium/array/ObjectArray.h"
// vsr_core
#include "vsr/scene/objects/Array.hpp"

namespace vsr_device {

using Array1DMemoryDescriptor = helium::Array1DMemoryDescriptor;
using Array2DMemoryDescriptor = helium::Array2DMemoryDescriptor;
using Array3DMemoryDescriptor = helium::Array3DMemoryDescriptor;
using ObjectArray = helium::ObjectArray;

struct Array : public helium::BaseArray
{
  Array(DeviceGlobalState *state, Array1DMemoryDescriptor desc);
  Array(DeviceGlobalState *state, Array2DMemoryDescriptor desc);
  Array(DeviceGlobalState *state, Array3DMemoryDescriptor desc);
  ~Array() override;

  bool isShared() const;

  void *map() override;
  void unmap() override;
  void privatize() override;

  void commitParameters() override;
  void finalize() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  vsr::scene::Object *vsrObject() const;
  DeviceGlobalState *deviceState() const;

 private:
  void syncSharedData();
  void freeAppMemory();

  mutable vsr::scene::ArrayRef m_array;
  const void *m_appMemory{nullptr};
  ANARIMemoryDeleter m_deleter{};
  const void *m_deleterPtr{nullptr};
};

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_SPECIALIZATION(vsr_device::Array *, ANARI_ARRAY);
