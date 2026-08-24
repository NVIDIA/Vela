// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#define VSR_DEFAULT_MOVEABLE(TYPE)                                             \
  TYPE(TYPE &&) = default;                                                     \
  TYPE &operator=(TYPE &&) = default;

#define VSR_DEFAULT_COPYABLE(TYPE)                                             \
  TYPE(const TYPE &) = default;                                                \
  TYPE &operator=(const TYPE &) = default;

#define VSR_NOT_MOVEABLE(TYPE)                                                 \
  TYPE(TYPE &&) = delete;                                                      \
  TYPE &operator=(TYPE &&) = delete;

#define VSR_NOT_COPYABLE(TYPE)                                                 \
  TYPE(const TYPE &) = delete;                                                 \
  TYPE &operator=(const TYPE &) = delete;
