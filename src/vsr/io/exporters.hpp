// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <limits>
#include <string_view>

namespace vsr::animation {
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::scene {
struct Scene;
struct SpatialField;
} // namespace vsr::scene

namespace vsr::io {

// NanoVDB quantization precision options
enum class VDBPrecision
{
  Float32, // No quantization (32-bit float)
  Fp4, // 4-bit fixed-point (~8:1 compression)
  Fp8, // 8-bit fixed-point (~4:1 compression)
  Fp16, // 16-bit fixed-point (~2:1 compression)
  FpN, // Variable bit fixed-point
  Half // IEEE 16-bit half float
};

void export_SceneToUSD(scene::Scene &scene,
    const char *filename,
    int framesPerSecond = 30,
    animation::AnimationManager *animMgr = nullptr);
void export_StructuredVolumeToNanoVDB(const scene::SpatialField *spatialField,
    std::string_view outputFilename,
    bool useUndefinedValue = false,
    float undefinedValue = std::numeric_limits<float>::quiet_NaN(),
    VDBPrecision precision = VDBPrecision::Fp16,
    bool enableDithering = false);

} // namespace vsr::io
