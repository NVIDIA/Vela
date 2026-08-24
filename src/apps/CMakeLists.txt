## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

if (VSR_BUILD_INTERACTIVE_APPS)
  add_subdirectory(interactive)
endif()
add_subdirectory(tutorial)
add_subdirectory(tools)
