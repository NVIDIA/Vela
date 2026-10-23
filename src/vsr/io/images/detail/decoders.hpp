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

  // The grid of UDIM tiles packed into this image, one cell per tile: tile
  // `1001 + u + 10 * v` sits in column `u` and, counting from the bottom, row
  // `v`. A plain image is the 1x1 grid. See decodeUdimTileSet().
  size_t tileColumns{1};
  size_t tileRows{1};

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
// Whether a path names a UDIM tile set rather than a file.
bool isUdimPattern(const std::string &path);

// Decode every tile a UDIM path names -- `<UDIM>` standing for the tile number
// -- and pack them into one image on the grid DecodedImage describes, so that
// an ordinary image sampler with an affine uv transform reads the set. Tiles
// are brought to the largest tile's size and channel count; a set whose tiles
// differ in component type, or that holds a block-compressed tile, does not
// decode. See docs/adr/0050-pack-udim-tile-sets-into-one-image.md.
DecodedImage decodeUdimTileSet(const std::string &pattern, ColorSpace colorSpace);

DecodedImage decodeImageFromMemory(const void *data,
    size_t numBytes,
    ColorSpace colorSpace,
    const std::string &formatHint,
    const std::string &id);

} // namespace vsr::io::detail
