// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// miniScene files written with miniScene's own saver, then imported.

#if VSR_USE_MINISCENE

// catch
#include "catch.hpp"
// vsr
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/io/exporters.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/scene/Scene.hpp"
// miniScene
#include "miniScene/Scene.h"
// std
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace vsr::scene;
using vsr::math::float2;
using vsr::math::float3;
using vsr::math::float4;
using vsr::math::mat4;

namespace {

std::string tempPath(const char *name)
{
  return (std::filesystem::temp_directory_path() / name).string();
}

void saveAndImport(mini::Scene &in, Scene &scene, const char *name)
{
  const auto filename = tempPath(name);
  in.save(filename);
  vsr::animation::AnimationManager animMgr(&scene);
  vsr::io::import_MINI(scene, animMgr, filename.c_str());
  std::filesystem::remove(filename);
}

// Two triangles sharing an edge, with an optional material.
mini::Mesh::SP makeMesh(mini::Material::SP material = {})
{
  auto mesh = mini::Mesh::create(material);
  mesh->vertices = {mini::vec3f(0.f, 0.f, 0.f),
      mini::vec3f(1.f, 0.f, 0.f),
      mini::vec3f(1.f, 1.f, 0.f),
      mini::vec3f(0.f, 1.f, 0.f)};
  mesh->indices = {mini::vec3i(0, 1, 2), mini::vec3i(0, 2, 3)};
  return mesh;
}

mini::affine3f translation(float x, float y, float z)
{
  mini::affine3f xfm;
  xfm.p = mini::vec3f(x, y, z);
  return xfm;
}

template <typename T>
T *firstObject(const Scene &scene, anari::DataType type)
{
  REQUIRE(scene.numberOfObjects(type) > 0);
  return scene.getObject<T>(0).data();
}

Light *lightOfSubtype(const Scene &scene, vsr::core::Token subtype)
{
  for (size_t i = 0; i < scene.numberOfObjects(ANARI_LIGHT); i++) {
    auto light = scene.getObject<Light>(i);
    if (light && light->subtype() == subtype)
      return light.data();
  }
  return nullptr;
}

template <typename T>
T value(const Object &o, const char *name)
{
  auto *p = o.parameter(name);
  REQUIRE(p);
  REQUIRE(p->value().is<T>());
  return p->value().get<T>();
}

} // namespace

SCENARIO("Instances share their object's Surfaces", "[MiniSceneImport]")
{
  GIVEN("One object placed three times and another placed once")
  {
    mini::Scene in;
    auto many = mini::Object::create({makeMesh()});
    auto once = mini::Object::create({makeMesh(), makeMesh()});
    for (float x : {0.f, 2.f, 4.f})
      in.instances.push_back(
          mini::Instance::create(many, translation(x, 0, 0)));
    in.instances.push_back(
        mini::Instance::create(once, translation(0.f, 5.f, 0.f)));

    WHEN("It is imported")
    {
      Scene scene;
      saveAndImport(in, scene, "vsr_test_mini_import_instances.mini");

      THEN("Each mesh becomes one Surface, however often it is placed")
      {
        REQUIRE(scene.numberOfObjects(ANARI_SURFACE) == 3);
        REQUIRE(scene.numberOfObjects(ANARI_GEOMETRY) == 3);
      }

      THEN(
          "The object placed many times is under one transform array, the "
          "other under one transform")
      {
        auto *layer = scene.defaultLayer();
        std::vector<Array *> arrays;
        std::vector<mat4> transforms;
        layer->traverse(layer->root(), [&](auto &node, int) {
          if (node->type() == ANARI_ARRAY1D && node->getTransformArray())
            arrays.push_back(node->getTransformArray());
          else if (node->type() == ANARI_FLOAT32_MAT4)
            transforms.push_back(node->getTransform());
          return true;
        });
        REQUIRE(arrays.size() == 1);
        REQUIRE(arrays[0]->size() == 3);
        REQUIRE(arrays[0]->dataAs<mat4>()[2][3].x == 4.f);
        REQUIRE(std::count_if(transforms.begin(),
                    transforms.end(),
                    [](const mat4 &m) { return m[3].y == 5.f; })
            == 1);
      }
    }
  }
}

