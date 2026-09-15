// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
#include "FrameMessages.h"
// std
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace vsr::scivis_studio::protocol;

namespace {

// A smooth RGB gradient with a varying alpha channel, so a JPEG round-trip
// stays close per channel and dropping alpha is observable.
std::vector<uint8_t> makeGradient(uint32_t width, uint32_t height)
{
  std::vector<uint8_t> rgba(size_t(width) * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      auto *p = rgba.data() + (size_t(y) * width + x) * 4;
      p[0] = uint8_t(x * 255 / std::max(1u, width - 1));
      p[1] = uint8_t(y * 255 / std::max(1u, height - 1));
      p[2] = uint8_t(128);
      p[3] = uint8_t((x + y) & 0xFF);
    }
  }
  return rgba;
}

// Encodes pixels, sends them through the Frame message, and decodes them
// back; returns the decoded view's header alongside the pixels.
bool roundTrip(FrameEncoding encoding,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t> &rgba,
    std::vector<uint8_t> &decoded,
    FrameHeader &decodedHeader)
{
  std::vector<std::byte> bytes;
  if (!encodeFramePixels(encoding, width, height, rgba.data(), bytes))
    return false;
  FrameHeader header;
  header.width = width;
  header.height = height;
  header.encoding = encoding;
  header.shotId = "shot-1";
  header.frame = 7;
  const auto msg = encodeFrame(header, bytes.data(), bytes.size());
  const auto view = decodeFrame(msg);
  if (!view)
    return false;
  decodedHeader = view->header;
  return decodeFramePixels(*view, decoded);
}

int maxChannelError(
    const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, int channel)
{
  int worst = 0;
  for (size_t i = channel; i < a.size() && i < b.size(); i += 4)
    worst = std::max(worst, std::abs(int(a[i]) - int(b[i])));
  return worst;
}

} // namespace

SCENARIO("Frame codec supported encodings", "[StudioProtocol]")
{
  GIVEN("this build's encodings")
  {
    const auto &supported = supportedFrameEncodings();

    THEN("Raw is always supported")
    {
      REQUIRE(isFrameEncodingSupported(FrameEncoding::Raw));
      REQUIRE(std::find(supported.begin(), supported.end(), FrameEncoding::Raw)
          != supported.end());
    }

    THEN("TurboJpeg follows the build flag")
    {
      const bool hasJpeg =
          std::find(
              supported.begin(), supported.end(), FrameEncoding::TurboJpeg)
          != supported.end();
#ifdef VSR_USE_TURBOJPEG
      REQUIRE(hasJpeg);
      REQUIRE(isFrameEncodingSupported(FrameEncoding::TurboJpeg));
#else
      REQUIRE_FALSE(hasJpeg);
      REQUIRE_FALSE(isFrameEncodingSupported(FrameEncoding::TurboJpeg));
#endif
    }

    THEN("values outside the enum are never supported")
    {
      REQUIRE_FALSE(isFrameEncodingSupported(FrameEncoding(200)));
    }
  }
}

SCENARIO("Frame encoding negotiation", "[StudioProtocol]")
{
  GIVEN("a client that advertises nothing")
  {
    THEN("Raw is chosen")
    {
      REQUIRE(negotiateFrameEncoding({}) == FrameEncoding::Raw);
    }
  }

  GIVEN("a client whose first choice this build does not know")
  {
    const std::vector<FrameEncoding> preferred = {
        FrameEncoding(200), FrameEncoding::Raw};

    THEN("the first supported entry wins")
    {
      REQUIRE(negotiateFrameEncoding(preferred) == FrameEncoding::Raw);
    }
  }

  GIVEN("a client that only advertises unknown encodings")
  {
    const std::vector<FrameEncoding> preferred = {
        FrameEncoding(200), FrameEncoding(201)};

    THEN("Raw is the fallback")
    {
      REQUIRE(negotiateFrameEncoding(preferred) == FrameEncoding::Raw);
    }
  }

  GIVEN("a client that prefers TurboJpeg over Raw")
  {
    const std::vector<FrameEncoding> preferred = {
        FrameEncoding::TurboJpeg, FrameEncoding::Raw};

    THEN("TurboJpeg wins only when this build can encode it")
    {
#ifdef VSR_USE_TURBOJPEG
      REQUIRE(negotiateFrameEncoding(preferred) == FrameEncoding::TurboJpeg);
#else
      REQUIRE(negotiateFrameEncoding(preferred) == FrameEncoding::Raw);
#endif
    }
  }
}

