// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/core/Token.hpp"
#include "vsr/io/images/ImageCache.hpp"
// std
#include <cstddef>
#include <string>
#include <vector>

namespace vsr::io::detail {

// What a decoder produced, before it is normalized and handed to a Scene.
// Decoders fill this in and declare the row order they wrote; nothing outside
// this file decides orientation for them.
struct DecodedImage
{
  std::vector<char> texels;
  anari::DataType elementType{ANARI_UNKNOWN};
  size_t width{0};
  size_t height{0};
  RowOrder rowOrder{RowOrder::TOP_DOWN};

  // A block-compressed payload is opaque: `texels` is the authored block
  // stream rather than a texel grid, `elementType` is ANARI_INT8, and
  // `width`/`height` describe the picture the blocks encode.
  bool blockCompressed{false};
  vsr::core::Token compressedFormat;

  explicit operator bool() const;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline DecodedImage::operator bool() const
{
  return elementType != ANARI_UNKNOWN && !texels.empty();
}

// Whether a file's own encoding overrides the color space a caller asked for.
// EXR carries linear values and DDS carries its encoding in the block format,
// so both collapse onto LINEAR -- which also keeps one file from being decoded
// once per color-space bucket.
ColorSpace colorSpaceForFile(const std::string &path, ColorSpace requested);
ColorSpace colorSpaceForFormatHint(
    const std::string &formatHint, ColorSpace requested);

// `id` names the image in diagnostics only.
DecodedImage decodeImageFile(const std::string &path, ColorSpace colorSpace);
DecodedImage decodeImageFromMemory(const void *data,
    size_t numBytes,
    ColorSpace colorSpace,
    const std::string &formatHint,
    const std::string &id);

} // namespace vsr::io::detail
