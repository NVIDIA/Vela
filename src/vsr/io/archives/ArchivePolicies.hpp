// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::io {

enum class ArchiveObjectPolicy
{
  All,
  LightsOnly
};

enum class FileBindingArchivePolicy
{
  Include,
  Omit
};

} // namespace vsr::io
