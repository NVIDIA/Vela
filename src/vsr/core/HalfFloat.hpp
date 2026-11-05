// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>
#include <cstring>

namespace vsr::core {

/*
 * Conversions between float and the IEEE 754 binary16 bits ANARI_FLOAT16
 * stores, rounding to nearest even.
 *
 * Example:
 *   uint16_t h = floatToHalf(0.5f); // 0x3800
 *   float f = halfToFloat(h);       // 0.5f
 */

inline float halfToFloat(uint16_t h)
{
  const uint32_t sign = uint32_t(h >> 15) & 1u;
  const uint32_t exponent = uint32_t(h >> 10) & 0x1fu;
  const uint32_t mantissa = uint32_t(h) & 0x3ffu;

  uint32_t bits = sign << 31;
  if (exponent == 0) {
    if (mantissa != 0) {
      // Subnormal: renormalize into a float, which has the range to spare.
      uint32_t m = mantissa;
      int e = 0;
      while ((m & 0x400u) == 0) {
        m <<= 1;
        e++;
      }
      bits |= uint32_t(127 - 15 - e + 1) << 23 | ((m & 0x3ffu) << 13);
    }
  } else if (exponent == 0x1fu) {
    bits |= 0x7f800000u | (mantissa << 13);
  } else {
    bits |= (exponent + 112u) << 23 | (mantissa << 13);
  }

  float f = 0.f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

inline uint16_t floatToHalf(float f)
{
  uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(bits));

  const uint32_t sign = (bits >> 16) & 0x8000u;
  const uint32_t rawExponent = (bits >> 23) & 0xffu;
  uint32_t mantissa = bits & 0x7fffffu;

  if (rawExponent == 0xffu) // inf or nan
    return uint16_t(sign | 0x7c00u | (mantissa ? 0x200u : 0u));

  const int32_t exponent = int32_t(rawExponent) - 127 + 15;
  if (exponent >= 31)
    return uint16_t(sign | 0x7c00u);

  if (exponent <= 0) {
    if (exponent < -10)
      return uint16_t(sign);
    mantissa |= 0x800000u;
    const uint32_t shift = uint32_t(14 - exponent);
    uint32_t half = mantissa >> shift;
    const uint32_t remainder = mantissa & ((1u << shift) - 1u);
    const uint32_t halfway = 1u << (shift - 1);
    if (remainder > halfway || (remainder == halfway && (half & 1u)))
      half++;
    return uint16_t(sign | half);
  }

  uint32_t half = (uint32_t(exponent) << 10) | (mantissa >> 13);
  const uint32_t remainder = mantissa & 0x1fffu;
  // Round to nearest even; a carry out of the mantissa correctly bumps the
  // exponent, and a carry out of the largest finite value lands on infinity.
  if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u)))
    half++;
  return uint16_t(sign | half);
}

} // namespace vsr::core
