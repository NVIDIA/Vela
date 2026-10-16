// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// tests
#include "RenderAOVCases.h"
// vsr_app
#include "vsr/app/ApplicationDump.h"
#include "vsr/app/Context.h"
#include "vsr/app/LegacyApplicationContext.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// catch
#include "catch.hpp"
// stb_image
#include "stb_image.h"
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>

namespace {

std::string quote(const std::string &value)
{
  std::string out = "'";
  for (char c : value)
    out += c == '\'' ? "'\\''" : std::string(1, c);
  return out + "'";
}

struct CLIFixture
{
  CLIFixture();
  ~CLIFixture();
  int run(const std::string &arguments, const std::string &environment = {});
  std::string save(
      vsr::core::Encoding encoding, bool legacy = false, bool wrapped = false);
  std::string log() const;
#ifdef VSR_MPI_EXECUTABLE
  int runMPI(
      int ranks, const std::string &arguments, const std::string &wrapper = {});
#endif

  vsr::scene::Scene scene;
  std::filesystem::path directory;
  // Configuration comparison tests explicitly opt into replacing their images.
  bool permitOverwrite{false};
};

CLIFixture::CLIFixture()
{
  directory = std::filesystem::temp_directory_path()
      / ("vsr_cli_"
          + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
  REQUIRE(std::filesystem::create_directory(directory));
  scene.removeAllObjects();
  auto camera = scene.createObject<vsr::scene::Camera>("orthographic");
  camera->setName("authored view");
  camera->setParameter("position", vsr::math::float3(0.f, 0.f, 3.f));
  camera->setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
  camera->setParameter("up", vsr::math::float3(0.f, 1.f, 0.f));
  camera->setParameter("height", 2.f);
  auto geometry = scene.createObject<vsr::scene::Geometry>("sphere");
  auto positions = scene.createArray(ANARI_FLOAT32_VEC3, 1);
  const vsr::math::float3 top(0.f, 0.5f, 0.f);
  positions->setData(&top, 1);
  geometry->setParameterObject("vertex.position", *positions);
  geometry->setParameter("radius", 0.25f);
  auto material = scene.createObject<vsr::scene::Material>("matte");
  material->setParameter("color", vsr::math::float3(1.f, 0.f, 0.f));
  auto surface = scene.createSurface("top sphere", geometry, material);
  scene.insertChildObjectNode(scene.defaultLayer()->root(), surface);
  auto light = scene.createObject<vsr::scene::Light>("directional");
  light->setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
  light->setParameter("irradiance", 3.f);
  scene.insertChildObjectNode(scene.defaultLayer()->root(), light);
}

CLIFixture::~CLIFixture()
{
  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
}

int CLIFixture::run(
    const std::string &arguments, const std::string &environment)
{
  const auto command = "cd " + quote(directory.string())
      + " && env -u VSR_ANARI_LIBRARIES -u ANARI_LIBRARY " + environment
      + " timeout 20s " + quote(VSR_RENDER_EXECUTABLE) + " " + arguments
      + (permitOverwrite ? " --overwrite" : "") + " <&- >cli.log 2>&1";
  const int result = std::system(command.c_str());
  UNSCOPED_INFO(arguments);
  UNSCOPED_INFO(log());
  REQUIRE(result != -1);
  REQUIRE(result != 124 * 256);
  // Both success and expected CLI failures must exit normally, not crash.
  REQUIRE((result == 0 || result == 1 * 256));
  return result;
}

#ifdef VSR_MPI_EXECUTABLE
int CLIFixture::runMPI(
    int ranks, const std::string &arguments, const std::string &wrapper)
{
  const auto executable = wrapper.empty() ? quote(VSR_RENDER_EXECUTABLE)
                                          : "/bin/sh " + quote(wrapper);
  const auto command = "cd " + quote(directory.string())
      + " && env -u VSR_ANARI_LIBRARIES -u ANARI_LIBRARY "
        "OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 "
        "OMPI_MCA_rmaps_base_oversubscribe=1 timeout 30s "
      + quote(VSR_MPI_EXECUTABLE) + " " + VSR_MPI_NUMPROC_FLAG + " "
      + std::to_string(ranks) + " " + VSR_MPI_PREFLAGS + " " + executable
      + " " + VSR_MPI_POSTFLAGS + " " + arguments
      + " <&- >cli.log 2>&1";
  const int result = std::system(command.c_str());
  UNSCOPED_INFO(command);
  UNSCOPED_INFO(log());
  REQUIRE(result != -1);
  REQUIRE(result != 124 * 256);
  REQUIRE(result != 137 * 256);
  REQUIRE((result & 127) == 0);
  return result;
}
#endif

std::string CLIFixture::log() const
{
  std::ifstream file(directory / "cli.log");
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

std::string CLIFixture::save(
    vsr::core::Encoding encoding, bool legacy, bool wrapped)
{
  vsr::core::DataTree tree;
  auto &payload = wrapped ? tree.root()["context"] : tree.root();
  REQUIRE(vsr::io::serialize_SceneArchive(scene, payload));
  if (legacy)
    payload.remove("__vsr_metadata");
  const auto path = (directory / "native.data").string();
  REQUIRE(tree.save(path.c_str(), encoding));
  return quote(path);
}

void prepareDump(CLIFixture &fixture, vsr::app::Context &ctx)
{
  vsr::core::DataTree scene;
  REQUIRE(vsr::io::serialize_SceneArchive(fixture.scene, scene.root()));
  REQUIRE(vsr::io::deserialize_SceneArchive(ctx.vsr.scene, scene.root()));
  ctx.offline.frame.width = 32;
  ctx.offline.frame.height = 32;
  ctx.offline.frame.samples = 2;
  ctx.offline.renderer.libraryName = "helide";
  ctx.offline.renderer.activeRenderer = 0;
  ctx.offline.renderer.rendererObjects.emplace_back(ANARI_RENDERER, "default");
  ctx.offline.renderer.rendererObjects.back().setName("saved renderer");
  ctx.offline.renderer.rendererObjects.back().setParameter(
      "background", vsr::math::float4(0.f, 1.f, 0.f, 1.f));
  ctx.view.poses.resize(3);
}

void writeDump(vsr::app::Context &ctx,
    vsr::core::DataTree &tree,
    bool legacy = false,
    bool wrapped = false,
    bool savedOutput = false)
{
  REQUIRE(vsr::app::serialize_ApplicationDump(ctx, tree.root()));
  if (legacy) {
    tree.root().remove("archives");
    auto &payload = wrapped ? tree.root()["context"] : tree.root();
    vsr::app::detail::serializeLegacyApplicationContext(ctx, payload);
    payload.remove("__vsr_metadata");
    ctx.offline.saveSettings(tree.root()["offlineRendering"]);
    auto &poses = tree.root()["cameraPoses"];
    for (const auto &pose : ctx.view.poses)
      vsr::app::serialize_CameraPose(pose, poses.append());
  }
  // Most fixtures exercise other settings, not supplied output defaults.
  if (!savedOutput)
    tree.root()["offlineRendering"].remove("output");
}

void checkDimensions(const std::filesystem::path &path, int w, int h)
{
  int width = 0, height = 0, channels = 0;
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load(path.c_str(), &width, &height, &channels, 4), stbi_image_free);
  REQUIRE(pixels);
  REQUIRE(width == w);
  REQUIRE(height == h);
}

size_t checkImage(const std::filesystem::path &path)
{
  int width = 0, height = 0, channels = 0;
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load(path.c_str(), &width, &height, &channels, 4), stbi_image_free);
  REQUIRE(pixels);
  REQUIRE(width == 32);
  REQUIRE(height == 32);
  size_t red = 0;
  double meanY = 0.;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto *p = pixels.get() + 4 * (y * width + x);
      if (p[0] > p[1] + 20) {
        ++red;
        meanY += y;
      }
    }
  }
  REQUIRE(red > 20);
  REQUIRE(red < 100);
  REQUIRE(meanY / red < 12.);
  return red;
}

void writeImportedScene(CLIFixture &fixture)
{
  std::ofstream(fixture.directory / "imported.pbrt")
      << R"(LookAt 0 0 3  0 0 0  0 1 0
Camera "perspective" "float fov" [40]
Film "rgb" "integer xresolution" [32] "integer yresolution" [32]
WorldBegin
LightSource "distant" "rgb L" [3 3 3] "point3 from" [0 0 3] "point3 to" [0 0 0]
Material "diffuse" "rgb reflectance" [1 0 0]
Shape "sphere" "float radius" [0.5]
WorldEnd
)";
  std::ofstream(fixture.directory / "detail.obj") << R"(o lower triangle
v -0.7 -0.8 0
v 0.7 -0.8 0
v 0 -0.1 0
f 1 2 3
)";
}

std::vector<unsigned char> imagePixels(const std::filesystem::path &path)
{
  int w, h, channels;
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load(path.c_str(), &w, &h, &channels, 4), stbi_image_free);
  REQUIRE(pixels);
  REQUIRE(w == 32);
  REQUIRE(h == 32);
  return {pixels.get(), pixels.get() + w * h * 4};
}

void prepareAnimation(
    CLIFixture &fixture, vsr::app::Context &ctx, bool cameraMotion = false)
{
  prepareDump(fixture, ctx);
  auto &manager = ctx.vsr.animationMgr;
  manager.setAnimationTotalFrames(9);
  auto &animation = manager.addAnimation("timeline motion");
  const float times[] = {0.f, 1.f};
  if (cameraMotion) {
    auto camera = ctx.vsr.scene.getObject<vsr::scene::Camera>(0);
    const vsr::math::float3 positions[] = {
        {0.75f, 0.f, 3.f}, {-0.75f, 0.f, 3.f}};
    animation.addObjectParameterBinding(
        camera.data(), "position", ANARI_FLOAT32_VEC3, positions, times, 2);
  } else {
    auto geometry = ctx.vsr.scene.getObject<vsr::scene::Geometry>(0);
    const float radii[] = {0.15f, 0.4f};
    animation.addObjectParameterBinding(
        geometry.data(), "radius", ANARI_FLOAT32, radii, times, 2);
  }
  manager.setAnimationFrame(3);
}

std::string fileContents(const std::filesystem::path &path)
{
  std::ifstream file(path, std::ios::binary);
  REQUIRE(file);
  std::ostringstream bytes;
  bytes << file.rdbuf();
  return bytes.str();
}

std::set<std::string> pngFiles(const CLIFixture &fixture)
{
  std::set<std::string> files;
  for (const auto &entry :
      std::filesystem::directory_iterator(fixture.directory)) {
    if (entry.path().extension() == ".png")
      files.insert(entry.path().filename().string());
  }
  return files;
}

} // namespace

TEST_CASE("Render CLI accepts an unmodified viewer-style quick-save dump",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.offline.output.outputDirectory = ".";
  ctx.offline.output.filePrefix = "quick-save";
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(ctx, tree.root()));
  const auto &camera = tree.root()["offlineRendering"]["camera"];
  REQUIRE(camera.child("apertureRadius") == nullptr);
  REQUIRE(camera.child("focusDistance") == nullptr);
  REQUIRE(tree.save((fixture.directory / "state.vsr").c_str(), encoding));
  REQUIRE(fixture.run("state.vsr") == 0);
  checkDimensions(fixture.directory / "quick-save.png", 32, 32);
}

TEST_CASE("Render CLI resolves explicit AOV controls", "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.offline.aov.aovType = vsr::rendering::AOVType::DEPTH;
  ctx.offline.aov.depthMin = 0.f;
  ctx.offline.aov.depthMax = 5.f;
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str()));
  REQUIRE(fixture.run("saved.data --output saved.png") == 0);
  const auto saved = imagePixels(fixture.directory / "saved.png");
  size_t depthHits = 0;
  for (size_t i = 0; i < saved.size(); i += 4) {
    REQUIRE(saved[i] == saved[i + 1]);
    REQUIRE(saved[i] == saved[i + 2]);
    depthHits += saved[i] > 0 && saved[i] < 255;
  }
  REQUIRE(depthHits > 20);
  REQUIRE(saved[0] == 255); // Background infinite depth clamps to the far end.
  REQUIRE(fixture.run("saved.data --aov none --output color.png") == 0);
  checkImage(fixture.directory / "color.png");
  REQUIRE(imagePixels(fixture.directory / "color.png") != saved);
  REQUIRE(
      fixture.run(
          "saved.data --aov depth --depth-min 0 --depth-max 5 --output explicit.png")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "explicit.png") == saved);
  REQUIRE(fixture.run("saved.data --depth-min 1 --output minimum.png") == 0);
  REQUIRE(imagePixels(fixture.directory / "minimum.png") != saved);
  REQUIRE(fixture.run("saved.data --depth-max 10 --output maximum.png") == 0);
  REQUIRE(imagePixels(fixture.directory / "maximum.png") != saved);

  ctx.offline.aov.depthMin = 1.f;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "nonzero.data").c_str()));
  REQUIRE(
      fixture.run("nonzero.data --depth-min 0 --output zero-depth.png") == 0);
  REQUIRE(imagePixels(fixture.directory / "zero-depth.png") == saved);
  ctx.offline.aov.depthMin = 0.f;
  ctx.offline.aov.aovType = vsr::rendering::AOVType::EDGES;
  ctx.offline.aov.edgeInvert = true;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "edges.data").c_str()));
  REQUIRE(fixture.run("edges.data --output inverted.png") == 0);
  const auto inverted = imagePixels(fixture.directory / "inverted.png");
  REQUIRE(
      fixture.run("edges.data --edge-invert false --output edges.png") == 0);
  const auto edges = imagePixels(fixture.directory / "edges.png");
  REQUIRE(edges != inverted);
  size_t complemented = 0;
  for (size_t i = 0; i < edges.size(); i += 4) {
    REQUIRE((edges[i] == 0 || edges[i] == 255));
    REQUIRE(edges[i] == edges[i + 1]);
    REQUIRE(edges[i] == edges[i + 2]);
    if (edges[i] != inverted[i]) {
      REQUIRE(edges[i] + inverted[i] == 255);
      ++complemented;
    }
  }
  REQUIRE(complemented > 20);
  REQUIRE(fixture.run("edges.data --edge-invert 0 --output zero.png") == 0);
  REQUIRE(imagePixels(fixture.directory / "zero.png") == edges);
  REQUIRE(
      fixture.run(
          "edges.data --edge-invert false --edge-invert true --output last.png")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "last.png") == inverted);

  // Removing individual saved fields restores only their built-in defaults.
  tree.root()["offlineRendering"]["aov"].remove("edgeInvert");
  REQUIRE(tree.save((fixture.directory / "partial.data").c_str()));
  REQUIRE(fixture.run("partial.data --output partial.png") == 0);
  REQUIRE(imagePixels(fixture.directory / "partial.png") == edges);
  tree.root()["offlineRendering"]["aov"].remove("aovType");
  REQUIRE(tree.save((fixture.directory / "absent.data").c_str()));
  REQUIRE(fixture.run("absent.data --output absent.png") == 0);
  checkImage(fixture.directory / "absent.png");

  ctx.offline.aov.aovType = vsr::rendering::AOVType::DEPTH;
  writeDump(ctx, tree);
  tree.root()["offlineRendering"]["aov"].remove("depthMin");
  REQUIRE(tree.save((fixture.directory / "minimum-absent.data").c_str()));
  REQUIRE(fixture.run("minimum-absent.data --output minimum-absent.png") == 0);
  REQUIRE(imagePixels(fixture.directory / "minimum-absent.png") == saved);
  tree.root()["offlineRendering"]["aov"].remove("depthMax");
  REQUIRE(tree.save((fixture.directory / "maximum-absent.data").c_str()));
  REQUIRE(fixture.run("maximum-absent.data --output maximum-absent.png") == 0);
  REQUIRE(imagePixels(fixture.directory / "maximum-absent.png") != saved);
}

TEST_CASE("Render CLI visualizes requested device channels", "[RenderCLI]")
{
  CLIFixture fixture;
  const auto group = fixture.scene.insertChildTransformNode(
      fixture.scene.defaultLayer()->root(),
      vsr::math::translation_matrix(vsr::math::float3(0.5f, 0.f, 0.f)));
  fixture.scene.insertChildObjectNode(
      group, fixture.scene.getObject<vsr::scene::Surface>(0));
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  for (const auto *mode :
      {"depth", "edges", "object-id", "primitive-id", "instance-id"}) {
    INFO(mode);
    REQUIRE(
        fixture.run(input
            + " --device helide --width 32 --height 32 --samples 2 --depth-max 5 --aov "
            + mode + " --output " + mode + ".png")
        == 0);
    const auto pixels =
        imagePixels(fixture.directory / (std::string(mode) + ".png"));
    size_t different = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
      different += pixels[i] != pixels[0] || pixels[i + 1] != pixels[1]
          || pixels[i + 2] != pixels[2];
    // RenderIndex-created Instances have no authored id, so their default
    // sentinel visualizes just like the background. The engine must still
    // validate the real instanceId channel, not substitute the color image.
    if (std::string(mode) == "instance-id") {
      REQUIRE(different == 0);
      REQUIRE(pixels[3] == 255);
    } else
      REQUIRE(different > 20);
  }
}

