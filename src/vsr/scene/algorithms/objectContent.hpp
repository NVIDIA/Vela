// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/Object.hpp"
// std
#include <cstddef>

namespace vsr::scene {

/*
 * Whether two Objects would look the same to a device: the same type and
 * subtype, and the same parameters -- by name, in any order -- with equal
 * values, enabled states and usage hints. Names and metadata are not content.
 * Object-valued parameters compare by identity, so content is decided bottom
 * up: two Materials match only once they reference the very same Samplers.
 *
 * Example:
 *   if (contentHash(*a) == contentHash(*b) && sameContent(*a, *b))
 *     useOneOf(a, b);
 */
bool sameContent(const Object &a, const Object &b);

// A hash consistent with sameContent(): objects with the same content hash
// the same.
size_t contentHash(const Object &o);

} // namespace vsr::scene
