// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/core/Token.hpp"
// std
#include <memory>
#include <mutex>
#include <unordered_set>

namespace vsr::core {

static std::mutex g_tokenRegistryMutex;
static std::unique_ptr<std::unordered_set<std::string>> g_tokenRegistry;

Token::Token(const char *s) : Token(std::string(s)) {}

Token::Token(const std::string &s)
{
  if (s.empty())
    return;
  std::lock_guard<std::mutex> lock(g_tokenRegistryMutex);
  if (!g_tokenRegistry)
    g_tokenRegistry = std::make_unique<std::unordered_set<std::string>>();
  auto result = g_tokenRegistry->insert(s);
  m_value = result.first->c_str();
}

const char *Token::c_str() const
{
  return value();
}

const char *Token::value() const
{
  return m_value;
}

std::string Token::str() const
{
  return empty() ? std::string() : std::string(c_str());
}

bool Token::empty() const
{
  return value() == nullptr;
}

Token::operator bool() const
{
  return !empty();
}

bool operator==(const Token &t1, const Token &t2)
{
  return t1.value() == t2.value();
}

bool operator!=(const Token &t1, const Token &t2)
{
  return !(t1.value() == t2.value());
}

} // namespace vsr::core
