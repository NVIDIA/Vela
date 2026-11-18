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
  m_xfmArray = getParamObject<Array>("transform");
  m_xfm = getParam<anari::math::mat4>("transform", linalg::identity);
  m_group = getParamObject<Group>("group");
}

void Instance::finalize()
{
  m_xfms.clear();
  if (m_xfmArray) {
    const auto *a = m_xfmArray->vsrArray();
    if (m_xfmArray->type() != ANARI_ARRAY1D
        || a->elementType() != ANARI_FLOAT32_MAT4) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "ignoring 'transform' array on ANARIInstance: expected an "
          "ANARI_ARRAY1D of ANARI_FLOAT32_MAT4, got %s of %s",
          anari::toString(m_xfmArray->type()),
          anari::toString(a->elementType()));
    } else {
      const auto *begin = a->dataAs<anari::math::mat4>();
      m_xfms.assign(begin, begin + a->size());
    }
  }
  if (m_xfms.empty())
    m_xfms.push_back(m_xfm);

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
  return uint32_t(m_xfms.size());
}

const anari::math::mat4 &Instance::xfm(uint32_t i) const
{
  return m_xfms[i];
}

const Group *Instance::group() const
{
  return m_group.ptr;
}

} // namespace vsr_device

VSR_DEVICE_ANARI_TYPEFOR_DEFINITION(vsr_device::Instance *);
