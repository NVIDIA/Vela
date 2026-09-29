// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/pipeline/passes/AutoExposurePass.h"
// std
#include <cmath>

using vsr::rendering::AutoExposurePass;
using vsr::rendering::ImageBuffers;
using vsr::rendering::ImagePipeline;
using vsr::rendering::ImagePass;
using vsr::rendering::ImageSource;

namespace {

constexpr uint32_t WIDTH = 32;
constexpr uint32_t HEIGHT = 32;
constexpr float MID_GRAY = 0.18f;
constexpr float RESPONSE = 0.15f; // AutoExposurePass::m_response default

// Produces uniform grey HDR pixels at the requested luminance.
struct FillSource : ImageSource
{
  float luminance{MID_GRAY};

  void render(ImageBuffers &b) override
  {
    if (!b.hdrColor)
      return;
    // Grey, so luminance(r,g,b) == the channel value exactly.
    for (size_t i = 0; i < size_t(WIDTH) * HEIGHT; i++) {
      b.hdrColor[i * 4 + 0] = luminance;
      b.hdrColor[i * 4 + 1] = luminance;
      b.hdrColor[i * 4 + 2] = luminance;
      b.hdrColor[i * 4 + 3] = 1.f;
    }
  }
};

// The exposure the pass should converge on for a uniform image: the stops
// needed to bring that luminance to mid-grey.
float targetExposureFor(float luminance)
{
  return std::log2(MID_GRAY / luminance);
}

struct Fixture
{
  ImagePipeline pipeline;
  FillSource *fill{nullptr};
  AutoExposurePass *pass{nullptr};

  Fixture()
  {
    fill = pipeline.setSource<FillSource>();
    pass = pipeline.addPass<AutoExposurePass>();
    pipeline.setDimensions(WIDTH, HEIGHT);
    pass->setHDREnabled(true);
  }
};

} // namespace

SCENARIO("AutoExposurePass seeds and then eases toward the target exposure",
    "[AutoExposure]")
{
  GIVEN("a uniformly lit frame four stops brighter than mid-grey")
  {
    Fixture f;
    f.fill->luminance = MID_GRAY * 16.f; // log2(1/16) = -4 stops
    const float target = targetExposureFor(f.fill->luminance);
    REQUIRE(target == Approx(-4.f));

    WHEN("the first frame is rendered")
    {
      f.pipeline.render();

      THEN("the exposure jumps straight to the target rather than easing in")
      {
        // No previous exposure exists, so blending would leave the first
        // frame visibly wrong.
        REQUIRE(f.pass->currentExposure() == Approx(target).margin(1e-3));
      }
    }

    WHEN("a second frame is rendered after the scene darkens to mid-grey")
    {
      f.pipeline.render(); // seeds at -4
      f.fill->luminance = MID_GRAY; // new target: 0 stops
      f.pipeline.render();

      THEN("the exposure eases part of the way, it does not snap")
      {
        // One response step from -4 toward 0.
        const float expected = -4.f + (0.f - -4.f) * RESPONSE;
        REQUIRE(f.pass->currentExposure() == Approx(expected).margin(1e-3));
        // Explicitly not snapped, and actually moved.
        REQUIRE(f.pass->currentExposure() > -4.f);
        REQUIRE(f.pass->currentExposure() < 0.f);
      }
    }

    WHEN("many frames are rendered at a steady luminance")
    {
      for (int i = 0; i < 200; i++)
        f.pipeline.render();

      THEN("the exposure converges on the target")
      {
        REQUIRE(f.pass->currentExposure() == Approx(target).margin(1e-3));
      }
    }
  }
}

SCENARIO("AutoExposurePass publishes its exposure to the shared buffers",
    "[AutoExposure]")
{
  GIVEN("a frame darker than mid-grey")
  {
    Fixture f;
    f.fill->luminance = MID_GRAY / 4.f; // +2 stops

    WHEN("a frame is rendered")
    {
      f.pipeline.render();

      THEN("the pass brightens, and downstream passes see the same value")
      {
        const float exposure = f.pass->currentExposure();
        REQUIRE(exposure == Approx(2.f).margin(1e-3));

        // A probe stage after the auto-exposure pass observes b.exposure.
        struct ProbePass : ImagePass
        {
          float seen{-999.f};
          void render(ImageBuffers &b) override { seen = b.exposure; }
        };
        auto *probe = f.pipeline.addPass<ProbePass>();
        f.pipeline.render();
        REQUIRE(probe->seen == Approx(f.pass->currentExposure()));
      }
    }
  }
}

SCENARIO("AutoExposurePass ignores frames it must not act on", "[AutoExposure]")
{
  GIVEN("a pass with HDR disabled")
  {
    Fixture f;
    f.fill->luminance = MID_GRAY * 16.f;
    f.pass->setHDREnabled(false);

    WHEN("frames are rendered")
    {
      f.pipeline.render();
      f.pipeline.render();

      THEN("it never adapts, despite a badly exposed frame")
      {
        REQUIRE(f.pass->currentExposure() == Approx(0.f));
      }
    }
  }

  GIVEN("a pass that has adapted to a bright scene")
  {
    Fixture f;
    f.fill->luminance = MID_GRAY * 16.f;
    f.pipeline.render();
    const float adapted = f.pass->currentExposure();
    REQUIRE(adapted == Approx(-4.f).margin(1e-3));

    WHEN("HDR is toggled off and back on")
    {
      f.pass->setHDREnabled(false);
      f.pass->setHDREnabled(true);
      f.fill->luminance = MID_GRAY; // target is now 0 stops

      THEN("the next frame re-seeds instead of easing from the stale value")
      {
        f.pipeline.render();
        // Re-enabling clears the held exposure, so this snaps to the new
        // target rather than blending 15% of the way from -4.
        REQUIRE(f.pass->currentExposure() == Approx(0.f).margin(1e-3));
      }
    }
  }
}
