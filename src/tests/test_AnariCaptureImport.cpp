// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// ANARI Captures written by hand, in the version-1 format the ANARI-SDK
// `capture` device documents (src/devices/capture/README.md), then imported.

// catch
#include "catch.hpp"
// vsr
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/scene/Scene.hpp"
// tests
#include "TestDirectories.h"
// std
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace vsr::scene;
using vsr::io::AnariCaptureImportReport;
using vsr::math::float3;
using vsr::math::mat4;

namespace {

struct CaptureFixture
{
  ScopedFixtureDirectory dir{"vsr_test_anari_capture_"};
  Scene scene;
  vsr::animation::AnimationManager animMgr{&scene};

  std::string write(const std::string &text, const char *name = "t.capture")
  {
    const auto path = dir.path / name;
    std::ofstream(path, std::ios::binary) << text;
    return path.string();
  }

  template <typename T>
  void writeBlob(const char *name, const std::vector<T> &values)
  {
    std::filesystem::create_directories(dir.path / "blobs");
    std::ofstream out(
        dir.path / "blobs" / (std::string(name) + ".bin"), std::ios::binary);
    out.write(reinterpret_cast<const char *>(values.data()),
        std::streamsize(values.size() * sizeof(T)));
  }

  AnariCaptureImportReport import(const std::string &text)
  {
    return vsr::io::import_ANARI_CAPTURE(scene, animMgr, write(text).c_str());
  }
};

LayerNodeRef findNode(Layer &layer, const std::string &name)
{
  LayerNodeRef found;
  layer.traverse(layer.root(), [&](auto &node, int) {
    if (!found && node->name() == name)
      found = layer.at(node.index());
    return !found;
  });
  return found;
}

std::vector<LayerNodeRef> children(Layer &layer, LayerNodeRef parent)
{
  std::vector<LayerNodeRef> result;
  layer.traverse(parent, [&](auto &node, int level) {
    if (level == 1)
      result.push_back(layer.at(node.index()));
    return level < 1;
  });
  return result;
}

template <typename T>
T value(const Object &o, const char *name)
{
  auto *p = o.parameter(name);
  REQUIRE(p);
  REQUIRE(p->isEnabled());
  REQUIRE(p->value().is<T>());
  return p->value().get<T>();
}

template <typename T>
T *objectNamed(const Scene &scene, anari::DataType type, const char *name)
{
  for (size_t i = 0; i < scene.numberOfObjects(type); i++) {
    auto o = scene.getObject<T>(i);
    if (o && o->name() == name)
      return o.data();
  }
  return nullptr;
}

bool contains(const std::vector<std::string> &v, const std::string &s)
{
  return std::find(v.begin(), v.end(), s) != v.end();
}

// The README's example: one triangle whose vertices are in a blob, a frame
// with a parameter the spec does not define, and the warning it caused.
const char *TRIANGLE = R"(anari-capture 1

device {
  extensions ANARI_STRING_LIST [
    "ANARI_KHR_CAMERA_PERSPECTIVE"
    "ANARI_KHR_GEOMETRY_TRIANGLE"
  ]
}

frame@0 {
  bogus ANARI_INT32 1
  camera ANARI_CAMERA camera@0
  channel.color ANARI_DATA_TYPE ANARI_UFIXED8_RGBA_SRGB
  renderer ANARI_RENDERER renderer@0
  size ANARI_UINT32_VEC2 (64, 48)
  world ANARI_WORLD world@0
}

camera@0 "perspective" {
  fovy ANARI_FLOAT32 0.7
  position ANARI_FLOAT32_VEC3 (0, 0, -3)
}

renderer@0 "default" {
  background ANARI_FLOAT32_VEC4 (0.1, 0.1, 0.1, 1)
}

world@0 {
  instance ANARI_ARRAY1D array1d@0
}

array1d@0 {
  elementType ANARI_INSTANCE
  dimensions (1)
  data [
    instance@0
  ]
}

instance@0 "transform" {
  group ANARI_GROUP group@0
}

group@0 {
  surface ANARI_ARRAY1D array1d@1
}

array1d@1 {
  elementType ANARI_SURFACE
  dimensions (1)
  data [
    surface@0
  ]
}

surface@0 {
  geometry ANARI_GEOMETRY geometry@0
  material ANARI_MATERIAL material@0
}

geometry@0 "triangle" {
  vertex.position ANARI_ARRAY1D array1d@2
}

array1d@2 {
  elementType ANARI_FLOAT32_VEC3
  dimensions (6)
  count 6
  min (0, 0, 0)
  max (5, 0.5, 0)
  blob 674a2d1779237ed0
}

material@0 "matte" {
  color ANARI_FLOAT32_VEC3 (0.8, 0.2, 0.1)
}

warnings {
  "frame@0: unknown parameter \"bogus\""
}
)";

