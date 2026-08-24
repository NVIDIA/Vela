-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: Apache-2.0

-- VSR scripts init.lua
-- Runs automatically in every viewer Lua context (Scripts menu and Terminal).
-- Use this to augment the vsr.viewer table with shared helpers.

if not vsr.viewer then return end