TEST_CASE("Render CLI fails unavailable channels without writing an image",
    "[RenderCLI]")
{
  vsr::app::ANARIDeviceManager devices;
  auto device = devices.loadDevice("helide");
  REQUIRE(device);
  const auto *aov = vsr::tests::unsupportedAOV(device);
  anari::release(device, device);
  if (!aov) {
    WARN(
        "SKIP unsupported-channel failure only: helide supports all tested "
        "optional AOV channels");
    return;
  }
  INFO("Unsupported channel: " << aov->channel);
  CLIFixture fixture;
  const auto input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(input
              + " --device helide --width 32 --height 32 --samples 1 --aov "
              + aov->mode)
      != 0);
  REQUIRE(fixture.log().find(aov->channel) != std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
}

TEST_CASE("Render CLI visualizes albedo and normals on a supporting device",
    "[RenderCLI]")
{
  vsr::app::ANARIDeviceManager devices;
  auto device = devices.loadDevice("ospray");
  if (!device) {
    WARN(
        "SKIP albedo/normal renders: optional CPU ospray device is unavailable");
    return;
  }
  std::vector<const vsr::tests::OptionalAOVCase *> supported;
  for (const auto &aov : vsr::tests::OPTIONAL_AOV_CASES) {
    if (aov.type != vsr::rendering::AOVType::ALBEDO
        && aov.type != vsr::rendering::AOVType::NORMAL)
      continue;
    if (vsr::rendering::deviceSupportsExtension(device, aov.extension))
      supported.push_back(&aov);
    else
      WARN("SKIP optional ospray " << aov.mode << " render: " << aov.extension
                                   << " is unavailable");
  }
  anari::release(device, device);
  CLIFixture fixture;
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  for (const auto *aov : supported) {
    const std::string mode = aov->mode;
    INFO(mode);
    REQUIRE(fixture.run(input
                + " --device ospray --width 32 --height 32 --samples 2 --aov "
                + mode + " --output " + mode + ".png")
        == 0);
    const auto pixels =
        imagePixels(fixture.directory / (std::string(mode) + ".png"));
    size_t meaningful = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
      if (aov->type == vsr::rendering::AOVType::ALBEDO)
        meaningful +=
            pixels[i] > 200 && pixels[i + 1] < 10 && pixels[i + 2] < 10;
      else
        meaningful += pixels[i + 2] > 200 && pixels[i + 2] > pixels[i] + 20;
    }
    REQUIRE(meaningful > 20);
  }
}

TEST_CASE(
    "Render CLI rejects invalid AOV controls before output", "[RenderCLI]")
{
  CLIFixture fixture;
  const auto input = fixture.save(vsr::core::Encoding::Text);
  for (const auto *arguments : {"--aov",
           "--aov unknown",
           "--aov DEPTH",
           "--depth-min",
           "--depth-min nan",
           "--depth-max nan",
           "--depth-min -inf",
           "--depth-min inf",
           "--depth-max -inf",
           "--depth-max inf",
           "--depth-max 1e40",
           "--depth-max 1junk",
           "--depth-min 5 --depth-max 5",
           "--depth-min 6 --depth-max 5",
           "--depth-min -3e38 --depth-max 3e38",
           "--edge-invert",
           "--edge-invert maybe",
           "--edge-invert 2"}) {
    REQUIRE(
        fixture.run(input + " --width 16 --height 16 --samples 1 " + arguments)
        != 0);
    REQUIRE(pngFiles(fixture).empty());
  }
}

TEST_CASE(
    "Render CLI accepts signed saved and explicit depth ranges", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.offline.aov.aovType = vsr::rendering::AOVType::DEPTH;
  for (const auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    INFO("encoding: " << int(encoding));
    for (const auto bounds : {vsr::math::float2(-5.f, -1.f),
             vsr::math::float2(-1.f, 5.f),
             vsr::math::float2(1.f, 5.f)}) {
      INFO("depth range: " << bounds.x << ", " << bounds.y);
      ctx.offline.aov.depthMin = bounds.x;
      ctx.offline.aov.depthMax = bounds.y;
      vsr::core::DataTree tree;
      writeDump(ctx, tree);
      REQUIRE(tree.save((fixture.directory / "signed.data").c_str(), encoding));
      REQUIRE(fixture.run("signed.data --output signed.png") == 0);
      const auto saved = imagePixels(fixture.directory / "signed.png");
      REQUIRE(std::filesystem::remove(fixture.directory / "signed.png"));
      // Different saved bounds make explicit precedence observable, not just
      // two ways of supplying identical configuration.
      ctx.offline.aov.depthMin = 0.f;
      ctx.offline.aov.depthMax = 10.f;
      writeDump(ctx, tree);
      REQUIRE(tree.save((fixture.directory / "signed.data").c_str(), encoding));
      REQUIRE(fixture.run("signed.data --aov depth --depth-min "
                  + std::to_string(bounds.x) + " --depth-max "
                  + std::to_string(bounds.y) + " --output explicit-signed.png")
          == 0);
      REQUIRE(imagePixels(fixture.directory / "explicit-signed.png") == saved);
      REQUIRE(
          std::filesystem::remove(fixture.directory / "explicit-signed.png"));
    }
  }
}

TEST_CASE("Render CLI validates supplied saved AOV fields", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  for (const auto *field : {"aovType", "depthMin", "depthMax", "edgeInvert"}) {
    vsr::core::DataTree tree;
    writeDump(ctx, tree);
    tree.root()["offlineRendering"]["aov"][field] = "bad type";
    REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
    REQUIRE(fixture.run("bad.data --aov none") != 0);
    REQUIRE(pngFiles(fixture).empty());
  }
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  for (const auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    INFO("encoding: " << int(encoding));
    for (const auto bounds : {vsr::math::float2(1.f, 1.f),
             vsr::math::float2(2.f, 1.f),
             vsr::math::float2(nan, 5.f),
             vsr::math::float2(0.f, nan),
             vsr::math::float2(-inf, 5.f),
             vsr::math::float2(inf, 5.f),
             vsr::math::float2(0.f, -inf),
             vsr::math::float2(0.f, inf),
             vsr::math::float2(-3e38f, 3e38f)}) {
      INFO("depth range: " << bounds.x << ", " << bounds.y);
      vsr::core::DataTree tree;
      writeDump(ctx, tree);
      tree.root()["offlineRendering"]["aov"]["depthMin"] = bounds.x;
      tree.root()["offlineRendering"]["aov"]["depthMax"] = bounds.y;
      REQUIRE(tree.save((fixture.directory / "bad.data").c_str(), encoding));
      REQUIRE(fixture.run("bad.data --depth-min 0 --depth-max 5") != 0);
      REQUIRE(fixture.log().find("Invalid Application Dump AOV settings")
          != std::string::npos);
      REQUIRE(pngFiles(fixture).empty());
    }
  }
}

TEST_CASE("Render CLI lights are additive and entirely opt-in", "[RenderCLI]")
{
  CLIFixture fixture;
  const auto input = fixture.save(vsr::core::Encoding::Text);
  const auto args = input + " --width 32 --height 32 --samples 2";
  REQUIRE(fixture.run(args + " --output authored.png") == 0);
  REQUIRE(fixture.log().find("lights=1") != std::string::npos);
  checkImage(fixture.directory / "authored.png");
  REQUIRE(fixture.run(
              args + " --default-lights --default-lights --output defaults.png")
      == 0);
  REQUIRE(fixture.log().find("lights=2") != std::string::npos);
  REQUIRE(fixture.run(args
              + " --directional-light 0 0 -1 1 0 0 3"
                " --directional-light 0 0 -2 0 1 0 2 --output additions.png")
      == 0);
  REQUIRE(fixture.log().find("lights=3") != std::string::npos);
  REQUIRE(fixture.run(args
              + " --default-lights --directional-light 0 0 -1 1 1 1 0"
                " --output combined.png")
      == 0);
  REQUIRE(fixture.log().find("lights=3") != std::string::npos);
  REQUIRE(fixture.scene.objectDB().light.size() == 1);
  // CLI additions affect only the render, never the supplied native file.
  vsr::scene::Scene loaded;
  REQUIRE(vsr::io::load_SceneArchive(
      loaded, (fixture.directory / "native.data").c_str()));
  REQUIRE(loaded.objectDB().light.size() == 1);
  REQUIRE(loaded.getObject<vsr::scene::Light>(0)
              ->parameterValueAs<float>("irradiance")
              .value()
      == 3.f);
}

TEST_CASE("Render CLI preserves unlit native and foreign lighting intent",
    "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.scene.removeObject(
      fixture.scene.getObject<vsr::scene::Light>(0).data());
  writeImportedScene(fixture);
  std::ofstream(fixture.directory / "unlit.pbrt")
      << R"(LookAt 0 0 3  0 0 0  0 1 0
Camera "perspective" "float fov" [40]
WorldBegin
Material "diffuse" "rgb reflectance" [1 0 0]
Shape "sphere" "float radius" [0.5]
WorldEnd
)";
  const std::string small = " --width 32 --height 32 --samples 1";
  REQUIRE(fixture.run("unlit.pbrt" + small + " --output foreign.png") == 0);
  REQUIRE(fixture.log().find("lights=0") != std::string::npos);
  REQUIRE(fixture.run("unlit.pbrt" + small
              + " --default-lights --output foreign-lit.png")
      == 0);
  REQUIRE(fixture.log().find("lights=1") != std::string::npos);
  for (auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    const auto native = fixture.save(encoding);
    REQUIRE(
        fixture.run(native + small + " --output native.png --overwrite") == 0);
    REQUIRE(fixture.log().find("lights=0") != std::string::npos);
    REQUIRE(fixture.run("detail.obj " + native + small
                + " --output augmented.png --overwrite")
        == 0);
    REQUIRE(fixture.log().find("lights=0") != std::string::npos);
    vsr::app::Context ctx;
    prepareDump(fixture, ctx);
    vsr::core::DataTree tree;
    writeDump(ctx, tree);
    REQUIRE(tree.save((fixture.directory / "dump.data").c_str(), encoding));
    REQUIRE(fixture.run("dump.data --output dump.png --overwrite") == 0);
    REQUIRE(fixture.log().find("lights=0") != std::string::npos);
    REQUIRE(
        fixture.run(
            "dump.data detail.obj --default-lights --output dump-lit.png --overwrite")
        == 0);
    REQUIRE(fixture.log().find("lights=1") != std::string::npos);
  }
}

TEST_CASE("Render CLI directional light values affect the shared world",
    "[RenderCLI]")
{
  vsr::app::ANARIDeviceManager devices;
  auto device = devices.loadDevice("ospray");
  if (!device) {
    WARN(
        "SKIP directional-light render: optional CPU ospray device is unavailable");
    return;
  }
  anari::release(device, device);
  CLIFixture fixture;
  fixture.scene.removeObject(
      fixture.scene.getObject<vsr::scene::Light>(0).data());
  fixture.scene.getObject<vsr::scene::Material>(0)->setParameter(
      "color", vsr::math::float3(1.f));
  const auto args = fixture.save(vsr::core::Encoding::Binary)
      + " --device ospray --width 32 --height 32 --samples 4"
        " --background 0 0 0 1";
  REQUIRE(fixture.run(args + " --output dark.png") == 0);
  const auto dark = imagePixels(fixture.directory / "dark.png");
  REQUIRE(
      fixture.run(args + " --directional-light 0 0 -1 1 0 0 3 --output red.png")
      == 0);
  const auto red = imagePixels(fixture.directory / "red.png");
  REQUIRE(
      fixture.run(args
          + " --directional-light 0 0 -1 1 0 0 3 --directional-light 0 0 -1 0 1 0 3 --output both.png")
      == 0);
  const auto both = imagePixels(fixture.directory / "both.png");
  size_t redChanges = 0, greenChanges = 0;
  for (size_t i = 0; i < dark.size(); i += 4) {
    redChanges += red[i] > dark[i] + 20 && red[i] > red[i + 1] + 20;
    greenChanges += both[i + 1] > red[i + 1] + 20 && both[i] > 20;
  }
  REQUIRE(redChanges > 20);
  REQUIRE(greenChanges > 20);
  REQUIRE(fixture.run(args
              + " --directional-light 0 0 -3e38 1 0 0 3 --output scaled.png")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "scaled.png") == red);
  REQUIRE(fixture.run(
              args + " --directional-light 0 0 -1 1 0 0 0 --output zero.png")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "zero.png") == dark);
  REQUIRE(
      fixture.run(args + " --directional-light 0 0 1 1 0 0 3 --output away.png")
      == 0);
  const auto away = imagePixels(fixture.directory / "away.png");
  size_t directionChanges = 0;
  for (size_t i = 0; i < red.size(); i += 4)
    directionChanges += red[i] > away[i] + 20;
  REQUIRE(directionChanges > 20);
  REQUIRE(
      fixture.run(args + " --default-lights --output default-lit.png") == 0);
  const auto defaultLit = imagePixels(fixture.directory / "default-lit.png");
  size_t defaultChanges = 0;
  for (size_t i = 0; i < dark.size(); i += 4)
    defaultChanges += defaultLit[i] > dark[i] + 20;
  REQUIRE(defaultChanges > 20);
  REQUIRE(
      fixture.run(args
          + " --directional-light 0 0 -1e-30 1 0 0 3 --output tiny-direction.png")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "tiny-direction.png") == red);
}

TEST_CASE("Render CLI light additions preserve saved renderer settings",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.offline.renderer.rendererObjects.back().setParameter(
      "ambientRadiance", 0.f);
  ctx.offline.renderer.rendererObjects.back().setParameter(
      "ambientColor", vsr::math::float3(0.f, 0.f, 1.f));
  for (auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    vsr::core::DataTree tree;
    writeDump(ctx, tree);
    REQUIRE(tree.save((fixture.directory / "saved.data").c_str(), encoding));
    REQUIRE(fixture.run("saved.data --output saved.png --overwrite") == 0);
    REQUIRE(fixture.log().find("lights=1") != std::string::npos);
    const auto original = imagePixels(fixture.directory / "saved.png");
    REQUIRE(original[1] > 240);
    REQUIRE(
        fixture.run(
            "saved.data --default-lights --directional-light 0 0 -1 1 1 1 0 --output extra.png --overwrite")
        == 0);
    REQUIRE(fixture.log().find("lights=3") != std::string::npos);
    REQUIRE(imagePixels(fixture.directory / "extra.png")[1] > 240);
    REQUIRE(
        fixture.run(
            "saved.data --directional-light 0 0 -1 1 1 1 0 --output zero.png --overwrite")
        == 0);
    REQUIRE(imagePixels(fixture.directory / "zero.png") == original);
  }
}

TEST_CASE("Render CLI preserves emissive and HDRI lighting without injection",
    "[RenderCLI]")
{
  vsr::app::ANARIDeviceManager devices;
  auto device = devices.loadDevice("ospray");
  if (!device) {
    WARN(
        "SKIP emissive/HDRI render: optional CPU ospray device is unavailable");
    return;
  }
  anari::release(device, device);
  CLIFixture fixture;
  fixture.scene.removeObject(
      fixture.scene.getObject<vsr::scene::Light>(0).data());
  auto material = fixture.scene.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::physicallyBased);
  material->setParameter("baseColor", vsr::math::float3(0.f));
  material->setParameter("metallic", 0.f);
  material->setParameter("emissive", vsr::math::float3(2.f, 0.f, 0.f));
  fixture.scene.getObject<vsr::scene::Surface>(0)->setParameterObject(
      "material", *material);
  const std::string small =
      " --device ospray --width 32 --height 32 --samples 4 --background 0 0 0 1";
  REQUIRE(fixture.run(fixture.save(vsr::core::Encoding::Binary) + small
              + " --output emissive.png")
      == 0);
  REQUIRE(fixture.log().find("lights=0") != std::string::npos);
  const auto emissive = imagePixels(fixture.directory / "emissive.png");
  size_t glowing = 0;
  for (size_t i = 0; i < emissive.size(); i += 4)
    glowing += emissive[i] > 100 && emissive[i] > emissive[i + 1] + 50;
  REQUIRE(glowing > 20);
  REQUIRE(emissive[0] < 10); // meaningful emission, not a bright background
  material->setParameter("emissive", vsr::math::float3(0.f));
  REQUIRE(fixture.run(fixture.save(vsr::core::Encoding::Text) + small
              + " --output nonemissive.png")
      == 0);
  const auto dark = imagePixels(fixture.directory / "nonemissive.png");
  size_t changes = 0;
  for (size_t i = 0; i < dark.size(); i += 4)
    changes += emissive[i] > dark[i] + 50;
  REQUIRE(changes > 20);

  material->setParameter("baseColor", vsr::math::float3(1.f));
  auto radiance = fixture.scene.createArray(ANARI_FLOAT32_VEC3, 1, 1);
  const vsr::math::float3 green(0.f, 3.f, 0.f);
  radiance->setData(&green, 1);
  auto hdri = fixture.scene.createObject<vsr::scene::Light>(
      vsr::scene::tokens::light::hdri);
  hdri->setName("authored environment");
  hdri->setParameterObject("radiance", *radiance);
  fixture.scene.insertChildObjectNode(
      fixture.scene.defaultLayer()->root(), hdri);
  for (auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    const auto input = fixture.save(encoding);
    REQUIRE(fixture.run(input + small + " --output environment.png --overwrite")
        == 0);
    REQUIRE(fixture.log().find("lights=1") != std::string::npos);
    const auto environment = imagePixels(fixture.directory / "environment.png");
    size_t lit = 0;
    for (size_t i = 0; i < environment.size(); i += 4)
      lit += environment[i + 1] > environment[i] + 20;
    REQUIRE(lit > 20);
    REQUIRE(
        fixture.run(input + small
            + " --directional-light 0 0 -1 3 0 0 3 --output augmented-environment.png --overwrite")
        == 0);
    REQUIRE(fixture.log().find("lights=2") != std::string::npos);
    const auto added =
        imagePixels(fixture.directory / "augmented-environment.png");
    size_t both = 0;
    for (size_t i = 0; i < added.size(); i += 4)
      both += added[i] > 20 && added[i + 1] > 20;
    REQUIRE(both > 20); // authored environment remains alongside the addition
  }
}

