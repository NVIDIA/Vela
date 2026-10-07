// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Scenes exported to miniScene and read back with miniScene's own loader.

#if VSR_USE_MINISCENE

// catch
#include "catch.hpp"
// vsr_tests
#include "LogCapture.h"
// vsr
#include "vsr/io/exporters.hpp"
#include "vsr/io/images/ImageCache.hpp"
#include "vsr/scene/Scene.hpp"
// miniScene
#include "miniScene/Scene.h"
// std
#include <cstdint>
#include <filesystem>
#include <vector>

using namespace vsr::scene;
using vsr::math::float2;
using vsr::math::float3;
using vsr::math::mat4;

namespace {

mini::Scene::SP exportAndLoad(const Scene &scene, const char *name)
{
  const auto filename =
      (std::filesystem::temp_directory_path() / name).string();
  REQUIRE(vsr::io::export_SceneToMiniScene(scene, filename.c_str()));
  auto loaded = mini::Scene::load(filename);
  std::filesystem::remove(filename);
  return loaded;
}

mat4 translation(float x, float y, float z)
{
  auto m = vsr::math::IDENTITY_MAT4;
  m[3] = vsr::math::float4(x, y, z, 1.f);
  return m;
}

// Two triangles sharing an edge, with an optional material.
GeometryRef makeTriangles(Scene &scene)
{
  auto geometry = scene.createObject<Geometry>(tokens::geometry::triangle);
  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 4);
  positions->setData(std::vector<float3>{
      {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, 0.f}});
  geometry->setParameterObject("vertex.position", *positions);
  auto index = scene.createArray(ANARI_UINT32_VEC3, 2);
  index->setData(std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
  geometry->setParameterObject("primitive.index", *index);
  return geometry;
}

SurfaceRef makeSurface(Scene &scene, const char *name)
{
  auto material = scene.createObject<Material>(tokens::material::matte);
  material->setParameter("color", float3(0.f, 1.f, 0.f));
  return scene.createSurface(name, makeTriangles(scene), material);
}

} // namespace

SCENARIO("A triangle mesh exports with its matte material", "[MiniSceneExport]")
{
  GIVEN("One matte surface at the layer root")
  {
    Scene scene;
    scene.insertChildObjectNode(
        scene.defaultLayer()->root(), makeSurface(scene, "tris"));

    WHEN("The scene is exported and read back")
    {
      auto mini = exportAndLoad(scene, "vsr_test_mini_triangles.mini");

      THEN("It holds one instance of one two-triangle mesh")
      {
        REQUIRE(mini->instances.size() == 1);
        auto &meshes = mini->instances[0]->object->meshes;
        REQUIRE(meshes.size() == 1);
        REQUIRE(meshes[0]->vertices.size() == 4);
        REQUIRE(meshes[0]->indices.size() == 2);
        REQUIRE(meshes[0]->indices[1].z == 3);
      }

      THEN("The matte color becomes a mini::Matte reflectance")
      {
        auto matte =
            mini->instances[0]->object->meshes[0]->material->as<mini::Matte>();
        REQUIRE(matte);
        REQUIRE(matte->reflectance.y == 1.f);
        REQUIRE(matte->reflectance.x == 0.f);
      }
    }
  }
}