const std::vector<float3> TRIANGLE_VERTICES = {{0.f, 0.f, 0.f},
    {1.f, 0.f, 0.f},
    {0.f, 0.5f, 0.f},
    {4.f, 0.f, 0.f},
    {5.f, 0.f, 0.f},
    {4.f, 0.5f, 0.f}};

} // namespace

SCENARIO("A capture's world imports as a layer subtree", "[AnariCaptureImport]")
{
  GIVEN("The capture device's one-triangle example and its blob")
  {
    CaptureFixture f;
    f.writeBlob("674a2d1779237ed0", TRIANGLE_VERTICES);

    WHEN("It is imported")
    {
      auto report = f.import(TRIANGLE);
      auto &layer = *f.scene.defaultLayer();

      THEN("It reads")
      {
        REQUIRE(report.read);
        REQUIRE(report.error.empty());
        REQUIRE(report.formatVersion == 1);
      }

      THEN("The instance is a transform node under a node named for the file")
      {
        auto file = findNode(layer, "t");
        REQUIRE(file);
        auto fileChildren = children(layer, file);
        REQUIRE(fileChildren.size() == 1);
        auto instance = fileChildren[0];
        REQUIRE((*instance)->name() == "instance@0");
        REQUIRE((*instance)->isTransform());
        REQUIRE((*instance)->getTransform() == vsr::math::IDENTITY_MAT4);

        auto contents = children(layer, instance);
        REQUIRE(contents.size() == 1);
        REQUIRE((*contents[0])->type() == ANARI_SURFACE);
      }

      THEN("Each object keeps its subtype, its parameters and its ID as name")
      {
        REQUIRE(f.scene.numberOfObjects(ANARI_SURFACE) == 1);
        auto *surface = f.scene.getObject<Surface>(0).data();
        REQUIRE(surface->name() == "surface@0");

        auto *geom = surface->parameterValueAsObject<Geometry>("geometry");
        REQUIRE(geom);
        REQUIRE(geom->name() == "geometry@0");
        REQUIRE(geom->subtype() == tokens::geometry::triangle);

        auto *mat = surface->parameterValueAsObject<Material>("material");
        REQUIRE(mat);
        REQUIRE(mat->subtype() == tokens::material::matte);
        REQUIRE(value<float3>(*mat, "color") == float3(0.8f, 0.2f, 0.1f));
      }

      THEN("A blob's elements fill the array it backs")
      {
        auto *geom =
            objectNamed<Geometry>(f.scene, ANARI_GEOMETRY, "geometry@0");
        REQUIRE(geom);
        auto *positions =
            geom->parameterValueAsObject<Array>("vertex.position");
        REQUIRE(positions);
        REQUIRE(positions->elementType() == ANARI_FLOAT32_VEC3);
        REQUIRE(positions->size() == 6);
        REQUIRE(
            std::memcmp(
                positions->data(), TRIANGLE_VERTICES.data(), 6 * sizeof(float3))
            == 0);
      }

      THEN("Parameters the capture does not set are disabled")
      {
        auto *surface = f.scene.getObject<Surface>(0).data();
        auto *mat = surface->parameterValueAsObject<Material>("material");
        REQUIRE(mat);
        for (size_t i = 0; i < mat->numParameters(); i++) {
          auto &p = mat->parameterAt(i);
          REQUIRE(p.isEnabled() == (p.name() == "color"));
        }
      }

      THEN("The camera joins the camera pool")
      {
        auto *cam = objectNamed<Camera>(f.scene, ANARI_CAMERA, "camera@0");
        REQUIRE(cam);
        REQUIRE(cam->subtype() == tokens::camera::perspective);
        REQUIRE(value<float>(*cam, "fovy") == 0.7f);
        REQUIRE(value<float3>(*cam, "position") == float3(0.f, 0.f, -3.f));
      }

      THEN("The renderer and frame are reported, not imported")
      {
        REQUIRE(f.scene.numberOfObjects(ANARI_RENDERER) == 0);
        REQUIRE(report.rendererSubtype == "default");
        REQUIRE(report.rendererParameters
            == std::vector<std::string>{
                "background ANARI_FLOAT32_VEC4 (0.1, 0.1, 0.1, 1)"});
        REQUIRE(report.frameParameters
            == std::vector<std::string>{"bogus ANARI_INT32 1",
                "channel.color ANARI_DATA_TYPE ANARI_UFIXED8_RGBA_SRGB",
                "size ANARI_UINT32_VEC2 (64, 48)"});
      }

      THEN("The device block and the warnings are reported verbatim")
      {
        REQUIRE(report.extensions
            == std::vector<std::string>{
                "ANARI_KHR_CAMERA_PERSPECTIVE", "ANARI_KHR_GEOMETRY_TRIANGLE"});
        REQUIRE(report.captureWarnings
            == std::vector<std::string>{
                "frame@0: unknown parameter \"bogus\""});
        REQUIRE(report.dropped.empty());
      }
    }
  }
}

