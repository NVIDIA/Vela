// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scripting
#include "vsr/scripting/LuaContext.hpp"
// anari
#include <anari/anari_cpp.hpp>

TEST_CASE("Lua Renderer picks use the rendered camera aspect", "[LuaRenderer]")
{
  auto library = anari::loadLibrary("helide");
  if (!library) {
    WARN("helide ANARI library unavailable, skipping the Lua renderer test");
    return;
  }
  anari::unloadLibrary(library);

  vsr::scripting::LuaContext context;
  context.createOwnedScene();
  const auto result = context.executeString(R"lua(
    local geometry = scene:createGeometry("triangle")
    local vertices = scene:createArray("float3", 3)
    vertices:setData({{-20, -20, 0}, {20, -20, 0}, {0, 20, 0}})
    geometry:setParameter("vertex.position", vertices)
    local material = scene:createMaterial("matte")
    local surface = scene:createSurface("plane", geometry, material)
    scene:insertObjectNode(scene:defaultLayer():root(), surface)
    local device = vsr.render.loadDevice("helide")
    local index = vsr.render.createRenderIndex(scene, device)
    index:populate()

    -- Known points on z=0, five units in front of a 90-degree camera.
    -- Include the default aspect and explicit aspects unlike the image's.
    local cases = {
      {width=512, height=512, x=319, y=256, aspect=1, expectedX=1.240234375},
      {width=512, height=512, x=319, y=256, aspect=4, expectedX=4.9609375},
      {width=512, height=512, x=319, y=256, expectedX=2.203896484375},
      {width=768, height=384, x=479, y=192, aspect=4, expectedX=4.973958333}
    }
    for _, case in ipairs(cases) do
      local camera = vsr.CameraSetup.new()
      camera.fovy = 90
      if case.aspect then camera.aspect = case.aspect end
      local renderer = vsr.render.createRenderer(
          case.width, case.height, device, index, camera)
      local hit = renderer:pick(case.x, case.y)
      assert(hit and hit.position and hit.objectType == "surface")
      -- Allow subpixel jitter in the device's depth sample.
      assert(math.abs(hit.position.z) < 0.1,
          string.format("aspect %g: picked z=%g instead of the z=0 plane",
              camera.aspect, hit.position.z))
      assert(math.abs(hit.position.x - case.expectedX) < 0.1,
          string.format("aspect %g: picked x=%g, expected %g",
              camera.aspect, hit.position.x, case.expectedX))
    end
  )lua");
  INFO(result.error);
  REQUIRE(result.success);
}
