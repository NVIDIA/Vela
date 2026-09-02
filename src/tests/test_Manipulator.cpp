// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/rendering/view/Manipulator.hpp"

namespace math = vsr::math;
namespace rendering = vsr::rendering;

static void requireNear(const math::float3 &a, const math::float3 &b)
{
  REQUIRE(math::neql(a.x, b.x, 1e-4f));
  REQUIRE(math::neql(a.y, b.y, 1e-4f));
  REQUIRE(math::neql(a.z, b.z, 1e-4f));
}

SCENARIO("Manipulator look mode preserves the camera anchor", "[Manipulator]")
{
  rendering::Manipulator m;
  m.setConfig(math::float3(0.f), 5.f, math::float2(30.f, 20.f));

  const auto orbitEye = m.eye();
  const auto orbitDir = m.dir();
  const auto orbitUp = m.up();

  WHEN("look mode is enabled")
  {
    m.setMode(rendering::ManipulatorMode::Look);

    THEN("the rendered camera pose does not jump")
    {
      requireNear(m.eye(), orbitEye);
      requireNear(m.dir(), orbitDir);
      requireNear(m.up(), orbitUp);
    }

    THEN("rotation keeps the camera position fixed")
    {
      m.startNewRotation();
      m.rotate(math::float2(0.1f, -0.05f));

      requireNear(m.eye(), orbitEye);
    }

    THEN("setting the center retargets without moving the camera")
    {
      m.setCenter(math::float3(1.f, 2.f, 3.f));

      requireNear(m.eye(), orbitEye);
      requireNear(m.at(), math::float3(1.f, 2.f, 3.f));
    }
  }
}

SCENARIO(
    "Manipulator adopts a pose given as eye, direction and up", "[Manipulator]")
{
  rendering::Manipulator reference;
  reference.setConfig(
      math::float3(1.f, 2.f, 3.f), 5.f, math::float2(30.f, 20.f));

  GIVEN("A manipulator looking somewhere else at another distance")
  {
    rendering::Manipulator m;
    m.setConfig(math::float3(0.f), 2.f, math::float2(-70.f, 5.f));

    WHEN("it is given the reference camera's eye, direction and up")
    {
      m.setPose(reference.eye(), reference.dir(), reference.up());

      THEN("it renders the same view from the same point")
      {
        requireNear(m.eye(), reference.eye());
        requireNear(m.dir(), reference.dir());
        requireNear(m.up(), reference.up());
      }

      THEN("its own distance is kept, so the center lies along the view")
      {
        REQUIRE(math::neql(m.distance(), 2.f, 1e-4f));
        requireNear(m.at(), reference.eye() + reference.dir() * 2.f);
      }
    }

    WHEN("the up vector points along another axis")
    {
      m.setPose(math::float3(0.f, 0.f, 10.f),
          math::float3(0.f, 0.f, -1.f),
          math::float3(0.f, 0.f, 0.f) + math::float3(1.f, 0.f, 0.f));

      THEN("that axis becomes the manipulator's up axis")
      {
        REQUIRE(m.axis() == rendering::UpAxis::POS_X);
        requireNear(m.dir(), math::float3(0.f, 0.f, -1.f));
        requireNear(m.up(), math::float3(1.f, 0.f, 0.f));
      }
    }
  }
}
