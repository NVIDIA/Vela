// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/io/importers/detail/usd/UsdImportContext.h"
// usd
#include <pxr/imaging/hd/sceneIndex.h>
// std
#include <string>

namespace vsr::io::usd {

// The Material this Import already built with `material`'s content, which
// then replaces it, or `material` itself; its Samplers are shared the same way
// first. A no-op unless the import options share repeated objects (ADR 0047).
// `material` must not be referenced by anything yet.
MaterialRef shareMaterial(ImportContext &ctx, MaterialRef material);

// Resolve the material bound at `materialPath` in the resolved scene, honouring
// the Render Context preference order with per-material fallback. Results are
// cached on the context so a shared material converts once.
ResolvedMaterial resolveMaterial(ImportContext &ctx,
    const pxr::HdSceneIndexBaseRefPtr &sceneIndex,
    const pxr::SdfPath &materialPath);

} // namespace vsr::io::usd