SCENARIO("Quads split into triangles", "[MiniSceneExport]")
{
  GIVEN("An unindexed quad with face-varying normals")
  {
    Scene scene;
    auto geometry = scene.createObject<Geometry>(tokens::geometry::quad);
    auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 4);
    positions->setData(std::vector<float3>{
        {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, 0.f}});
    geometry->setParameterObject("vertex.position", *positions);
    auto normals = scene.createArray(ANARI_FLOAT32_VEC3, 4);
    normals->setData(std::vector<float3>{
        {0.f, 0.f, 1.f}, {0.f, 0.f, 2.f}, {0.f, 0.f, 3.f}, {0.f, 0.f, 4.f}});
    geometry->setParameterObject("faceVarying.normal", *normals);
    scene.insertChildObjectNode(scene.defaultLayer()->root(),
        scene.createSurface("quad", geometry, {}));

    WHEN("The scene is exported and read back")
    {
      auto mini = exportAndLoad(scene, "vsr_test_mini_quads.mini");
      auto mesh = mini->instances.at(0)->object->meshes.at(0);

      THEN("The quad becomes triangles (0,1,2) and (0,2,3)")
      {
        REQUIRE(mesh->indices.size() == 2);
        REQUIRE(mesh->indices[0] == mini::vec3i(0, 1, 2));
        REQUIRE(mesh->indices[1] == mini::vec3i(0, 2, 3));
      }

      THEN("Face-varying normals follow the split, three per triangle")
      {
        REQUIRE(mesh->normals.size() == 6);
        std::vector<float> z;
        for (auto &n : mesh->normals)
          z.push_back(n.z);
        REQUIRE(z == std::vector<float>{1.f, 2.f, 3.f, 1.f, 3.f, 4.f});
      }
    }
  }
}

SCENARIO("Instancing is preserved", "[MiniSceneExport]")
{
  GIVEN(
      "Two surfaces under one transform, and one placed by a transform "
      "array of three")
  {
    Scene scene;
    auto root = scene.defaultLayer()->root();

    auto a = makeSurface(scene, "a");
    auto b = makeSurface(scene, "b");
    auto moved =
        scene.insertChildTransformNode(root, translation(5.f, 0.f, 0.f));
    scene.insertChildObjectNode(moved, a);
    scene.insertChildObjectNode(moved, b);

    auto xfms = scene.createArray(ANARI_FLOAT32_MAT4, 3);
    xfms->setData(std::vector<mat4>{translation(0.f, 1.f, 0.f),
        translation(0.f, 2.f, 0.f),
        translation(0.f, 3.f, 0.f)});
    auto scatter = scene.insertChildTransformArrayNode(moved, xfms.data());
    scene.insertChildObjectNode(scatter, a);

    WHEN("The scene is exported and read back")
    {
      auto mini = exportAndLoad(scene, "vsr_test_mini_instancing.mini");

      THEN(
          "The transform's surfaces form one two-mesh object, and the array "
          "places a one-mesh object three times")
      {
        REQUIRE(mini->instances.size() == 4);

        size_t pairs = 0;
        std::vector<mini::Object::SP> scattered;
        for (auto &inst : mini->instances) {
          if (inst->object->meshes.size() == 2) {
            pairs++;
            REQUIRE(inst->xfm.p.x == 5.f);
          } else {
            scattered.push_back(inst->object);
            // the array's matrices compose with the enclosing transform
            REQUIRE(inst->xfm.p.x == 5.f);
            REQUIRE(inst->xfm.p.y >= 1.f);
          }
        }
        REQUIRE(pairs == 1);
        REQUIRE(scattered.size() == 3);
        REQUIRE(scattered[0] == scattered[1]);
        REQUIRE(scattered[1] == scattered[2]);
      }
    }
  }
}

SCENARIO("Only what renders is exported", "[MiniSceneExport]")
{
  GIVEN("A disabled node, an inactive layer, and a volume")
  {
    Scene scene;
    auto root = scene.defaultLayer()->root();
    scene.insertChildObjectNode(root, makeSurface(scene, "shown"));
    auto hidden =
        scene.insertChildObjectNode(root, makeSurface(scene, "hidden"));
    (*hidden)->setEnabled(false);

    auto *other = scene.addLayer("other");
    scene.insertChildObjectNode(other->root(), makeSurface(scene, "inactive"));
    scene.setLayerActive("other", false);

    auto volume = scene.createObject<Volume>("transferFunction1D");
    scene.insertChildObjectNode(root, volume);

    WHEN("The scene is exported and read back")
    {
      auto mini = exportAndLoad(scene, "vsr_test_mini_visibility.mini");

      THEN("Only the enabled surface on the active layer remains")
      {
        REQUIRE(mini->instances.size() == 1);
        REQUIRE(mini->instances[0]->object->meshes.size() == 1);
      }
    }
  }
}

