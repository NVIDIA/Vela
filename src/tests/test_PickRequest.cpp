// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scene
#include "vsr/scene/ObjectId.hpp"
// vsr_rendering
#include "vsr/rendering/pick/PickRequest.h"
#include "vsr/rendering/view/CameraView.h"

// std
#include <limits>

namespace math = vsr::math;
namespace rendering = vsr::rendering;
namespace scene = vsr::scene;

namespace {

void requireNear(const math::float3 &a, const math::float3 &b)
{
  REQUIRE(math::neql(a.x, b.x, 1e-4f));
  REQUIRE(math::neql(a.y, b.y, 1e-4f));
  REQUIRE(math::neql(a.z, b.z, 1e-4f));
}

struct FakePickable : public rendering::PickableSource
{
  math::uint2 size{4, 3};
  std::optional<rendering::PickSample> sample;
  math::uint2 sampledPixel{~0u, ~0u};

  math::uint2 pickImageSize() const override
  {
    return size;
  }

  std::optional<rendering::PickSample> renderPickSample(
      math::uint2 pixel) override
  {
    sampledPixel = pixel;
    return sample;
  }
};

rendering::PickSample surfaceSample(float depth)
{
  rendering::PickSample s;
  s.depth = depth;
  s.objectId = scene::encodeObjectId(ANARI_SURFACE, 7);
  s.instanceId = 2;
  s.primitiveId = 9;
  return s;
}

} // namespace

// Object ids //////////////////////////////////////////////////////////////////

TEST_CASE("Object ids round-trip surfaces and volumes", "[PickRequest]")
{
  const uint32_t surface = scene::encodeObjectId(ANARI_SURFACE, 12);
  const uint32_t volume = scene::encodeObjectId(ANARI_VOLUME, 12);

  REQUIRE(surface != volume);
  REQUIRE(surface != scene::NO_OBJECT_ID);
  REQUIRE(volume != scene::NO_OBJECT_ID);

  const auto s = scene::decodeObjectId(surface);
  REQUIRE(s);
  REQUIRE(s->type == ANARI_SURFACE);
  REQUIRE(s->index == 12);

  const auto v = scene::decodeObjectId(volume);
  REQUIRE(v);
  REQUIRE(v->type == ANARI_VOLUME);
  REQUIRE(v->index == 12);

  REQUIRE(!scene::decodeObjectId(scene::NO_OBJECT_ID));
}

// Camera rays /////////////////////////////////////////////////////////////////

TEST_CASE("Camera view primary rays", "[PickRequest]")
{
  const math::float3 eye(0.f, 0.f, 10.f);
  const math::float3 dir(0.f, 0.f, -1.f);
  const math::float3 up(0.f, 1.f, 0.f);

  SECTION("perspective rays leave the eye through the image plane")
  {
    const auto view =
        rendering::CameraView::perspective(eye, dir, up, math::radians(90.f));

    const auto center = rendering::primaryRay(view, {0.5f, 0.5f}, 1.f);
    requireNear(center.origin, eye);
    requireNear(center.direction, dir);

    // Image y runs up: (1, 1) is the top-right corner.
    const auto corner = rendering::primaryRay(view, {1.f, 1.f}, 2.f);
    requireNear(corner.origin, eye);
    requireNear(
        corner.direction, math::normalize(math::float3(2.f, 1.f, -1.f)));
  }

  SECTION("orthographic rays are parallel and offset on the eye plane")
  {
    const auto view = rendering::CameraView::orthographic(eye, dir, up, 6.f);

    const auto left = rendering::primaryRay(view, {0.f, 0.5f}, 2.f);
    requireNear(left.origin, math::float3(-6.f, 0.f, 10.f));
    requireNear(left.direction, dir);

    const auto top = rendering::primaryRay(view, {0.5f, 1.f}, 2.f);
    requireNear(top.origin, math::float3(0.f, 3.f, 10.f));
  }
}

// Pick requests ///////////////////////////////////////////////////////////////