SCENARIO("Raw frame pixel round-trip", "[StudioProtocol]")
{
  GIVEN("an RGBA8 gradient")
  {
    const uint32_t width = 16, height = 9;
    const auto rgba = makeGradient(width, height);

    WHEN(
        "it goes through encodeFramePixels/encodeFrame/decodeFrame/"
        "decodeFramePixels as Raw")
    {
      std::vector<uint8_t> decoded;
      FrameHeader header;
      REQUIRE(
          roundTrip(FrameEncoding::Raw, width, height, rgba, decoded, header));

      THEN("the pixels and header come back verbatim")
      {
        REQUIRE(header.encoding == FrameEncoding::Raw);
        REQUIRE(header.width == width);
        REQUIRE(header.height == height);
        REQUIRE(header.shotId == "shot-1");
        REQUIRE(header.frame == 7);
        REQUIRE(decoded == rgba);
      }
    }

    WHEN("encoding is asked for zero dimensions or no pixels")
    {
      std::vector<std::byte> bytes;
      THEN("it is refused")
      {
        REQUIRE_FALSE(encodeFramePixels(
            FrameEncoding::Raw, 0, height, rgba.data(), bytes));
        REQUIRE_FALSE(encodeFramePixels(
            FrameEncoding::Raw, width, 0, rgba.data(), bytes));
        REQUIRE_FALSE(encodeFramePixels(
            FrameEncoding::Raw, width, height, nullptr, bytes));
      }
    }
  }

  GIVEN("a Raw view whose byte count disagrees with its dimensions")
  {
    const auto rgba = makeGradient(4, 4);
    FrameView view;
    view.header.width = 4;
    view.header.height = 4;
    view.header.encoding = FrameEncoding::Raw;
    view.data = reinterpret_cast<const std::byte *>(rgba.data());
    view.size = rgba.size() - 4;

    THEN("decodeFramePixels rejects it")
    {
      std::vector<uint8_t> decoded;
      REQUIRE_FALSE(decodeFramePixels(view, decoded));
    }
  }

  GIVEN("a view tagged with an encoding this build cannot decode")
  {
    const auto rgba = makeGradient(4, 4);
    FrameView view;
    view.header.width = 4;
    view.header.height = 4;
    view.header.encoding = FrameEncoding(200);
    view.data = reinterpret_cast<const std::byte *>(rgba.data());
    view.size = rgba.size();

    THEN("decodeFramePixels rejects it")
    {
      std::vector<uint8_t> decoded;
      REQUIRE_FALSE(decodeFramePixels(view, decoded));
    }
  }
}

#ifdef VSR_USE_TURBOJPEG

SCENARIO("TurboJpeg frame pixel round-trip", "[StudioProtocol]")
{
  GIVEN("an RGBA8 gradient")
  {
    const uint32_t width = 64, height = 48;
    const auto rgba = makeGradient(width, height);

    WHEN("it round-trips as TurboJpeg")
    {
      std::vector<uint8_t> decoded;
      FrameHeader header;
      REQUIRE(roundTrip(
          FrameEncoding::TurboJpeg, width, height, rgba, decoded, header));

      THEN("the header and size match, colors are close, alpha is 255")
      {
        REQUIRE(header.encoding == FrameEncoding::TurboJpeg);
        REQUIRE(header.width == width);
        REQUIRE(header.height == height);
        REQUIRE(decoded.size() == rgba.size());
        const int tolerance = 8;
        REQUIRE(maxChannelError(rgba, decoded, 0) <= tolerance);
        REQUIRE(maxChannelError(rgba, decoded, 1) <= tolerance);
        REQUIRE(maxChannelError(rgba, decoded, 2) <= tolerance);
        for (size_t i = 3; i < decoded.size(); i += 4)
          REQUIRE(decoded[i] == 255);
      }
    }

    WHEN("it is encoded as TurboJpeg")
    {
      std::vector<std::byte> bytes;
      REQUIRE(encodeFramePixels(
          FrameEncoding::TurboJpeg, width, height, rgba.data(), bytes));

      THEN("the JPEG is smaller than the raw pixels")
      {
        REQUIRE(bytes.size() < rgba.size());
      }

      THEN("a header claiming other dimensions is rejected on decode")
      {
        FrameView view;
        view.header.width = width + 1;
        view.header.height = height;
        view.header.encoding = FrameEncoding::TurboJpeg;
        view.data = bytes.data();
        view.size = bytes.size();
        std::vector<uint8_t> decoded;
        REQUIRE_FALSE(decodeFramePixels(view, decoded));
      }

      THEN("garbage bytes are rejected on decode")
      {
        const std::vector<std::byte> garbage(64, std::byte{0x5A});
        FrameView view;
        view.header.width = width;
        view.header.height = height;
        view.header.encoding = FrameEncoding::TurboJpeg;
        view.data = garbage.data();
        view.size = garbage.size();
        std::vector<uint8_t> decoded;
        REQUIRE_FALSE(decodeFramePixels(view, decoded));
      }
    }

    WHEN("quality is out of range")
    {
      std::vector<std::byte> bytes;
      FrameEncodeOptions options;
      options.jpegQuality = 1000;

      THEN("it is clamped rather than refused")
      {
        REQUIRE(encodeFramePixels(FrameEncoding::TurboJpeg,
            width,
            height,
            rgba.data(),
            bytes,
            options));
      }
    }
  }
}

#endif // VSR_USE_TURBOJPEG
