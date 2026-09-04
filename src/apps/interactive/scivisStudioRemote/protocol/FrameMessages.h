// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/Message.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vsr::scivis_studio::protocol {

/*
 * Frame delivery: the binary Frame message (header + image bytes, never a
 * DataTree) and the small DataTree payloads that configure it. The header is
 * the sole carrier of in-motion time, so every image arrives paired with the
 * shot and integer frame it was rendered at.
 *
 * Example:
 *   FrameHeader h;
 *   h.width = 800; h.height = 600; h.shotId = "shot-1"; h.frame = 12;
 *   auto msg = encodeFrame(h, pixels.data(), pixels.size());
 *   if (auto view = decodeFrame(msg))
 *     upload(view->header, view->data, view->size);
 */

// Encodings and pixel formats ////////////////////////////////////////////////

// v1 ships raw (LAN default) and turbojpeg (remote default). An NVENC
// HEVC/H.264 value is reserved for v2 and deliberately not defined.
enum class FrameEncoding : uint8_t
{
  Raw = 0,
  TurboJpeg = 1
};

// Today's demo sends the ANARI UFIXED8_RGBA_SRGB color channel verbatim, so
// sRGB-encoded RGBA8 is the default.
enum class PixelFormat : uint8_t
{
  RGBA8 = 0,
  RGBA8_sRGB = 1
};

const char *toString(FrameEncoding encoding);
const char *toString(PixelFormat format);
std::optional<FrameEncoding> frameEncodingFromString(std::string_view name);
std::optional<PixelFormat> pixelFormatFromString(std::string_view name);

// True only for values the enums define; decodeFrame() rejects anything else.
constexpr bool isFrameEncoding(uint8_t value);
constexpr bool isPixelFormat(uint8_t value);

// Frame header ///////////////////////////////////////////////////////////////

// The fixed-size part of a frame's header, which is also its wire layout: a
// Frame message is this struct written verbatim with payloadWrite(), then the
// image byte count as a uint32_t, then shotId as a null-terminated string,
// then exactly that many bytes of image data. The enums are uint8_t-based,
// so a byte read off the wire lands in them as is and decodeFrame() checks
// it against isPixelFormat()/isFrameEncoding() before use.
struct FrameHeaderFixed
{
  uint32_t width{0};
  uint32_t height{0};
  PixelFormat pixelFormat{PixelFormat::RGBA8_sRGB};
  FrameEncoding encoding{FrameEncoding::Raw};
  uint8_t reserved[2]{0, 0};
  int32_t frame{0};
};

static_assert(sizeof(FrameHeaderFixed) == 16,
    "FrameHeaderFixed is a wire format and must stay padding-free");

// What a frame carries about itself: the fixed part plus the shot it was
// rendered for.
struct FrameHeader : FrameHeaderFixed
{
  std::string shotId;
};

// A decoded frame. `data` points into the Message it was decoded from and is
// valid only as long as that Message is.
struct FrameView
{
  FrameHeader header;
  const std::byte *data{nullptr};
  size_t size{0};
};

// Builds a Frame message: fixed header, shotId, then `size` image bytes.
// `data` may be null when `size` is zero.
vsr::network::Message encodeFrame(
    const FrameHeader &header, const std::byte *data, size_t size);

// Empty when the type byte is not Frame, the header or shotId is truncated,
// the byte count disagrees with the header, an enum value is unknown, or a
// Raw frame's size is not width*height*4. Never throws on malformed input.
std::optional<FrameView> decodeFrame(const vsr::network::Message &msg);

// Configuration payloads /////////////////////////////////////////////////////

// A frame size, under the tag that says which way it travels: SetFrameConfig
// (client -> server) is the viewport size the client wants frames rendered
// at, FrameConfig (server -> client) the effective size in force. One
// description; the tag keeps them distinct message types.
template <StudioMessageType TAG>
struct FrameSize
{
  static constexpr StudioMessageType MESSAGE_TYPE = TAG;
  uint32_t width{0};
  uint32_t height{0};
};

using SetFrameConfig = FrameSize<StudioMessageType::SetFrameConfig>;
using FrameConfig = FrameSize<StudioMessageType::FrameConfig>;

// Client -> server: decodings the client supports, most preferred first.
struct SetEncodings
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SetEncodings;
  std::vector<FrameEncoding> supported;
};

struct StartRendering
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::StartRendering;
};

struct StopRendering
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::StopRendering;
};

// FrameSize and the two empty payloads are fields() descriptions
// (PayloadCommon.h); width and height are required.

// supported travels as a string list; an absent list reads as empty and an
// unknown encoding name is rejected.
void toNode(const SetEncodings &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SetEncodings &);

// Inlined definitions ////////////////////////////////////////////////////////

constexpr bool isFrameEncoding(uint8_t value)
{
  switch (FrameEncoding(value)) {
  case FrameEncoding::Raw:
  case FrameEncoding::TurboJpeg:
    return true;
  default:
    return false;
  }
}

constexpr bool isPixelFormat(uint8_t value)
{
  switch (PixelFormat(value)) {
  case PixelFormat::RGBA8:
  case PixelFormat::RGBA8_sRGB:
    return true;
  default:
    return false;
  }
}

template <typename V, StudioMessageType TAG>
void fields(V &v, FrameSize<TAG> &c)
{
  v.required("width", c.width);
  v.required("height", c.height);
}

template <typename V>
void fields(V &, StartRendering &)
{}

template <typename V>
void fields(V &, StopRendering &)
{}

} // namespace vsr::scivis_studio::protocol
