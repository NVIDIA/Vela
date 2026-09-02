// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>

namespace vsr::scivis_studio::protocol {

/*
 * Where a Studio server listens: the port both executables assume when
 * --port is not given, and the one parser both use to read it, so the client
 * and the server accept exactly the same spellings.
 *
 * Example:
 *   int port = DEFAULT_PORT;
 *   if (!parsePort(argv[i], port))
 *     return usageError("--port requires an integer in 1..65535");
 */

constexpr int DEFAULT_PORT = 12345;

// Accepts a decimal integer in 1..65535 and nothing else (no sign, no
// whitespace, no trailing characters). False leaves `port` untouched.
bool parsePort(const std::string &text, int &port);

} // namespace vsr::scivis_studio::protocol
