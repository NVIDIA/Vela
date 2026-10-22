// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scene/algorithms/objectContent.hpp"
// std
#include <functional>
#include <string>
#include <string_view>

namespace vsr::scene {

namespace {

bool sameValue(const Any &a, const Any &b)
{
  // Any's own equality treats two unset values as different.
  if (!a.valid() || !b.valid())
    return a.valid() == b.valid();
  return a == b;
}

size_t valueHash(const Any &v)
{
  if (!v.valid())
    return 0;
  if (v.type() == ANARI_STRING)
    return std::hash<std::string>{}(v.getString());
  if (v.type() == ANARI_BOOL)
    return std::hash<bool>{}(v.get<bool>());
  return std::hash<std::string_view>{}(std::string_view(
      static_cast<const char *>(v.data()), anari::sizeOf(v.type())));
}

size_t combine(size_t seed, size_t h)
{
  return seed ^ (h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

} // namespace

bool sameContent(const Object &a, const Object &b)
{
  if (a.type() != b.type() || a.subtype() != b.subtype()
      || a.numParameters() != b.numParameters())
    return false;

  for (size_t i = 0; i < a.numParameters(); i++) {
    const auto &pa = a.parameterAt(i);
    const auto *pb = b.parameter(pa.name());
    if (!pb || pa.isEnabled() != pb->isEnabled() || pa.usage() != pb->usage()
        || !sameValue(pa.value(), pb->value()))
      return false;
  }

  return true;
}

size_t contentHash(const Object &o)
{
  size_t retval = combine(std::hash<int>{}(o.type()),
      std::hash<std::string_view>{}(o.subtype().c_str()));

  // Summed rather than chained, so the order parameters were set in does not
  // change the hash.
  size_t parameters = 0;
  for (size_t i = 0; i < o.numParameters(); i++) {
    const auto &p = o.parameterAt(i);
    size_t h = std::hash<std::string_view>{}(o.parameterNameAt(i));
    h = combine(h, valueHash(p.value()));
    h = combine(h, p.isEnabled());
    h = combine(h, p.usage());
    parameters += h;
  }

  return combine(retval, parameters);
}

} // namespace vsr::scene
