// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_model
#include "Dataset.h"
// std
#include <cstddef>
#include <string>
#include <vector>

namespace vsr::scivis_studio::test_client {

/*
 * Text the command handlers share: case folding, the spellings of the record
 * stream (quoted strings, true/false, layer:node and type:index references)
 * and the argument parsers more than one command uses. Nothing here knows
 * about the runner.
 *
 * Example:
 *   SceneObjectRef ref;
 *   size_t consumed = 0;
 *   std::string error;
 *   if (parseObjectRefArgs(args, 0, ref, consumed, error))
 *     record += " object=" + objectRefText(ref);
 */

std::string lower(std::string text);
std::string upper(std::string text);
std::string quoted(const std::string &text);
std::string join(const std::vector<std::string> &items, const char *sep);

const char *boolText(bool value);
// true/false, on/off, 1/0; case-insensitive.
bool parseBool(const std::string &text, bool &out);

// layer:node
std::string nodeText(const SceneNodeRef &node);
// type:index, the type in its short spelling (camera:0).
std::string objectRefText(const SceneObjectRef &ref);

// An object reference written as two arguments, a scene object type and an
// index. False with the reason.
bool parseObjectRef(const std::string &typeText,
    const std::string &indexText,
    SceneObjectRef &ref,
    std::string &error);

// An object reference written as `type index` (two arguments) or `type:index`
// (one, the spelling $lastObjectRef expands to), starting at args[first].
// `consumed` is how many arguments it took.
bool parseObjectRefArgs(const std::vector<std::string> &args,
    size_t first,
    SceneObjectRef &ref,
    size_t &consumed,
    std::string &error);

} // namespace vsr::scivis_studio::test_client