SCENARIO("Mesh data imports as a triangle Geometry", "[MiniSceneImport]")
{
  GIVEN("A mesh with face-varying normals and per-vertex texcoords")
  {
    mini::Scene in;
    auto mesh = makeMesh();
    mesh->normals.assign(6, mini::vec3f(0.f, 0.f, 1.f));
    mesh->texcoords = {mini::vec2f(0.f, 0.f),
        mini::vec2f(1.f, 0.f),
        mini::vec2f(1.f, 1.f),
        mini::vec2f(0.f, 1.f)};
    in.instances.push_back(
        mini::Instance::create(mini::Object::create({mesh})));

    WHEN("It is imported")
    {
      Scene scene;
      saveAndImport(in, scene, "vsr_test_mini_import_mesh.mini");
      auto *geom = firstObject<Geometry>(scene, ANARI_GEOMETRY);

      THEN("Positions and indices carry over")
      {
        REQUIRE(geom->subtype() == tokens::geometry::triangle);
        auto *index = geom->parameterValueAsObject<Array>("primitive.index");
        REQUIRE(index);
        REQUIRE(index->elementType() == ANARI_UINT32_VEC3);
        REQUIRE(index->size() == 2);
        REQUIRE(index->dataAs<uint32_t>()[5] == 3);
      }

      THEN("Attribute scope follows the element count")
      {
        REQUIRE(geom->parameterValueAsObject<Array>("faceVarying.normal"));
        REQUIRE(!geom->parameter("vertex.normal"));
        auto *uv = geom->parameterValueAsObject<Array>("vertex.attribute0");
        REQUIRE(uv);
        REQUIRE(uv->dataAs<float2>()[2] == float2(1.f, 1.f));
      }
    }
  }
}

SCENARIO("Materials map the way hayStack renders them", "[MiniSceneImport]")
{
  GIVEN("An ANARIMaterial with a texture, a Matte, and a default material")
  {
    mini::Scene in;

    auto tex = mini::Texture::create();
    tex->format = mini::Texture::RGBA_UINT8;
    tex->filterMode = mini::Texture::FILTER_NEAREST;
    tex->size = mini::vec2i(1, 2);
    tex->data = {255, 0, 0, 255, 0, 0, 255, 255};

    auto pbr = mini::ANARIMaterial::create();
    pbr->baseColor_texture = tex;
    pbr->metallic = 0.25f;
    pbr->alphaMode = mini::ANARIMaterial::AM_MASK;

    auto matte = mini::Matte::create();
    matte->reflectance = mini::vec3f(0.f, 1.f, 0.f);

    in.instances.push_back(mini::Instance::create(
        mini::Object::create({makeMesh(pbr), makeMesh(matte), makeMesh()})));

    WHEN("It is imported")
    {
      Scene scene;
      saveAndImport(in, scene, "vsr_test_mini_import_materials.mini");

      auto material = [&](size_t i) {
        auto surface = scene.getObject<Surface>(i);
        REQUIRE(surface);
        auto *m = surface->parameterValueAsObject<Material>(
            tokens::surface::material);
        REQUIRE(m);
        return m;
      };

      THEN("ANARIMaterial becomes physicallyBased with its alphaMode named")
      {
        auto *m = material(0);
        REQUIRE(m->subtype() == tokens::material::physicallyBased);
        REQUIRE(value<float>(*m, "metallic") == 0.25f);
        auto *mode = m->parameter("alphaMode");
        REQUIRE(mode);
        REQUIRE(mode->value().getString() == std::string("mask"));
      }

      THEN("Its texture is sampled with rows and filter as stored")
      {
        auto *s = material(0)->parameterValueAsObject<Sampler>("baseColor");
        REQUIRE(s);
        REQUIRE(s->subtype() == tokens::sampler::image2D);
        auto *filter = s->parameter("filter");
        REQUIRE(filter);
        REQUIRE(filter->value().getString() == std::string("nearest"));
        auto *image = s->parameterValueAsObject<Array>("image");
        REQUIRE(image);
        REQUIRE(image->elementType() == ANARI_UFIXED8_VEC4);
        REQUIRE(std::memcmp(image->data(), tex->data.data(), 8) == 0);
      }

      THEN("Matte reflectance becomes the matte color")
      {
        auto *m = material(1);
        REQUIRE(m->subtype() == tokens::material::matte);
        REQUIRE(value<float3>(*m, "color") == float3(0.f, 1.f, 0.f));
      }

      THEN("miniScene's default DisneyMaterial becomes physicallyBased")
      {
        auto *m = material(2);
        REQUIRE(m->subtype() == tokens::material::physicallyBased);
        REQUIRE(value<float3>(*m, "baseColor") == float3(.5f));
        REQUIRE(value<float>(*m, "opacity") == 1.f);
      }
    }
  }
}

