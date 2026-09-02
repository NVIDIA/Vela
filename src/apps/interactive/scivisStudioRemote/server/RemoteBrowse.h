// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataRoots.h"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
// std
#include <filesystem>
#include <string>

namespace vsr::scivis_studio::server {

/*
 * Remote Browse: the server side of ListRoots and ListDirectory. Listings
 * stay inside the Data Roots, mark project directories (a directory holding
 * a project manifest) without hiding them, skip entries the process cannot
 * stat with a log line, and come back directories first, then
 * case-insensitively by name, so the client shows them as they arrive.
 *
 * Example:
 *   protocol::ListDirectoryResult listing;
 *   std::string error;
 *   if (listDirectory(roots, request.directory, listing, &error))
 *     setResults(reply, listing);
 */

protocol::ListRootsResult listRoots(const DataRoots &roots);

// False with `error` when `directory` is outside every root or is not a
// directory the process can read.
bool listDirectory(const DataRoots &roots,
    const std::filesystem::path &directory,
    protocol::ListDirectoryResult &out,
    std::string *error = nullptr);

} // namespace vsr::scivis_studio::server
