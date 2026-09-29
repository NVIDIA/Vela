// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp.hpp>
// std
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vsr::scene {

/*
 * The 32-bit value VSR assigns to an ANARI surface or volume's "id"
 * parameter, which devices report back in the frame's objectId channel.
 * Surfaces and volumes have separate index spaces, so the high bit marks a
 * volume.
 *
 * Example:
 *   anari::setParameter(d, o, "id", encodeObjectId(ANARI_VOLUME, index));
 *   if (auto obj = decodeObjectId(frameObjectId)) scene.getObject(obj->type,
 *       obj->index);
 */
struct ObjectIdentity
{
  anari::DataType type{ANARI_UNKNOWN}; // ANARI_SURFACE or ANARI_VOLUME
  size_t index{0};
};

// The objectId channel value of pixels no object covers.
constexpr uint32_t NO_OBJECT_ID = ~0u;

constexpr uint32_t encodeObjectId(anari::DataType type, size_t index);
constexpr std::optional<ObjectIdentity> decodeObjectId(uint32_t id);

// Inlined definitions ////////////////////////////////////////////////////////

namespace detail {
constexpr uint32_t VOLUME_OBJECT_ID_BIT = 0x80000000u;
} // namespace detail

constexpr uint32_t encodeObjectId(anari::DataType type, size_t index)
{
  const auto id = uint32_t(index) & ~detail::VOLUME_OBJECT_ID_BIT;
  return type == ANARI_VOLUME ? id | detail::VOLUME_OBJECT_ID_BIT : id;
}

constexpr std::optional<ObjectIdentity> decodeObjectId(uint32_t id)
{
  if (id == NO_OBJECT_ID)
    return {};
  const bool volume = (id & detail::VOLUME_OBJECT_ID_BIT) != 0;
  return ObjectIdentity{volume ? ANARI_VOLUME : ANARI_SURFACE,
      size_t(id & ~detail::VOLUME_OBJECT_ID_BIT)};
}

} // namespace vsr::scene
