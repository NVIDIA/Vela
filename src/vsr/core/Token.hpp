// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/core/TypeMacros.hpp"
// std
#include <string>
#include <string_view>

namespace vsr::core {

/*
 * Interned string handle that stores a pointer into a shared string pool,
 * enabling O(1) equality comparison by pointer identity.
 *
 * Example:
 *   Token a("position");
 *   Token b("position");
 *   bool same = (a == b); // true — same pool pointer
 *   const char *s = a.c_str();
 */
struct Token
{
  Token() = default;
  Token(const char *s);
  Token(const std::string &s);

  const char *c_str() const;
  const char *value() const;
  std::string str() const;

  bool empty() const;
  operator bool() const;

  VSR_DEFAULT_MOVEABLE(Token)
  VSR_DEFAULT_COPYABLE(Token)

 private:
  const char *m_value{nullptr};
};

bool operator==(const Token &t1, const Token &t2);
bool operator!=(const Token &t1, const Token &t2);

} // namespace vsr::core