TEST_CASE("Render CLI rejects malformed directional lights before rendering",
    "[RenderCLI]")
{
  CLIFixture fixture;
  const auto args = fixture.save(vsr::core::Encoding::Binary);
  for (const auto *operands : {"",
           "0 0 -1",
           "0 0 -1 1 1 1",
           "0 0 0 1 1 1 1",
           "nan 0 -1 1 1 1 1",
           "0 inf -1 1 1 1 1",
           "0 0 -1 1 -1 1 1",
           "0 0 -1 1 1 1 -1",
           "0 0 -1 1 1 1 1junk",
           "0 0 -1 1 1 1 1e100",
           "0 0 -1 1 1 1 1e-1000",
           "0 0 -1 nan 1 1 1",
           "0 0 -1 1 1 1 inf",
           "0 0 -1 1 1 1 --default-lights"}) {
    REQUIRE(fixture.run(args + " --directional-light " + operands) != 0);
    REQUIRE(fixture.log().find("--directional-light") != std::string::npos);
    REQUIRE(pngFiles(fixture).empty());
  }
  REQUIRE(
      fixture.run(args
          + " --directional-light 0 0 0 1 1 1 1 --directional-light 0 0 -1 1 1 1 1")
      != 0);
  REQUIRE(pngFiles(fixture).empty());
  REQUIRE(fixture.run("--help") == 0);
  REQUIRE(fixture.log().find("--directional-light") != std::string::npos);
  REQUIRE(fixture.log().find("--default-lights") != std::string::npos);
}

TEST_CASE("Render CLI imports an authored foreign camera through the engine",
    "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  writeImportedScene(fixture);
  REQUIRE(fixture.run("imported.pbrt --width 32 --height 32 --samples 2") == 0);
  const auto pixels = imagePixels(fixture.directory / "vsrRender.png");
  size_t red = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    red += pixels[i] > pixels[i + 1] + 20;
  REQUIRE(red > 40);
  REQUIRE(fixture.log().find("cameras=1 animations=0") != std::string::npos);
  REQUIRE(fixture.log().find("lights=1") != std::string::npos);
  REQUIRE(fixture.log().find("frame=0") != std::string::npos);
  REQUIRE(fixture.run("detail.obj --width 32 --height 32 --samples 1") != 0);
  REQUIRE(fixture.log().find("cameras=0") != std::string::npos);
  REQUIRE(fixture.log().find("lights=0") != std::string::npos);
  REQUIRE(fixture.log().find("No scene Camera") != std::string::npos);
  std::ofstream(fixture.directory / "camera-less.pbrt") << R"(WorldBegin
Material "diffuse" "rgb reflectance" [1 0 0]
Shape "sphere" "float radius" [0.5]
WorldEnd
)";
  REQUIRE(
      fixture.run("camera-less.pbrt --width 32 --height 32 --samples 1") != 0);
  REQUIRE(fixture.log().find("cameras=0") != std::string::npos);
  REQUIRE(fixture.log().find("lights=0") != std::string::npos);
  REQUIRE(fixture.log().find("No scene Camera") != std::string::npos);
  const auto base = fixture.save(vsr::core::Encoding::Binary);
  REQUIRE(
      fixture.run(base + " camera-less.pbrt --width 32 --height 32 --samples 1")
      == 0);
  REQUIRE(fixture.log().find("cameras=1") != std::string::npos);
  REQUIRE(fixture.log().find("lights=1") != std::string::npos);
}

TEST_CASE("Render CLI augments native bases before ordered layered imports",
    "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  writeImportedScene(fixture);
  fixture.scene.addLayer("hidden");
  fixture.scene.setLayerActive("hidden", false);
  const auto base = fixture.save(vsr::core::Encoding::Text);
  const std::string small = " --width 32 --height 32 --samples 2";
  REQUIRE(fixture.run(base + small + " --output baseline.png") == 0);
  REQUIRE(fixture.run("--layer hidden detail.obj " + base + small
              + " --output hidden.png")
      == 0);
  checkImage(fixture.directory / "hidden.png");
  REQUIRE(fixture.log().find("import=detail.obj layer=hidden")
      != std::string::npos);
  REQUIRE(fixture.log().find("surfaces=2") != std::string::npos);
  REQUIRE(fixture.run(base + " --layer visible detail.obj" + small
              + " --output active.png")
      == 0);
  auto baseline = imagePixels(fixture.directory / "baseline.png");
  auto hidden = imagePixels(fixture.directory / "hidden.png");
  auto active = imagePixels(fixture.directory / "active.png");
  size_t hiddenChanges = 0, activeChanges = 0;
  for (size_t i = 0; i < baseline.size(); i += 4) {
    hiddenChanges += std::abs(int(baseline[i]) - int(hidden[i])) > 20;
    activeChanges += std::abs(int(baseline[i]) - int(active[i])) > 20;
  }
  REQUIRE(hiddenChanges < 10);
  REQUIRE(activeChanges > 30);
  REQUIRE(fixture.run("--layer visible detail.obj " + base + small
              + " --output reordered.png")
      == 0);
  const auto reordered = imagePixels(fixture.directory / "reordered.png");
  size_t reorderedChanges = 0;
  for (size_t i = 0; i < active.size(); i += 4)
    reorderedChanges += std::abs(int(active[i]) - int(reordered[i])) > 20;
  REQUIRE(reorderedChanges < 10);
  REQUIRE(fixture.run(base
              + " --importer obj detail.obj --importer auto imported.pbrt"
              + small + " --camera 'authored view' --output auto.png")
      == 0);
  REQUIRE(fixture.log().find("cameras=2") != std::string::npos);
  REQUIRE(fixture.run("--layer first detail.obj " + base
              + " --layer second detail.obj" + small + " --output ordered.png")
      == 0);
  const auto log = fixture.log();
  REQUIRE(log.find("layer=first") < log.find("layer=second"));
  REQUIRE(log.find("surfaces=3") != std::string::npos);
  REQUIRE(fixture.run("detail.obj " + base + " " + base) != 0);
  REQUIRE(fixture.log().find("Multiple native bases") != std::string::npos);

  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree dump;
  writeDump(ctx, dump);
  REQUIRE(dump.save((fixture.directory / "base.dump").c_str()));
  REQUIRE(fixture.run(base + " detail.obj base.dump") != 0);
  REQUIRE(fixture.log().find("Multiple native bases") != std::string::npos);
  for (bool baseFirst : {false, true}) {
    const auto args =
        baseFirst ? "base.dump detail.obj" : "detail.obj base.dump";
    REQUIRE(fixture.run(std::string(args) + " --output augmented.png") == 0);
    REQUIRE(
        fixture.log().find("dimensions=32x32 samples=2") != std::string::npos);
    REQUIRE(fixture.log().find("surfaces=2") != std::string::npos);
    REQUIRE(fixture.run(std::string(args)
                + " --width 16 --samples 1 --output explicit.png")
        == 0);
    checkDimensions(fixture.directory / "explicit.png", 16, 32);
  }
}

TEST_CASE(
    "Render CLI retains grouped imported animation bindings", "[RenderCLI]")
{
  CLIFixture fixture;
  const auto base = fixture.save(vsr::core::Encoding::Binary);
  for (int frame : {0, 1}) {
    std::ofstream file(
        fixture.directory / ("points" + std::to_string(frame) + ".bin"),
        std::ios::binary);
    const size_t count = 1;
    const vsr::math::float3 position(float(frame) * 0.05f, -0.5f, 0.f);
    const float scalar = float(frame);
    file.write(reinterpret_cast<const char *>(&count), sizeof(count));
    file.write(reinterpret_cast<const char *>(&position), sizeof(position));
    file.write(reinterpret_cast<const char *>(&count), sizeof(count));
    file.write(reinterpret_cast<const char *>(&count), sizeof(count));
    file.write(reinterpret_cast<const char *>(&scalar), sizeof(scalar));
  }
  REQUIRE(fixture.run("--layer motion --importer pointsbin points0.bin " + base
              + " points1.bin --width 32 --height 32 --samples 2")
      == 0);
  checkImage(fixture.directory / "vsrRender.png");
  REQUIRE(fixture.log().find("surfaces=2") != std::string::npos);
  REQUIRE(fixture.log().find("cameras=1 animations=1") != std::string::npos);
  REQUIRE(fixture.log().find("frame=0") != std::string::npos);
  size_t images = 0;
  for (const auto &entry :
      std::filesystem::directory_iterator(fixture.directory))
    images += entry.path().extension() == ".png";
  REQUIRE(images == 1);
  // Frame the imported points tightly: their fixed 0.01 radius must cover
  // several pixels, and both positions must stay visible at this resolution.
  REQUIRE(
      fixture.run(base
          + " --layer motion --importer pointsbin points0.bin points1.bin"
            " --animation --frames 0:99:99 --width 32 --height 32 --samples 2"
            " --camera-height 0.1 --camera-position 0.025 -0.5 3"
            " --output 'points_{frame:04}.png'")
      == 0);
  const auto pointsFirst = imagePixels(fixture.directory / "points_0000.png");
  const auto pointsLast = imagePixels(fixture.directory / "points_0099.png");
  size_t firstVisible = 0, lastVisible = 0, changed = 0;
  double firstX = 0., lastX = 0.;
  for (size_t i = 0; i < pointsFirst.size(); i += 4) {
    if (std::max({pointsFirst[i], pointsFirst[i + 1], pointsFirst[i + 2]})
        > 20) {
      ++firstVisible;
      firstX += (i / 4) % 32;
    }
    if (std::max({pointsLast[i], pointsLast[i + 1], pointsLast[i + 2]}) > 20) {
      ++lastVisible;
      lastX += (i / 4) % 32;
    }
    changed += std::abs(int(pointsFirst[i]) - int(pointsLast[i])) > 20
        || std::abs(int(pointsFirst[i + 1]) - int(pointsLast[i + 1])) > 20
        || std::abs(int(pointsFirst[i + 2]) - int(pointsLast[i + 2])) > 20;
  }
  REQUIRE(firstVisible >= 16);
  REQUIRE(lastVisible >= 16);
  // Check spatial motion, not just the animated scalar's color change.
  REQUIRE(lastX / lastVisible - firstX / firstVisible > 10.);
  REQUIRE(changed >= 32);
  REQUIRE(
      fixture.run(base + " --importer pointsbin points0.bin missing.bin") != 0);
  REQUIRE(fixture.log().find("Input file not found: missing.bin")
      != std::string::npos);
  for (int frame : {0, 1}) {
    std::ofstream file(fixture.directory
            / ("density" + std::to_string(frame) + "_2x2x2_float32.raw"),
        std::ios::binary);
    const float voxels[] = {0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f};
    file.write(reinterpret_cast<const char *>(voxels), sizeof(voxels));
  }
  REQUIRE(fixture.run(base
              + " --importer volume-animation density0_2x2x2_float32.raw"
                " density1_2x2x2_float32.raw --width 32 --height 32 --samples 1"
                " --output volume.png")
      == 0);
  checkDimensions(fixture.directory / "volume.png", 32, 32);
  REQUIRE(fixture.log().find("volumes=1") != std::string::npos);
  REQUIRE(fixture.log().find("cameras=1 animations=1") != std::string::npos);
}

TEST_CASE(
    "Render CLI reports missing unsupported and failed imports", "[RenderCLI]")
{
  CLIFixture fixture;
  writeImportedScene(fixture);
  const auto base = fixture.save(vsr::core::Encoding::Binary);
  for (const auto *args : {"--importer",
           "--layer",
           "--vtu-property",
           "--importer invented",
           "--importer invented detail.obj",
           "--layer 'invalid;layer' detail.obj",
           "missing.obj",
           "--importer gltf missing.glb"}) {
    REQUIRE(fixture.run(base + " " + args) != 0);
  }
  std::ofstream(fixture.directory / "broken.gltf") << "invalid gltf";
  REQUIRE(fixture.run(base + " broken.gltf") != 0);
  REQUIRE(
      fixture.log().find("Import failed for broken.gltf") != std::string::npos);
  std::ofstream(fixture.directory / "empty.obj") << "# no shapes\n";
  REQUIRE(fixture.run(base + " empty.obj") != 0);
  REQUIRE(fixture.log().find("no scene content") != std::string::npos);
  std::ofstream(fixture.directory / "unavailable.usda") << "#usda 1.0\n";
#if !VSR_USE_USD
  REQUIRE(fixture.run(base + " unavailable.usda") != 0);
  REQUIRE(fixture.log().find("USD not enabled") != std::string::npos);
#endif
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));
}

TEST_CASE("Render CLI still rejects warning-only imports without content",
    "[RenderCLI]")
{
  CLIFixture fixture;
  const auto base = fixture.save(vsr::core::Encoding::Binary);
  std::ofstream(fixture.directory / "empty.pbrt") << R"(WorldBegin
Shape "unsupported"
WorldEnd
)";
  REQUIRE(fixture.run(base + " empty.pbrt") != 0);
  REQUIRE(fixture.log().find("unsupported shape type: unsupported")
      != std::string::npos);
  REQUIRE(fixture.log().find("Import produced no scene content: empty.pbrt")
      != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));
}

TEST_CASE("Render CLI retains transfer-function content exceptions and errors",
    "[RenderCLI]")
{
  CLIFixture fixture;
  const auto base = fixture.save(vsr::core::Encoding::Binary);
  const auto args = base + " --width 32 --height 32 --samples 2 --importer xf ";
  std::ofstream(fixture.directory / "valid.1dt")
      << "0 0 0 0\nnot RGBA\n1 1 1 1\n";
  REQUIRE(fixture.run(args + "valid.1dt --output valid.png") == 0);
  REQUIRE(fixture.log().find("Failed to parse line, skipping")
      != std::string::npos);
  REQUIRE(fixture.log().find("surfaces=1 volumes=0 lights=1 cameras=1")
      != std::string::npos);
  checkImage(fixture.directory / "valid.png");

  std::ofstream(fixture.directory / "empty.1dt") << "# no points\n";
  REQUIRE(fixture.run(args + "empty.1dt --output failed.png") != 0);
  REQUIRE(fixture.log().find("No valid RGBA entries") != std::string::npos);
  // This importer reports an error but still returns usable color/opacity
  // points; content alone must not mask the error.
  std::ofstream(fixture.directory / "broken.json")
      << R"({"RGBPoints":[0,0,0,0,1,1,1,1],"Points":[0,0,2,1]})";
  REQUIRE(fixture.run(args + "broken.json --output failed.png") != 0);
  REQUIRE(
      fixture.log().find("Import failed for broken.json") != std::string::npos);
  REQUIRE(fixture.log().find("Invalid Points data") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "failed.png"));
}

TEST_CASE(
    "Render CLI reports importer exceptions without rendering", "[RenderCLI]")
{
  CLIFixture fixture;
  const auto base = fixture.save(vsr::core::Encoding::Binary);
  std::ofstream(fixture.directory / "broken.case")
      << "FORMAT\ntype: ensight gold\nTIME\nnumber of steps: invalid\n";
  REQUIRE(fixture.run(base + " --importer ensight broken.case") != 0);
  REQUIRE(fixture.log().find("stoi") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));
}

TEST_CASE("Render CLI renders supported conversions with visible warnings",
    "[RenderCLI]")
{
  CLIFixture fixture;
  writeImportedScene(fixture);
  std::ifstream input(fixture.directory / "imported.pbrt");
  std::ostringstream text;
  text << input.rdbuf();
  auto approximation = text.str();
  const auto camera = approximation.find("Camera \"perspective\"");
  REQUIRE(camera != std::string::npos);
  approximation.replace(camera,
      std::string("Camera \"perspective\"").size(),
      "Camera \"spherical\"");
  std::ofstream(fixture.directory / "warned.pbrt") << approximation;
  REQUIRE(fixture.run("warned.pbrt --device helide --width 32 --height 32"
                      " --samples 2 --output warned.png")
      == 0);
  REQUIRE(fixture.log().find("'spherical' camera approximated as perspective")
      != std::string::npos);
  REQUIRE(fixture.log().find("surfaces=1 volumes=0 lights=1 cameras=1")
      != std::string::npos);
  const auto pixels = imagePixels(fixture.directory / "warned.png");
  size_t red = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    red += pixels[i] > pixels[i + 1] + 20;
  // The spherical approximation uses a wider FOV than the perspective fixture.
  REQUIRE(red > 10);
}

