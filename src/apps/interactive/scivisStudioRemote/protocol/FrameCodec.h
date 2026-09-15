// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FrameMessages.h"
// std
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vsr::scivis_studio::protocol {

/*
 * Frame codec: the pixel encoder/decoder both ends of the Frame message
 * share. It sits between the renderer's RGBA8 color channel and the image
 * bytes carried by encodeFrame()/decodeFrame(); the wire format itself is
 * untouched. Raw is always available so a session can never fail to
 * negotiate; TurboJpeg (4:4:4, alpha dropped) is compiled in only with
 * VSR_USE_TURBOJPEG.
 *
 * Example:
 *   // server, once per session
 *   const auto encoding = negotiateFrameEncoding(setEncodings.supported);
 *   // server, per frame
 *   std::vector<std::byte> bytes;
 *   if (encodeFramePixels(encoding, w, h, rgba, bytes)) {
 *     header.encoding = encoding;
 *     send(encodeFrame(header, bytes.data(), bytes.size()));
 *   }
 *   // client, per frame
 *   std::vector<uint8_t> pixels;
 *   if (auto view = decodeFrame(msg); view && decodeFramePixels(*view, pixels))
 *     upload(view->header.width, view->header.height, pixels.data());
 */

// Supported encodings /////////////////////////////////////////////////////////

// Encodings this build can both encode and decode, in the order the server
// prefers them. Raw is always present; TurboJpeg only with VSR_USE_TURBOJPEG.
const std::vector<FrameEncoding> &supportedFrameEncodings();
bool isFrameEncodingSupported(FrameEncoding encoding);

// Server-side negotiation (RFB SetEncodings model): the first encoding in
// `clientPreferred` this build supports. Raw when the list is empty or nothing
// matches, since Raw is mandatory on both ends.
FrameEncoding negotiateFrameEncoding(
    const std::vector<FrameEncoding> &clientPreferred);

// Pixel encode/decode ////////////////////////////////////////////////////////

struct FrameEncodeOptions
{
  // Only consulted for TurboJpeg; clamped to [1, 100].
  int jpegQuality{90};
};

// Encodes width*height tightly packed RGBA8 pixels into `out` for `encoding`.
// Raw copies the bytes verbatim; TurboJpeg compresses with 4:4:4 chroma (to
// keep text and isolines crisp) and drops the alpha channel. False when the
// encoding is unsupported by this build, the dimensions are zero, or the
// encoder fails; `out` is then unspecified.
bool encodeFramePixels(FrameEncoding encoding,
    uint32_t width,
    uint32_t height,
    const uint8_t *rgba,
    std::vector<std::byte> &out,
    const FrameEncodeOptions &options = {});

// Decodes a received frame into width*height RGBA8 pixels, resizing `out`.
// JPEG frames decode with alpha forced to 255. False when the encoding is
// unsupported by this build, the image dimensions disagree with the header,
// or the decoder fails; `out` is then unspecified.
bool decodeFramePixels(const FrameView &view, std::vector<uint8_t> &out);

} // namespace vsr::scivis_studio::protocol