TEST_CASE("Pick request decodes what lies under a pixel", "[PickRequest]")
{
  FakePickable source;
  rendering::PickRequest request;
  request.pixel = {1, 2};

  SECTION("surface hit")
  {
    source.sample = surfaceSample(5.f);
    const auto hit = rendering::pick(source, request);
    REQUIRE(hit);
    REQUIRE(source.sampledPixel == math::uint2(1, 2));
    REQUIRE(hit->depth == 5.f);
    REQUIRE(hit->object);
    REQUIRE(hit->object->type == ANARI_SURFACE);
    REQUIRE(hit->object->index == 7);
    REQUIRE(hit->instanceId == 2);
    REQUIRE(hit->primitiveId == 9);
    REQUIRE(!hit->position); // no camera view given
  }

  SECTION("volume hit")
  {
    rendering::PickSample s;
    s.depth = 3.f;
    s.objectId = scene::encodeObjectId(ANARI_VOLUME, 4);
    source.sample = s;
    const auto hit = rendering::pick(source, request);
    REQUIRE(hit);
    REQUIRE(hit->object);
    REQUIRE(hit->object->type == ANARI_VOLUME);
    REQUIRE(hit->object->index == 4);
  }

  SECTION("background is a miss")
  {
    source.sample = rendering::PickSample{};
    REQUIRE(!rendering::pick(source, request));
  }

  SECTION("background reported as the largest float is a miss")
  {
    // Devices commonly clear depth to FLT_MAX rather than infinity.
    rendering::PickSample s;
    s.depth = std::numeric_limits<float>::max();
    source.sample = s;
    REQUIRE(!rendering::pick(source, request));
  }

  SECTION("an object without depth is a hit without position")
  {
    auto s = surfaceSample(math::inf);
    source.sample = s;
    request.view = rendering::CameraView::perspective(math::float3(0.f),
        math::float3(0.f, 0.f, -1.f),
        math::float3(0.f, 1.f, 0.f),
        math::radians(60.f));
    const auto hit = rendering::pick(source, request);
    REQUIRE(hit);
    REQUIRE(hit->object);
    REQUIRE(!hit->position);
  }

  SECTION("depth without an object is a hit without object")
  {
    rendering::PickSample s;
    s.depth = 2.f;
    source.sample = s;
    const auto hit = rendering::pick(source, request);
    REQUIRE(hit);
    REQUIRE(!hit->object);
  }

  SECTION("source unable to render a pick sample")
  {
    source.sample.reset();
    REQUIRE(!rendering::pick(source, request));
  }

  SECTION("out-of-range pixels are clamped to the image")
  {
    source.sample = surfaceSample(1.f);
    request.pixel = {10, 10};
    rendering::pick(source, request);
    REQUIRE(source.sampledPixel == math::uint2(3, 2));
  }

  SECTION("empty image cannot be picked")
  {
    source.size = {0, 0};
    source.sample = surfaceSample(1.f);
    REQUIRE(!rendering::pick(source, request));
  }
}

TEST_CASE("Pick request places the hit in world space", "[PickRequest]")
{
  FakePickable source;
  source.size = {3, 3};
  source.sample = surfaceSample(4.f);

  const math::float3 dir(0.f, 0.f, -1.f);
  const math::float3 up(0.f, 1.f, 0.f);

  SECTION("perspective: depth is the distance along the pixel's ray")
  {
    rendering::PickRequest request;
    request.view = rendering::CameraView::perspective(
        math::float3(0.f), dir, up, math::radians(90.f));

    request.pixel = {1, 1};
    auto hit = rendering::pick(source, request);
    REQUIRE(hit->position);
    requireNear(*hit->position, math::float3(0.f, 0.f, -4.f));

    // Pixel (2, 2) is the top-right one; its center is 1/3 of the way from
    // the image center to the edge.
    request.pixel = {2, 2};
    hit = rendering::pick(source, request);
    requireNear(*hit->position,
        4.f * math::normalize(math::float3(2.f / 3.f, 2.f / 3.f, -1.f)));
  }

  SECTION("the view's own aspect wins over the image's")
  {
    // A 3x3 image rendered by a camera with aspect 2: pixel (2, 1) sits
    // 1/3 of the way to the right edge of a plane twice as wide as tall.
    rendering::PickRequest request;
    request.view = rendering::CameraView::perspective(
        math::float3(0.f), dir, up, math::radians(90.f));
    request.view->aspect = 2.f;

    request.pixel = {2, 1};
    const auto hit = rendering::pick(source, request);
    REQUIRE(hit->position);
    requireNear(*hit->position,
        4.f * math::normalize(math::float3(2.f / 3.f * 2.f, 0.f, -1.f)));
  }

  SECTION("orthographic: depth is measured from the eye plane")
  {
    rendering::PickRequest request;
    request.view = rendering::CameraView::orthographic(
        math::float3(0.f, 0.f, 10.f), dir, up, 6.f);
    source.sample = surfaceSample(3.f);

    request.pixel = {0, 1};
    const auto hit = rendering::pick(source, request);
    REQUIRE(hit->position);
    requireNear(*hit->position, math::float3(-2.f, 0.f, 7.f));
  }
}