SCENARIO("Lights map from miniScene's light kinds", "[MiniSceneImport]")
{
  GIVEN("A directional, a quad facing -z, and an env-map light")
  {
    mini::Scene in;

    mini::DirLight dir;
    dir.direction = mini::vec3f(0.f, 0.f, -1.f);
    dir.radiance = mini::vec3f(1.f, 2.f, 3.f);
    in.dirLights.push_back(dir);

    mini::QuadLight quad;
    quad.corner = mini::vec3f(0.f, 0.f, 3.f);
    quad.edge0 = mini::vec3f(1.f, 0.f, 0.f);
    quad.edge1 = mini::vec3f(0.f, 1.f, 0.f);
    quad.normal = mini::vec3f(0.f, 0.f, -1.f); // against cross(edge0, edge1)
    quad.emission = mini::vec3f(4.f);
    quad.area = 1.f;
    in.quadLights.push_back(quad);

    // 1x2, top row red, bottom row blue
    auto env = mini::EnvMapLight::create();
    env->texture = mini::Texture::create();
    env->texture->format = mini::Texture::FLOAT4;
    env->texture->size = mini::vec2i(1, 2);
    const float texels[] = {1, 0, 0, 0, 0, 0, 1, 0};
    env->texture->data.resize(sizeof(texels));
    std::memcpy(env->texture->data.data(), texels, sizeof(texels));
    env->transform.l.vx = mini::vec3f(-1.f, 0.f, 0.f);
    env->transform.l.vy = mini::vec3f(0.f, 0.f, 1.f);
    env->transform.l.vz = mini::vec3f(0.f, 1.f, 0.f);
    in.envMapLight = env;

    WHEN("It is imported")
    {
      Scene scene;
      saveAndImport(in, scene, "vsr_test_mini_import_lights.mini");

      THEN("The directional light's irradiance is twice its average radiance")
      {
        auto *l = lightOfSubtype(scene, tokens::light::directional);
        REQUIRE(l);
        REQUIRE(value<float>(*l, "irradiance") == Approx(4.f));
        REQUIRE(value<float3>(*l, "color").z == Approx(1.5f));
        REQUIRE(value<float3>(*l, "direction") == float3(0.f, 0.f, -1.f));
      }

      THEN("The quad light's edges are ordered to emit along its normal")
      {
        auto *l = lightOfSubtype(scene, tokens::light::quad);
        REQUIRE(l);
        REQUIRE(value<float3>(*l, "edge1") == float3(0.f, 1.f, 0.f));
        REQUIRE(value<float3>(*l, "edge2") == float3(1.f, 0.f, 0.f));
        REQUIRE(value<float>(*l, "radiance") == Approx(4.f));
      }

      THEN("The env map becomes an hdri light, bottom row first")
      {
        auto *l = lightOfSubtype(scene, tokens::light::hdri);
        REQUIRE(l);
        auto *radiance = l->parameterValueAsObject<Array>("radiance");
        REQUIRE(radiance);
        REQUIRE(radiance->elementType() == ANARI_FLOAT32_VEC3);
        REQUIRE(radiance->dataAs<float3>()[0] == float3(0.f, 0.f, 1.f));
        REQUIRE(value<float3>(*l, "up") == float3(0.f, 1.f, 0.f));
        REQUIRE(value<float3>(*l, "direction") == float3(1.f, 0.f, 0.f));
      }
    }
  }
}

