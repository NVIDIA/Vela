// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdarg>
#include <functional>
#include <string>

namespace vsr::core {

void logStatus(const char *fmt, ...);
void logError(const char *fmt, ...);
void logWarning(const char *fmt, ...);
void logPerfWarning(const char *fmt, ...);
void logInfo(const char *fmt, ...);
void logDebug(const char *fmt, ...);

enum LogLevel
{
  STATUS,
  ERROR,
  WARNING,
  PERF_WARNING,
  INFO,
  DEBUG,
  UNKNOWN
};

using LoggingCallback = std::function<void(LogLevel, std::string)>;

void setLoggingCallback(LoggingCallback cb);
void setLogToStdout(bool verbose = false);
void setLogToStderr(bool verbose = false);
void setNoLogging();

} // namespace vsr::core