TEST_CASE("Render CLI explicitly generates and refines Cameras",
    "[RenderCLI][RenderDiagnostics]")
{
  CLIFixture fixture;
  SECTION("Generation replaces selection despite existing Cameras")
  {
    fixture.scene.createObject<vsr::scene::Camera>("orthographic")
        ->setName("other");
  }
  SECTION("Generation accepts camera-less input")
  {
    fixture.scene.removeObject(
        fixture.scene.getObject<vsr::scene::Camera>(0).data());
  }
  SECTION("Generation accepts camera-less foreign import")
  {
    writeImportedScene(fixture);
    REQUIRE(
        fixture.run(
            "detail.obj --default-camera --width 32 --height 32 --samples 2")
        == 0);
    imagePixels(fixture.directory / "vsrRender.png");
    return;
  }
  const auto input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(
              input + " --default-camera --width 32 --height 32 --samples 2")
      == 0);
  REQUIRE(fixture.log().find("camera=generated 'generated default Camera' "
                             "(perspective)")
      != std::string::npos);
  REQUIRE(fixture.log().find("camera=" + std::to_string(VSR_INVALID_INDEX))
      == std::string::npos);
  const auto pixels = imagePixels(fixture.directory / "vsrRender.png");
  size_t red = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    red += pixels[i] > pixels[i + 1] + 20;
  REQUIRE(red > 40);
  REQUIRE(fixture.run(input + " --default-camera --camera 0") != 0);
  REQUIRE(fixture.log().find("cannot be combined") != std::string::npos);
}

TEST_CASE("Render CLI validates subtype-aware camera overrides", "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  const std::string quality = " --width 32 --height 32 --samples 2";
  REQUIRE(
      fixture.run(input + quality
          + " --camera-index 0 --camera-position 0 0 9"
            " --camera-look-at 0 0 0 --camera-up 0 1 0 --camera-height 1 --camera-height 2")
      == 0);
  REQUIRE(fixture.log().find("orthographic") != std::string::npos);
  checkImage(fixture.directory / "vsrRender.png");
  REQUIRE(fixture.run(input + quality
              + " --default-camera --camera-position 0 0 3"
                " --camera-look-at 0 0 0 --camera-fovy 40")
      == 0);
  imagePixels(fixture.directory / "vsrRender.png");
  for (const auto *arguments : {"--camera-fovy 40",
           "--default-camera --camera-height 2",
           "--camera-position 0 0",
           "--camera-up 0 0 0",
           "--camera-up 0 0 1",
           "--camera-position 0 0 0 --camera-look-at 0 0 0",
           "--camera-position nan 0 3",
           "--camera-position 1e100 0 3",
           "--camera-fovy 180",
           "--camera-fovy 0",
           "--camera-focus 0",
           "--camera-aperture -1",
           "--camera-height 0",
           "--camera-height 2junk"}) {
    INFO(arguments);
    REQUIRE(fixture.run(input + quality + " " + arguments) != 0);
  }
}

TEST_CASE(
    "Render CLI fits generated Cameras at the saved still frame", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.vsr.animationMgr.setAnimationTotalFrames(5);
  auto geometry = ctx.vsr.scene.getObject<vsr::scene::Geometry>(0);
  const float times[] = {0.f, 1.f};
  const float radii[] = {0.25f, 10.f};
  ctx.vsr.animationMgr.addAnimation("expanding bounds")
      .addObjectParameterBinding(
          geometry.data(), "radius", ANARI_FLOAT32, radii, times, 2);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  tree.root()["archives"]["animationManager"]["time"] = 1.f;
  const auto path = (fixture.directory / "generated-dump.data").string();
  REQUIRE(tree.save(path.c_str(), vsr::core::Encoding::Text));
  REQUIRE(fixture.run(quote(path) + " --default-camera") == 0);
  REQUIRE(fixture.log().find("frame=4") != std::string::npos);
  const auto pixels = imagePixels(fixture.directory / "vsrRender.png");
  size_t red = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    red += pixels[i] > pixels[i + 1] + 20;
  REQUIRE(red > 40);
  REQUIRE(red < 800);
}

TEST_CASE("Render CLI camera controls override only supplied saved fields",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  // Legacy offline lens settings are inert, but an explicit request still
  // fails support validation for helide orthographic.
  tree.root()["offlineRendering"]["camera"]["focusDistance"] = 4.f;
  const auto path = (fixture.directory / "camera-dump.data").string();
  REQUIRE(tree.save(path.c_str()));
  REQUIRE(fixture.run(quote(path) + " --camera-focus 3") != 0);
  REQUIRE(fixture.log().find("Unsupported override") != std::string::npos);
  tree.root()["offlineRendering"].remove("camera");
  REQUIRE(tree.save(path.c_str()));
  REQUIRE(fixture.run(quote(path) + " --camera-height 2") == 0);
  checkImage(fixture.directory / "vsrRender.png");
}

TEST_CASE("Render CLI resolves devices without fallback", "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  const auto args = input + " --width 32 --height 32 --samples 1";
  REQUIRE(fixture.run(args) == 0);
  REQUIRE(fixture.log().find("device=helide") != std::string::npos);
  REQUIRE(fixture.run(args, "ANARI_LIBRARY=helide") == 0);
  REQUIRE(fixture.log().find("device=environment") != std::string::npos);
  REQUIRE(fixture.run(args,
              "VSR_ANARI_LIBRARIES=default@helide,bogus ANARI_LIBRARY=bogus")
      == 0);
  REQUIRE(fixture.log().find("device=helide") != std::string::npos);
  REQUIRE(fixture.run(args + " --device default@helide",
              "VSR_ANARI_LIBRARIES=bogus ANARI_LIBRARY=bogus")
      == 0);
  checkImage(fixture.directory / "vsrRender.png");
  REQUIRE(fixture.run(args + " --device @helide") == 0);
  REQUIRE(fixture.log().find("device=helide") != std::string::npos);
  REQUIRE(
      fixture.run(args, "VSR_ANARI_LIBRARIES=,, ANARI_LIBRARY=helide") == 0);
  REQUIRE(fixture.log().find("device=environment") != std::string::npos);
  const std::pair<const char *, const char *> failures[] = {
      {"bogus", "Cannot load selected ANARI library: bogus"},
      {"bogus@helide", "Unsupported device subtype: bogus"},
      {"default@", "Invalid Device Identifier"},
      {"x@y@helide", "Invalid Device Identifier"},
      {"{none}", "Invalid Device Identifier"}};
  for (const auto &[id, diagnostic] : failures) {
    REQUIRE(
        fixture.run(args + " --device " + quote(id) + " --output rejected.png",
            "VSR_ANARI_LIBRARIES=helide ANARI_LIBRARY=helide")
        != 0);
    REQUIRE(fixture.log().find(diagnostic) != std::string::npos);
    REQUIRE(
        fixture.log().find("trying the other devices") == std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "rejected.png"));
  }
  REQUIRE(
      fixture.run(args, "VSR_ANARI_LIBRARIES=bogus,helide ANARI_LIBRARY=helide")
      != 0);
  REQUIRE(fixture.log().find("bogus") != std::string::npos);
  REQUIRE(fixture.run(args, "ANARI_LIBRARY=bogus") != 0);
  REQUIRE(fixture.log().find("environment") != std::string::npos);
}

TEST_CASE("Render CLI preserves compatible saved renderers and resets devices",
    "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str()));
  REQUIRE(
      fixture.run("saved.data", "VSR_ANARI_LIBRARIES=bogus ANARI_LIBRARY=bogus")
      == 0);
  REQUIRE(fixture.log().find("device=helide") != std::string::npos);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  REQUIRE(fixture.run("saved.data --device default@helide --renderer default")
      == 0);
  REQUIRE(fixture.log().find("Renderer reset") == std::string::npos);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  REQUIRE(fixture.run("saved.data --device environment", "ANARI_LIBRARY=helide")
      == 0);
  REQUIRE(fixture.log().find("Renderer reset: device changed")
      != std::string::npos);
  const auto reset = imagePixels(fixture.directory / "vsrRender.png");
  REQUIRE(reset[1] < 10);
  REQUIRE(
      fixture.run(
          "saved.data --device environment --renderer-param background=0,0,1,1",
          "ANARI_LIBRARY=helide")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[2] > 240);
  REQUIRE(
      fixture.run("saved.data --renderer-param eyeLightBlendRatio=0.25") == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  ctx.offline.renderer.rendererObjects[0]
      .parameter("background")
      ->setEnabled(false);
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str()));
  REQUIRE(fixture.run("saved.data") == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] < 10);
  REQUIRE(fixture.run("saved.data --background 0 1 0 1") == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  REQUIRE(fixture.run("saved.data --renderer invented") != 0);
  REQUIRE(
      fixture.log().find("Unsupported renderer subtype") != std::string::npos);
  ctx.offline.renderer.rendererObjects.clear();
  ctx.offline.renderer.rendererObjects.emplace_back(
      ANARI_RENDERER, "unavailable");
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str()));
  REQUIRE(fixture.run("saved.data") != 0);
  REQUIRE(
      fixture.log().find("Unsupported renderer subtype") != std::string::npos);
  REQUIRE(fixture.run("saved.data --renderer default") == 0);
  REQUIRE(fixture.log().find("Renderer reset: renderer subtype changed")
      != std::string::npos);
}

TEST_CASE("Render CLI preserves saved renderer array references", "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  auto background = ctx.vsr.scene.createArray(ANARI_FLOAT32_VEC4, 2, 2);
  const vsr::math::float4 blue[] = {{0.f, 0.f, 1.f, 1.f},
      {0.f, 0.f, 1.f, 1.f},
      {0.f, 0.f, 1.f, 1.f},
      {0.f, 0.f, 1.f, 1.f}};
  background->setData(blue, 4);
  ctx.offline.renderer.rendererObjects[0].setParameterObject(
      "background", *background);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str()));
  REQUIRE(fixture.run("saved.data --device default@helide") == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[2] > 240);
  REQUIRE(fixture.run("saved.data --background 0 1 0 1") == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  REQUIRE(fixture.run("saved.data --device environment", "ANARI_LIBRARY=helide")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[2] < 10);
  ctx.offline.renderer.rendererObjects[0].setParameter(
      "background", vsr::core::Any(ANARI_ARRAY2D, size_t(999)));
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str()));
  REQUIRE(fixture.run("saved.data --output invalid.png") != 0);
  REQUIRE(
      fixture.log().find("Invalid saved renderer object reference: background")
      != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "invalid.png"));
  REQUIRE(fixture.run("saved.data --background 0 1 0 1") == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
}

TEST_CASE(
    "Render CLI resets between available renderer subtypes", "[RenderCLI]")
{
  vsr::app::ANARIDeviceManager devices;
  auto device = devices.loadDevice("ospray");
  if (!device) {
    WARN(
        "SKIP renderer-subtype render: optional CPU ospray device is unavailable");
    return;
  }
  const auto subtypes =
      vsr::scene::getANARIObjectSubtypes(device, ANARI_RENDERER);
  anari::release(device, device);
  if (std::find(subtypes.begin(), subtypes.end(), "scivis") == subtypes.end()) {
    WARN("SKIP renderer-subtype render: ospray does not advertise scivis");
    return;
  }
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.offline.renderer.libraryName = "default@ospray";
  ctx.offline.renderer.rendererObjects.emplace_back(ANARI_RENDERER, "scivis");
  ctx.offline.renderer.rendererObjects.back().setName("active scivis");
  ctx.offline.renderer.rendererObjects.back().setParameter(
      "background", vsr::math::float4(0.f, 1.f, 0.f, 1.f));
  ctx.offline.renderer.rendererObjects.back().setParameter("aoSamples", 0);
  ctx.offline.renderer.activeRenderer = 1;
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str()));
  REQUIRE(
      fixture.run("saved.data", "VSR_ANARI_LIBRARIES=bogus ANARI_LIBRARY=bogus")
      == 0);
  REQUIRE(fixture.log().find("renderer=scivis") != std::string::npos);
  REQUIRE(fixture.log().find("Renderer reset") == std::string::npos);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  REQUIRE(fixture.run("saved.data --device ospray --renderer scivis") == 0);
  REQUIRE(fixture.log().find("Renderer reset") == std::string::npos);
  REQUIRE(fixture.log().find("renderer=scivis") != std::string::npos);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  REQUIRE(
      fixture.run(
          "saved.data --renderer default --renderer-param denoise=false --renderer-param 'name=a renderer with spaces=and equals'")
      == 0);
  REQUIRE(fixture.log().find("Renderer reset: renderer subtype changed")
      != std::string::npos);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] < 10);
  REQUIRE(
      fixture.run(
          "saved.data --renderer default --renderer-param background=0,0,1,1 --renderer-param denoise=false")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[2] > 240);
  REQUIRE(
      fixture.run(
          "saved.data --device environment --renderer scivis --background 0 0 1 1",
          "ANARI_LIBRARY=ospray")
      == 0);
  REQUIRE(fixture.log().find("Renderer reset: device changed")
      != std::string::npos);
  REQUIRE(fixture.log().find("renderer=scivis") != std::string::npos);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[2] > 240);
  REQUIRE(fixture.run("saved.data --device helide") == 0);
  REQUIRE(fixture.log().find("renderer=default") != std::string::npos);
  REQUIRE(fixture.log().find("Renderer reset: device changed")
      != std::string::npos);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] < 10);
}

TEST_CASE("Render CLI converts renderer overrides through real metadata",
    "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  const auto args = input
      + " --device helide --renderer default --width 32 --height 32 --samples 1";
  REQUIRE(
      fixture.run(args
          + " --renderer-param background=0,0,1,1 --renderer-param mode=hitSurface"
            " --renderer-param ignoreAmbientLighting=false --renderer-param ambientRadiance=0"
            " --renderer-param eyeLightBlendRatio=0.75 --renderer-param taskGrainSizeWidth=8")
      == 0);
  const auto pixels = imagePixels(fixture.directory / "vsrRender.png");
  REQUIRE(pixels[2] > 240);
  size_t green = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    green += pixels[i + 1] > 200 && pixels[i] < 10;
  REQUIRE(green > 20); // named string mode, not saved/default shading
  REQUIRE(fixture.run(
              args + " --renderer-param ignoreAmbientLighting=0 --ambient 0")
      == 0);
  const auto dark = imagePixels(fixture.directory / "vsrRender.png");
  REQUIRE(fixture.run(
              args + " --renderer-param ignoreAmbientLighting=true --ambient 0")
      == 0);
  const auto bright = imagePixels(fixture.directory / "vsrRender.png");
  size_t changed = 0;
  for (size_t i = 0; i < bright.size(); i += 4)
    changed += bright[i] > dark[i] + 20;
  REQUIRE(changed > 20); // false/zero are present values, not absence
  for (const auto *override : {"missing=1",
           "=1",
           "missing",
           "background=0,1,0",
           "background=0,1,0,1,2",
           "background=0,,0,1",
           "background=array:foo",
           "background=nan,0,0,1",
           "background=1e100,0,0,1",
           "eyeLightBlendRatio=1.1",
           "eyeLightBlendRatio=-0.1",
           "eyeLightBlendRatio=1junk",
           "eyeLightBlendRatio= 0.5",
           "eyeLightBlendRatio=1e-1000",
           "eyeLightBlendRatio=inf",
           "volumeSamplingRate=0",
           "taskGrainSizeWidth=0",
           "taskGrainSizeWidth=129",
           "taskGrainSizeWidth=1.5",
           "taskGrainSizeWidth=2147483648",
           "mode=not-a-mode",
           "ignoreAmbientLighting=yes",
           "ignoreAmbientLighting=2"}) {
    REQUIRE(fixture.run(args + " --renderer-param " + quote(override)) != 0);
    REQUIRE(fixture.log().find("renderer") != std::string::npos);
  }
  for (const auto *flag : {"--device",
           "--renderer",
           "--renderer-param",
           "--background 0 1 0",
           "--ambient",
           "--ambient-color 0 1"})
    REQUIRE(fixture.run(args + " " + flag) != 0);
}

TEST_CASE("Render CLI renderer shorthand shares last-assignment precedence",
    "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  const auto args = fixture.save(vsr::core::Encoding::Text)
      + " --device helide --width 32 --height 32 --samples 1";
  REQUIRE(
      fixture.run(args
          + " --background 0 1 0 1 --ambient 0.5 --ambient-color 1 1 1 --output shorthand.png")
      == 0);
  REQUIRE(
      fixture.run(args
          + " --renderer-param background=0,1,0,1 --renderer-param ambientRadiance=0.5 --renderer-param ambientColor=1,1,1 --output general.png")
      == 0);
  const auto shorthand = imagePixels(fixture.directory / "shorthand.png");
  const auto general = imagePixels(fixture.directory / "general.png");
  REQUIRE(shorthand == general);
  REQUIRE(fixture.run(args
              + " --renderer-param background=1,0,0,1 --background 0 1 0 1")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[1] > 240);
  REQUIRE(fixture.run(args
              + " --background 0 1 0 1 --renderer-param background=0,0,1,1")
      == 0);
  REQUIRE(imagePixels(fixture.directory / "vsrRender.png")[2] > 240);
  REQUIRE(
      fixture.run(args
          + " --renderer-param background=1,0,0,1 --renderer-param background=0,1,0,1 --renderer-param ignoreAmbientLighting=false --ambient 1 --renderer-param ambientRadiance=0")
      == 0);
  const auto dark = imagePixels(fixture.directory / "vsrRender.png");
  REQUIRE(dark[1] > 240);
  REQUIRE(
      fixture.run(args
          + " --renderer-param ignoreAmbientLighting=false --renderer-param ambientRadiance=0 --ambient 1")
      == 0);
  const auto bright = imagePixels(fixture.directory / "vsrRender.png");
  size_t changed = 0;
  for (size_t i = 0; i < bright.size(); i += 4)
    changed += bright[i] > dark[i] + 20;
  REQUIRE(changed > 20);
  REQUIRE(
      fixture.run(args
          + " --renderer-param mode=not-a-mode --renderer-param mode=default")
      != 0);
}

