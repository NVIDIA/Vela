// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// std
#include <cstring>
#include <string>
#include <vector>

using namespace vsr::scivis_studio::protocol;

namespace {

std::vector<std::byte> makePixels(uint32_t width, uint32_t height)
{
  std::vector<std::byte> pixels(size_t(width) * height * 4);
  for (size_t i = 0; i < pixels.size(); ++i)
    pixels[i] = std::byte((i * 31 + 7) & 0xFF);
  return pixels;
}

bool sameBytes(const FrameView &view, const std::vector<std::byte> &expected)
{
  return view.size == expected.size()
      && (expected.empty()
          || std::memcmp(view.data, expected.data(), expected.size()) == 0);
}

} // namespace

SCENARIO("Frame encodings and pixel formats", "[StudioProtocol]")
{
  GIVEN("the frame enums")
  {
    THEN("names round-trip through toString()/fromString()")
    {
      REQUIRE(frameEncodingFromString(toString(FrameEncoding::Raw))
          == FrameEncoding::Raw);
      REQUIRE(frameEncodingFromString(toString(FrameEncoding::TurboJpeg))
          == FrameEncoding::TurboJpeg);
      REQUIRE(pixelFormatFromString(toString(PixelFormat::RGBA8))
          == PixelFormat::RGBA8);
      REQUIRE(pixelFormatFromString(toString(PixelFormat::RGBA8_sRGB))
          == PixelFormat::RGBA8_sRGB);
    }

    THEN("unknown names and values are rejected")
    {
      REQUIRE_FALSE(frameEncodingFromString("Nvenc"));
      REQUIRE_FALSE(frameEncodingFromString(""));
      REQUIRE_FALSE(pixelFormatFromString("RGB8"));
      REQUIRE(std::string(toString(FrameEncoding(200))) == "Unknown");
      REQUIRE(std::string(toString(PixelFormat(200))) == "Unknown");
      REQUIRE(isFrameEncoding(0));
      REQUIRE(isFrameEncoding(1));
      REQUIRE_FALSE(isFrameEncoding(2));
      REQUIRE(isPixelFormat(0));
      REQUIRE(isPixelFormat(1));
      REQUIRE_FALSE(isPixelFormat(2));
    }

    THEN("the default header matches what the demo sends today")
    {
      FrameHeader header;
      REQUIRE(header.pixelFormat == PixelFormat::RGBA8_sRGB);
      REQUIRE(header.encoding == FrameEncoding::Raw);
    }
  }
}

