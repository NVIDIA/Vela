// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/images/detail/decoders.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/importers/detail/dds.h"
#include "vsr/io/importers/detail/importer_common.hpp"
// stb_image
#include "stb_image.h"
#ifndef _WIN32
#include "tinyexr.h"
#endif
#if VSR_USE_OIIO
// OpenImageIO
#include <OpenImageIO/imageio.h>
#endif
// std
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace vsr::io::detail {

using namespace vsr::core;

namespace {

std::string lowered(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return std::tolower(c);
  });
  return s;
}

// Texels keep the bit depth the file stored them at: expanding an 8-bit
// texture to float quadruples it, which on a texture-heavy USD Stage is most of
// the import's memory. Only float files decode to float.
//
// 8-bit color texels take the *_SRGB formats, so the device applies the sRGB
// EOTF on fetch -- the same contract import_GLTF has always used. Alpha stays
// linear in those formats, as it does in every texture path here.
anari::DataType float32Type(int numChannels)
{
  return ANARI_FLOAT32 + (numChannels - 1);
}

anari::DataType ufixed8Type(int numChannels, ColorSpace colorSpace)
{
  return (colorSpace == ColorSpace::SRGB ? ANARI_UFIXED8_R_SRGB : ANARI_UFIXED8)
      + (numChannels - 1);
}

// ANARI has no 16-bit sRGB format, so 16-bit color texels are taken through the
// sRGB EOTF here instead and stored linear, still at 16 bits. stb takes an odd
// channel count to be all-color and an even one to end in alpha (stb_image.h:
// `if (comp & 1) n = comp; else n = comp-1`), which is what leaves alpha
// linear.
void linearizeSrgb16InPlace(
    std::uint16_t *texels, size_t numTexels, int numChannels)
{
  std::vector<std::uint16_t> table(65536);
  for (size_t i = 0; i < table.size(); i++) {
    const float c = float(i) / 65535.f;
    const float linear =
        c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    table[i] = std::uint16_t(std::lround(linear * 65535.f));
  }

  const int numColorChannels =
      (numChannels & 1) ? numChannels : numChannels - 1;
  for (size_t t = 0; t < numTexels; t++) {
    auto *texel = texels + t * numChannels;
    for (int c = 0; c < numColorChannels; c++)
      texel[c] = table[texel[c]];
  }
}

DecodedImage decodeStb(
    const void *data, size_t numBytes, ColorSpace colorSpace, const char *id)
{
  const auto *bytes = static_cast<const stbi_uc *>(data);
  const int length = int(numBytes);

  int width = 0;
  int height = 0;
  int n = 0;
  void *decoded = nullptr;
  size_t bytesPerChannel = 0;

  // Radiance HDR is the one float format stb reads; its values are linear.
  const bool isHdr = stbi_is_hdr_from_memory(bytes, length);
  const bool is16Bit = !isHdr && stbi_is_16_bit_from_memory(bytes, length);
  if (isHdr) {
    stbi_ldr_to_hdr_scale(1.0f);
    stbi_ldr_to_hdr_gamma(1.0f);
    decoded = stbi_loadf_from_memory(bytes, length, &width, &height, &n, 0);
    bytesPerChannel = sizeof(float);
  } else if (is16Bit) {
    decoded = stbi_load_16_from_memory(bytes, length, &width, &height, &n, 0);
    bytesPerChannel = sizeof(std::uint16_t);
  } else {
    decoded = stbi_load_from_memory(bytes, length, &width, &height, &n, 0);
    bytesPerChannel = sizeof(std::uint8_t);
  }

  if (!decoded) {
    logError("[decodeImage] failed to decode image '%s'", id);
    return {};
  }
  if (n < 1 || n > 4) {
    logWarning("[decodeImage] image '%s' with %i channels not imported", id, n);
    stbi_image_free(decoded);
    return {};
  }

  const size_t numTexels = size_t(width) * size_t(height);
  if (is16Bit && colorSpace == ColorSpace::SRGB) {
    linearizeSrgb16InPlace(static_cast<std::uint16_t *>(decoded), numTexels, n);
  }

  DecodedImage image;
  if (isHdr)
    image.elementType = float32Type(n);
  else if (is16Bit)
    image.elementType = ANARI_UFIXED16 + (n - 1);
  else
    image.elementType = ufixed8Type(n, colorSpace);
  image.width = size_t(width);
  image.height = size_t(height);
  // stb hands back the picture's first row first, whatever the container's own
  // storage order was -- it undoes BMP's and TGA's bottom-up layouts itself.
  image.rowOrder = RowOrder::TOP_DOWN;
  const auto *texels = static_cast<const char *>(decoded);
  image.texels.assign(texels, texels + numTexels * n * bytesPerChannel);

  stbi_image_free(decoded);
  return image;
}