TEST_CASE("Render CLI marks bounded diagnostics without losing context",
    "[RenderCLI][RenderDiagnostics]")
{
  CLIFixture fixture;
  for (size_t length : {4094, 4095, 4096, 8192}) {
    const std::string context = "Unknown option: ";
    const auto prefix = context + "--serial-diagnostic-";
    const std::string tail = "-end-of-diagnostic";
    const auto diagnostic =
        prefix + std::string(length - prefix.size() - tail.size(), 'x') + tail;
    REQUIRE(fixture.run(quote(diagnostic.substr(context.size()))) == 256);
    const auto log = fixture.log();
    REQUIRE(log.find("vsrRender: " + prefix) == 0);
    REQUIRE(log.find('\0') == std::string::npos);
    REQUIRE(log.back() == '\n');
    if (length <= 4095) {
      REQUIRE(log == "vsrRender: " + diagnostic + "\n");
      REQUIRE(log.find("[truncated]") == std::string::npos);
    } else {
      REQUIRE(log.size() <= 11 + 4095 + 1);
      REQUIRE(log.find("-end-of-diagnostic") == std::string::npos);
      REQUIRE(log.substr(log.size() - 13) == " [truncated]\n");
    }
  }
}

TEST_CASE("Render CLI help points to the canonical policy reference",
    "[RenderCLI][RenderHelp]")
{
  CLIFixture fixture;
  REQUIRE(fixture.run("--help") == 0);
  const auto help = fixture.log();
  REQUIRE(help.find("CLI reference: src/apps/tools/README.md")
      != std::string::npos);
  REQUIRE(help.find("global first selected frame") != std::string::npos);
  REQUIRE(help.find("Camera-less input: vsrRender model.obj --default-camera")
      != std::string::npos);
  REQUIRE(help.find("later animated motion may leave its view")
      != std::string::npos);
  REQUIRE(help.find("never try a fallback") != std::string::npos);
  REQUIRE(help.find("mpiexec -n 2 vsrRender saved.vsr --animation")
      != std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
}

TEST_CASE("Render CLI strictly validates arguments", "[RenderCLI]")
{
  CLIFixture fixture;
  REQUIRE(fixture.run("--help") == 0);
  REQUIRE(fixture.log().find("Scene Archive") != std::string::npos);
  REQUIRE(fixture.log().find("--camera NAME") != std::string::npos);
  REQUIRE(fixture.log().find("--camera-index N") != std::string::npos);
  REQUIRE(fixture.log().find("Importer warnings are advisory")
      != std::string::npos);
  REQUIRE(fixture.log().find("errors/no-content imports fail")
      != std::string::npos);
  REQUIRE(
      fixture.log().find("errors/warnings fail closed") == std::string::npos);
  REQUIRE(fixture.log().find("--overwrite") != std::string::npos);
  REQUIRE(fixture.log().find("{frame:04}") != std::string::npos);
  REQUIRE(
      fixture.log().find("complete job is preflighted") != std::string::npos);
  REQUIRE(fixture.run("") != 0);
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  for (const auto &args : {"--unknown",
           "--output",
           "--camera",
           "--camera-index",
           "--camera-index --output result.png",
           "--width",
           "--width 0",
           "--height -1",
           "--samples 0",
           "--samples 2x",
           "--width 4294967296",
           "--width 2147483647",
           "--height 1.5",
           "--samples +2",
           "--camera ''",
           "--camera-index ''",
           "--output result.jpg",
           "--width 20000 --height 20000",
           "--samples 2147483648"}) {
    INFO(args);
    REQUIRE(fixture.run(input + " " + args) != 0);
  }
  REQUIRE(fixture.run(input + " " + input) != 0);
  REQUIRE(fixture.run("missing.data") != 0);
  std::ofstream(fixture.directory / "invalid.data") << "not a Data Tree";
  REQUIRE(fixture.run("invalid.data") != 0);
  vsr::core::DataTree empty;
  REQUIRE(empty.save((fixture.directory / "empty.data").c_str()));
  REQUIRE(fixture.run("empty.data") != 0);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));
}

TEST_CASE("Render CLI rejects application evidence before scene loading",
    "[RenderCLI][NativeValidation]")
{
  CLIFixture fixture;
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const bool wrapped = GENERATE(false, true);
  CAPTURE(encoding, wrapped);
  for (const auto *evidence : {"archives",
           "offlineRendering",
           "ANARIDeviceManager",
           "cameraPoses",
           "settings",
           "animations"}) {
    CAPTURE(evidence);
    vsr::core::DataTree tree;
    auto &payload = wrapped ? tree.root()["context"] : tree.root();
    REQUIRE(vsr::io::serialize_SceneArchive(fixture.scene, payload));
    auto &envelope =
        std::string(evidence) == "archives" ? tree.root() : payload;
    envelope[evidence] = "malformed application state";
    REQUIRE(tree.save((fixture.directory / "dump.data").c_str(), encoding));
    const int result = fixture.run("dump.data");
    INFO(fixture.log());
    REQUIRE(result != 0);
    REQUIRE(fixture.log().find("Application Dump") != std::string::npos);
    REQUIRE(pngFiles(fixture).empty());
  }
}

TEST_CASE("Render CLI never falls back after required dump Archives fail",
    "[RenderCLI][NativeValidation]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const auto *archive = GENERATE("scene", "animationManager");
  const bool missing = GENERATE(false, true);
  CAPTURE(encoding, archive, missing);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  // A valid legacy scene alongside the broken required Archives must not
  // become a fallback after Application Dump reconstruction fails.
  auto &archives = tree.root()["archives"];
  tree.root()["objectDB"] = archives["scene"]["objectDB"];
  tree.root()["layers"] = archives["scene"]["layers"];
  tree.root()["offlineRendering"].remove("renderer");
  if (missing)
    archives.remove(archive);
  else if (std::string(archive) == "scene")
    archives[archive].remove("objectDB");
  else
    archives[archive].remove("fps");
  const auto validation = vsr::io::validate_SceneArchive(tree.root());
  INFO(validation.message);
  REQUIRE(validation.accepted());
  REQUIRE(tree.save((fixture.directory / "invalid.data").c_str(), encoding));
  const int result = fixture.run("invalid.data");
  INFO(fixture.log());
  REQUIRE(result != 0);
  REQUIRE(fixture.log().find("Invalid Application Dump at root/archives")
      != std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
}

TEST_CASE("Render CLI rejects camera-less Application Dumps in either encoding",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  ctx.vsr.scene.removeAllObjects();
  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(ctx, tree.root()));
  for (auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    REQUIRE(
        tree.save((fixture.directory / "application.data").c_str(), encoding));
    REQUIRE(fixture.run("application.data") != 0);
    INFO(fixture.log());
    REQUIRE(fixture.log().find("Camera") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));
  }
}

TEST_CASE("Render CLI selects exact authored cameras", "[RenderCLI]")
{
  CLIFixture fixture;
  auto input = fixture.save(vsr::core::Encoding::Text);
  const std::string small = " --width 32 --height 32 --samples 2";
  REQUIRE(fixture.run(
              input + small + " --camera 'authored view' --output named.png")
      == 0);
  REQUIRE(fixture.run(input + small + " --camera-index 0 --output indexed.png")
      == 0);
  auto first = fixture.scene.getObject<vsr::scene::Camera>(0);
  first->setName("-authored");
  input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(input + small + " --camera -authored --output -exact.png")
      == 0);
  first->setName("999");
  input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(input + " --camera-index 999") != 0);
  REQUIRE(
      fixture.log().find("Camera index not found: 999") != std::string::npos);
  first->setName("authored view");
  input = fixture.save(vsr::core::Encoding::Text);
  for (const auto *camera :
      {"missing", "1", "-1", "1x", "18446744073709551616"})
    REQUIRE(fixture.run(input + " --camera " + camera) != 0);
  auto second = fixture.scene.createObject<vsr::scene::Camera>("perspective");
  second->setName("second");
  input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(input) != 0);
  REQUIRE(fixture.log().find("authored view") != std::string::npos);
  REQUIRE(fixture.log().find("second") != std::string::npos);
  second->setName("authored view");
  input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(input + " --camera 'authored view'") != 0);
  REQUIRE(fixture.run(
              input + small + " --camera-index 0 --output duplicate-index.png")
      == 0);
  fixture.scene.removeAllObjects();
  input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(input) != 0);
  REQUIRE(fixture.log().find("Camera") != std::string::npos);
}

TEST_CASE(
    "Render CLI disambiguates numeric Camera names and indices", "[RenderCLI]")
{
  CLIFixture fixture;
  auto first = fixture.scene.getObject<vsr::scene::Camera>(0);
  first->setName("12");
  for (size_t index = 1; index <= 12; ++index) {
    auto camera =
        fixture.scene.createObject<vsr::scene::Camera>("orthographic");
    REQUIRE(camera->index() == index);
    camera->setName("view " + std::to_string(index));
    camera->setParameter("position", vsr::math::float3(0.f, 0.f, 3.f));
    camera->setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
    camera->setParameter("up", vsr::math::float3(0.f, 1.f, 0.f));
    camera->setParameter("height", 4.f);
  }
  const auto input = fixture.save(vsr::core::Encoding::Text);
  const std::string small = " --width 32 --height 32 --samples 2";
  REQUIRE(fixture.run(input + small + " --camera 12 --output named.png") == 0);
  REQUIRE(fixture.log().find("camera=0 '12'") != std::string::npos);
  checkImage(fixture.directory / "named.png");
  REQUIRE(fixture.run(input + small + " --camera-index 12 --output indexed.png")
      == 0);
  REQUIRE(fixture.log().find("camera=12 'view 12'") != std::string::npos);
  checkDimensions(fixture.directory / "indexed.png", 32, 32);
  REQUIRE(imagePixels(fixture.directory / "named.png")
      != imagePixels(fixture.directory / "indexed.png"));
  REQUIRE(fixture.run(input + " --camera 0") != 0);
  REQUIRE(fixture.log().find("Camera name not found: 0") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));

  for (const auto *arguments : {"--camera 12 --camera-index 12",
           "--camera-index 12 --camera 12",
           "--default-camera --camera 12",
           "--camera 12 --default-camera",
           "--default-camera --camera-index 12",
           "--camera-index 12 --default-camera"}) {
    REQUIRE(fixture.run(input + " " + arguments) != 0);
    REQUIRE(fixture.log().find("cannot be combined") != std::string::npos);
  }
  for (const auto *index :
      {"-1", "+1", "1x", "1.0", "18446744073709551616", "999"}) {
    first->setName(index);
    const auto namedInput = fixture.save(vsr::core::Encoding::Text);
    REQUIRE(fixture.run(namedInput + " --camera-index " + index) != 0);
    REQUIRE(fixture.log().find("Camera index") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));
  }
  first->setName("12");
  fixture.scene.getObject<vsr::scene::Camera>(12)->setName("12");
  const auto duplicateInput = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(duplicateInput + " --camera 12") != 0);
  REQUIRE(fixture.log().find("Ambiguous Camera name: 12") != std::string::npos);
}

TEST_CASE(
    "Render CLI writes one oriented authored orthographic still", "[RenderCLI]")
{
  for (auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    for (bool legacy : {false, true}) {
      CLIFixture fixture;
      const auto input = fixture.save(encoding, legacy);
      REQUIRE(fixture.run(input
                  + " --width 32 --height 32 --samples 2 --output exact.png")
          == 0);
      checkImage(fixture.directory / "exact.png");
      size_t images = 0;
      for (const auto &entry :
          std::filesystem::directory_iterator(fixture.directory))
        images += entry.path().extension() == ".png";
      REQUIRE(images == 1);
      REQUIRE(fixture.log().find("frame=0") != std::string::npos);
      REQUIRE(fixture.log().find("device=helide") != std::string::npos);
      REQUIRE(fixture.log().find("orthographic") != std::string::npos);
      REQUIRE(fixture.log().find("renderer=default") != std::string::npos);
      REQUIRE(fixture.log().find("dimensions=32x32 samples=2")
          != std::string::npos);
      REQUIRE(
          std::filesystem::create_directory(fixture.directory / "blocked.png"));
      REQUIRE(fixture.run(input
                  + " --width 16 --height 16 --samples 1 --output blocked.png")
          != 0);
    }
  }
}

TEST_CASE(
    "Render CLI preserves orthographic projection with distance", "[RenderCLI]")
{
  CLIFixture fixture;
  auto input = fixture.save(vsr::core::Encoding::Text);
  const std::string small = " --width 32 --height 32 --samples 4";
  REQUIRE(fixture.run(input + small) == 0);
  const auto nearCount = checkImage(fixture.directory / "vsrRender.png");
  auto camera = fixture.scene.getObject<vsr::scene::Camera>(0);
  camera->setParameter("position", vsr::math::float3(0.f, 0.f, 6.f));
  input = fixture.save(vsr::core::Encoding::Text);
  REQUIRE(fixture.run(input + small + " --output far.png") == 0);
  const auto farCount = checkImage(fixture.directory / "far.png");
  REQUIRE(std::abs(double(nearCount) - double(farCount)) < 10.);
}

TEST_CASE("Render CLI preserves native dump stills and renderer settings",
    "[RenderCLI]")
{
  for (auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    for (int kind : {0, 1, 2}) {
      CLIFixture fixture;
      vsr::app::Context ctx;
      prepareDump(fixture, ctx);
      vsr::core::DataTree tree;
      writeDump(ctx, tree, kind != 0, kind == 2);
      REQUIRE(
          tree.save((fixture.directory / "renamed.dump").c_str(), encoding));
      const int result = fixture.run("renamed.dump");
      INFO(kind);
      INFO(fixture.log());
      REQUIRE(result == 0);
      REQUIRE(fixture.log().find("dimensions=32x32 samples=2")
          != std::string::npos);
      REQUIRE(fixture.log().find("orthographic") != std::string::npos);
      checkImage(fixture.directory / "vsrRender.png");
      int w, h, channels;
      const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
          stbi_load((fixture.directory / "vsrRender.png").c_str(),
              &w,
              &h,
              &channels,
              4),
          stbi_image_free);
      REQUIRE(pixels);
      REQUIRE(pixels.get()[1] > 240); // saved renderer background, not baseline
      size_t images = 0;
      for (const auto &entry :
          std::filesystem::directory_iterator(fixture.directory))
        images += entry.path().extension() == ".png";
      REQUIRE(images == 1); // saved poses never enable a batch
      REQUIRE(fixture.run("renamed.dump renamed.dump") != 0);
      REQUIRE(
          fixture.run(
              "renamed.dump --width 1024 --height 1 --samples 1 --output explicit.png")
          == 0);
      checkDimensions(fixture.directory / "explicit.png", 1024, 1);
    }
  }
}

TEST_CASE("Render CLI ignores mistyped saved poses with an authored Camera",
    "[RenderCLI][NativeValidation]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const int kind = GENERATE(0, 1, 2);
  const auto *field =
      GENERATE("name", "lookat", "azeldist", "fixedDist", "upAxis", "mode");
  CAPTURE(encoding, kind, field);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree, kind != 0, kind == 2);
  auto &pose = *tree.root()["cameraPoses"].child(size_t(0));
  if (std::string(field) == "name")
    pose[field] = 42;
  else
    pose[field] = "not a pose value";
  if (kind == 2) {
    tree.root()["context"]["cameraPoses"] = tree.root()["cameraPoses"];
    tree.root().remove("cameraPoses");
  }
  REQUIRE(tree.save((fixture.directory / "poses.data").c_str(), encoding));
  const int result = fixture.run("poses.data");
  INFO(fixture.log());
  REQUIRE(result == 0);
  REQUIRE(fixture.log().find("cameras=1") != std::string::npos);
  REQUIRE(fixture.log().find("'authored view'") != std::string::npos);
  REQUIRE(fixture.log().find("orthographic") != std::string::npos);
  REQUIRE(pngFiles(fixture) == std::set<std::string>{"vsrRender.png"});
  checkImage(fixture.directory / "vsrRender.png");
}

TEST_CASE("Render CLI preserves valid and extensible saved pose acceptance",
    "[RenderCLI][NativeValidation]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const int kind = GENERATE(0, 1, 2);
  const bool extraField = GENERATE(false, true);
  CAPTURE(encoding, kind, extraField);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree, kind != 0, kind == 2);
  if (extraField)
    (*tree.root()["cameraPoses"].child(size_t(0)))["futurePoseField"] = true;
  REQUIRE(tree.save((fixture.directory / "poses.data").c_str(), encoding));
  const int result = fixture.run("poses.data");
  INFO(fixture.log());
  REQUIRE(result == 0);
  REQUIRE(fixture.log().find("cameras=1") != std::string::npos);
  REQUIRE(pngFiles(fixture) == std::set<std::string>{"vsrRender.png"});
  checkImage(fixture.directory / "vsrRender.png");
}

