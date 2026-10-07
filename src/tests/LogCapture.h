// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr
#include "vsr/core/Logging.hpp"
// std
#include <string>
#include <utility>
#include <vector>

// Collects log messages for the lifetime of one scenario, for what a test can
// only observe through the log: which importer a file reached, or what an
// exporter warned it dropped.
struct LogCapture
{
  LogCapture();
  ~LogCapture();

  bool sawMessageContaining(const char *text) const;

  std::vector<std::string> messages;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline LogCapture::LogCapture()
{
  vsr::core::setLoggingCallback(
      [this](vsr::core::LogLevel, std::string message) {
        messages.push_back(std::move(message));
      });
}

inline LogCapture::~LogCapture()
{
  // No callback is the state the test binary starts in.
  vsr::core::setNoLogging();
}

inline bool LogCapture::sawMessageContaining(const char *text) const
{
  for (const auto &message : messages) {
    if (message.find(text) != std::string::npos)
      return true;
  }
  return false;
}
