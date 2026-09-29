// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_rendering
#include "vsr/rendering/overlay/WorldBoundsOverlay.h"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
// std
#include <algorithm>
#include <vector>

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

// Solid-color source with an optional depth channel.
struct FlatSource : public rendering::ImageSource
{
  float depth{math::inf};

  rendering::ImageChannels supportedChannels() const override
  {
    return rendering::ImageChannels::DEPTH;
  }

 private:
  void render(rendering::ImageBuffers &b) override
  {
    const auto size = dimensions();
    const size_t n = size_t(size.x) * size_t(size.y);
    std::fill(b.color, b.color + n, 0u);
    if (b.depth)
      std::fill(b.depth, b.depth + n, depth);
  }
};

size_t countNonZero(const rendering::ImagePipeline &p)
{
  const auto size = p.dimensions();
  const auto *c = p.getColorBuffer();
  return size_t(std::count_if(
      c, c + size.x * size.y, [](uint32_t v) { return v != 0u; }));
}

const math::box3 UNIT_BOX{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};

rendering::CameraView frontView()
{
  return rendering::CameraView::perspective(
      {0.f, 0.f, 5.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}, math::radians(60.f));
}

} // namespace

// Camera views ///////////////////////////////////////////////////////////////

TEST_CASE("makeCameraView matches what the ANARI camera is given",
    "[WorldBoundsOverlay]")
{
  scene::Scene s;
  rendering::Manipulator m;
  m.setConfig(math::float3(0.f), 4.f, math::float2(0.f, 0.f));

  SECTION("perspective uses the eye and the camera's fovy")
  {
    auto cam =
        s.createObject<scene::Camera>(scene::tokens::camera::perspective);
    cam->setParameter("fovy", math::radians(30.f));
    const auto view = rendering::makeCameraView(*cam, m);
    REQUIRE(view);
    REQUIRE(view->kind == rendering::CameraView::Kind::PERSPECTIVE);
    requireNear(view->eye, m.eye());
    requireNear(view->dir, m.dir());
    REQUIRE(math::neql(view->fovy, math::radians(30.f), 1e-6f));
  }

  SECTION("orthographic uses the fixed-distance eye and 0.75 * distance")
  {
    auto cam =
        s.createObject<scene::Camera>(scene::tokens::camera::orthographic);
    const auto view = rendering::makeCameraView(*cam, m);
    REQUIRE(view);
    REQUIRE(view->kind == rendering::CameraView::Kind::ORTHOGRAPHIC);
    requireNear(view->eye, m.eye_FixedDistance());
    REQUIRE(math::neql(view->height, m.distance() * 0.75f, 1e-6f));
  }

  SECTION("other camera subtypes have no view")
  {
    auto cam =
        s.createObject<scene::Camera>(scene::tokens::camera::omnidirectional);
    REQUIRE(!rendering::makeCameraView(*cam, m));
  }
}

// World Bounds Overlay ////////////////////////////////////////////////////////

TEST_CASE("World Bounds Overlay draws only when shown with bounds and a view",
    "[WorldBoundsOverlay]")
{
  rendering::ImagePipeline pipeline(64, 64);
  pipeline.setSource<FlatSource>();
  rendering::WorldBoundsOverlay overlay(pipeline);

  SECTION("hidden by default")
  {
    overlay.update(UNIT_BOX, frontView());
    pipeline.render();
    REQUIRE(countNonZero(pipeline) == 0);
  }

  SECTION("shown")
  {
    overlay.setShown(true);
    overlay.update(UNIT_BOX, frontView());
    pipeline.render();
    REQUIRE(countNonZero(pipeline) > 0);
  }

  SECTION("shown but without a view (unsupported camera)")
  {
    overlay.setShown(true);
    overlay.update(UNIT_BOX, std::nullopt);
    pipeline.render();
    REQUIRE(countNonZero(pipeline) == 0);
  }

  SECTION("shown but the world is empty")
  {
    overlay.setShown(true);
    overlay.update(std::nullopt, frontView());
    pipeline.render();
    REQUIRE(countNonZero(pipeline) == 0);
  }
}

TEST_CASE("World Bounds Overlay requests depth only while drawn",
    "[WorldBoundsOverlay]")
{
  rendering::ImagePipeline pipeline(64, 64);
  auto *source = pipeline.setSource<FlatSource>();
  rendering::WorldBoundsOverlay overlay(pipeline);

  overlay.update(UNIT_BOX, frontView());
  pipeline.render();
  REQUIRE(source->channels() == rendering::ImageChannels::NONE);

  overlay.setShown(true);
  overlay.update(UNIT_BOX, frontView());
  pipeline.render();
  REQUIRE(source->channels() == rendering::ImageChannels::DEPTH);
}

TEST_CASE("World Bounds Overlay is depth-tested against the scene",
    "[WorldBoundsOverlay]")
{
  rendering::ImagePipeline pipeline(64, 64);
  auto *source = pipeline.setSource<FlatSource>();
  rendering::WorldBoundsOverlay overlay(pipeline);
  overlay.setShown(true);

  // Box is 4..6 units from the eye; a wall at depth 1 hides it entirely.
  source->depth = 1.f;
  overlay.update(UNIT_BOX, frontView());
  pipeline.render();
  REQUIRE(countNonZero(pipeline) == 0);
}

TEST_CASE(
    "World Bounds Overlay follows the view's aspect", "[WorldBoundsOverlay]")
{
  rendering::ImagePipeline pipeline(64, 64);
  pipeline.setSource<FlatSource>();
  rendering::WorldBoundsOverlay overlay(pipeline);
  overlay.setShown(true);

  // A camera with a much wider aspect than the square image squeezes the box
  // horizontally, so fewer columns are covered.
  auto columnsCovered = [&]() {
    const auto *c = pipeline.getColorBuffer();
    int lo = 64, hi = -1;
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
        if (c[y * 64 + x]) {
          lo = std::min(lo, x);
          hi = std::max(hi, x);
        }
    return hi - lo;
  };

  overlay.update(UNIT_BOX, frontView());
  pipeline.render();
  const int square = columnsCovered();

  auto wide = frontView();
  wide.aspect = 4.f;
  overlay.update(UNIT_BOX, wide);
  pipeline.render();
  REQUIRE(columnsCovered() < square);
}

TEST_CASE("World Bounds Overlay applies its style", "[WorldBoundsOverlay]")
{
  rendering::ImagePipeline pipeline(64, 64);
  pipeline.setSource<FlatSource>();
  rendering::WorldBoundsOverlay overlay(pipeline);
  overlay.setShown(true);

  overlay.update(UNIT_BOX, frontView());
  pipeline.render();
  const auto thin = countNonZero(pipeline);

  overlay.setWidth(3);
  overlay.update(UNIT_BOX, frontView());
  pipeline.render();
  REQUIRE(countNonZero(pipeline) > thin);

  // A width below 1 still draws.
  overlay.setWidth(0);
  REQUIRE(overlay.width() == 1);
}