TEST_CASE("Render CLI never uses saved poses as missing scene Cameras",
    "[RenderCLI][NativeValidation]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const int kind = GENERATE(0, 1, 2);
  const bool mistypedPose = GENERATE(false, true);
  CAPTURE(encoding, kind, mistypedPose);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.vsr.scene.removeObject(
      ctx.vsr.scene.getObject<vsr::scene::Camera>(0).data());
  vsr::core::DataTree tree;
  writeDump(ctx, tree, kind != 0, kind == 2);
  if (mistypedPose)
    (*tree.root()["cameraPoses"].child(size_t(0)))["lookat"] = "not a vector";
  REQUIRE(tree.save((fixture.directory / "poses.data").c_str(), encoding));
  const int result = fixture.run("poses.data");
  INFO(fixture.log());
  REQUIRE(result != 0);
  REQUIRE(
      fixture.log().find("Saved poses are not Cameras") != std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
  REQUIRE(fixture.run("poses.data --default-camera") == 0);
  REQUIRE(pngFiles(fixture) == std::set<std::string>{"vsrRender.png"});
  checkDimensions(fixture.directory / "vsrRender.png", 32, 32);
}

TEST_CASE("Render CLI keeps consumed settings strict beside inert poses",
    "[RenderCLI][NativeValidation]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const int kind = GENERATE(0, 1, 2);
  const auto *section =
      GENERATE("frame", "camera", "renderer", "aov", "output");
  CAPTURE(encoding, kind, section);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree, kind != 0, kind == 2);
  (*tree.root()["cameraPoses"].child(size_t(0)))["lookat"] = "not a vector";
  const std::string setting = section;
  const char *field = setting == "frame" ? "width"
      : setting == "camera"              ? "cameraIndex"
      : setting == "renderer"            ? "libraryName"
      : setting == "aov"                 ? "depthMin"
                                         : "outputDirectory";
  auto &value = tree.root()["offlineRendering"][section][field];
  if (setting == "renderer" || setting == "output")
    value = 42;
  else
    value = "wrong type";
  REQUIRE(tree.save((fixture.directory / "invalid.data").c_str(), encoding));
  const int result =
      fixture.run("invalid.data --width 32 --height 32 --samples 1");
  INFO(fixture.log());
  REQUIRE(result != 0);
  REQUIRE(fixture.log().find(
              std::string("Invalid Application Dump field: ") + field)
      != std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
}

TEST_CASE(
    "Render CLI distinguishes absent and partial saved settings", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  tree.root()["offlineRendering"]["frame"].remove("height");
  tree.root()["offlineRendering"]["frame"].remove("samples");
  REQUIRE(tree.save((fixture.directory / "partial.data").c_str()));
  REQUIRE(fixture.run("partial.data --height 32") == 0);
  REQUIRE(
      fixture.log().find("dimensions=32x32 samples=128") != std::string::npos);
  checkImage(fixture.directory / "vsrRender.png");
  REQUIRE(fixture.run("partial.data --samples 1 --output partial.png") == 0);
  checkDimensions(fixture.directory / "partial.png", 32, 768);
  tree.root().remove("offlineRendering");
  REQUIRE(tree.save((fixture.directory / "missing.data").c_str()));
  REQUIRE(
      fixture.run("missing.data --height 32 --samples 1 --output missing.png")
      == 0);
  REQUIRE(
      fixture.log().find("dimensions=1024x32 samples=1") != std::string::npos);
  checkDimensions(fixture.directory / "missing.png", 1024, 32);
  writeDump(ctx, tree, true);
  tree.root().remove("animations");
  REQUIRE(tree.save((fixture.directory / "no-frame.data").c_str()));
  REQUIRE(fixture.run("no-frame.data --output no-frame.png") == 0);
  REQUIRE(fixture.log().find("frame=0") != std::string::npos);
  checkImage(fixture.directory / "no-frame.png");
}

TEST_CASE("Render CLI validates saved settings and references before rendering",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  for (const auto *field : {"width", "height", "samples"}) {
    for (bool wrongType : {false, true}) {
      vsr::core::DataTree tree;
      writeDump(ctx, tree);
      auto &value = tree.root()["offlineRendering"]["frame"][field];
      if (wrongType)
        value = "bad";
      else
        value = uint32_t(0);
      REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
      REQUIRE(fixture.run("bad.data --width 32 --height 32 --samples 1") != 0);
      REQUIRE(fixture.log().find("Application Dump") != std::string::npos);
    }
  }
  for (int invalid : {-2, 1, 100}) {
    vsr::core::DataTree tree;
    writeDump(ctx, tree);
    tree.root()["offlineRendering"]["renderer"]["activeRenderer"] = invalid;
    REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
    REQUIRE(fixture.run("bad.data") != 0);
    REQUIRE(fixture.log().find("renderer reference") != std::string::npos);
  }
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  tree.root()["offlineRendering"]["renderer"].remove("rendererObjects");
  REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
  REQUIRE(fixture.run("bad.data") != 0);
  for (float time : {-1.f, 2.f, std::numeric_limits<float>::quiet_NaN()}) {
    writeDump(ctx, tree);
    tree.root()["archives"]["animationManager"]["time"] = time;
    REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
    REQUIRE(fixture.run("bad.data") != 0);
  }
  writeDump(ctx, tree);
  tree.root()["offlineRendering"]["frame"]["width"] = uint32_t(20000);
  tree.root()["offlineRendering"]["frame"]["height"] = uint32_t(20000);
  REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
  REQUIRE(fixture.run("bad.data --width 32 --height 32") != 0);
  writeDump(ctx, tree);
  auto &renderer = tree.root()["offlineRendering"]["renderer"]
                              ["rendererObjects"]["saved renderer"];
  renderer["parameters"]["background"]["enabled"] = "bad bool";
  REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
  REQUIRE(fixture.run("bad.data") != 0);
  for (const auto *section : {"camera", "renderer", "frame", "aov"}) {
    writeDump(ctx, tree);
    tree.root()["offlineRendering"][section] = "malformed section";
    REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
    REQUIRE(fixture.run("bad.data") != 0);
  }
  writeDump(ctx, tree);
  tree.root()["archives"]["animationManager"]["totalFrames"] =
      std::numeric_limits<int>::max();
  tree.root()["archives"]["animationManager"]["time"] = 1.f;
  REQUIRE(tree.save((fixture.directory / "bad.data").c_str()));
  REQUIRE(fixture.run("bad.data") != 0);
  REQUIRE(fixture.log().find("frame arithmetic") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "vsrRender.png"));
}

TEST_CASE(
    "Render CLI preserves saved AOV configuration and ignores legacy lens fields",
    "[RenderCLI]")
{
  CLIFixture fixture;
  fixture.permitOverwrite = true;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.offline.aov.aovType = vsr::rendering::AOVType::DEPTH;
  ctx.offline.aov.depthMax = 10.f;
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "aov.data").c_str()));
  REQUIRE(fixture.run("aov.data") == 0);
  checkDimensions(fixture.directory / "vsrRender.png", 32, 32);
  int w, h, channels;
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load(
          (fixture.directory / "vsrRender.png").c_str(), &w, &h, &channels, 4),
      stbi_image_free);
  REQUIRE(pixels);
  size_t gray = 0;
  for (int i = 0; i < w * h; ++i) {
    const auto *p = pixels.get() + i * 4;
    gray += p[0] > 20 && p[0] < 240 && p[0] == p[1] && p[1] == p[2];
  }
  REQUIRE(gray > 20);
  writeDump(ctx, tree);
  tree.root()["offlineRendering"]["camera"]["cameraIndex"] = "invalid";
  REQUIRE(tree.save((fixture.directory / "invalid.data").c_str()));
  REQUIRE(fixture.run("invalid.data") != 0);
  REQUIRE(fixture.log().find("Application Dump field") != std::string::npos);
  // Older viewers always wrote these values, even with depth of field off.
  // They must not become mandatory controls on the orthographic Scene Camera.
  for (bool defaults : {true, false}) {
    writeDump(ctx, tree);
    tree.root()["offlineRendering"]["camera"]["apertureRadius"] =
        defaults ? 0.f : 0.25f;
    tree.root()["offlineRendering"]["camera"]["focusDistance"] =
        defaults ? 1.f : 4.f;
    REQUIRE(tree.save((fixture.directory / "legacy-lens.data").c_str()));
    REQUIRE(fixture.run("legacy-lens.data") == 0);
    REQUIRE(fixture.log().find("Ignoring legacy offline lens settings")
        != std::string::npos);
    REQUIRE(fixture.run("legacy-lens.data --camera-aperture 0") != 0);
    REQUIRE(fixture.log().find("Unsupported override") != std::string::npos);
  }
}

TEST_CASE(
    "Render CLI resolves saved camera selection without explicit fallback",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  auto second = ctx.vsr.scene.createObject<vsr::scene::Camera>("orthographic");
  second->setName("other");
  second->setParameter("position", vsr::math::float3(0.f, 0.f, 3.f));
  second->setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
  second->setParameter("up", vsr::math::float3(0.f, 1.f, 0.f));
  second->setParameter("height", 2.f);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  tree.root()["offlineRendering"]["camera"]["cameraIndex"] =
      uint64_t(second->index());
  REQUIRE(tree.save((fixture.directory / "camera.data").c_str()));
  REQUIRE(fixture.run("camera.data") == 0);
  REQUIRE(fixture.log().find("'other'") != std::string::npos);
  REQUIRE(
      fixture.run("camera.data --camera 'authored view' --output explicit.png")
      == 0);
  REQUIRE(fixture.log().find("'authored view'") != std::string::npos);
  REQUIRE(
      fixture.run("camera.data --camera-index 0 --output indexed.png") == 0);
  REQUIRE(fixture.log().find("'authored view'") != std::string::npos);
  REQUIRE(fixture.run("camera.data --camera-index 999") != 0);
  REQUIRE(
      fixture.log().find("Camera index not found: 999") != std::string::npos);
  REQUIRE(fixture.run("camera.data --camera missing") != 0);
  tree.root()["offlineRendering"]["camera"]["cameraIndex"] = uint64_t(999);
  REQUIRE(tree.save((fixture.directory / "camera.data").c_str()));
  REQUIRE(fixture.run("camera.data") != 0);
  REQUIRE(fixture.log().find("other") != std::string::npos);
  ctx.vsr.scene.removeObject(second.data());
  writeDump(ctx, tree);
  tree.root()["offlineRendering"]["camera"]["cameraIndex"] = uint64_t(999);
  REQUIRE(tree.save((fixture.directory / "camera.data").c_str()));
  REQUIRE(fixture.run("camera.data --output sole.png") == 0);
  ctx.vsr.scene.removeAllObjects();
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "camera.data").c_str()));
  REQUIRE(fixture.run("camera.data") != 0);
  REQUIRE(
      fixture.log().find("Saved poses are not Cameras") != std::string::npos);
}

TEST_CASE(
    "Render CLI selects stills and explicit timeline sequences", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  tree.root()["offlineRendering"].remove("output");
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));

  SECTION("Animated inputs still default to saved current frame")
  {
    REQUIRE(fixture.run("animated.data") == 0);
    REQUIRE(fixture.log().find("frame=3 ") != std::string::npos);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"vsrRender.png"});
  }
  SECTION("Explicit still frame overrides the saved current frame")
  {
    REQUIRE(fixture.run("animated.data --frame 8 --output still.png") == 0);
    REQUIRE(fixture.log().find("frame=8 ") != std::string::npos);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"still.png"});
  }
  SECTION("Still output remains an exact literal path")
  {
    REQUIRE(
        fixture.run("animated.data --frame 0 --output '{frame:04}.png'") == 0);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"{frame:04}.png"});
  }
  SECTION("Full sequence uses existing nine-frame timeline")
  {
    REQUIRE(fixture.run("animated.data --animation") == 0);
    REQUIRE(fixture.log().find("mode=animation selected=9 last=8")
        != std::string::npos);
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{"vsrRender_0000.png",
            "vsrRender_0001.png",
            "vsrRender_0002.png",
            "vsrRender_0003.png",
            "vsrRender_0004.png",
            "vsrRender_0005.png",
            "vsrRender_0006.png",
            "vsrRender_0007.png",
            "vsrRender_0008.png"});
  }
  SECTION("Inclusive end need not land on the step grid")
  {
    REQUIRE(fixture.run("animated.data --animation --frames 2:8:4 "
                        "--output 'shot_{frame:04}.png'")
        == 0);
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{"shot_0002.png", "shot_0006.png"});
    REQUIRE(fixture.log().find("frame=2 ") != std::string::npos);
    REQUIRE(fixture.log().find("selected=2 last=6") != std::string::npos);
  }
  SECTION("Inclusive end on the grid is rendered")
  {
    REQUIRE(fixture.run("animated.data --animation --frames 2:8:3") == 0);
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{
            "vsrRender_0002.png", "vsrRender_0005.png", "vsrRender_0008.png"});
  }
  SECTION("Generated Camera prepares at the first selected nonzero frame")
  {
    REQUIRE(
        fixture.run("animated.data --animation --frames 7:8 --default-camera "
                    "--output 'generated_{frame:04}.png'")
        == 0);
    REQUIRE(fixture.log().find("frame=7 ") != std::string::npos);
    REQUIRE(fixture.run("animated.data --frame 7 --default-camera "
                        "--output isolated.png")
        == 0);
    const auto sequence = imagePixels(fixture.directory / "generated_0007.png");
    const auto isolated = imagePixels(fixture.directory / "isolated.png");
    size_t changed = 0;
    for (size_t i = 0; i < sequence.size(); i += 4)
      changed += (sequence[i] > sequence[i + 1] + 20)
          != (isolated[i] > isolated[i + 1] + 20);
    REQUIRE(changed < 12);
  }
  SECTION("Four digits are a minimum width, not a frame-index limit")
  {
    tree.root()["archives"]["animationManager"]["totalFrames"] = 10002;
    REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
    REQUIRE(fixture.run("animated.data --animation --frames 10000:10000") == 0);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"vsrRender_10000.png"});
  }
  SECTION("Omitted step means one")
  {
    REQUIRE(fixture.run("animated.data --animation --frames 6:8") == 0);
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{
            "vsrRender_0006.png", "vsrRender_0007.png", "vsrRender_0008.png"});
  }
  SECTION("One-frame animation still uses sequence naming")
  {
    REQUIRE(fixture.run("animated.data --animation --frames 7:7") == 0);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"vsrRender_0007.png"});
  }
  SECTION("Large positive step stops without addition overflow")
  {
    REQUIRE(
        fixture.run("animated.data --animation --frames 7:8:2147483647") == 0);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"vsrRender_0007.png"});
  }
  for (const auto &filename : pngFiles(fixture))
    checkDimensions(fixture.directory / filename, 32, 32);
}

TEST_CASE("Render CLI honors saved subset stride and supplied sequence names",
    "[RenderCLI]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  ctx.offline.frame.renderSubset = true;
  ctx.offline.frame.startFrame = 2;
  ctx.offline.frame.endFrame = 7;
  ctx.offline.frame.frameIncrement = 3;
  ctx.offline.frame.numFrames = 2; // Legacy count must not retime the timeline.
  ctx.offline.output.outputDirectory = "saved";
  ctx.offline.output.filePrefix = "take_";
  vsr::core::DataTree tree;
  writeDump(ctx, tree, false, false, true);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str(), encoding));
  SECTION("Saved subset selects timeline frames two and five")
  {
    REQUIRE(fixture.run("animated.data --animation") == 0);
    checkDimensions(fixture.directory / "saved/take_0002.png", 32, 32);
    checkDimensions(fixture.directory / "saved/take_0005.png", 32, 32);
    REQUIRE_FALSE(
        std::filesystem::exists(fixture.directory / "saved/take_0007.png"));
    REQUIRE(fixture.log().find("selected=2 last=5") != std::string::npos);
  }
  SECTION("Saved stride also selects from the full timeline")
  {
    tree.root()["offlineRendering"]["frame"]["renderSubset"] = false;
    REQUIRE(tree.save((fixture.directory / "animated.data").c_str(), encoding));
    REQUIRE(
        fixture.run("animated.data --animation --output 'full_{frame:04}.png'")
        == 0);
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{
            "full_0000.png", "full_0003.png", "full_0006.png"});
  }
  SECTION("CLI selection replaces saved subset and stride")
  {
    REQUIRE(fixture.run("animated.data --animation --frames 6:8 "
                        "--output 'cli_{frame:04}.png'")
        == 0);
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{
            "cli_0006.png", "cli_0007.png", "cli_0008.png"});
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "saved"));
  }
  SECTION("Absent prefix does not acquire default-initialized dump prefix")
  {
    tree.root()["offlineRendering"]["output"].remove("filePrefix");
    REQUIRE(tree.save((fixture.directory / "animated.data").c_str(), encoding));
    REQUIRE(fixture.run("animated.data --animation --frames 4:4") == 0);
    checkDimensions(fixture.directory / "saved/vsrRender_0004.png", 32, 32);
  }
  SECTION("Supplied empty prefix does not acquire a default")
  {
    tree.root()["offlineRendering"]["output"]["filePrefix"] = "";
    REQUIRE(tree.save((fixture.directory / "animated.data").c_str(), encoding));
    REQUIRE(fixture.run("animated.data --animation --frames 4:4") == 0);
    checkDimensions(fixture.directory / "saved/0004.png", 32, 32);
  }
  SECTION("Absent directory keeps supplied prefix in current directory")
  {
    tree.root()["offlineRendering"]["output"].remove("outputDirectory");
    REQUIRE(tree.save((fixture.directory / "animated.data").c_str(), encoding));
    REQUIRE(fixture.run("animated.data --animation --frames 4:4") == 0);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"take_0004.png"});
  }
}

