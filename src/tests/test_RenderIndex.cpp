// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/index/RenderToAnariObjectsVisitor.hpp"
#include "vsr/scene/Scene.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <vector>

namespace {

using namespace vsr::core;
using namespace vsr::scene;

// A device the tests can drive without a display or a GPU. Absent builds skip
// rather than fail: what is under test is VSR's bookkeeping, not the device.
struct TestDevice
{
  TestDevice()
  {
    library = anari::loadLibrary("helide", [](const void *,
                                               ANARIDevice,
                                               ANARIObject,
                                               anari::DataType,
                                               ANARIStatusSeverity,
                                               ANARIStatusCode,
                                               const char *) {});
    if (library)
      device = anari::newDevice(library, "default");
  }

  ~TestDevice()
  {
    if (device)
      anari::release(device, device);
    if (library)
      anari::unloadLibrary(library);
  }

  explicit operator bool() const
  {
    return device != nullptr;
  }

  anari::Library library{nullptr};
  anari::Device device{nullptr};
};

// The world's bounds are the cheapest thing a device will tell us about where
// the geometry it was given actually ended up.
vsr::math::float3 worldBoundsMax(
    anari::Device d, vsr::rendering::RenderIndexAllLayers &index)
{
  vsr::math::float3 bounds[2] = {{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
  anariGetProperty(d,
      index.world(),
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds[0],
      sizeof(bounds),
      ANARI_WAIT);
  return bounds[1];
}

// One unit sphere at the origin, placed by a single-element transform array so
// that the only thing that can move it is the contents of that Array.
struct InstancedSphereScene
{
  InstancedSphereScene()
  {
    auto geometry = scene.createObject<Geometry>(tokens::geometry::sphere);
    auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 1);
    const vsr::math::float3 origin(0.f, 0.f, 0.f);
    positions->setData(&origin, 1);
    geometry->setParameterObject("vertex.position", *positions);
    geometry->setParameter("radius", 1.f);

    auto material = scene.createObject<Material>(tokens::material::matte);
    auto surface = scene.createSurface("sphere", geometry, material);

    transforms = scene.createArray(ANARI_FLOAT32_MAT4, 1);
    const auto identity = vsr::math::IDENTITY_MAT4;
    transforms->setData(&identity, 1);

    auto node = scene.insertChildTransformArrayNode(
        scene.defaultLayer()->root(), transforms.data(), "instances");
    scene.insertChildObjectNode(node, surface, "sphere");
  }

  Scene scene;
  ArrayRef transforms;
};

vsr::math::mat4 translation(float x)
{
  auto retval = vsr::math::IDENTITY_MAT4;
  retval[3] = vsr::math::float4(x, 0.f, 0.f, 1.f);
  return retval;
}

} // namespace

SCENARIO("Rewriting a transform array moves its instances", "[RenderIndex]")
{
  TestDevice anariDevice;
  if (!anariDevice) {
    WARN("helide unavailable, skipping");
    return;
  }

  GIVEN("A populated render index over a transform-array node")
  {
    InstancedSphereScene content;
    auto *index =
        content.scene.updateDelegate()
            .emplace<vsr::rendering::RenderIndexAllLayers>(
                content.scene, Token("helide"), anariDevice.device);
    index->populate();

    REQUIRE(worldBoundsMax(anariDevice.device, *index).x == Approx(1.f));

    WHEN("The transform array is rewritten in place")
    {
      const auto moved = translation(50.f);
      content.transforms->setData(&moved, 1);

      // A layer's node transforms are copied into its ANARI instances rather
      // than referenced, so an Array rewritten behind the render index's back
      // has to make that copy happen again. Rebuilding the world does not.
      THEN("The instance moves with it")
      {
        REQUIRE(worldBoundsMax(anariDevice.device, *index).x == Approx(51.f));
      }
    }

    WHEN("The transform array is rewritten inside an update batch")
    {
      content.scene.beginUpdateBatch();
      const auto moved = translation(50.f);
      content.transforms->setData(&moved, 1);
      content.scene.endUpdateBatch();

      THEN("The instance has moved by the time the batch ends")
      {
        REQUIRE(worldBoundsMax(anariDevice.device, *index).x == Approx(51.f));
      }
    }
  }
}

// A Stage that repeats an asset places one Surface under many transforms. A
// device builds its acceleration structure per Group, so each placement should
// reuse one Group rather than bring its own copy of the same Surfaces.
SCENARIO(
    "Placements of the same Surfaces share one ANARI Group", "[RenderIndex]")
{
  TestDevice anariDevice;
  if (!anariDevice) {
    WARN("helide unavailable, skipping");
    return;
  }

  GIVEN("One sphere Surface placed twice, and a second Surface placed once")
  {
    Scene scene;
    auto makeSurface = [&](const char *name) {
      auto geometry = scene.createObject<Geometry>(tokens::geometry::sphere);
      auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 1);
      const vsr::math::float3 origin(0.f, 0.f, 0.f);
      positions->setData(&origin, 1);
      geometry->setParameterObject("vertex.position", *positions);
      geometry->setParameter("radius", 1.f);
      auto material = scene.createObject<Material>(tokens::material::matte);
      return scene.createSurface(name, geometry, material);
    };
    auto shared = makeSurface("shared");
    auto other = makeSurface("other");

    auto root = scene.defaultLayer()->root();
    auto place = [&](SurfaceRef surface, float x) {
      auto node = scene.insertChildTransformNode(root, translation(x));
      scene.insertChildObjectNode(node, surface);
    };
    place(shared, 0.f);
    place(shared, 10.f);
    place(other, -20.f);

    WHEN("The layer is turned into ANARI instances")
    {
      // Sized the way RenderIndex::populate() sizes its own cache.
      AnariHandleCache cache(scene, Token("helide"), anariDevice.device);
      auto addSlots = [](const auto &objects, auto &handles) {
        foreach_item_const(objects, [&](auto *) { handles.insert(nullptr); });
        handles.sync_slots(objects);
      };
      const auto &db = scene.objectDB();
      addSlots(db.array, cache.array);
      addSlots(db.material, cache.material);
      addSlots(db.geometry, cache.geometry);
      addSlots(db.surface, cache.surface);
      std::vector<anari::Instance> instances;
      size_t groups = 0;
      {
        vsr::rendering::RenderToAnariObjectsVisitor visitor(
            anariDevice.device, cache, &instances);
        auto *layer = scene.defaultLayer();
        layer->traverse_const(layer->root(), visitor);
        groups = visitor.groupCount();
      }

      THEN("Each placement gets its own Instance")
      {
        REQUIRE(instances.size() == 3);
      }

      THEN("The two placements of one Surface share a Group")
      {
        REQUIRE(groups == 2);
      }

      for (auto i : instances)
        anari::release(anariDevice.device, i);
    }

    WHEN("A render index draws the layer")
    {
      auto *index =
          scene.updateDelegate().emplace<vsr::rendering::RenderIndexAllLayers>(
              scene, Token("helide"), anariDevice.device);
      index->populate();

      THEN("The second placement still lands where its own transform puts it")
      {
        REQUIRE(worldBoundsMax(anariDevice.device, *index).x == Approx(11.f));
      }
    }
  }
}