DecodedImage decodeDds(const void *data, size_t numBytes, const char *id)
{
  if (numBytes < sizeof(dds::DdsFile)) {
    logError("[decodeImage] invalid DDS buffer '%s'", id);
    return {};
  }

  auto *file = reinterpret_cast<const dds::DdsFile *>(data);
  if (file->magic != dds::DDS_MAGIC
      || file->header.size != sizeof(dds::DdsHeader)) {
    logError("[decodeImage] invalid DDS buffer '%s'", id);
    return {};
  }

  constexpr auto baseReqFlags = dds::DDSD_CAPS | dds::DDSD_HEIGHT
      | dds::DDSD_WIDTH | dds::DDSD_PIXELFORMAT;
  if ((file->header.flags & baseReqFlags) != baseReqFlags) {
    logError("[decodeImage] invalid DDS buffer '%s'", id);
    return {};
  }

  if ((file->header.caps & dds::DDSCAPS_TEXTURE) != dds::DDSCAPS_TEXTURE) {
    logError("[decodeImage] invalid DDS buffer '%s'", id);
    return {};
  }

  const bool alpha = file->header.pixelFormat.flags & dds::DDPF_ALPHAPIXELS;
  Token compressedFormat = {};
  switch (dds::getDxgiFormat(file)) {
  case dds::DXGI_FORMAT_BC1_UNORM:
    // BC1: RGB/RGBA, 1bit alpha
    compressedFormat = alpha ? "BC1_RGBA" : "BC1_RGB";
    break;
  case dds::DXGI_FORMAT_BC1_UNORM_SRGB:
    compressedFormat = alpha ? "BC1_RGBA_SRGB" : "BC1_RGB_SRGB";
    break;
  case dds::DXGI_FORMAT_BC2_UNORM:
    compressedFormat = "BC2";
    break;
  case dds::DXGI_FORMAT_BC2_UNORM_SRGB:
    compressedFormat = "BC2_SRGB";
    break;
  case dds::DXGI_FORMAT_BC3_UNORM:
    compressedFormat = "BC3";
    break;
  case dds::DXGI_FORMAT_BC3_UNORM_SRGB:
    compressedFormat = "BC3_SRGB";
    break;
  case dds::DXGI_FORMAT_BC4_UNORM:
    compressedFormat = "BC4";
    break;
  case dds::DXGI_FORMAT_BC4_SNORM:
    compressedFormat = "BC4_SNORM";
    break;
  case dds::DXGI_FORMAT_BC5_UNORM:
    compressedFormat = "BC5";
    break;
  case dds::DXGI_FORMAT_BC5_SNORM:
    compressedFormat = "BC5_SNORM";
    break;
  case dds::DXGI_FORMAT_BC6H_UF16:
    compressedFormat = "BC6H_UFLOAT";
    break;
  case dds::DXGI_FORMAT_BC6H_SF16:
    compressedFormat = "BC6H_SFLOAT";
    break;
  case dds::DXGI_FORMAT_BC7_UNORM:
    compressedFormat = "BC7";
    break;
  case dds::DXGI_FORMAT_BC7_UNORM_SRGB:
    compressedFormat = "BC7_SRGB";
    break;
  default:
    logError("[decodeImage] unsupported DDS format '%c%c%c%c' for '%s'",
        file->header.pixelFormat.fourCC & 0xff,
        (file->header.pixelFormat.fourCC >> 8) & 0xff,
        (file->header.pixelFormat.fourCC >> 16) & 0xff,
        (file->header.pixelFormat.fourCC >> 24) & 0xff,
        id);
    return {};
  }

  // Simple implementation that only handles single level mipmaps and
  // non-cubemap textures.
  const auto linearSize = dds::computeLinearSize(file);
  if ((file->header.flags & dds::DDSD_LINEARSIZE)
      && (linearSize != file->header.pitchOrLinearSize)) {
    logError(
        "[decodeImage] ignoring invalid linear size %u (should be %u) for compressed texture '%s'",
        file->header.pitchOrLinearSize,
        linearSize,
        id);
  }

  DecodedImage image;
  image.elementType = ANARI_INT8;
  image.width = file->header.width;
  image.height = file->header.height;
  image.blockCompressed = true;
  image.compressedFormat = compressedFormat;
  // Block-compressed rows come in 4x4 groups, so this is the order the file
  // authored and the only order the blocks can be handed on in.
  image.rowOrder = RowOrder::TOP_DOWN;
  const auto *blocks =
      reinterpret_cast<const char *>(dds::getDataPointer(file));
  image.texels.assign(blocks, blocks + linearSize);

  return image;
}

#ifndef _WIN32
// Follow actual HDRI importer: tinyexr is excluded on Windows; to be
// investigated.
DecodedImage decodeExr(const std::string &path)
{
  float *rgba = nullptr;
  int width = 0;
  int height = 0;
  const char *err = nullptr;

  if (LoadEXR(&rgba, &width, &height, path.c_str(), &err) != TINYEXR_SUCCESS) {
    logError("[decodeImage] failed to load EXR '%s': %s",
        path.c_str(),
        err ? err : "unknown error");
    if (err)
      FreeEXRErrorMessage(err);
    return {};
  }

  DecodedImage image;
  image.elementType = ANARI_FLOAT32_VEC4;
  image.width = size_t(width);
  image.height = size_t(height);
  image.rowOrder = RowOrder::TOP_DOWN;
  const auto *bytes = reinterpret_cast<const char *>(rgba);
  image.texels.assign(
      bytes, bytes + size_t(width) * size_t(height) * 4 * sizeof(float));

  free(rgba);
  return image;
}
#endif

