// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/images/ImageCache.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/images/detail/decoders.hpp"
#include "vsr/io/importers/detail/importer_common.hpp"
// std
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>
#include <utility>

namespace vsr::io {

using namespace vsr::core;
using namespace vsr::scene;

namespace {

std::string keyOf(const ImageSource &source)
{
  return source.id
      + (source.colorSpace == ColorSpace::LINEAR ? "_linear" : "_srgb")
      + (source.rowOrder == RowOrder::BOTTOM_UP ? "_up" : "_down");
}

size_t contentHashOf(const detail::DecodedImage &image)
{
  const auto bytes = std::hash<std::string_view>{}(
      std::string_view(image.texels.data(), image.texels.size()));
  return bytes ^ (size_t(image.elementType) * 0x9e3779b97f4a7c15ull)
      ^ (image.width << 32) ^ image.height;
}

// Whether `held` is the image `decoded` would become: the same texels, read
// the same way. `image` carries what store() has already derived for it.
bool sameTexels(
    const Image &held, const Image &image, const detail::DecodedImage &decoded)
{
  if (held.width != image.width || held.height != image.height
      || held.compressedFormat != image.compressedFormat
      || held.vFlipInSampler != image.vFlipInSampler)
    return false;

  const auto &texels = *held.texels;
  const auto expectedType =
      image.blockCompressed() ? ANARI_INT8 : decoded.elementType;
  if (texels.elementType() != expectedType
      || texels.size() * texels.elementSize() != decoded.texels.size())
    return false;

  return std::memcmp(
             texels.data(), decoded.texels.data(), decoded.texels.size())
      == 0;
}

// Reverse the image's rows in place so it lands in `target`, reporting whether
// it got there. This is the only place in the tree that reorders texels.
bool normalizeRowOrder(detail::DecodedImage &image, RowOrder target)
{
  if (image.rowOrder == target || image.height < 2)
    return true;

  if (image.blockCompressed) {
    // BC blocks cover 4x4 texels, so reversing rows would mean decoding and
    // re-encoding, which is the whole cost compressedImage2D exists to avoid.
    // makeImageSampler compensates in the sampler's transform instead.
    return false;
  }

  const size_t rowBytes = image.texels.size() / image.height;
  auto *first = image.texels.data();
  for (size_t r = 0; r < image.height / 2; ++r) {
    std::swap_ranges(first + r * rowBytes,
        first + (r + 1) * rowBytes,
        first + (image.height - 1 - r) * rowBytes);
  }
  image.rowOrder = target;
  return true;
}

// Compose `v -> 1 - v` onto a sampler's uv transform, applied after whatever
// the importer authored: the fetch becomes flip(T*uv + offset). Premultiplying
// by diag(1, -1, 1, 1) negates the transform's v row whatever the caller put
// there, and the +1 lands in the offset.
void composeVFlip(vsr::math::mat4 &transform, vsr::math::float4 &offset)
{
  const vsr::math::mat4 flip{vsr::math::float4(1.f, 0.f, 0.f, 0.f),
      vsr::math::float4(0.f, -1.f, 0.f, 0.f),
      vsr::math::float4(0.f, 0.f, 1.f, 0.f),
      vsr::math::float4(0.f, 0.f, 0.f, 1.f)};
  transform = vsr::math::mul(flip, transform);
  offset = vsr::math::mul(flip, offset) + vsr::math::float4(0.f, 1.f, 0.f, 0.f);
}

} // namespace

ImageCache::ImageCache(Scene *scene) : m_scene(scene) {}

Scene *ImageCache::scene() const
{
  return m_scene;
}

Image ImageCache::acquire(const ImageSource &source)
{
  auto resolved = source;
  resolved.colorSpace = detail::colorSpaceForFile(source.id, source.colorSpace);

  if (auto cached = find(resolved))
    return cached;
  if (failedBefore(resolved))
    return {};

  return store(
      resolved, detail::decodeImageFile(resolved.id, resolved.colorSpace));
}

Image ImageCache::acquire(const ImageSource &source,
    const void *data,
    size_t numBytes,
    const std::string &formatHint)
{
  auto resolved = source;
  resolved.colorSpace =
      detail::colorSpaceForFormatHint(formatHint, source.colorSpace);

  if (auto cached = find(resolved))
    return cached;
  if (failedBefore(resolved))
    return {};

  return store(resolved,
      detail::decodeImageFromMemory(
          data, numBytes, resolved.colorSpace, formatHint, resolved.id));
}

Image ImageCache::acquireDecoded(const ImageSource &source,
    anari::DataType elementType,
    size_t width,
    size_t height,
    RowOrder rowOrder,
    const void *texels)
{
  if (auto cached = find(source))
    return cached;

  detail::DecodedImage decoded;
  decoded.elementType = elementType;
  decoded.width = width;
  decoded.height = height;
  decoded.rowOrder = rowOrder;
  const auto numBytes = width * height * anari::sizeOf(elementType);
  const auto *bytes = static_cast<const char *>(texels);
  decoded.texels.assign(bytes, bytes + numBytes);

  return store(source, std::move(decoded));
}

Image ImageCache::find(const ImageSource &source) const
{
  auto found = m_images.find(keyOf(source));
  return found == m_images.end() ? Image{} : found->second;
}

void ImageCache::clear()
{
  m_images.clear();
  m_imagesByContent.clear();
  m_failures.clear();
}

size_t ImageCache::size() const
{
  return m_images.size();
}

bool ImageCache::failedBefore(const ImageSource &source) const
{
  return m_failures.count(keyOf(source)) != 0;
}

Image ImageCache::store(
    const ImageSource &source, detail::DecodedImage &&decoded)
{
  if (!decoded) {
    m_failures.insert(keyOf(source));
    return {};
  }

  if (!m_scene) {
    logError("[ImageCache] no scene to store image '%s' in", source.id.c_str());
    return {};
  }

  Image image;
  image.vFlipInSampler = !normalizeRowOrder(decoded, source.rowOrder);
  image.width = decoded.width;
  image.height = decoded.height;
  image.compressedFormat = decoded.compressedFormat;

  // Assets often ship their own copy of a texture another asset ships too, so
  // a source that is new by name can still be an image already held.
  const auto contentKey = contentHashOf(decoded);
  auto &candidates = m_imagesByContent[contentKey];
  for (const auto &candidate : candidates) {
    if (sameTexels(candidate, image, decoded)) {
      m_images[keyOf(source)] = candidate;
      return candidate;
    }
  }

  if (image.blockCompressed()) {
    image.texels = m_scene->createArray(ANARI_INT8, decoded.texels.size());
    image.texels->setData(decoded.texels.data());
  } else {
    image.texels = m_scene->createArray(
        decoded.elementType, decoded.width, decoded.height);
    image.texels->setData(decoded.texels.data());
  }

  m_images[keyOf(source)] = image;
  candidates.push_back(image);
  return image;
}

bool operator==(const OutputTransform &a, const OutputTransform &b)
{
  return a.transform == b.transform && a.offset == b.offset;
}

bool operator!=(const OutputTransform &a, const OutputTransform &b)
{
  return !(a == b);
}

OutputTransform outputTransformOf(const Sampler &sampler)
{
  OutputTransform retval;
  retval.transform = sampler.parameterValueAs<math::mat4>("outTransform")
                         .value_or(retval.transform);
  retval.offset = sampler.parameterValueAs<math::float4>("outOffset")
                      .value_or(retval.offset);
  return retval;
}

void setOutputTransform(Sampler &sampler, const OutputTransform &output)
{
  sampler.setParameter("outTransform", output.transform);
  sampler.setParameter("outOffset", output.offset);
}

OutputTransform normalMapDecode(float scale)
{
  OutputTransform retval;
  retval.transform[0][0] = 2.f * scale;
  retval.transform[1][1] = 2.f * scale;
  retval.transform[2][2] = 2.f;
  retval.offset = math::float4(-scale, -scale, -1.f, 0.f);
  return retval;
}

SamplerRef makeImageSampler(ImageCache &cache,
    const Image &image,
    const std::string &displayName,
    const SamplerSettings &settings)
{
  auto *scene = cache.scene();
  if (!image || !scene)
    return {};

  auto sampler = scene->createObject<Sampler>(image.blockCompressed()
          ? tokens::sampler::compressedImage2D
          : tokens::sampler::image2D);

  sampler->setParameterObject("image", *image.texels);
  if (image.blockCompressed()) {
    sampler->setParameter("format", image.compressedFormat.c_str());
    // Passed untyped because no vector type in the tree maps onto
    // ANARI_UINT64_VEC2, and this is the only parameter that wants one.
    const std::uint64_t size[] = {image.width, image.height};
    sampler->setParameter("size", ANARI_UINT64_VEC2, size);
  }
  sampler->setParameter("inAttribute", settings.inAttribute);
  sampler->setParameter("wrapMode1", settings.wrapMode1);
  sampler->setParameter("wrapMode2", settings.wrapMode2);
  sampler->setParameter("filter", settings.filter);

  // An image normalizeRowOrder could not reverse is still in the order the
  // file authored, against the coordinates the importer hands ANARI. Undo that
  // here, composed onto the caller's own transform rather than replacing it,
  // which is why makeImageSampler owns inTransform/inOffset outright.
  if (settings.uvTransform || image.vFlipInSampler) {
    auto uv = settings.uvTransform.value_or(UvTransform{});
    if (image.vFlipInSampler)
      composeVFlip(uv.transform, uv.offset);
    sampler->setParameter("inTransform", uv.transform);
    sampler->setParameter("inOffset", uv.offset);
  }
  sampler->setName(fileOf(displayName).c_str());

  return sampler;
}

} // namespace vsr::io
