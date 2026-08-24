// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"

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
  anari::math::mat4 m_xfm;
  std::vector<anari::math::mat4> m_invXfmData;
  helium::ChangeObserverPtr<helium::Array1D> m_xfmArray;
  helium::IntrusivePtr<Group> m_group;
};

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_SPECIALIZATION(vsr_device::Instance *, ANARI_INSTANCE);
