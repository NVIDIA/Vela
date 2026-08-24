// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/io/importers/detail/usd/UsdImportContext.h"
// usd
#include <pxr/imaging/hd/sceneIndex.h>
// std
#include <string>

namespace vsr::io::usd {

// Resolve the material bound at `materialPath` in the resolved scene, honouring
// the Render Context preference order with per-material fallback. Results are
// cached on the context so a shared material converts once.
ResolvedMaterial resolveMaterial(ImportContext &ctx,
    const pxr::HdSceneIndexBaseRefPtr &sceneIndex,
    const pxr::SdfPath &materialPath);

} // namespace vsr::io::usd
