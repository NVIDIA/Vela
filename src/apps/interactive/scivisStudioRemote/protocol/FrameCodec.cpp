// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "FrameCodec.h"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
#ifdef VSR_USE_TURBOJPEG
// turbojpeg
#include <turbojpeg.h>
#endif
// std
#include <algorithm>
#include <cstring>

namespace vsr::scivis_studio::protocol {

namespace {

size_t rgbaByteCount(uint32_t width, uint32_t height)
{
  return size_t(width) * height * 4;
}

#ifdef VSR_USE_TURBOJPEG

// Owns a turbojpeg handle for the duration of one encode or decode call.
struct TurboJpegHandle
{
  explicit TurboJpegHandle(int initType);
  ~TurboJpegHandle();

  VSR_NOT_COPYABLE(TurboJpegHandle)

  tjhandle handle{nullptr};
};

TurboJpegHandle::TurboJpegHandle(int initType) : handle(tj3Init(initType)) {}

TurboJpegHandle::~TurboJpegHandle()
{
  if (handle)
    tj3Destroy(handle);
}

bool encodeTurboJpeg(uint32_t width,
    uint32_t height,
    const uint8_t *rgba,
    int quality,
    std::vector<std::byte> &out)
{
  TurboJpegHandle tj(TJINIT_COMPRESS);
  if (!tj.handle)
    return false;
  if (tj3Set(tj.handle, TJPARAM_QUALITY, quality) != 0
      || tj3Set(tj.handle, TJPARAM_SUBSAMP, TJSAMP_444) != 0
      || tj3Set(tj.handle, TJPARAM_NOREALLOC, 1) != 0)
    return false;

  // Compress straight into `out` (NOREALLOC keeps turbojpeg from swapping in
  // its own allocation), then trim to the bytes actually written.
  out.resize(tj3JPEGBufSize(int(width), int(height), TJSAMP_444));
  auto *jpegBuf = reinterpret_cast<unsigned char *>(out.data());
  size_t jpegSize = out.size();
  if (tj3Compress8(tj.handle,
          rgba,
          int(width),
          0,
          int(height),
          TJPF_RGBA,
          &jpegBuf,
          &jpegSize)
      != 0)
    return false;
  out.resize(jpegSize);
  return true;
}

bool decodeTurboJpeg(const FrameView &view, std::vector<uint8_t> &out)
{
  TurboJpegHandle tj(TJINIT_DECOMPRESS);
  if (!tj.handle)
    return false;
  const auto *jpegBuf = reinterpret_cast<const unsigned char *>(view.data);
  if (tj3DecompressHeader(tj.handle, jpegBuf, view.size) != 0)
    return false;
  if (tj3Get(tj.handle, TJPARAM_JPEGWIDTH) != int(view.header.width)
      || tj3Get(tj.handle, TJPARAM_JPEGHEIGHT) != int(view.header.height))
    return false;

  // TJPF_RGBA guarantees the alpha byte is 0xFF on decompression.
  out.resize(rgbaByteCount(view.header.width, view.header.height));
  return tj3Decompress8(tj.handle, jpegBuf, view.size, out.data(), 0, TJPF_RGBA)
      == 0;
}

#endif // VSR_USE_TURBOJPEG

} // namespace

// Supported encodings /////////////////////////////////////////////////////////

const std::vector<FrameEncoding> &supportedFrameEncodings()
{
  static const std::vector<FrameEncoding> encodings = {
#ifdef VSR_USE_TURBOJPEG
      FrameEncoding::TurboJpeg,
#endif
      FrameEncoding::Raw};
  return encodings;
}

bool isFrameEncodingSupported(FrameEncoding encoding)
{
  const auto &supported = supportedFrameEncodings();
  return std::find(supported.begin(), supported.end(), encoding)
      != supported.end();
}

FrameEncoding negotiateFrameEncoding(
    const std::vector<FrameEncoding> &clientPreferred)
{
  for (auto encoding : clientPreferred) {
    if (isFrameEncodingSupported(encoding))
      return encoding;
  }
  return FrameEncoding::Raw;
}

// Pixel encode/decode ////////////////////////////////////////////////////////

bool encodeFramePixels(FrameEncoding encoding,
    uint32_t width,
    uint32_t height,
    const uint8_t *rgba,
    std::vector<std::byte> &out,
    const FrameEncodeOptions &options)
{
  if (width == 0 || height == 0 || !rgba)
    return false;

  switch (encoding) {
  case FrameEncoding::Raw:
    out.resize(rgbaByteCount(width, height));
    std::memcpy(out.data(), rgba, out.size());
    return true;
  case FrameEncoding::TurboJpeg:
#ifdef VSR_USE_TURBOJPEG
    return encodeTurboJpeg(
        width, height, rgba, std::clamp(options.jpegQuality, 1, 100), out);
#else
    return false;
#endif
  default:
    return false;
  }
}

bool decodeFramePixels(const FrameView &view, std::vector<uint8_t> &out)
{
  const auto &header = view.header;
  switch (header.encoding) {
  case FrameEncoding::Raw:
    if (view.size != rgbaByteCount(header.width, header.height))
      return false;
    out.resize(view.size);
    if (view.size > 0)
      std::memcpy(out.data(), view.data, view.size);
    return true;
  case FrameEncoding::TurboJpeg:
#ifdef VSR_USE_TURBOJPEG
    if (view.size == 0 || !view.data)
      return false;
    return decodeTurboJpeg(view, out);
#else
    return false;
#endif
  default:
    return false;
  }
}

} // namespace vsr::scivis_studio::protocol