SCENARIO("A scene exported to miniScene imports back", "[MiniSceneImport]")
{
  GIVEN("A textured surface, a directional light, and an hdri light")
  {
    Scene scene;
    auto root = scene.defaultLayer()->root();

    auto geometry = scene.createObject<Geometry>(tokens::geometry::triangle);
    auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 3);
    positions->setData(
        std::vector<float3>{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
    geometry->setParameterObject("vertex.position", *positions);
    auto uv = scene.createArray(ANARI_FLOAT32_VEC2, 3);
    uv->setData(std::vector<float2>{{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}});
    geometry->setParameterObject("vertex.attribute0", *uv);

    auto image = scene.createArray(ANARI_UFIXED8_VEC4, 1, 2);
    image->setData(std::vector<uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
    auto sampler = scene.createObject<Sampler>(tokens::sampler::image2D);
    sampler->setParameterObject("image", *image);
    auto material =
        scene.createObject<Material>(tokens::material::physicallyBased);
    material->setParameterObject("baseColor", *sampler);
    scene.insertChildObjectNode(
        root, scene.createSurface("textured", geometry, material));

    auto dir = scene.createObject<Light>(tokens::light::directional);
    dir->setParameter("direction", float3(0.f, -1.f, 0.f));
    dir->setParameter("color", float3(1.f, .5f, .5f));
    dir->setParameter("irradiance", 3.f);
    scene.insertChildObjectNode(root, dir);

    auto hdriRadiance = scene.createArray(ANARI_FLOAT32_VEC3, 1, 2);
    hdriRadiance->setData(
        std::vector<float3>{{1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}});
    auto hdri = scene.createObject<Light>(tokens::light::hdri);
    hdri->setParameterObject("radiance", *hdriRadiance);
    scene.insertChildObjectNode(root, hdri);

    WHEN("It is exported and imported into a fresh scene")
    {
      const auto filename = tempPath("vsr_test_mini_import_roundtrip.mini");
      REQUIRE(vsr::io::export_SceneToMiniScene(scene, filename.c_str()));
      Scene back;
      vsr::animation::AnimationManager animMgr(&back);
      vsr::io::import_MINI(back, animMgr, filename.c_str());
      std::filesystem::remove(filename);

      THEN("Texture rows and texcoords come back as they were")
      {
        auto *g = firstObject<Geometry>(back, ANARI_GEOMETRY);
        auto *backUV = g->parameterValueAsObject<Array>("vertex.attribute0");
        REQUIRE(backUV);
        REQUIRE(backUV->dataAs<float2>()[2] == float2(0.f, 1.f));

        auto *m =
            firstObject<Surface>(back, ANARI_SURFACE)
                ->parameterValueAsObject<Material>(tokens::surface::material);
        REQUIRE(m);
        auto *s = m->parameterValueAsObject<Sampler>("baseColor");
        REQUIRE(s);
        auto *backImage = s->parameterValueAsObject<Array>("image");
        REQUIRE(backImage);
        REQUIRE(std::memcmp(backImage->data(), image->data(), 8) == 0);
      }

      THEN("The directional light's color times irradiance is unchanged")
      {
        auto *l = lightOfSubtype(back, tokens::light::directional);
        REQUIRE(l);
        const auto c =
            value<float3>(*l, "color") * value<float>(*l, "irradiance");
        REQUIRE(c.x == Approx(3.f));
        REQUIRE(c.y == Approx(1.5f));
      }

      THEN("The hdri radiance comes back in its original row order")
      {
        auto *l = lightOfSubtype(back, tokens::light::hdri);
        REQUIRE(l);
        auto *r = l->parameterValueAsObject<Array>("radiance");
        REQUIRE(r);
        REQUIRE(r->dataAs<float3>()[0] == float3(1.f, 0.f, 0.f));
        REQUIRE(r->dataAs<float3>()[1] == float3(0.f, 0.f, 1.f));
      }
    }
  }
}

#endif
