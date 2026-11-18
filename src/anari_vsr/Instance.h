// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array.h"
#include "Group.h"
// std
#include <vector>

namespace vsr_device {

struct Instance : public Object
{
  Instance(DeviceGlobalState *s, vsr::core::Token subtype);
  virtual ~Instance() = default;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  uint32_t numTransforms() const;

  const anari::math::mat4 &xfm(uint32_t i = 0) const;

  const Group *group() const;

 private:
  // "transform" is either a single mat4 or, with
  // KHR_INSTANCE_TRANSFORM_ARRAY, an ANARI_ARRAY1D of mat4; finalize() copies
  // whichever was given into m_xfms, which always holds at least one matrix
  anari::math::mat4 m_xfm;
  helium::ChangeObserverPtr<Array> m_xfmArray;
  std::vector<anari::math::mat4> m_xfms;
  helium::IntrusivePtr<Group> m_group;
};

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_SPECIALIZATION(vsr_device::Instance *, ANARI_INSTANCE);