#if VSR_USE_OIIO
// TIFF is the format the USD/MaterialX assets reach for that stb cannot decode.
// OpenImageIO covers it (and everything else it has a reader for) without VSR
// taking on a format-specific decoder.
DecodedImage decodeOiio(const std::string &path, ColorSpace colorSpace)
{
  // OpenImageIO premultiplies unassociated alpha into the colour channels by
  // default; stb never does. Ask for the file's own values so a TIFF with
  // alpha lands on the same contract as every other texture path.
  OIIO::ImageSpec config;
  config.attribute("oiio:UnassociatedAlpha", 1);

  // The returned unique_ptr's deleter closes the file on every exit path.
  auto input = OIIO::ImageInput::open(path, &config);
  if (!input) {
    logError("[decodeImage] failed to open image '%s': %s",
        path.c_str(),
        OIIO::geterror().c_str());
    return {};
  }

  const auto &spec = input->spec();
  const int width = spec.width;
  const int height = spec.height;
  const int numChannels = spec.nchannels;
  if (width < 1 || height < 1 || numChannels < 1 || numChannels > 4) {
    logWarning("[decodeImage] image '%s' with %i channels not imported",
        path.c_str(),
        numChannels);
    return {};
  }
  // The same bit-depth contract as decodeStb: 8-bit files stay 8-bit, wider
  // integer files become 16-bit, and only a float or half file -- which
  // already carries linear values -- decodes to float.
  const size_t numTexels = size_t(width) * size_t(height);
  OIIO::TypeDesc readAs = OIIO::TypeDesc::FLOAT;
  DecodedImage image;
  if (spec.format == OIIO::TypeDesc::UINT8) {
    readAs = OIIO::TypeDesc::UINT8;
    image.elementType = ufixed8Type(numChannels, colorSpace);
  } else if (!spec.format.is_floating_point()) {
    readAs = OIIO::TypeDesc::UINT16;
    image.elementType = ANARI_UFIXED16 + (numChannels - 1);
  } else {
    image.elementType = float32Type(numChannels);
  }
  image.width = size_t(width);
  image.height = size_t(height);
  // OpenImageIO's scanline order runs from the picture's top.
  image.rowOrder = RowOrder::TOP_DOWN;
  image.texels.resize(numTexels * size_t(numChannels) * readAs.size());

  if (!input->read_image(0, 0, 0, numChannels, readAs, image.texels.data())) {
    logError("[decodeImage] failed to decode image '%s': %s",
        path.c_str(),
        input->geterror().c_str());
    return {};
  }

  if (readAs == OIIO::TypeDesc::UINT16 && colorSpace == ColorSpace::SRGB) {
    linearizeSrgb16InPlace(
        reinterpret_cast<std::uint16_t *>(image.texels.data()),
        numTexels,
        numChannels);
  }

  return image;
}
#endif

std::vector<char> readWholeFile(const std::string &path)
{
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    logError("[decodeImage] failed to open image '%s'", path.c_str());
    return {};
  }
  return std::vector<char>(
      (std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

} // namespace

ColorSpace colorSpaceForFile(const std::string &path, ColorSpace requested)
{
  const auto ext = lowered(extensionOf(path));
  if (ext == ".exr" || ext == ".dds")
    return ColorSpace::LINEAR;
  return requested;
}

ColorSpace colorSpaceForFormatHint(
    const std::string &formatHint, ColorSpace requested)
{
  return lowered(formatHint) == "dds" ? ColorSpace::LINEAR : requested;
}

DecodedImage decodeImageFile(const std::string &path, ColorSpace colorSpace)
{
  const auto ext = lowered(extensionOf(path));

  if (ext == ".dds") {
    const auto bytes = readWholeFile(path);
    return bytes.empty() ? DecodedImage{}
                         : decodeDds(bytes.data(), bytes.size(), path.c_str());
  }

#ifndef _WIN32
  if (ext == ".exr")
    return decodeExr(path);
#endif

  if (ext == ".tif" || ext == ".tiff") {
#if VSR_USE_OIIO
    return decodeOiio(path, colorSpace);
#else
    // Falling through to stb would fail with a decode error that says nothing
    // about the actual cause, which is that VSR was built without OpenImageIO.
    logError(
        "[decodeImage] cannot decode TIFF image '%s': VSR was built without"
        " OpenImageIO (set VSR_USE_OIIO=ON)",
        path.c_str());
    return {};
#endif
  }

  const auto bytes = readWholeFile(path);
  return bytes.empty()
      ? DecodedImage{}
      : decodeStb(bytes.data(), bytes.size(), colorSpace, path.c_str());
}

DecodedImage decodeImageFromMemory(const void *data,
    size_t numBytes,
    ColorSpace colorSpace,
    const std::string &formatHint,
    const std::string &id)
{
  if (lowered(formatHint) == "dds")
    return decodeDds(data, numBytes, id.c_str());
  return decodeStb(data, numBytes, colorSpace, id.c_str());
}

} // namespace vsr::io::detail