SCENARIO("Frame encode/decode", "[StudioProtocol]")
{
  GIVEN("a raw RGBA8 frame")
  {
    FrameHeader header;
    header.width = 7;
    header.height = 5;
    header.pixelFormat = PixelFormat::RGBA8;
    header.encoding = FrameEncoding::Raw;
    header.shotId = "shot-a";
    header.frame = -3;
    const auto pixels = makePixels(header.width, header.height);
    const auto msg = encodeFrame(header, pixels.data(), pixels.size());

    THEN("the message is tagged Frame with a consistent length")
    {
      REQUIRE(msg.header.type == uint8_t(StudioMessageType::Frame));
      REQUIRE(msg.header.payload_length == msg.payload.size());
      REQUIRE(msg.payload.size()
          == sizeof(FrameHeaderFixed) + header.shotId.size() + 1
              + pixels.size());
      REQUIRE(messageType(msg) == StudioMessageType::Frame);
    }

    THEN("it decodes to an identical header and byte range")
    {
      const auto view = decodeFrame(msg);
      REQUIRE(view);
      REQUIRE(view->header.width == 7);
      REQUIRE(view->header.height == 5);
      REQUIRE(view->header.pixelFormat == PixelFormat::RGBA8);
      REQUIRE(view->header.encoding == FrameEncoding::Raw);
      REQUIRE(view->header.shotId == "shot-a");
      REQUIRE(view->header.frame == -3);
      REQUIRE(sameBytes(*view, pixels));
      REQUIRE(view->data >= msg.payload.data());
      REQUIRE(
          view->data + view->size == msg.payload.data() + msg.payload.size());
    }

    THEN("a wrong type byte decodes to empty")
    {
      auto other = msg;
      other.header.type = uint8_t(StudioMessageType::FrameConfig);
      REQUIRE_FALSE(decodeFrame(other));
      other.header.type = vsr::network::MESSAGE_TYPE_INVALID;
      REQUIRE_FALSE(decodeFrame(other));
    }

    THEN("a truncated payload decodes to empty rather than throwing")
    {
      // Cut inside the fixed header, inside the shotId and inside the pixels.
      for (size_t keep :
          {size_t(3), sizeof(FrameHeaderFixed) + 2, msg.payload.size() - 1}) {
        auto cut = msg;
        cut.payload.resize(keep);
        cut.header.payload_length = uint32_t(keep);
        REQUIRE_NOTHROW(decodeFrame(cut));
        REQUIRE_FALSE(decodeFrame(cut));
      }
    }

    THEN("a header that overstates the buffer decodes to empty")
    {
      auto lying = msg;
      lying.header.payload_length += 16;
      REQUIRE_FALSE(decodeFrame(lying));
    }

    THEN("a raw frame with the wrong byte count decodes to empty")
    {
      auto tooFew = pixels;
      tooFew.pop_back();
      REQUIRE_FALSE(
          decodeFrame(encodeFrame(header, tooFew.data(), tooFew.size())));

      auto tooMany = pixels;
      tooMany.push_back(std::byte(1));
      REQUIRE_FALSE(
          decodeFrame(encodeFrame(header, tooMany.data(), tooMany.size())));

      REQUIRE_FALSE(decodeFrame(encodeFrame(header, nullptr, 0)));
    }

    THEN("trailing bytes beyond the declared image are rejected")
    {
      auto padded = msg;
      padded.payload.push_back(std::byte(0));
      padded.header.payload_length = uint32_t(padded.payload.size());
      REQUIRE_FALSE(decodeFrame(padded));
    }

    THEN("an unknown encoding or pixel format byte is rejected")
    {
      auto badEncoding = msg;
      badEncoding.payload[offsetof(FrameHeaderFixed, encoding)] = std::byte(7);
      REQUIRE_FALSE(decodeFrame(badEncoding));

      auto badFormat = msg;
      badFormat.payload[offsetof(FrameHeaderFixed, pixelFormat)] = std::byte(9);
      REQUIRE_FALSE(decodeFrame(badFormat));
    }
  }

  GIVEN("a turbojpeg-tagged frame with arbitrary bytes")
  {
    FrameHeader header;
    header.width = 1920;
    header.height = 1080;
    header.pixelFormat = PixelFormat::RGBA8_sRGB;
    header.encoding = FrameEncoding::TurboJpeg;
    header.shotId = "shot-b";
    header.frame = 42;
    std::vector<std::byte> jpeg;
    for (int i = 0; i < 333; ++i)
      jpeg.push_back(std::byte(0xFF ^ (i * 13)));

    THEN("it round-trips without a size check")
    {
      const auto view =
          decodeFrame(encodeFrame(header, jpeg.data(), jpeg.size()));
      REQUIRE(view);
      REQUIRE(view->header.width == 1920);
      REQUIRE(view->header.height == 1080);
      REQUIRE(view->header.pixelFormat == PixelFormat::RGBA8_sRGB);
      REQUIRE(view->header.encoding == FrameEncoding::TurboJpeg);
      REQUIRE(view->header.shotId == "shot-b");
      REQUIRE(view->header.frame == 42);
      REQUIRE(sameBytes(*view, jpeg));
    }

    THEN("an empty image is accepted")
    {
      const auto view = decodeFrame(encodeFrame(header, nullptr, 0));
      REQUIRE(view);
      REQUIRE(view->size == 0);
      REQUIRE(view->data == nullptr);
      REQUIRE(view->header.shotId == "shot-b");
    }
  }

  GIVEN("a frame with an empty shotId")
  {
    FrameHeader header;
    header.width = 2;
    header.height = 2;
    header.encoding = FrameEncoding::Raw;
    header.frame = 0;
    const auto pixels = makePixels(2, 2);
    const auto msg = encodeFrame(header, pixels.data(), pixels.size());

    THEN("it still decodes with the bytes intact")
    {
      REQUIRE(
          msg.payload.size() == sizeof(FrameHeaderFixed) + 1 + pixels.size());
      const auto view = decodeFrame(msg);
      REQUIRE(view);
      REQUIRE(view->header.shotId.empty());
      REQUIRE(view->header.pixelFormat == PixelFormat::RGBA8_sRGB);
      REQUIRE(sameBytes(*view, pixels));
    }
  }

  GIVEN("a bare Frame message with no bytes")
  {
    const auto msg =
        vsr::network::makeMessage(uint8_t(StudioMessageType::Frame));
    THEN("it decodes to empty")
    {
      REQUIRE_FALSE(decodeFrame(msg));
    }
  }
}

