// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Normal maps arrive decoded the way ANARI's physicallyBased material expects
// them: the sampler returns the tangent-space normal, not the encoded texel.

// catch
#include "catch.hpp"
// vsr_tests
#include "SamplerAssertions.h"
#include "TestDirectories.h"
// vsr
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/io/images/ImageCache.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <filesystem>
#include <fstream>
#include <string>

using namespace vsr::scene;

namespace {

// One triangle with normals and texcoords, its normal map scaled by 0.5 and
// its clearcoat normal map by 2, all inline so the file stands alone. The
// image is a 1x1 PNG.
constexpr const char *NORMAL_MAPPED_GLTF = R"({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_clearcoat"],
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
    "material": 0
  }]}],
  "materials": [{
    "normalTexture": {"index": 0, "scale": 0.5},
    "extensions": {"KHR_materials_clearcoat": {
      "clearcoatFactor": 1.0,
      "clearcoatNormalTexture": {"index": 0, "scale": 2.0}
    }}
  }],
  "textures": [{"source": 0}],
  "images": [{"uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNoaPj/HwAGggL/s75RMwAAAABJRU5ErkJggg=="}],
  "buffers": [{"byteLength": 96, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"}
  ]
})";

// The glTF above, written to a scratch directory for one scenario.
struct GltfFixture
{
  explicit GltfFixture(const char *name);

  ScopedFixtureDirectory directory{"vsr_test_normal_map_"};
  std::string path;
};

GltfFixture::GltfFixture(const char *name)
    : path((directory.path / name).string())
{
  std::ofstream(path) << NORMAL_MAPPED_GLTF;
}

Material *importedMaterial(Scene &scene)
{
  auto surface = scene.getObject<Surface>(0);
  REQUIRE(surface);
  auto *material =
      surface->parameterValueAsObject<Material>(tokens::surface::material);
  REQUIRE(material != nullptr);
  return material;
}

} // namespace

SCENARIO("The ANARI normal decode folds in glTF's normal scale", "[Importers]")
{
  GIVEN("A normal scale of 1, and one of 0.5")
  {
    THEN("x and y are decoded through the scale, z through 2 * texel - 1")
    {
      const auto unit = vsr::io::normalMapDecode();
      REQUIRE(unit.transform
          == vsr::math::mat4(
              {2, 0, 0, 0}, {0, 2, 0, 0}, {0, 0, 2, 0}, {0, 0, 0, 1}));
      REQUIRE(unit.offset == vsr::math::float4(-1, -1, -1, 0));

      const auto half = vsr::io::normalMapDecode(0.5f);
      REQUIRE(half.transform
          == vsr::math::mat4(
              {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 2, 0}, {0, 0, 0, 1}));
      REQUIRE(half.offset == vsr::math::float4(-0.5f, -0.5f, -1, 0));
    }
  }
}

SCENARIO("glTF normal maps are decoded for ANARI", "[Importers]")
{
  GIVEN("A glTF material with a scaled normal map and clearcoat normal map")
  {
    GltfFixture gltf("normal_mapped.gltf");

    WHEN("It is imported")
    {
      Scene scene;
      vsr::animation::AnimationManager animMgr(&scene);
      vsr::io::import_GLTF(scene, animMgr, gltf.path.c_str());
      auto *material = importedMaterial(scene);

      THEN("The normal sampler decodes with the authored scale")
      {
        REQUIRE(boundOutputTransform(*material, "normal")
            == vsr::io::normalMapDecode(0.5f));
      }

      THEN("The clearcoat normal sampler decodes with its own scale")
      {
        REQUIRE(boundOutputTransform(*material, "clearcoatNormal")
            == vsr::io::normalMapDecode(2.f));
      }
    }
  }
}

#if VSR_USE_ASSIMP
SCENARIO("Assimp normal maps are decoded for ANARI", "[Importers]")
{
  GIVEN("A glTF material with a scaled normal map, read through Assimp")
  {
    GltfFixture gltf("normal_mapped_assimp.gltf");

    WHEN("It is imported")
    {
      Scene scene;
      vsr::animation::AnimationManager animMgr(&scene);
      vsr::io::import_ASSIMP(scene, animMgr, gltf.path.c_str());
      auto *material = importedMaterial(scene);

      THEN("The normal sampler decodes with the authored scale")
      {
        REQUIRE(boundOutputTransform(*material, "normal")
            == vsr::io::normalMapDecode(0.5f));
      }

      THEN("The clearcoat normal sampler decodes with its own scale")
      {
        REQUIRE(boundOutputTransform(*material, "clearcoatNormal")
            == vsr::io::normalMapDecode(2.f));
      }
    }
  }
}
#endif
