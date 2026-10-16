// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/rendering/pipeline/saveImage.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// stb_image
// A private writer keeps other stb callers' process-global settings out of
// this operation. Its orientation stays at the default; row order is local.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
// std
#include <fstream>
#include <limits>

namespace vsr::rendering {

bool saveImage(const ImagePipeline &pipeline, const std::string &filename)
{
  const auto size = pipeline.dimensions();
  const uint32_t *pixels = pipeline.getColorBuffer();
  if (!pixels || size.x == 0 || size.y == 0) {
    vsr::core::logError(
        "[saveImage] nothing rendered, not writing '%s'", filename.c_str());
    return false;
  }

  // stb uses signed int arithmetic for row offsets, filtered bytes and its
  // growable compression buffer. Check before narrowing or multiplying.
  constexpr uint64_t MAX_FILTERED_BYTES =
      uint64_t(std::numeric_limits<int>::max()) / 2 - 64;
  const uint64_t rowBytes = uint64_t(size.x) * 4;
  if (rowBytes > uint64_t(std::numeric_limits<int>::max())
      || uint64_t(size.y) > MAX_FILTERED_BYTES / (rowBytes + 1)) {
    vsr::core::logError(
        "[saveImage] image exceeds PNG writer limits, not writing '%s'",
        filename.c_str());
    return false;
  }

  // stb's filename writer ignores fwrite/fclose errors. Use the same encoder
  // with a checked stream so late filesystem failures are observable.
  std::ofstream file(filename, std::ios::binary | std::ios::trunc);
  if (!file) {
    vsr::core::logError(
        "[saveImage] cannot open '%s' for writing", filename.c_str());
    return false;
  }

  // Start at the top row and walk backwards through ANARI's bottom-up
  // buffer, without changing writer state or copying the image.
  const uint32_t *topRow = pixels + size_t(size.y - 1) * size.x;
  const int ok = stbi_write_png_to_func(
      [](void *context, void *data, int bytes) {
        static_cast<std::ofstream *>(context)->write(
            static_cast<const char *>(data), bytes);
      },
      &file,
      int(size.x),
      int(size.y),
      4,
      topRow,
      -int(rowBytes));
  file.close();

  if (!ok || !file) {
    vsr::core::logError("[saveImage] failed to write '%s'", filename.c_str());
    return false;
  }
  vsr::core::logStatus("[saveImage] saved '%s'", filename.c_str());
  return true;
}

} // namespace vsr::rendering
