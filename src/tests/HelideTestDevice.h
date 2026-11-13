// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"
// anari
#include <anari/anari_cpp.hpp>

// A helide device the tests can drive without a display or a GPU. It is
// empty when helide does not load, and suites then skip rather than fail:
// what they test is VSR's bookkeeping, not the device.
struct HelideTestDevice
{
  HelideTestDevice();
  ~HelideTestDevice();

  VSR_NOT_COPYABLE(HelideTestDevice)
  VSR_NOT_MOVEABLE(HelideTestDevice)

  explicit operator bool() const;

  anari::Library library{nullptr};
  anari::Device device{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline HelideTestDevice::HelideTestDevice()
{
  library = anari::loadLibrary("helide",
      [](const void *,
          ANARIDevice,
          ANARIObject,
          anari::DataType,
          ANARIStatusSeverity,
          ANARIStatusCode,
          const char *) {});
  if (library)
    device = anari::newDevice(library, "default");
}

inline HelideTestDevice::~HelideTestDevice()
{
  if (device)
    anari::release(device, device);
  if (library)
    anari::unloadLibrary(library);
}

inline HelideTestDevice::operator bool() const
{
  return device != nullptr;
}
