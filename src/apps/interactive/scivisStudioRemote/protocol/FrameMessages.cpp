// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "FrameMessages.h"
#include "PayloadMacros.h"

namespace vsr::scivis_studio::protocol {

// Encodings and pixel formats ////////////////////////////////////////////////

const char *toString(FrameEncoding encoding)
{
  switch (encoding) {
  case FrameEncoding::Raw:
    return "Raw";
  case FrameEncoding::TurboJpeg:
    return "TurboJpeg";
  default:
    return "Unknown";
  }
}

const char *toString(PixelFormat format)
{
  switch (format) {
  case PixelFormat::RGBA8:
    return "RGBA8";
  case PixelFormat::RGBA8_sRGB:
    return "RGBA8_sRGB";
  default:
    return "Unknown";
  }
}

std::optional<FrameEncoding> frameEncodingFromString(std::string_view name)
{
  if (name == "Raw")
    return FrameEncoding::Raw;
  if (name == "TurboJpeg")
    return FrameEncoding::TurboJpeg;
  return {};
}

std::optional<PixelFormat> pixelFormatFromString(std::string_view name)
{
  if (name == "RGBA8")
    return PixelFormat::RGBA8;
  if (name == "RGBA8_sRGB")
    return PixelFormat::RGBA8_sRGB;
  return {};
}

// Frame header ///////////////////////////////////////////////////////////////

vsr::network::Message encodeFrame(
    const FrameHeader &header, const std::byte *data, size_t size)
{
  auto msg = vsr::network::makeMessage(uint8_t(StudioMessageType::Frame));
  FrameHeaderFixed fixed;
  fixed.width = header.width;
  fixed.height = header.height;
  fixed.pixelFormat = uint8_t(header.pixelFormat);
  fixed.encoding = uint8_t(header.encoding);
  fixed.frame = int32_t(header.frame);
  fixed.payloadBytes = uint32_t(size);
  vsr::network::payloadWrite(msg, &fixed);
  vsr::network::payloadWrite(msg, header.shotId);
  if (size > 0)
    vsr::network::payloadWrite(msg, data, uint32_t(size));
  return msg;
}

std::optional<FrameView> decodeFrame(const vsr::network::Message &msg)
{
  if (msg.header.type != uint8_t(StudioMessageType::Frame))
    return {};
  // payloadRead() bounds-checks against the header's length, not the buffer,
  // so a header that overstates the buffer must be refused up front.
  if (msg.payload.size() < msg.header.payload_length)
    return {};

  uint32_t offset = 0;
  FrameHeaderFixed fixed;
  if (!vsr::network::payloadRead(msg, offset, &fixed))
    return {};
  if (!isPixelFormat(fixed.pixelFormat) || !isFrameEncoding(fixed.encoding))
    return {};

  FrameView view;
  // An unterminated shotId runs strnlen to the end and pushes offset one past
  // payload_length; that shows up as a byte-count mismatch below.
  if (!vsr::network::payloadRead(msg, offset, view.header.shotId))
    return {};
  if (offset > msg.header.payload_length
      || msg.header.payload_length - offset != fixed.payloadBytes)
    return {};

  const auto encoding = FrameEncoding(fixed.encoding);
  if (encoding == FrameEncoding::Raw) {
    const uint64_t expected = uint64_t(fixed.width) * fixed.height * 4;
    if (expected != fixed.payloadBytes)
      return {};
  }

  view.header.width = fixed.width;
  view.header.height = fixed.height;
  view.header.pixelFormat = PixelFormat(fixed.pixelFormat);
  view.header.encoding = encoding;
  view.header.frame = fixed.frame;
  view.data = fixed.payloadBytes > 0 ? msg.payload.data() + offset : nullptr;
  view.size = fixed.payloadBytes;
  return view;
}

// Configuration payloads /////////////////////////////////////////////////////

void toNode(const SetFrameConfig &c, vsr::core::DataNode &n)
{
  writeChild(n, "width", c.width);
  writeChild(n, "height", c.height);
}

bool fromNode(const vsr::core::DataNode &n, SetFrameConfig &c)
{
  return readChild(n, "width", c.width) && readChild(n, "height", c.height);
}

void toNode(const FrameConfig &c, vsr::core::DataNode &n)
{
  writeChild(n, "width", c.width);
  writeChild(n, "height", c.height);
}

bool fromNode(const vsr::core::DataNode &n, FrameConfig &c)
{
  return readChild(n, "width", c.width) && readChild(n, "height", c.height);
}

void toNode(const SetEncodings &e, vsr::core::DataNode &n)
{
  std::vector<std::string> names;
  names.reserve(e.supported.size());
  for (auto encoding : e.supported)
    names.emplace_back(toString(encoding));
  writeStringList(n, "supported", names);
}

bool fromNode(const vsr::core::DataNode &n, SetEncodings &e)
{
  std::vector<std::string> names;
  if (!readStringList(n, "supported", names))
    return false;
  std::vector<FrameEncoding> supported;
  supported.reserve(names.size());
  for (const auto &name : names) {
    const auto encoding = frameEncodingFromString(name);
    if (!encoding)
      return false;
    supported.push_back(*encoding);
  }
  e.supported = std::move(supported);
  return true;
}

// Empty payloads /////////////////////////////////////////////////////////////

VSR_STUDIO_EMPTY_PAYLOAD(StartRendering)
VSR_STUDIO_EMPTY_PAYLOAD(StopRendering)

} // namespace vsr::scivis_studio::protocol
