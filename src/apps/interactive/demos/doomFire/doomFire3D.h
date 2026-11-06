// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>
#include <memory>

namespace vsr::demo {

// 3D generalization of the Doom PSX fire effect
// (https://github.com/filipedeschamps/doom-fire-algorithm), run on the GPU.
//
// The grid holds one 8-bit intensity per cell, +Y is up. The bottom layer
// (y == 0) is the fire source, held at the base intensity. Each step, every
// cell above it takes the intensity of a randomly chosen cell in the layer
// below, minus a random decay; the horizontal offset of that choice is where
// the wind comes from.
struct DoomFire3D
{
  struct Params
  {
    uint8_t baseIntensity{255};
    int maxDecay{6}; // decay per layer is a random pick from [0, maxDecay]
    float windX{0.f}; // cells per layer the flames drift along +X
    float windZ{0.f}; // cells per layer the flames drift along +Z
    bool jitter{true}; // random +/-1 cell sideways spread on X and Z
  };

  DoomFire3D();
  ~DoomFire3D();

  // Reallocates the grid and clears it; only the base layer is lit.
  void resize(int nx, int ny, int nz, uint8_t baseIntensity);
  // Clears the grid; only the base layer is lit.
  void reset(uint8_t baseIntensity);
  void step(const Params &p, int steps = 1);

  // 'dst' may be host or device memory, holding nx * ny * nz bytes.
  void copyTo(void *dst) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace vsr::demo