namespace {

// A capture whose frame reaches only `world@0`, followed by `rest`.
std::string withWorld(const std::string &rest)
{
  return "anari-capture 1\n\ndevice {\n  extensions ANARI_STRING_LIST []\n}\n\n"
         "frame@0 {\n  world ANARI_WORLD world@0\n}\n\n"
      + rest;
}

bool hasDropped(const AnariCaptureImportReport &report,
    const std::string &path,
    const std::string &reasonPart = "")
{
  return std::any_of(report.dropped.begin(),
      report.dropped.end(),
      [&](const vsr::io::AnariCaptureDropped &d) {
        return d.path == path && d.reason.find(reasonPart) != std::string::npos;
      });
}

size_t nodeCount(Layer &layer)
{
  size_t n = 0;
  layer.traverse(layer.root(), [&](auto &, int) {
    n++;
    return true;
  });
  return n;
}

} // namespace

SCENARIO("Input that is not a usable capture imports nothing",
    "[AnariCaptureImport]")
{
  const char *cases[][2] = {
      {"anari-capture 3\n\nframe@0 {\n}\n", "version 3"},
      {"hello world\n", "not an ANARI Capture"},
      {"anari-capture 1\n\ndevice {\n  extensions ANARI_STRING_LIST []\n}\n",
          "no frame"},
      {"anari-capture 1\n\narray1d@0 {\n  bogus 1\n}\n", "unknown array field"},
      {"anari-capture 1\n\nbogus@0 {\n}\n", "malformed object ID"},
      {"anari-capture 1\n\ncamera@x {\n}\n", "malformed object ID"},
  };
  for (auto &c : cases) {
    GIVEN(std::string("The text: ") + c[0])
    {
      CaptureFixture f;
      auto &layer = *f.scene.defaultLayer();
      const size_t nodesBefore = nodeCount(layer);

      auto report = f.import(c[0]);

      THEN("The report says why, and the scene is untouched")
      {
        REQUIRE_FALSE(report.read);
        REQUIRE(report.error.find(c[1]) != std::string::npos);
        REQUIRE(nodeCount(layer) == nodesBefore);
      }
    }
  }

  GIVEN("A file that does not exist")
  {
    CaptureFixture f;
    auto report = vsr::io::import_ANARI_CAPTURE(
        f.scene, f.animMgr, (f.dir.path / "missing.capture").string().c_str());
    THEN("It is reported as unreadable")
    {
      REQUIRE_FALSE(report.read);
      REQUIRE(report.error.find("cannot open") != std::string::npos);
    }
  }
}

