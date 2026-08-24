// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace vsr_device {

Instance::Instance(DeviceGlobalState *s, vsr::core::Token /*subtype*/)
    : Object(ANARI_INSTANCE, s), m_xfmArray(this)
{
  // TODO: handle subtype
}

void Instance::commitParameters()
{
  m_xfmArray = getParamObject<helium::Array1D>("transform");
  m_xfm = getParam<anari::math::mat4>("transform", linalg::identity);
  m_group = getParamObject<Group>("group");
}

void Instance::finalize()
{
  m_invXfmData.clear();
  if (m_xfmArray) {
    m_invXfmData.resize(m_xfmArray->totalSize());
    std::transform(m_xfmArray->beginAs<anari::math::mat4>(),
        m_xfmArray->endAs<anari::math::mat4>(),
        m_invXfmData.begin(),
        [](const anari::math::mat4 &m) { return anari::math::inverse(m); });
  }
  if (!m_group)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'group' on ANARIInstance");

  auto *s = deviceState();
  s->objectUpdates.instancing++;
}

bool Instance::isValid() const
{
  return m_group;
}

uint32_t Instance::numTransforms() const
{
  return m_xfmArray ? uint32_t(m_xfmArray->totalSize()) : 1u;
}

const anari::math::mat4 &Instance::xfm(uint32_t i) const
{
  return m_xfmArray ? *m_xfmArray->valueAt<anari::math::mat4>(i) : m_xfm;
}

const Group *Instance::group() const
{
  return m_group.ptr;
}

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_DEFINITION(vsr_device::Instance *);
