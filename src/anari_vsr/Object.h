// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DeviceGlobalState.h"
// helium
#include <helium/BaseObject.h>
#include <helium/utility/ChangeObserverPtr.h>
// std
#include <memory>
#include <string_view>

namespace vsr_device {

struct Object : public helium::BaseObject
{
  Object(anari::DataType type, DeviceGlobalState *s);
  virtual ~Object() = default;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;
  virtual void commitParameters() override;
  virtual void finalize() override;
  virtual bool isValid() const override;

  DeviceGlobalState *deviceState() const;
};

struct VSRObject : public Object
{
  VSRObject(anari::DataType type,
      DeviceGlobalState *s,
      vsr::core::Token subtype = vsr::scene::tokens::none);
  virtual ~VSRObject();

  virtual void commitParameters() override;

  vsr::scene::Object *vsrObject() const;

 private:
  vsr::core::Any m_object; // scene ref for non-renderer objects
};

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_SPECIALIZATION(vsr_device::VSRObject *, ANARI_OBJECT);