SCENARIO(
    "Instances that share a group share its objects", "[AnariCaptureImport]")
{
  GIVEN("Two translated instances of one group, each with an id")
  {
    CaptureFixture f;
    auto report = f.import(withWorld(R"(
world@0 {
  instance ANARI_ARRAY1D array1d@0
}
array1d@0 {
  elementType ANARI_INSTANCE
  dimensions (2)
  data [
    instance@0
    instance@1
  ]
}
instance@0 "transform" {
  group ANARI_GROUP group@0
  id ANARI_UINT32 7
  transform ANARI_FLOAT32_MAT4 (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1)
}
instance@1 "transform" {
  group ANARI_GROUP group@0
  name ANARI_STRING "second"
  transform ANARI_FLOAT32_MAT4 (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4, 0, 0, 1)
}
group@0 {
  surface ANARI_ARRAY1D array1d@1
}
array1d@1 {
  elementType ANARI_SURFACE
  dimensions (1)
  data [
    surface@0
  ]
}
surface@0 {
  geometry ANARI_GEOMETRY geometry@0
}
geometry@0 "sphere" {
  radius ANARI_FLOAT32 0.5
}
)"));
    auto &layer = *f.scene.defaultLayer();

    THEN("One surface sits under two transform nodes")
    {
      REQUIRE(report.read);
      REQUIRE(f.scene.numberOfObjects(ANARI_SURFACE) == 1);
      auto first = findNode(layer, "instance@0");
      auto second = findNode(layer, "second");
      REQUIRE(first);
      REQUIRE(second);
      REQUIRE((*first)->getTransform()[3].x == 2.f);
      REQUIRE((*second)->getTransform()[3].x == 4.f);
      auto a = children(layer, first);
      auto b = children(layer, second);
      REQUIRE(a.size() == 1);
      REQUIRE(b.size() == 1);
      REQUIRE((*a[0])->getObject() == (*b[0])->getObject());
    }

    THEN("Instance parameters other than transform and group stay on the node")
    {
      auto first = findNode(layer, "instance@0");
      auto &params = (*first)->getInstanceParameters();
      REQUIRE(params.size() == 1);
      auto *id = params.at("id");
      REQUIRE(id);
      REQUIRE(id->get<uint32_t>() == 7u);
      REQUIRE((*findNode(layer, "second"))->getInstanceParameters().empty());
    }
  }
}

SCENARIO("An instance's transform array becomes a transform array node",
    "[AnariCaptureImport]")
{
  GIVEN("An instance with two transforms inline")
  {
    CaptureFixture f;
    auto report = f.import(withWorld(R"(
world@0 {
  instance ANARI_ARRAY1D array1d@0
}
array1d@0 {
  elementType ANARI_INSTANCE
  dimensions (1)
  data [
    instance@0
  ]
}
instance@0 "transform" {
  group ANARI_GROUP group@0
  transform ANARI_ARRAY1D array1d@1
}
array1d@1 {
  elementType ANARI_FLOAT32_MAT4
  dimensions (2)
  data [
    (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 3, 0, 1)
  ]
}
group@0 {
}
)"));

    THEN("The node holds both transforms")
    {
      REQUIRE(report.read);
      auto node = findNode(*f.scene.defaultLayer(), "instance@0");
      REQUIRE(node);
      auto *a = (*node)->getTransformArray();
      REQUIRE(a);
      REQUIRE(a->size() == 2);
      REQUIRE(a->dataAs<mat4>()[1][3].y == 3.f);
    }
  }
}

namespace {

// One sphere geometry whose `vertex.position` is blob `name` of `count`
// float3s, placed directly in the world.
std::string sphereWithBlob(const char *name, int count)
{
  return withWorld(R"(
world@0 {
  surface ANARI_ARRAY1D array1d@0
}
array1d@0 {
  elementType ANARI_SURFACE
  dimensions (1)
  data [
    surface@0
  ]
}
surface@0 {
  geometry ANARI_GEOMETRY geometry@0
}
geometry@0 "sphere" {
  radius ANARI_FLOAT32 0.25
  vertex.position ANARI_ARRAY1D array1d@1
}
array1d@1 {
  elementType ANARI_FLOAT32_VEC3
  dimensions ()"
      + std::to_string(count) + ")\n  count " + std::to_string(count)
      + "\n  blob " + name + "\n}\n");
}

} // namespace

SCENARIO("A bad blob leaves its parameter out and the rest imports",
    "[AnariCaptureImport]")
{
  const std::string path = "world.surface[0].geometry.vertex.position";

  GIVEN("A blob that is not there")
  {
    CaptureFixture f;
    auto report = f.import(sphereWithBlob("00000000000000aa", 100));

    THEN("The parameter is reported and the geometry still imports")
    {
      REQUIRE(report.read);
      REQUIRE(hasDropped(report, path, "missing"));
      auto *g = objectNamed<Geometry>(f.scene, ANARI_GEOMETRY, "geometry@0");
      REQUIRE(g);
      REQUIRE(value<float>(*g, "radius") == 0.25f);
      auto *p = g->parameter("vertex.position");
      REQUIRE((p == nullptr || !p->isEnabled()));
    }

    THEN("The world's own surface sits under a world node")
    {
      auto &layer = *f.scene.defaultLayer();
      auto world = findNode(layer, "world");
      REQUIRE(world);
      REQUIRE(children(layer, world).size() == 1);
    }
  }

  GIVEN("A blob shorter than its array")
  {
    CaptureFixture f;
    f.writeBlob("00000000000000bb", std::vector<float3>(3));
    auto report = f.import(sphereWithBlob("00000000000000bb", 4));

    THEN("The size mismatch is reported")
    {
      REQUIRE(hasDropped(report, path, "has 36 bytes, expected 48"));
    }
  }
}

SCENARIO("Arrays with the same blob name are one array", "[AnariCaptureImport]")
{
  GIVEN("Two geometries whose positions name the same blob")
  {
    CaptureFixture f;
    f.writeBlob("00000000000000cc", std::vector<float3>(100, float3(1.f)));
    auto report = f.import(withWorld(R"(
world@0 {
  surface ANARI_ARRAY1D array1d@0
}
array1d@0 {
  elementType ANARI_SURFACE
  dimensions (2)
  data [
    surface@0
    surface@1
  ]
}
surface@0 {
  geometry ANARI_GEOMETRY geometry@0
}
surface@1 {
  geometry ANARI_GEOMETRY geometry@1
}
geometry@0 "sphere" {
  vertex.position ANARI_ARRAY1D array1d@1
}
geometry@1 "sphere" {
  vertex.position ANARI_ARRAY1D array1d@2
}
array1d@1 {
  elementType ANARI_FLOAT32_VEC3
  dimensions (100)
  count 100
  blob 00000000000000cc
}
array1d@2 {
  elementType ANARI_FLOAT32_VEC3
  dimensions (100)
  count 100
  blob 00000000000000cc
}
)"));

    THEN("Both geometries refer to one VSR array")
    {
      REQUIRE(report.dropped.empty());
      auto *a = objectNamed<Geometry>(f.scene, ANARI_GEOMETRY, "geometry@0");
      auto *b = objectNamed<Geometry>(f.scene, ANARI_GEOMETRY, "geometry@1");
      REQUIRE(a->parameterValueAsObject<Array>("vertex.position")
          == b->parameterValueAsObject<Array>("vertex.position"));
    }
  }
}

SCENARIO("What the application set is kept as written", "[AnariCaptureImport]")
{
  GIVEN("Unknown subtypes and parameters, opaque values and fixed-point data")
  {
    CaptureFixture f;
    auto report = f.import(withWorld(R"(
world@0 {
  light ANARI_ARRAY1D array1d@0
  surface ANARI_ARRAY1D array1d@1
}
array1d@0 {
  elementType ANARI_LIGHT
  dimensions (2)
  data [
    light@0
    null
  ]
}
light@0 "directional" {
  callback ANARI_VOID_POINTER <opaque>
  color ANARI_UFIXED8_VEC3 (255, 128, 0)
  made.up ANARI_FLOAT64 -0
  half ANARI_FLOAT16_VEC2 (0.5, -2)
  label ANARI_STRING "a \"quoted\"\tlabel"
  enabled ANARI_BOOL false
}
array1d@1 {
  elementType ANARI_SURFACE
  dimensions (1)
  data [
    surface@0
  ]
}
surface@0 {
  geometry ANARI_GEOMETRY geometry@0
}
geometry@0 "blobby" {
  primitive.color ANARI_ARRAY1D array1d@2
}
array1d@2 {
  elementType ANARI_UFIXED16
  dimensions (3)
  data [
    0
    32768
    65535
  ]
}
)"));
    auto *light = objectNamed<Light>(f.scene, ANARI_LIGHT, "light@0");
    REQUIRE(light);

    THEN("Unknown parameters and subtypes are kept")
    {
      REQUIRE(value<double>(*light, "made.up") == 0.0);
      auto *half = light->parameter("half");
      REQUIRE(half);
      REQUIRE(half->value().type() == ANARI_FLOAT16_VEC2);
      auto *h = static_cast<const uint16_t *>(half->value().data());
      REQUIRE(h[0] == 0x3800); // 0.5
      REQUIRE(h[1] == 0xc000); // -2
      auto *g = objectNamed<Geometry>(f.scene, ANARI_GEOMETRY, "geometry@0");
      REQUIRE(g);
      REQUIRE(g->subtype() == vsr::core::Token("blobby"));
    }

    THEN("Strings are unescaped and booleans read")
    {
      auto *p = light->parameter("label");
      REQUIRE(p);
      REQUIRE(p->value().getString() == "a \"quoted\"\tlabel");
      REQUIRE(value<bool>(*light, "enabled") == false);
    }

    THEN("Fixed-point values stay raw integers in their own type")
    {
      auto *p = light->parameter("color");
      REQUIRE(p);
      REQUIRE(p->value().type() == ANARI_UFIXED8_VEC3);
      auto *bytes = static_cast<const uint8_t *>(p->value().data());
      REQUIRE(bytes[0] == 255);
      REQUIRE(bytes[1] == 128);
      REQUIRE(bytes[2] == 0);

      auto *g = objectNamed<Geometry>(f.scene, ANARI_GEOMETRY, "geometry@0");
      auto *a = g->parameterValueAsObject<Array>("primitive.color");
      REQUIRE(a);
      REQUIRE(a->elementType() == ANARI_UFIXED16);
      REQUIRE(a->dataAs<uint16_t>()[1] == 32768);
    }

    THEN("Opaque values are reported")
    {
      REQUIRE(light->parameter("callback") == nullptr);
      REQUIRE(hasDropped(report, "world.light[0].callback", "not recorded"));
    }

    THEN("A null element is an empty slot, not something dropped")
    {
      REQUIRE_FALSE(hasDropped(report, "world.light[1]"));
    }
  }
}

SCENARIO("Motion instances keep their first pose", "[AnariCaptureImport]")
{
  GIVEN("A motionTransform instance with two transforms")
  {
    CaptureFixture f;
    auto report = f.import(withWorld(R"(
world@0 {
  instance ANARI_ARRAY1D array1d@0
}
array1d@0 {
  elementType ANARI_INSTANCE
  dimensions (1)
  data [
    instance@0
  ]
}
instance@0 "motionTransform" {
  group ANARI_GROUP group@0
  motion.transform ANARI_ARRAY1D array1d@1
  time ANARI_FLOAT32_BOX1 (0, 1)
}
array1d@1 {
  elementType ANARI_FLOAT32_MAT4
  dimensions (2)
  data [
    (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1)
    (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 9, 0, 0, 1)
  ]
}
group@0 {
}
)"));

    THEN("The node takes the first transform and the motion is reported")
    {
      auto node = findNode(*f.scene.defaultLayer(), "instance@0");
      REQUIRE(node);
      REQUIRE((*node)->getTransform()[3].x == 5.f);
      REQUIRE(
          hasDropped(report, "world.instance[0]", "first transform is used"));
    }

    THEN("Motion parameters do not become instance parameters")
    {
      auto node = findNode(*f.scene.defaultLayer(), "instance@0");
      REQUIRE((*node)->getInstanceParameters().empty());
    }
  }
}

SCENARIO(
    "A version 2 capture's call counts are reported", "[AnariCaptureImport]")
{
  GIVEN("A version 2 capture with a calls block")
  {
    CaptureFixture f;
    auto report = f.import(R"(anari-capture 2

device {
  extensions ANARI_STRING_LIST []
}

calls {
  total 40
  anariCommitParameters 10
  anariRenderFrame 1
}

frame@0 {
  size ANARI_UINT32_VEC2 (8, 8)
}
)");

    THEN("It reads, and the counts are in the report as written")
    {
      REQUIRE(report.read);
      REQUIRE(report.formatVersion == 2);
      REQUIRE(report.callCounts
          == std::vector<std::string>{
              "total 40", "anariCommitParameters 10", "anariRenderFrame 1"});
    }
  }

  GIVEN("A version 1 capture with a calls block, which it cannot have")
  {
    CaptureFixture f;
    auto report =
        f.import("anari-capture 1\n\ncalls {\n  total 1\n}\n\nframe@0 {\n}\n");

    THEN("It is rejected")
    {
      REQUIRE_FALSE(report.read);
    }
  }
}

SCENARIO(
    "An object array keeps the elements VSR can hold", "[AnariCaptureImport]")
{
  GIVEN("A material whose sampler array also names the frame's renderer")
  {
    CaptureFixture f;
    auto report = f.import(R"(anari-capture 1

frame@0 {
  renderer ANARI_RENDERER renderer@0
  world ANARI_WORLD world@0
}
renderer@0 "default" {
}
world@0 {
  surface ANARI_ARRAY1D array1d@0
}
array1d@0 {
  elementType ANARI_SURFACE
  dimensions (1)
  data [
    surface@0
  ]
}
surface@0 {
  material ANARI_MATERIAL material@0
}
material@0 "matte" {
  layers ANARI_ARRAY1D array1d@1
}
array1d@1 {
  elementType ANARI_SAMPLER
  dimensions (3)
  data [
    sampler@0
    renderer@0
    null
  ]
}
sampler@0 "image1D" {
}
)");

    THEN("The array keeps its shape, with an empty slot for the element")
    {
      REQUIRE(report.read);
      auto *m = objectNamed<Material>(f.scene, ANARI_MATERIAL, "material@0");
      REQUIRE(m);
      auto *a = m->parameterValueAsObject<Array>("layers");
      REQUIRE(a);
      REQUIRE(a->size() == 3);
      auto *s = objectNamed<Sampler>(f.scene, ANARI_SAMPLER, "sampler@0");
      REQUIRE(s);
      REQUIRE(a->dataAs<size_t>()[0] == s->index());
      REQUIRE(a->dataAs<size_t>()[1] == vsr::core::INVALID_INDEX);
      REQUIRE(a->dataAs<size_t>()[2] == vsr::core::INVALID_INDEX);
    }

    THEN("Only the element that could not be held is reported")
    {
      REQUIRE(hasDropped(report,
          "world.surface[0].material.layers[1]",
          "cannot be a parameter"));
      REQUIRE_FALSE(hasDropped(report, "world.surface[0].material.layers"));
      REQUIRE_FALSE(hasDropped(report, "world.surface[0].material.layers[2]"));
    }
  }
}
