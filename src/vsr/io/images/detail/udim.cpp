// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/images/detail/decoders.hpp"
// anari
#include <anari/frontend/type_utility.h>
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>

namespace vsr::io::detail {

using namespace vsr::core;

namespace {

const std::string UDIM_TOKEN = "<UDIM>";

// One decoded tile and where it sits on the UDIM grid.
struct Tile
{
  size_t u{0};
  size_t v{0};
  DecodedImage image;
};

// The element type a tile's texels are a channel count of: the 1-channel
// member of its family (ANARI_UFIXED8, ANARI_UFIXED8_R_SRGB, ...), which the
// decoders' own `base + (channels - 1)` arithmetic counts up from.
anari::DataType familyOf(anari::DataType type)
{
  return type - anari::DataType(anari::componentsOf(type) - 1);
}

// Which source channel feeds each of R, G, B and A, -1 standing for an opaque
// alpha. An odd channel count is all colour and an even one ends in alpha,
// stb's own convention for what it decodes.
std::array<int, 4> rgbaSources(size_t channels)
{
  switch (channels) {
  case 1:
    return {0, 0, 0, -1};
  case 2:
    return {0, 0, 0, 1};
  case 3:
    return {0, 1, 2, -1};
  default:
    return {0, 1, 2, 3};
  }
}

// Which of R, G, B and A each of `channels` destination channels takes.
std::array<int, 4> rgbaTargets(size_t channels)
{
  switch (channels) {
  case 1:
    return {0, 0, 0, 0};
  case 2:
    return {0, 3, 0, 0};
  case 3:
    return {0, 1, 2, 0};
  default:
    return {0, 1, 2, 3};
  }
}

// The bytes of one opaque component of a family: 1.0 for floats, all bits set
// for normalized integers.
std::vector<char> opaqueComponent(anari::DataType family, size_t bytes)
{
  if (family == ANARI_FLOAT32) {
    const float one = 1.f;
    std::vector<char> retval(sizeof(one));
    std::memcpy(retval.data(), &one, sizeof(one));
    return retval;
  }
  return std::vector<char>(bytes, char(0xff));
}

// Copy `tile` into its cell of `atlas`, nearest-sampled to the cell's size and
// widened to the atlas's channel count.
void blit(const Tile &tile,
    DecodedImage &atlas,
    size_t cellWidth,
    size_t cellHeight,
    size_t channels,
    size_t componentBytes,
    const std::vector<char> &opaque)
{
  const auto &src = tile.image;
  const size_t srcChannels = anari::componentsOf(src.elementType);
  const auto sources = rgbaSources(srcChannels);
  const auto targets = rgbaTargets(channels);
  const size_t srcTexelBytes = srcChannels * componentBytes;
  const size_t dstTexelBytes = channels * componentBytes;

  // Grid rows count up from the bottom, image rows down from the top.
  const size_t cellX = tile.u * cellWidth;
  const size_t cellY = (atlas.tileRows - 1 - tile.v) * cellHeight;

  for (size_t y = 0; y < cellHeight; ++y) {
    auto sy = y * src.height / cellHeight;
    if (src.rowOrder == RowOrder::BOTTOM_UP)
      sy = src.height - 1 - sy;
    const char *srcRow = src.texels.data() + sy * src.width * srcTexelBytes;
    char *dstRow = atlas.texels.data()
        + ((cellY + y) * atlas.width + cellX) * dstTexelBytes;
    for (size_t x = 0; x < cellWidth; ++x) {
      const char *texel = srcRow + (x * src.width / cellWidth) * srcTexelBytes;
      for (size_t c = 0; c < channels; ++c) {
        const int from = sources[targets[c]];
        std::memcpy(dstRow + x * dstTexelBytes + c * componentBytes,
            from < 0 ? opaque.data() : texel + size_t(from) * componentBytes,
            componentBytes);
      }
    }
  }
}

} // namespace

bool isUdimPattern(const std::string &path)
{
  return path.find(UDIM_TOKEN) != std::string::npos;
}

DecodedImage decodeUdimTileSet(const std::string &pattern, ColorSpace colorSpace)
{
  const auto at = pattern.find(UDIM_TOKEN);
  if (at == std::string::npos)
    return {};

  const std::filesystem::path patternPath(pattern);
  const auto directory = patternPath.parent_path();
  const auto name = patternPath.filename().string();
  const auto nameAt = name.find(UDIM_TOKEN);
  if (nameAt == std::string::npos) {
    logError("[decodeImage] UDIM token outside the file name in '%s'",
        pattern.c_str());
    return {};
  }
  const auto prefix = name.substr(0, nameAt);
  const auto suffix = name.substr(nameAt + UDIM_TOKEN.size());

  // Tile numbers in order, so the decode -- and so any failure it reports --
  // does not depend on the directory's listing order.
  std::map<int, std::filesystem::path> tileFiles;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
    const auto file = entry.path().filename().string();
    if (file.size() != prefix.size() + 4 + suffix.size()
        || file.compare(0, prefix.size(), prefix) != 0
        || file.compare(file.size() - suffix.size(), suffix.size(), suffix)
            != 0)
      continue;
    const auto digits = file.substr(prefix.size(), 4);
    if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
          return std::isdigit(c);
        }))
      continue;
    const int number = std::stoi(digits);
    if (number >= 1001)
      tileFiles[number] = entry.path();
  }

  if (tileFiles.empty()) {
    logError("[decodeImage] no tiles found for '%s'", pattern.c_str());
    return {};
  }

  std::vector<Tile> tiles;
  tiles.reserve(tileFiles.size());
  for (const auto &[number, file] : tileFiles) {
    Tile tile;
    tile.u = size_t(number - 1001) % 10;
    tile.v = size_t(number - 1001) / 10;
    tile.image = decodeImageFile(file.string(), colorSpace);
    if (!tile.image)
      return {};
    if (tile.image.blockCompressed) {
      logError("[decodeImage] block-compressed tile '%s' cannot be packed",
          file.string().c_str());
      return {};
    }
    tiles.push_back(std::move(tile));
  }

  // Every tile is brought to the largest tile's size and channel count; only
  // the component type has to agree already, because converting between those
  // would change what the texels mean rather than just how many there are.
  const auto family = familyOf(tiles.front().image.elementType);
  size_t channels = 0;
  size_t cellWidth = 0;
  size_t cellHeight = 0;
  DecodedImage atlas;
  for (const auto &tile : tiles) {
    if (familyOf(tile.image.elementType) != family) {
      logError("[decodeImage] tiles of '%s' differ in component type",
          pattern.c_str());
      return {};
    }
    channels = std::max(channels, anari::componentsOf(tile.image.elementType));
    cellWidth = std::max(cellWidth, tile.image.width);
    cellHeight = std::max(cellHeight, tile.image.height);
    atlas.tileColumns = std::max(atlas.tileColumns, tile.u + 1);
    atlas.tileRows = std::max(atlas.tileRows, tile.v + 1);
  }

  const size_t componentBytes =
      anari::sizeOf(family + anari::DataType(channels - 1)) / channels;
  atlas.elementType = family + anari::DataType(channels - 1);
  atlas.width = cellWidth * atlas.tileColumns;
  atlas.height = cellHeight * atlas.tileRows;
  atlas.rowOrder = RowOrder::TOP_DOWN;
  // A cell no tile fills stays zero: transparent black.
  atlas.texels.assign(atlas.width * atlas.height * channels * componentBytes, 0);

  const auto opaque = opaqueComponent(family, componentBytes);
  for (auto &tile : tiles) {
    blit(tile, atlas, cellWidth, cellHeight, channels, componentBytes, opaque);
    tile.image = {};
  }

  return atlas;
}

} // namespace vsr::io::detail