TEST_CASE("Render CLI rejects invalid frame selections and sequence patterns",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  const std::vector<std::string> invalid = {"--frame",
      "--frames",
      "--frame -1",
      "--frame +1",
      "--frame 9",
      "--frame 1.5",
      "--frame 2147483648",
      "--frame 18446744073709551616",
      "--frame 1 --animation",
      "--frames 0:1",
      "--animation --frames 1",
      "--animation --frames :2",
      "--animation --frames 2:",
      "--animation --frames 1:2:",
      "--animation --frames 0:1:2:3",
      "--animation --frames -1:2",
      "--animation --frames 2:1",
      "--animation --frames 0:9",
      "--animation --frames 0:8:0",
      "--animation --frames 0:8:-1",
      "--animation --frames 0:8:2147483648",
      "--animation --frames 2147483648:2147483648",
      "--animation --frames 0:1.5",
      "--animation --frames '0: 1'",
      "--animation --output plain.png",
      "--animation --output '{frame:03}.png'",
      "--animation --output '{frame:04}{frame:04}.png'",
      "--animation --output '{frame:04}_{other}.png'",
      "--animation --output '{frame:04.png'",
      "--animation --output '{frame:04}.jpg'",
      "--animation --output '{frame:04}/../same.png'"};
  for (const auto &arguments : invalid) {
    REQUIRE(fixture.run("animated.data " + arguments) != 0);
    REQUIRE(pngFiles(fixture).empty());
    REQUIRE(fixture.log().find("[offline] frame=") == std::string::npos);
  }
  REQUIRE(fixture.log().find("Duplicate selected output destination")
      != std::string::npos);
  SECTION("Invalid saved subset cannot be clamped into the timeline")
  {
    tree.root()["offlineRendering"]["frame"]["renderSubset"] = true;
    tree.root()["offlineRendering"]["frame"]["endFrame"] = 9;
    REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
    REQUIRE(fixture.run("animated.data --animation") != 0);
    REQUIRE(fixture.log().find("Invalid frame range") != std::string::npos);
  }
}

TEST_CASE(
    "Render CLI refuses sequences without animated content", "[RenderCLI]")
{
  CLIFixture fixture;
  const auto scene = fixture.save(vsr::core::Encoding::Binary);
  REQUIRE(fixture.run(scene + " --animation --frames 0:0") != 0);
  REQUIRE(fixture.log().find("No animation to render") != std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.vsr.animationMgr.addAnimation("empty animation record");
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "empty.data").c_str()));
  REQUIRE(fixture.run("empty.data --animation") != 0);
  REQUIRE(fixture.log().find("No animation to render") != std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
}

TEST_CASE("Render CLI advances transform-only animation", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  auto &scene = ctx.vsr.scene;
  auto group = scene.insertChildTransformNode(scene.defaultLayer()->root());
  scene.insertChildObjectNode(group, scene.getObject<vsr::scene::Surface>(0));
  auto &manager = ctx.vsr.animationMgr;
  manager.setAnimationTotalFrames(9);
  auto &binding =
      manager.addAnimation("transform motion").addTransformBinding(group);
  binding.insertKeyframe(
      0.f, vsr::math::translation_matrix(vsr::math::float3(-0.7f, 0.f, 0.f)));
  binding.insertKeyframe(
      1.f, vsr::math::translation_matrix(vsr::math::float3(0.7f, 0.f, 0.f)));
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  REQUIRE(fixture.run("animated.data --animation --frames 0:8:8 --samples 4 "
                      "--output 'transform_{frame:04}.png'")
      == 0);
  const auto first = imagePixels(fixture.directory / "transform_0000.png");
  const auto second = imagePixels(fixture.directory / "transform_0008.png");
  size_t changed = 0;
  for (size_t i = 0; i < first.size(); i += 4)
    changed +=
        (first[i] > first[i + 1] + 20) != (second[i] > second[i + 1] + 20);
  REQUIRE(changed > 40);
}

TEST_CASE(
    "Render CLI animation matches isolated frames without retiming or blending",
    "[RenderCLI]")
{
  const bool cameraMotion = GENERATE(false, true);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx, cameraMotion);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  REQUIRE(fixture.run("animated.data --animation --frames 1:7:6 "
                      "--samples 4 --output 'sequence_{frame:04}.png'")
      == 0);
  const auto first = imagePixels(fixture.directory / "sequence_0001.png");
  const auto second = imagePixels(fixture.directory / "sequence_0007.png");
  size_t changed = 0;
  for (size_t i = 0; i < first.size(); i += 4)
    changed +=
        (first[i] > first[i + 1] + 20) != (second[i] > second[i + 1] + 20);
  REQUIRE(changed > 40);
  REQUIRE(fixture.run("animated.data --frame 7 --samples 4 --output alone.png")
      == 0);
  const auto alone = imagePixels(fixture.directory / "alone.png");
  changed = 0;
  for (size_t i = 0; i < alone.size(); i += 4)
    changed +=
        (alone[i] > alone[i + 1] + 20) != (second[i] > second[i + 1] + 20);
  REQUIRE(changed < 12);
}

TEST_CASE(
    "Render CLI evaluates saved frames for geometry and cameras", "[RenderCLI]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const int kind = GENERATE(0, 1, 2);
  for (bool cameraMotion : {false, true}) {
    CLIFixture fixture;
    vsr::app::Context ctx;
    prepareDump(fixture, ctx);
    auto &manager = ctx.vsr.animationMgr;
    manager.setAnimationTotalFrames(5);
    auto &animation = manager.addAnimation("saved motion");
    const float times[] = {0.f, 1.f};
    if (cameraMotion) {
      auto camera = ctx.vsr.scene.getObject<vsr::scene::Camera>(0);
      const vsr::math::float3 positions[] = {
          {0.5f, 0.f, 3.f}, {-0.5f, 0.f, 3.f}};
      animation.addObjectParameterBinding(
          camera.data(), "position", ANARI_FLOAT32_VEC3, positions, times, 2);
    } else {
      auto geometry = ctx.vsr.scene.getObject<vsr::scene::Geometry>(0);
      const float radii[] = {0.2f, 0.4f};
      animation.addObjectParameterBinding(
          geometry.data(), "radius", ANARI_FLOAT32, radii, times, 2);
    }
    std::vector<unsigned char> first;
    for (int frame : {0, 4}) {
      manager.setAnimationFrame(0);
      vsr::core::DataTree tree;
      writeDump(ctx, tree, kind != 0, kind == 2);
      auto &timeline = kind == 0 ? tree.root()["archives"]["animationManager"]
          : kind == 1            ? tree.root()["animations"]
                                 : tree.root()["context"]["animations"];
      timeline["time"] = float(frame) / 4.f;
      REQUIRE(
          tree.save((fixture.directory / "animated.data").c_str(), encoding));
      const auto result = fixture.run(
          "animated.data --output frame" + std::to_string(frame) + ".png");
      INFO(kind);
      INFO(cameraMotion);
      INFO(frame);
      INFO(fixture.log());
      REQUIRE(result == 0);
      REQUIRE(fixture.log().find("frame=" + std::to_string(frame))
          != std::string::npos);
      int w, h, channels;
      const auto path =
          fixture.directory / ("frame" + std::to_string(frame) + ".png");
      const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
          stbi_load(path.c_str(), &w, &h, &channels, 4), stbi_image_free);
      REQUIRE(pixels);
      REQUIRE(w == 32);
      REQUIRE(h == 32);
      if (frame == 0)
        first.assign(pixels.get(), pixels.get() + 32 * 32 * 4);
      else {
        size_t different = 0;
        for (size_t i = 0; i < first.size(); i += 4)
          different += std::abs(int(first[i]) - int(pixels.get()[i])) > 20;
        REQUIRE(different > 40);
      }
    }
  }
}

TEST_CASE(
    "Render CLI accepts legacy scene-only context wrappers", "[RenderCLI]")
{
  for (auto encoding :
      {vsr::core::Encoding::Binary, vsr::core::Encoding::Text}) {
    CLIFixture fixture;
    auto input = fixture.save(encoding, true, true);
    REQUIRE(fixture.run(input + " --width 32 --height 32 --samples 2") == 0);
    checkImage(fixture.directory / "vsrRender.png");
    vsr::core::DataTree dump;
    REQUIRE(
        vsr::io::serialize_SceneArchive(fixture.scene, dump.root()["context"]));
    dump.root()["context"]["animations"] = "malformed";
    REQUIRE(
        dump.save((fixture.directory / "legacy-dump.data").c_str(), encoding));
    REQUIRE(fixture.run("legacy-dump.data") != 0);
    REQUIRE(fixture.log().find("Application Dump") != std::string::npos);
  }
}

TEST_CASE(
    "Render CLI diagnoses corrupt scene-only wrappers without dump identity",
    "[RenderCLI][NativeValidation]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const bool scalarWrapper = GENERATE(false, true);
  CAPTURE(encoding, scalarWrapper);
  CLIFixture fixture;
  vsr::core::DataTree tree;
  REQUIRE(
      vsr::io::serialize_SceneArchive(fixture.scene, tree.root()["context"]));
  tree.root()["context"].remove("__vsr_metadata");
  if (scalarWrapper)
    tree.root()["context"] = "not a scene wrapper";
  else
    tree.root()["context"].remove("objectDB");
  REQUIRE(tree.save((fixture.directory / "corrupt.data").c_str(), encoding));
  const int result = fixture.run("corrupt.data");
  INFO(fixture.log());
  REQUIRE(result != 0);
  REQUIRE(fixture.log().find("Invalid Scene Archive") != std::string::npos);
  REQUIRE(fixture.log().find("objectDB") != std::string::npos);
  REQUIRE(fixture.log().find("Application Dump") == std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
}

TEST_CASE("Render CLI honors supplied still output fields", "[RenderCLI]")
{
  const auto encoding =
      GENERATE(vsr::core::Encoding::Binary, vsr::core::Encoding::Text);
  const int kind = GENERATE(0, 1, 2);
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareDump(fixture, ctx);
  ctx.offline.output.outputDirectory = "saved/nested";
  ctx.offline.output.filePrefix = "take_";
  vsr::core::DataTree tree;
  writeDump(ctx, tree, kind != 0, kind == 2, true);
  auto &output = tree.root()["offlineRendering"]["output"];
  std::string expected = "saved/nested/take.png";
  std::string arguments = "saved.data --frame 7";
  SECTION("Saved still is unnumbered even at an explicit frame") {}
  SECTION("Absent prefix uses the still default in the supplied directory")
  {
    output.remove("filePrefix");
    expected = "saved/nested/vsrRender.png";
  }
  SECTION("Absent directory uses the supplied prefix in the current directory")
  {
    output.remove("outputDirectory");
    expected = "take.png";
  }
  SECTION("Supplied empty prefix stays empty rather than using a default")
  {
    output["filePrefix"] = "";
    expected = "saved/nested/.png";
  }
  SECTION("Supplied empty directory denotes the current directory")
  {
    output["outputDirectory"] = "";
    expected = "take.png";
  }
  SECTION("An empty output section does not supply dump defaults")
  {
    output.remove("outputDirectory");
    output.remove("filePrefix");
    expected = "vsrRender.png";
  }
  SECTION("An absent output section does not supply dump defaults")
  {
    tree.root()["offlineRendering"].remove("output");
    expected = "vsrRender.png";
  }
  SECTION("Explicit literal path overrides both saved output fields")
  {
    expected = "explicit/deep/{frame:04}.png";
    arguments += " --output '" + expected + "'";
  }
  SECTION("An explicit trailing underscore remains literal")
  {
    expected = "explicit/frame_.png";
    arguments += " --output " + expected;
  }
  SECTION("A prefix without a sequence separator is unchanged")
  {
    output["filePrefix"] = "take";
  }
  SECTION("An explicit hidden PNG filename is still an exact output path")
  {
    expected = "explicit/.png";
    arguments += " --output '" + expected + "'";
  }
  SECTION("Actually supplied persistence defaults remain supplied settings")
  {
    output["outputDirectory"] = "./";
    output["filePrefix"] = "frame_";
    expected = "frame.png";
  }
  REQUIRE(tree.save((fixture.directory / "saved.data").c_str(), encoding));
  REQUIRE(fixture.run(arguments) == 0);
  checkImage(fixture.directory / expected);
  REQUIRE(fixture.log().find(expected) != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "frame_0007.png"));
  if (expected.find("saved/") != 0)
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "saved"));
}

TEST_CASE("Render CLI protects exact still outputs", "[RenderCLI]")
{
  CLIFixture fixture;
  const auto arguments = fixture.save(vsr::core::Encoding::Binary)
      + " --width 32 --height 32 --samples 2 --output nested/preview.png";
  REQUIRE(std::filesystem::create_directory(fixture.directory / "nested"));
  const auto destination = fixture.directory / "nested/preview.png";
  const std::string marker = "keep this original image";
  SECTION("An ordinary existing file requires permission")
  {
    std::ofstream(destination) << marker;
  }
  SECTION("A link to a regular file requires permission and writes its target")
  {
    std::ofstream(fixture.directory / "target.png") << marker;
    std::filesystem::create_symlink("../target.png", destination);
  }
  REQUIRE(fixture.run(arguments) != 0);
  REQUIRE(fileContents(destination) == marker);
  REQUIRE(fixture.log().find("nested/preview.png") != std::string::npos);
  REQUIRE(fixture.log().find("--overwrite") != std::string::npos);
  REQUIRE(fixture.log().find("[offline] frame=") == std::string::npos);
  REQUIRE(fixture.run(arguments + " --overwrite") == 0);
  checkImage(destination);
  if (std::filesystem::is_symlink(destination))
    checkImage(fixture.directory / "target.png");
}

TEST_CASE(
    "Render CLI preflights the complete selected output set", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  REQUIRE(std::filesystem::create_directory(fixture.directory / "nested"));
  const auto destination = fixture.directory / "nested/shot_0008.png";
  const std::string marker = "existing selected frame";
  std::ofstream(destination) << marker;
  std::ofstream(fixture.directory / "nested/shot_0004.png") << "unselected";
  const std::string arguments =
      "animated.data --animation --frames 2:8:3 "
      "--output 'nested/shot_{frame:04}.png'";
  REQUIRE(fixture.run(arguments) != 0);
  REQUIRE(fileContents(destination) == marker);
  REQUIRE(fixture.log().find("nested/shot_0008.png") != std::string::npos);
  REQUIRE(fixture.log().find("[offline] frame=") == std::string::npos);
  REQUIRE_FALSE(
      std::filesystem::exists(fixture.directory / "nested/shot_0002.png"));
  REQUIRE_FALSE(
      std::filesystem::exists(fixture.directory / "nested/shot_0005.png"));
  REQUIRE(fixture.run(arguments + " --overwrite") == 0);
  for (int frame : {2, 5, 8})
    checkDimensions(fixture.directory
            / ("nested/shot_000" + std::to_string(frame) + ".png"),
        32,
        32);
  REQUIRE(
      fileContents(fixture.directory / "nested/shot_0004.png") == "unselected");
}

TEST_CASE(
    "Render CLI creates directories for timeline-index paths", "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  SECTION("A strided subrange creates every selected directory")
  {
    REQUIRE(fixture.run("animated.data --animation --frames 2:8:3 "
                        "--output 'new/deep/{frame:04}/image.png'")
        == 0);
    for (int frame : {2, 5, 8})
      checkDimensions(fixture.directory
              / ("new/deep/000" + std::to_string(frame) + "/image.png"),
          32,
          32);
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "new/deep/0003"));
  }
  SECTION("A one-frame sequence still expands the timeline index")
  {
    REQUIRE(fixture.run("animated.data --animation --frames 7:7 "
                        "--output 'new/deep/image_{frame:04}.png'")
        == 0);
    checkDimensions(fixture.directory / "new/deep/image_0007.png", 32, 32);
    REQUIRE_FALSE(
        std::filesystem::exists(fixture.directory / "new/deep/image_0000.png"));
  }
}

TEST_CASE("Render CLI rejects invalid destination types even with overwrite",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  std::string pattern = "shot_{frame:04}.png";
  std::string diagnostic = "shot_0008.png";
  SECTION("An existing directory is not an image file")
  {
    REQUIRE(std::filesystem::create_directory(fixture.directory / diagnostic));
  }
  SECTION("A dangling link is not an unused image file")
  {
    std::filesystem::create_symlink(
        "missing.png", fixture.directory / diagnostic);
  }
  SECTION("A regular file cannot be an output parent directory")
  {
    std::ofstream(fixture.directory / "0008") << "not a directory";
    pattern = "{frame:04}/nested/image.png";
    diagnostic = "0008/nested/image.png";
  }
  for (const auto *permission : {"", " --overwrite"}) {
    REQUIRE(fixture.run("animated.data --animation --frames 2:8:3 --output "
                + quote(pattern) + permission)
        != 0);
    REQUIRE(fixture.log().find(diagnostic) != std::string::npos);
    REQUIRE(fixture.log().find("[offline] frame=") == std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "shot_0002.png"));
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "shot_0005.png"));
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "0002"));
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "missing.png"));
  }
}