SCENARIO("Frame configuration payloads", "[StudioProtocol]")
{
  GIVEN("SetFrameConfig and FrameConfig")
  {
    SetFrameConfig set;
    set.width = 1280;
    set.height = 720;
    FrameConfig cfg;
    cfg.width = 640;
    cfg.height = 480;

    THEN("both round-trip through the codec")
    {
      const auto outSet = decode<SetFrameConfig>(encode(set));
      REQUIRE(outSet);
      REQUIRE(outSet->width == 1280);
      REQUIRE(outSet->height == 720);

      const auto outCfg = decode<FrameConfig>(encode(cfg));
      REQUIRE(outCfg);
      REQUIRE(outCfg->width == 640);
      REQUIRE(outCfg->height == 480);
    }

    THEN("they are distinct message types")
    {
      REQUIRE_FALSE(decode<FrameConfig>(encode(set)));
      REQUIRE_FALSE(decode<SetFrameConfig>(encode(cfg)));
    }

    THEN("a missing or mistyped dimension is rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["width"] = uint32_t(1);
      SetFrameConfig out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
      tree.root()["height"] = std::string("tall");
      REQUIRE_FALSE(fromNode(tree.root(), out));
      tree.root()["height"] = uint32_t(2);
      REQUIRE(fromNode(tree.root(), out));
      REQUIRE(out.width == 1);
      REQUIRE(out.height == 2);
    }
  }

  GIVEN("SetEncodings")
  {
    SetEncodings enc;
    enc.supported = {FrameEncoding::TurboJpeg, FrameEncoding::Raw};

    THEN("preference order is preserved")
    {
      const auto out = decode<SetEncodings>(encode(enc));
      REQUIRE(out);
      REQUIRE(out->supported
          == std::vector<FrameEncoding>{
              FrameEncoding::TurboJpeg, FrameEncoding::Raw});
    }

    THEN("an empty list round-trips as empty")
    {
      const auto out = decode<SetEncodings>(encode(SetEncodings{}));
      REQUIRE(out);
      REQUIRE(out->supported.empty());
    }

    THEN("encodings travel as readable names")
    {
      vsr::core::DataTree tree;
      toNode(enc, tree.root());
      const auto *list = tree.root().child("supported");
      REQUIRE(list);
      REQUIRE(list->child("0")->getValueAs<std::string>() == "TurboJpeg");
      REQUIRE(list->child("1")->getValueAs<std::string>() == "Raw");
    }

    THEN("an unknown encoding name is rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["supported"]["0"] = std::string("Raw");
      tree.root()["supported"]["1"] = std::string("Nvenc");
      SetEncodings out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }

  GIVEN("StartRendering and StopRendering")
  {
    THEN("both empty payloads encode and decode")
    {
      const auto start = encode(StartRendering{});
      REQUIRE(start.header.type == uint8_t(StudioMessageType::StartRendering));
      REQUIRE(decode<StartRendering>(start));
      REQUIRE_FALSE(decode<StopRendering>(start));
      REQUIRE(decode<StopRendering>(encode(StopRendering{})));
    }
  }
}
