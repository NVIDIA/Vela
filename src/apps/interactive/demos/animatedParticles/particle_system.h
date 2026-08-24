// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vsr/core/VSRMath.hpp>

namespace vsr::demo {

struct ParticleSystemParameters
{
  float gravity{1000.f};
  float particleMass{0.1f};
  float maxDistance{45.f};
  float deltaT{5e-4f};
};

// Compute new positions/velocities using existing GPU buffers
void particlesComputeTimestep(int numParticles,
    vsr::math::float3 *positions /* GPU */,
    vsr::math::float3 *velocities /* GPU */,
    float *distances /* GPU */,
    const vsr::math::float3 &bhPosition1,
    const vsr::math::float3 &bhPosition2,
    const ParticleSystemParameters &params);

} // namespace vsr::demo