SCENARIO("physicallyBased textures are exported as VSR holds them",
    "[MiniSceneExport]")
{
  GIVEN("A physicallyBased material with a 1x2 baseColor texture")
  {
    Scene scene;
    auto image = scene.createArray(ANARI_UFIXED8_VEC4, 1, 2);
    // ANARI orientation: row 0 is the top of the image (ADR 0014)
    image->setData(std::vector<uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
    auto sampler = scene.createObject<Sampler>(tokens::sampler::image2D);
    sampler->setParameterObject("image", *image);
    sampler->setParameter("filter", "nearest");

    auto material =
        scene.createObject<Material>(tokens::material::physicallyBased);
    material->setParameterObject("baseColor", *sampler);
    material->setParameter("metallic", 0.25f);
    material->setParameter("roughness", 0.5f);
    material->setParameter("alphaMode", "opaque");

    scene.insertChildObjectNode(scene.defaultLayer()->root(),
        scene.createSurface("textured", makeTriangles(scene), material));

    WHEN("The scene is exported and read back")
    {
      auto mini = exportAndLoad(scene, "vsr_test_mini_texture.mini");
      auto pbr = mini->instances.at(0)
                     ->object->meshes.at(0)
                     ->material->as<mini::ANARIMaterial>();

      THEN("It is a mini::ANARIMaterial with the scalar parameters")
      {
        REQUIRE(pbr);
        REQUIRE(pbr->metallic == 0.25f);
        REQUIRE(pbr->roughness == 0.5f);
        REQUIRE(pbr->alphaMode == mini::ANARIMaterial::AM_OPAQUE);
      }

      THEN("The texture survives the save, rows in VSR's order")
      {
        REQUIRE(pbr);
        auto tex = pbr->baseColor_texture;
        REQUIRE(tex);
        REQUIRE(tex->format == mini::Texture::RGBA_UINT8);
        REQUIRE(tex->filterMode == mini::Texture::FILTER_NEAREST);
        REQUIRE(tex->size == mini::vec2i(1, 2));
        REQUIRE(
            tex->data == std::vector<uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
      }
    }
  }
}

SCENARIO("Lights map to miniScene's light kinds", "[MiniSceneExport]")
{
  GIVEN("A directional, a quad, and a point light")
  {
    Scene scene;
    auto root = scene.defaultLayer()->root();

    auto dir = scene.createObject<Light>(tokens::light::directional);
    dir->setParameter("irradiance", 4.f);
    dir->setParameter("direction", float2(0.f, 0.f)); // azimuth/elevation
    scene.insertChildObjectNode(root, dir);

    auto quad = scene.createObject<Light>(tokens::light::quad);
    quad->setParameter("edge1", float3(2.f, 0.f, 0.f));
    quad->setParameter("intensity", 8.f);
    auto moved =
        scene.insertChildTransformNode(root, translation(0.f, 0.f, 3.f));
    scene.insertChildObjectNode(moved, quad);

    scene.insertChildObjectNode(
        root, scene.createObject<Light>(tokens::light::point));

    WHEN("The scene is exported and read back")
    {
      auto mini = exportAndLoad(scene, "vsr_test_mini_lights.mini");

      THEN("The directional light carries half its irradiance as radiance")
      {
        REQUIRE(mini->dirLights.size() == 1);
        REQUIRE(mini->dirLights[0].radiance.x == 2.f);
        REQUIRE(mini->dirLights[0].direction.z == Approx(1.f));
      }

      THEN(
          "The quad light is placed in world space with radiance "
          "intensity / area")
      {
        REQUIRE(mini->quadLights.size() == 1);
        auto &q = mini->quadLights[0];
        REQUIRE(q.corner.z == 3.f);
        REQUIRE(q.area == Approx(2.f));
        REQUIRE(q.emission.x == Approx(4.f));
      }

      THEN("The point light is dropped")
      {
        REQUIRE(mini->instances.empty());
        REQUIRE(mini->dirLights.size() == 1);
        REQUIRE(mini->quadLights.size() == 1);
      }
    }
  }
}

SCENARIO("An hdri light becomes a top-row-first env map", "[MiniSceneExport]")
{
  GIVEN("An hdri light with a 1x2 radiance image, held bottom row first")
  {
    Scene scene;
    auto radiance = scene.createArray(ANARI_FLOAT32_VEC3, 1, 2);
    // row 0 is the bottom of the environment (ADR 0014)
    radiance->setData(std::vector<float3>{{1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}});
    auto hdri = scene.createObject<Light>(tokens::light::hdri);
    hdri->setParameterObject("radiance", *radiance);
    hdri->setParameter("scale", 2.f);
    scene.insertChildObjectNode(scene.defaultLayer()->root(), hdri);

    WHEN("The scene is exported and read back")
    {
      auto mini = exportAndLoad(scene, "vsr_test_mini_hdri.mini");

      THEN("The env map holds the top row first, scaled")
      {
        REQUIRE(mini->envMapLight);
        auto tex = mini->envMapLight->texture;
        REQUIRE(tex);
        REQUIRE(tex->format == mini::Texture::FLOAT4);
        const auto *texels = reinterpret_cast<const float *>(tex->data.data());
        REQUIRE(texels[2] == 2.f); // top (blue) first
        REQUIRE(texels[4] == 2.f); // then bottom (red)
      }

      THEN("hayStack's reading of the frame recovers up and direction")
      {
        auto &l = mini->envMapLight->transform.l;
        REQUIRE(l.vz.y == Approx(1.f)); // up = vz
        REQUIRE(-l.vx.x == Approx(1.f)); // direction = -vx
      }
    }
  }
}

// miniScene stores normal maps as encoded texels and its renderer decodes
// them, so the decode ANARI needs on the sampler is exactly what miniScene
// already implies -- not a transform it lacks.
SCENARIO("A normal map's ANARI decode exports without a dropped transform",
    "[MiniSceneExport]")
{
  GIVEN(
      "A physicallyBased material whose normal maps carry the decode, and"
      " a base colour carrying some other transform")
  {
    Scene scene;
    auto image = scene.createArray(ANARI_UFIXED8_VEC4, 1, 1);
    image->setData(std::vector<uint8_t>{128, 128, 255, 255});

    auto makeSampler = [&](const vsr::io::OutputTransform &output) {
      auto sampler = scene.createObject<Sampler>(tokens::sampler::image2D);
      sampler->setParameterObject("image", *image);
      vsr::io::setOutputTransform(*sampler, output);
      return sampler;
    };

    auto material =
        scene.createObject<Material>(tokens::material::physicallyBased);
    material->setParameterObject(
        "normal", *makeSampler(vsr::io::normalMapDecode()));
    material->setParameterObject(
        "clearcoatNormal", *makeSampler(vsr::io::normalMapDecode()));
    scene.insertChildObjectNode(scene.defaultLayer()->root(),
        scene.createSurface("normal_mapped", makeTriangles(scene), material));

    WHEN("The scene is exported and read back")
    {
      LogCapture log;
      auto mini = exportAndLoad(scene, "vsr_test_mini_normal_maps.mini");
      auto pbr = mini->instances.at(0)
                     ->object->meshes.at(0)
                     ->material->as<mini::ANARIMaterial>();

      THEN("The encoded texels go out as stored, with nothing reported lost")
      {
        REQUIRE(pbr);
        REQUIRE(pbr->normal_texture);
        REQUIRE(pbr->normal_texture->data
            == std::vector<uint8_t>{128, 128, 255, 255});
        REQUIRE(pbr->clearcoatNormal_texture);
        REQUIRE(!log.sawMessageContaining("texture transforms ignored"));
      }
    }

    WHEN("A base colour carries the same transform")
    {
      material->setParameterObject(
          "baseColor", *makeSampler(vsr::io::normalMapDecode()));
      LogCapture log;
      exportAndLoad(scene, "vsr_test_mini_decoded_base_color.mini");

      THEN("There it is a transform miniScene cannot hold")
      {
        REQUIRE(log.sawMessageContaining("texture transforms ignored"));
      }
    }
  }
}

#endif
