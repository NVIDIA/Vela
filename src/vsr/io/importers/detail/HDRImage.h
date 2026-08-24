// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/io/images.hpp"
// std
#include <string>
#include <vector>

namespace vsr::io {

// The one decoder that does not go through ImageCache's own decode paths: it
// handles multipart EXR and forces three channels, neither of which the shared
// texture path does. It still declares the row order it produced, so callers
// that store its texels through ImageCache::acquireDecoded get the same
// normalization, keying, and lifetime as any other image.
struct HDRImage
{
  bool import(std::string fileName);

  unsigned width;
  unsigned height;
  unsigned numComponents;
  // Both branches below emit the picture's bottom row first, which is the
  // order the hdri lights this decoder feeds want. See
  // docs/adr/0014-store-images-in-anari-orientation.md.
  RowOrder rowOrder{RowOrder::BOTTOM_UP};
  std::vector<float> pixel;
};

} // namespace vsr::io
