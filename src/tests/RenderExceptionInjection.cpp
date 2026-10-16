// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// anari
#include <anari/anari.h>
// POSIX
#include <dlfcn.h>
// std
#include <cstdlib>
#include <cstring>
#include <stdexcept>

/*
 * Linux subprocess-only interposition for the real CLI application boundary.
 * Throw from the render call or withhold a requested mapped buffer, after job
 * preparation/resources are acquired, without introducing a test hook into the
 * engine or executable. The library
 * is never installed and is preloaded only by the explicit exception tests.
 */
extern "C" void anariRenderFrame(ANARIDevice device, ANARIFrame frame)
{
  const auto *kind = std::getenv("VELA_TEST_RENDER_EXCEPTION");
  if (kind && std::strcmp(kind, "nonstandard") == 0)
    throw 42;
  if (kind && std::strcmp(kind, "standard") == 0)
    throw std::runtime_error("Injected unexpected render exception");
  const auto next = reinterpret_cast<decltype(&anariRenderFrame)>(
      dlsym(RTLD_NEXT, "anariRenderFrame"));
  if (!next)
    throw std::runtime_error("Cannot resolve the real ANARI render call");
  next(device, frame);
}

extern "C" const void *anariMapFrame(ANARIDevice device,
    ANARIFrame frame,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  const auto *kind = std::getenv("VELA_TEST_RENDER_EXCEPTION");
  if (kind && std::strcmp(kind, channel) == 0) {
    *width = 0;
    *height = 0;
    *pixelType = ANARI_UNKNOWN;
    return nullptr;
  }
  const auto next = reinterpret_cast<decltype(&anariMapFrame)>(
      dlsym(RTLD_NEXT, "anariMapFrame"));
  if (!next)
    throw std::runtime_error("Cannot resolve the real ANARI map call");
  return next(device, frame, channel, width, height, pixelType);
}
