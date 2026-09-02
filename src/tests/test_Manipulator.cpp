// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/rendering/view/Manipulator.hpp"
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
#include "vsr/scene/Scene.hpp"

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

SCENARIO("Manipulator adopts an orthographic camera's eye and height",
    "[Manipulator]")
{
  GIVEN("A manipulator that drove an orthographic camera object")
  {
    vsr::scene::Scene scene;
    auto camera = scene.createObject<vsr::scene::Camera>(
        vsr::scene::tokens::camera::orthographic);
    rendering::Manipulator m;
    m.setConfig(math::float3(0.f), 4.f, math::float2(30.f, 20.f));
    rendering::updateCameraObject(*camera, m, false);
    const auto height = camera->parameterValueAs<float>("height");
    REQUIRE(height);
    REQUIRE(math::neql(*height, 3.f, 1e-4f));

    WHEN("a client zooms the camera by editing height and position")
    {
      const math::float3 eye(1.f, 2.f, 3.f);
      const math::float3 dir = m.dir();
      camera->setParameter("position", eye);
      camera->setParameter("direction", dir);
      camera->setParameter("up", m.up());
      camera->setParameter("height", 6.f);
      rendering::updateManipulatorFromCameraPose(m, *camera);

      THEN("the manipulator's fixed-distance eye and distance follow")
      {
        requireNear(m.eye_FixedDistance(), eye);
        requireNear(m.dir(), dir);
        REQUIRE(math::neql(m.distance(), 8.f, 1e-4f));
      }

      THEN("writing the camera back reproduces the edit")
      {
        rendering::updateCameraObject(*camera, m, false);
        const auto position =
            camera->parameterValueAs<math::float3>("position");
        const auto written = camera->parameterValueAs<float>("height");
        REQUIRE(position);
        REQUIRE(written);
        requireNear(*position, eye);
        REQUIRE(math::neql(*written, 6.f, 1e-4f));
      }
    }

    WHEN("the camera is perspective")
    {
      auto perspective = scene.createObject<vsr::scene::Camera>(
          vsr::scene::tokens::camera::perspective);
      perspective->setParameter("position", math::float3(0.f, 0.f, 10.f));
      perspective->setParameter("direction", math::float3(0.f, 0.f, -1.f));
      perspective->setParameter("up", math::float3(0.f, 1.f, 0.f));
      rendering::updateManipulatorFromCameraPose(m, *perspective);

      THEN("the eye is adopted at the manipulator's own distance")
      {
        requireNear(m.eye(), math::float3(0.f, 0.f, 10.f));
        REQUIRE(math::neql(m.distance(), 4.f, 1e-4f));
      }
    }
  }
}
