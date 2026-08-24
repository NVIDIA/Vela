// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/io/importers/detail/usd/UsdImportContext.h"
// usd
#include <pxr/imaging/hd/sceneIndex.h>
// std
#include <string>

namespace vsr::io::usd {

bool isLightPrimType(const pxr::TfToken &primType);

// Convert one resolved light prim. Returns a null ref for light types VSR
// cannot represent, which the caller reports rather than dropping silently;
// `skipDetail` carries back what only this function knows about the decline,
// and stays empty when the light type alone says it all.
LightRef convertLight(ImportContext &ctx,
    const pxr::SdfPath &primPath,
    const pxr::HdSceneIndexPrim &prim,
    std::string *skipDetail);

// Cameras import into the Scene's camera pool with their world transform,
// animated through bindings where a rig authors motion anywhere above them.
void convertCamera(ImportContext &ctx, const pxr::SdfPath &primPath);

} // namespace vsr::io::usd