TEST_CASE("Render CLI rejects filesystem-aliased destinations before writes",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  const auto original = fixture.directory / "original";
  const std::string marker = "do not replace an aliased file";
  std::string pattern = "alias_{frame:04}.png";
  SECTION("Symbolic links to one existing file collide")
  {
    std::ofstream(original) << marker;
    for (int frame : {2, 8})
      std::filesystem::create_symlink("original",
          fixture.directory / ("alias_000" + std::to_string(frame) + ".png"));
  }
  SECTION("Hard links to one existing file collide")
  {
    std::ofstream(original) << marker;
    for (int frame : {2, 8})
      std::filesystem::create_hard_link(original,
          fixture.directory / ("alias_000" + std::to_string(frame) + ".png"));
  }
  SECTION("Directory aliases collide even when outputs do not exist")
  {
    REQUIRE(std::filesystem::create_directory(original));
    for (int frame : {2, 5, 8})
      std::filesystem::create_directory_symlink(
          "original", fixture.directory / ("000" + std::to_string(frame)));
    pattern = "{frame:04}/same.png";
  }
  REQUIRE(fixture.run(
              "animated.data --animation --frames 2:8:3 --overwrite --output "
              + quote(pattern))
      != 0);
  REQUIRE(fixture.log().find("Duplicate selected output destination")
      != std::string::npos);
  REQUIRE(fixture.log().find("[offline] frame=") == std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "alias_0005.png"));
  if (std::filesystem::is_regular_file(original))
    REQUIRE(fileContents(original) == marker);
  else
    REQUIRE(std::filesystem::is_empty(original));
}

#ifdef __linux__
TEST_CASE("Render CLI propagates deterministic directory and write failures",
    "[RenderCLI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  SECTION("procfs refuses new destination directories even for root")
  {
    const auto destination = std::filesystem::path("/proc")
        / fixture.directory.filename() / "nested/image.png";
    REQUIRE(fixture.run("animated.data --output " + quote(destination.string()))
        != 0);
    REQUIRE(fixture.log().find("Cannot create destination directory")
        != std::string::npos);
    REQUIRE(fixture.log().find(destination.string()) != std::string::npos);
    REQUIRE(fixture.log().find("[offline] frame=") == std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(destination));
  }
  SECTION("A regular procfs file passes preflight but cannot be written")
  {
    // The kernel rejects writes, independently of permission bits/test user.
    REQUIRE(std::filesystem::is_regular_file("/proc/version"));
    const auto original = fileContents("/proc/version");
    std::filesystem::create_symlink(
        "/proc/version", fixture.directory / "shot_0008.png");
    REQUIRE(fixture.run("animated.data --animation --frames 2:8:3 --overwrite "
                        "--output 'shot_{frame:04}.png'")
        != 0);
    REQUIRE(fixture.log().find("Render failed for shot_0008.png")
        != std::string::npos);
    REQUIRE(fixture.log().find("Cannot save image to 'shot_0008.png'")
        != std::string::npos);
    checkDimensions(fixture.directory / "shot_0002.png", 32, 32);
    checkDimensions(fixture.directory / "shot_0005.png", 32, 32);
    REQUIRE(fileContents("/proc/version") == original);
  }
}
#endif

#ifdef VSR_RENDER_EXCEPTION_LIBRARY
TEST_CASE("Render CLI returns expected mapped-buffer failures before output",
    "[RenderCLI][RenderException]")
{
  CLIFixture fixture;
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  for (const auto *channel : {"channel.color", "channel.depth"}) {
    for (int samples : {1, 2}) {
      const auto environment =
          "LD_PRELOAD=" + quote(VSR_RENDER_EXCEPTION_LIBRARY)
          + " VELA_TEST_RENDER_EXCEPTION=" + channel;
      REQUIRE(fixture.run(input
                      + " --device helide --width 16 --height 16 --samples "
                      + std::to_string(samples)
                      + " --aov depth --output missing-buffer.png",
                  environment)
          == 256);
      REQUIRE(
          fixture.log().find("rank=0 frame=0 local=1/1") != std::string::npos);
      // This prefix is produced only for a returned Failed engine result, not
      // an exception converted at the CLI phase boundary.
      REQUIRE(
          fixture.log().find(
              std::string(
                  "Render failed for missing-buffer.png: Missing or invalid requested buffer: ")
              + channel)
          != std::string::npos);
      REQUIRE(
          fixture.log().find("Completed global frames=") == std::string::npos);
      REQUIRE_FALSE(
          std::filesystem::exists(fixture.directory / "missing-buffer.png"));
    }
  }
}

TEST_CASE("Render CLI diagnoses unexpected engine exceptions",
    "[RenderCLI][RenderException]")
{
  CLIFixture fixture;
  const auto input = fixture.save(vsr::core::Encoding::Binary);
  for (const auto &kind : {"standard", "nonstandard"}) {
    const auto environment = "LD_PRELOAD=" + quote(VSR_RENDER_EXCEPTION_LIBRARY)
        + " VELA_TEST_RENDER_EXCEPTION=" + kind;
    REQUIRE(
        fixture.run(input
                + " --device helide --width 16 --height 16 --samples 1 --output interrupted.png",
            environment)
        == 256);
    REQUIRE(
        fixture.log().find("rank=0 frame=0 local=1/1") != std::string::npos);
    REQUIRE(fixture.log().find(std::string(kind) == "standard"
                    ? "vsrRender: Injected unexpected render exception"
                    : "vsrRender: Unknown application exception")
        != std::string::npos);
    REQUIRE(
        fixture.log().find("Completed global frames=") == std::string::npos);
    REQUIRE_FALSE(
        std::filesystem::exists(fixture.directory / "interrupted.png"));
  }
  REQUIRE(
      fixture.run(input
          + " --device helide --width 16 --height 16 --samples 1 --output retry.png")
      == 0);
  checkDimensions(fixture.directory / "retry.png", 16, 16);
}
#endif

#ifdef VSR_MPI_EXECUTABLE
TEST_CASE("Render MPI bounds diagnostics from the lowest failing rank",
    "[RenderMPI][RenderDiagnostics]")
{
  CLIFixture fixture;
  const auto wrapper = fixture.directory / "rank-diagnostic.sh";
  std::ofstream(wrapper)
      << "#!/bin/sh\n"
         "rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-unknown}}\n"
         "case $rank in\n"
         "0) ;;\n"
         "1|2) set -- \"$@\" \"--rank-$rank-diagnostic-"
      << std::string(8192, 'x')
      << "-end-of-diagnostic\" ;;\n"
         "*) exit 9 ;;\n"
         "esac\nexec "
      << quote(VSR_RENDER_EXECUTABLE) << " \"$@\"\n";
  REQUIRE(fixture.runMPI(3, "--help", wrapper.string()) != 0);
  const auto log = fixture.log();
  const std::string prefix =
      "vsrRender: Arguments failed on rank 1: Unknown option: --rank-1-diagnostic-";
  const auto start = log.find(prefix);
  REQUIRE(start != std::string::npos);
  const auto end = log.find('\n', start);
  REQUIRE(end != std::string::npos);
  REQUIRE(end - start
      <= std::string("vsrRender: Arguments failed on rank 1: ").size() + 4095);
  REQUIRE(log.substr(end - 12, 12) == " [truncated]");
  REQUIRE(log.find('\0') == std::string::npos);
  REQUIRE(log.find("--rank-2-diagnostic-") == std::string::npos);
  REQUIRE(log.find("-end-of-diagnostic") == std::string::npos);
  REQUIRE(log.find(prefix, end) == std::string::npos);
  REQUIRE(log.find("Completed global frames=") == std::string::npos);
  REQUIRE(pngFiles(fixture).empty());
}

TEST_CASE(
    "Render MPI distributes global frames and fixed Cameras", "[RenderMPI]")
{
  CLIFixture fixture;
  vsr::app::Context ctx;
  prepareAnimation(fixture, ctx);
  ctx.vsr.animationMgr.setAnimationTotalFrames(33);
  vsr::core::DataTree tree;
  writeDump(ctx, tree);
  REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
  const std::string selection = "animated.data --animation --frames 20:27:2";
  SECTION("Strided sequence matches serial generated framing on both ranks")
  {
    REQUIRE(fixture.run(selection
                + " --default-camera --output 'serial_{frame:04}.png'")
        == 0);
    REQUIRE(
        fixture.runMPI(2,
            selection + " --default-camera --output 'parallel_{frame:04}.png'")
        == 0);
    const auto log = fixture.log();
    for (int frame : {20, 22, 24, 26}) {
      const auto index = "00" + std::to_string(frame);
      REQUIRE(imagePixels(fixture.directory / ("serial_" + index + ".png"))
          == imagePixels(fixture.directory / ("parallel_" + index + ".png")));
      const auto progress = "rank=" + std::to_string(((frame - 20) / 2) % 2)
          + " frame=" + std::to_string(frame) + " local=";
      const auto position = log.find(progress);
      REQUIRE(position != std::string::npos);
      REQUIRE(
          log.find(progress, position + progress.size()) == std::string::npos);
    }
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{"serial_0020.png",
            "serial_0022.png",
            "serial_0024.png",
            "serial_0026.png",
            "parallel_0020.png",
            "parallel_0022.png",
            "parallel_0024.png",
            "parallel_0026.png"});
    REQUIRE(imagePixels(fixture.directory / "serial_0020.png")
        != imagePixels(fixture.directory / "serial_0026.png"));
    const auto firstPixels =
        imagePixels(fixture.directory / "parallel_0020.png");
    size_t colored = 0;
    for (size_t i = 0; i < firstPixels.size(); i += 4)
      colored += firstPixels[i] > firstPixels[i + 1] + 20;
    REQUIRE(colored > 20);
    REQUIRE(log.find("Completed global frames=4 ranks=2") != std::string::npos);
  }
  SECTION("Explicit numeric names and indices resolve the same global Camera")
  {
    ctx.vsr.scene.getObject<vsr::scene::Camera>(0)->setName("12");
    auto second = ctx.vsr.scene.createObject<vsr::scene::Camera>("perspective");
    second->setName("other");
    writeDump(ctx, tree);
    REQUIRE(tree.save((fixture.directory / "animated.data").c_str()));
    REQUIRE(
        fixture.run(selection + " --camera 12 --output 'serial_{frame:04}.png'")
        == 0);
    REQUIRE(fixture.runMPI(
                2, selection + " --camera 12 --output 'named_{frame:04}.png'")
        == 0);
    REQUIRE(fixture.log().find("camera=0 '12'") != std::string::npos);
    REQUIRE(
        fixture.runMPI(2,
            selection + " --camera-index 0 --output 'indexed_{frame:04}.png'")
        == 0);
    for (int frame : {20, 22, 24, 26}) {
      const auto index = "00" + std::to_string(frame);
      const auto serial =
          imagePixels(fixture.directory / ("serial_" + index + ".png"));
      REQUIRE(serial
          == imagePixels(fixture.directory / ("named_" + index + ".png")));
      REQUIRE(serial
          == imagePixels(fixture.directory / ("indexed_" + index + ".png")));
    }
    REQUIRE(fixture.log().find("Completed global frames=4 ranks=2")
        != std::string::npos);
  }
  SECTION("More ranks than sequence frames still complete collectively")
  {
    REQUIRE(
        fixture.runMPI(3,
            "animated.data --animation --frames 22:22 --default-camera --output 'idle_{frame:04}.png'")
        == 0);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"idle_0022.png"});
    checkDimensions(fixture.directory / "idle_0022.png", 32, 32);
    REQUIRE(fixture.log().find("rank=1/3 assigned=0") != std::string::npos);
    REQUIRE(fixture.log().find("rank=2/3 assigned=0") != std::string::npos);
  }
  SECTION("Only rank zero renders a still")
  {
    REQUIRE(
        fixture.run("animated.data --frame 22 --output serial-still.png") == 0);
    REQUIRE(
        fixture.runMPI(3, "animated.data --frame 22 --output still.png") == 0);
    REQUIRE(pngFiles(fixture)
        == std::set<std::string>{"still.png", "serial-still.png"});
    REQUIRE(imagePixels(fixture.directory / "still.png")
        == imagePixels(fixture.directory / "serial-still.png"));
    checkDimensions(fixture.directory / "still.png", 32, 32);
    REQUIRE(
        fixture.log().find("rank=0 frame=22 local=1/1") != std::string::npos);
    REQUIRE(fixture.log().find("rank=1 frame=") == std::string::npos);
    REQUIRE(fixture.log().find("rank=2 frame=") == std::string::npos);
  }
  SECTION("Complete global preflight refuses an output assigned to rank one")
  {
    std::ofstream(fixture.directory / "shot_0022.png") << "existing output";
    REQUIRE(
        fixture.runMPI(2, selection + " --output 'shot_{frame:04}.png'") != 0);
    REQUIRE(pngFiles(fixture) == std::set<std::string>{"shot_0022.png"});
    REQUIRE(
        fileContents(fixture.directory / "shot_0022.png") == "existing output");
    REQUIRE(fixture.log().find("Output preflight failed on rank 0")
        != std::string::npos);
    REQUIRE(fixture.log().find("frame=20 local=") == std::string::npos);
    REQUIRE(fixture.runMPI(
                2, selection + " --overwrite --output 'shot_{frame:04}.png'")
        == 0);
    for (int frame : {20, 22, 24, 26})
      checkDimensions(
          fixture.directory / ("shot_00" + std::to_string(frame) + ".png"),
          32,
          32);
  }
  SECTION(
      "Global invalid input, range, pattern and arguments fail before writes")
  {
    for (const auto &arguments :
        {"missing.data --output bad.png",
            "animated.data --animation --frames 20:40 --output 'bad_{frame:04}.png'",
            "animated.data --animation --frames 20:27:2 --output bad.png",
            "animated.data --unknown"}) {
      REQUIRE(fixture.runMPI(2, arguments) != 0);
      REQUIRE(pngFiles(fixture).empty());
      REQUIRE(fixture.log().find(" local=") == std::string::npos);
    }
  }
  SECTION("Rank-specific argument, input and device failures are collective")
  {
    for (const auto &suffix : {"--device missing_mpi_test_device",
             "missing-rank-input.data",
             "--width invalid",
             "--camera-index -1",
             "--camera-index 999",
             "--camera 12 --camera-index 0",
             "--default-camera --camera-index 0"}) {
      const auto wrapper = fixture.directory / "rank-failure.sh";
      std::ofstream(wrapper)
          << "#!/bin/sh\n"
             "rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-unknown}}\n"
             "case $rank in\n"
             "1) exec "
          << quote(VSR_RENDER_EXECUTABLE) << " \"$@\" " << suffix
          << " ;;\n"
             "0) exec "
          << quote(VSR_RENDER_EXECUTABLE)
          << " \"$@\" ;;\n"
             "*) echo 'Launcher did not expose rank' >&2; exit 9 ;;\n"
             "esac\n";
      REQUIRE(fixture.runMPI(2,
                  selection + " --output 'device_{frame:04}.png'",
                  wrapper.string())
          != 0);
      REQUIRE(fixture.log().find("failed on rank 1") != std::string::npos);
      REQUIRE(pngFiles(fixture).empty());
      REQUIRE(fixture.log().find(" local=") == std::string::npos);
    }
  }
#ifdef VSR_RENDER_EXCEPTION_LIBRARY
  SECTION("Rank-specific unexpected render exceptions complete collectively")
  {
    for (const auto &kind : {"standard", "nonstandard"}) {
      const auto wrapper = fixture.directory / "rank-exception.sh";
      std::ofstream(wrapper)
          << "#!/bin/sh\n"
             "rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-unknown}}\n"
             "case $rank in\n"
             "1) export LD_PRELOAD="
          << quote(VSR_RENDER_EXCEPTION_LIBRARY)
          << " VELA_TEST_RENDER_EXCEPTION=" << kind
          << " ;;\n0) ;;\n*) exit 9 ;;\nesac\nexec "
          << quote(VSR_RENDER_EXECUTABLE) << " \"$@\"\n";
      const auto prefix = std::string(kind);
      REQUIRE(fixture.runMPI(2,
                  selection + " --output '" + prefix + "_{frame:04}.png'",
                  wrapper.string())
          != 0);
      const auto log = fixture.log();
      REQUIRE(log.find("Rendering failed on rank 1") != std::string::npos);
      REQUIRE(
          log.find(prefix == "standard" ? "Injected unexpected render exception"
                                        : "Unknown application exception")
          != std::string::npos);
      REQUIRE(log.find("Completed global frames=") == std::string::npos);
      checkDimensions(fixture.directory / (prefix + "_0020.png"), 32, 32);
      checkDimensions(fixture.directory / (prefix + "_0024.png"), 32, 32);
      REQUIRE_FALSE(
          std::filesystem::exists(fixture.directory / (prefix + "_0022.png")));
      REQUIRE_FALSE(
          std::filesystem::exists(fixture.directory / (prefix + "_0026.png")));
    }
  }
#endif
#ifdef __linux__
  SECTION("Rank-specific runtime write failure returns nonzero without hanging")
  {
    REQUIRE(std::filesystem::is_regular_file("/proc/version"));
    const auto original = fileContents("/proc/version");
    std::filesystem::create_symlink(
        "/proc/version", fixture.directory / "fail_0022.png");
    REQUIRE(fixture.runMPI(
                2, selection + " --overwrite --output 'fail_{frame:04}.png'")
        != 0);
    REQUIRE(
        fixture.log().find("Rendering failed on rank 1") != std::string::npos);
    REQUIRE(fixture.log().find("Cannot save image to 'fail_0022.png'")
        != std::string::npos);
    REQUIRE(
        fixture.log().find("Completed global frames=") == std::string::npos);
    REQUIRE(fileContents("/proc/version") == original);
    checkDimensions(fixture.directory / "fail_0020.png", 32, 32);
    checkDimensions(fixture.directory / "fail_0024.png", 32, 32);
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "fail_0026.png"));
  }
#endif
}
#endif
