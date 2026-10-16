// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// tests
#include "RenderAOVCases.h"
// vsr_app
#include "vsr/app/Context.h"
#include "vsr/app/renderAnimationSequence.h"

// catch
#include "catch.hpp"
// stb_image
#include "stb_image.h"
// std
#include <chrono>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {

struct RenderFixture
{
  RenderFixture();
  ~RenderFixture();
  void checkRestored() const;

  vsr::app::Context ctx;
  vsr::scene::Object renderer{ANARI_RENDERER, "default"};
  vsr::app::OfflineRenderJob job;
  std::filesystem::path directory;
  size_t delegates{0};
  vsr::animation::AnimationManager::RuntimeState clock;
};

RenderFixture::RenderFixture()
{
  directory = std::filesystem::temp_directory_path()
      / ("vsr_offline_"
          + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
  REQUIRE(std::filesystem::create_directory(directory));
  auto &scene = ctx.vsr.scene;
  auto camera = scene.createObject<vsr::scene::Camera>("orthographic");
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
  renderer.setParameter("background", vsr::math::float4(0.f, 0.f, 0.f, 1.f));
  renderer.setParameter("ambientRadiance", 1.f);
  job.cameraIndex = camera->index();
  job.renderer = &renderer;
  job.width = 32;
  job.height = 32;
  job.samples = 2;
  job.frames = {{1, (directory / "first.png").string()},
      {3, (directory / "second.png").string()}};
  auto &manager = ctx.vsr.animationMgr;
  manager.setAnimationTime(0.0731f);
  manager.setAnimationFPS(8.f);
  manager.setAnimationIncrement(0.023f);
  manager.setLoop(false);
  manager.play();
  REQUIRE_FALSE(manager.tick(0.04f));
  clock = manager.runtimeState();
  delegates = scene.updateDelegate().size();
}

RenderFixture::~RenderFixture()
{
  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
}

void RenderFixture::checkRestored() const
{
  REQUIRE(ctx.vsr.scene.updateDelegate().size() == delegates);
  const auto restored = ctx.vsr.animationMgr.runtimeState();
  REQUIRE(restored.time == clock.time);
  REQUIRE(restored.increment == clock.increment);
  REQUIRE(restored.fps == clock.fps);
  REQUIRE(restored.totalFrames == clock.totalFrames);
  REQUIRE(restored.playing == clock.playing);
  REQUIRE(restored.loop == clock.loop);
  REQUIRE(restored.playbackAccumulator == clock.playbackAccumulator);
}

} // namespace

namespace {

struct ThrowingRestoreDelegate : vsr::scene::EmptyUpdateDelegate
{
  ThrowingRestoreDelegate() = default;
  VSR_NOT_COPYABLE(ThrowingRestoreDelegate)
  VSR_NOT_MOVEABLE(ThrowingRestoreDelegate)

  void signalParameterUpdated(const vsr::scene::Object *object,
      const vsr::scene::Parameter *parameter) override;
};

void ThrowingRestoreDelegate::signalParameterUpdated(
    const vsr::scene::Object *, const vsr::scene::Parameter *parameter)
{
  if (parameter->name() == "height" && !parameter->isEnabled()
      && parameter->value().get<float>() == 2.f)
    throw std::runtime_error("authored restoration observer failed");
  if (parameter->name() == "radius" && !parameter->isEnabled()
      && parameter->value().get<float>() == 0.9f)
    throw std::runtime_error("Geometry restoration observer failed");
}

struct RedImage
{
  double centerX{0.};
  size_t count{0};
  std::vector<bool> mask;
};

RedImage readRedImage(const std::string &path)
{
  int width = 0, height = 0, channels = 0;
  const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load(path.c_str(), &width, &height, &channels, 4), stbi_image_free);
  REQUIRE(pixels);
  REQUIRE(width == 32);
  REQUIRE(height == 32);
  RedImage image;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto *p = pixels.get() + 4 * (y * width + x);
      const bool red = p[0] > p[1] + 20;
      image.mask.push_back(red);
      if (red) {
        ++image.count;
        image.centerX += x;
      }
    }
  }
  REQUIRE(image.count > 20);
  image.centerX /= image.count;
  return image;
}

} // namespace

TEST_CASE("Generated offline Cameras fit once at the global first frame",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto &scene = fixture.ctx.vsr.scene;
  auto &manager = fixture.ctx.vsr.animationMgr;
  fixture.job.cameraIndex = vsr::core::INVALID_INDEX;
  fixture.job.generateCamera = true;
  fixture.job.globalFirstSelectedFrame = 3;
  fixture.job.frames[0].frameIndex = 3;
  fixture.job.frames[1].frameIndex = 7;
  manager.setAnimationTotalFrames(9);
  auto *positions =
      scene.getObject<vsr::scene::Geometry>(0)
          ->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  manager.addAnimation("moving bounds")
      .addCallbackBinding([positions](float time) {
        const vsr::math::float3 point(20.f * time, 0.5f, 0.f);
        positions->setData(&point, 1);
      });
  manager.setAnimationTime(fixture.clock.time);
  fixture.clock = manager.runtimeState();
  const size_t camerasBefore = scene.objectDB().camera.size();
  std::vector<vsr::math::float3> fitted;
  auto capture = [&](const vsr::app::OfflineRenderProgress &) {
    REQUIRE(scene.objectDB().camera.size() == camerasBefore + 1);
    vsr::core::foreach_item_const(scene.objectDB().camera, [&](auto *camera) {
      if (camera && camera->name() == "generated default Camera") {
        REQUIRE(camera->subtype() == vsr::scene::tokens::camera::perspective);
        fitted.push_back(
            *camera->template parameterValueAs<vsr::math::float3>("position"));
      }
    });
    return true;
  };
  auto expected = vsr::app::OfflineRenderStatus::Completed;
  SECTION("Sequence with authored Cameras") {}
  SECTION("Camera-less input")
  {
    std::vector<const vsr::scene::Camera *> cameras;
    vsr::core::foreach_item_const(scene.objectDB().camera, [&](auto *camera) {
      if (camera)
        cameras.push_back(camera);
    });
    for (auto *camera : cameras)
      scene.removeObject(camera);
    // This section uses a separate callback since the initial count changed.
    const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
    INFO(result.diagnostic);
    REQUIRE(result.status == expected);
    REQUIRE(scene.objectDB().camera.size() == 0);
    readRedImage(fixture.job.frames[0].destination);
    fixture.checkRestored();
    return;
  }
  SECTION("Cancellation")
  {
    expected = vsr::app::OfflineRenderStatus::Cancelled;
  }
  SECTION("Failure after generation")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    fixture.job.frames[1].destination = fixture.directory.string();
  }
  const auto result = vsr::app::renderOfflineJob(
      fixture.ctx, fixture.job, [&](const auto &progress) {
        capture(progress);
        return expected != vsr::app::OfflineRenderStatus::Cancelled
            || progress.position == 0;
      });
  INFO(result.diagnostic);
  REQUIRE(result.status == expected);
  REQUIRE(fitted.size() == 2);
  REQUIRE(fitted[0] == fitted[1]);
  REQUIRE(fitted[0].x == Approx(7.5f));
  REQUIRE(fitted[0].y == Approx(0.5f));
  REQUIRE(scene.objectDB().camera.size() == camerasBefore);
  fixture.checkRestored();
  readRedImage(fixture.job.frames[0].destination);
  // Simulate a rank whose local first frame is 7; global preparation stays 3.
  fixture.job.frames = {{7, (fixture.directory / "rank.png").string()}};
  fitted.clear();
  const auto rankResult =
      vsr::app::renderOfflineJob(fixture.ctx, fixture.job, capture);
  REQUIRE(rankResult.status == vsr::app::OfflineRenderStatus::Completed);
  REQUIRE(fitted.size() == 1);
  REQUIRE(fitted[0].x == Approx(7.5f));
  REQUIRE(scene.objectDB().camera.size() == camerasBefore);
  fixture.checkRestored();
}

TEST_CASE("Generated Camera preparation preserves exact Geometry entry state",
    "[App][OfflineRender][EntryState]")
{
  RenderFixture fixture;
  auto &scene = fixture.ctx.vsr.scene;
  auto &manager = fixture.ctx.vsr.animationMgr;
  auto geometry = scene.getObject<vsr::scene::Geometry>(0);
  manager.setAnimationTotalFrames(9);
  const float times[] = {0.f, 1.f};
  const float radii[] = {0.2f, 0.6f};
  manager.addAnimation("radius motion")
      .addObjectParameterBinding(
          geometry.data(), "radius", ANARI_FLOAT32, radii, times, 2);
  auto *positions =
      geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  manager.animations().front().addCallbackBinding([positions](float time) {
    const vsr::math::float3 point(4.f * time, 0.5f, 0.f);
    positions->setData(&point, 1);
  });
  manager.setAnimationTime(0.123f);
  REQUIRE_FALSE(manager.tick(0.04f));
  fixture.clock = manager.runtimeState();
  geometry->setParameter("radius", 0.9f)
      ->setDescription("entry radius, not the evaluated radius")
      .setMin(0.01f)
      .setMax(2.f)
      .setUsage(vsr::scene::VALUE_RANGE_TRANSFORM)
      .setStringValues({"small", "large"})
      .setStringSelection(1);
  fixture.job.generateCamera = true;
  fixture.job.cameraIndex = vsr::core::INVALID_INDEX;
  fixture.job.globalFirstSelectedFrame = 6;
  fixture.job.frames = {{6, (fixture.directory / "geometry.png").string()},
      {7, (fixture.directory / "later.png").string()}};
  fixture.job.overrides = {
      {geometry.data(), "radius", 0.1f}, {geometry.data(), "radius", 0.45f}};
  const size_t camerasBefore = scene.objectDB().camera.size();
  auto expected = vsr::app::OfflineRenderStatus::Completed;
  size_t completed = 2;
  size_t callbacks = 2;
  bool missing = false;
  bool enabled = true;
  vsr::core::Any original(0.9f);
  std::optional<std::string> exception;
  bool preparationException = false;
  bool unsupportedOverride = false;
  ThrowingRestoreDelegate *restorationDelegate = nullptr;
  auto material = scene.getObject<vsr::scene::Material>(0);
  SECTION("Completion") {}
  SECTION("Disabled parameter and metadata")
  {
    enabled = false;
    geometry->parameter("radius")->setEnabled(false);
  }
  SECTION("Entry type differs from animated and override type")
  {
    original = vsr::core::Any(0.9);
    geometry->setParameter("radius", original);
  }
  SECTION("Originally absent animated parameter")
  {
    missing = true;
    geometry->removeParameter("radius");
  }
  SECTION("Cancellation before any frame")
  {
    expected = vsr::app::OfflineRenderStatus::Cancelled;
    completed = 0;
    callbacks = 1;
  }
  SECTION("Cancellation after an overridden frame")
  {
    expected = vsr::app::OfflineRenderStatus::Cancelled;
    completed = 1;
  }
  SECTION("Device setup failure after preparation")
  {
    fixture.job.deviceIdentifier = "missing_offline_library";
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 0;
    callbacks = 0;
  }
  SECTION("Unsupported override fails after preparation")
  {
    fixture.job.overrides.push_back({geometry.data(), "radius", "invalid"});
    unsupportedOverride = true;
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 0;
    callbacks = 0;
  }
  SECTION("Write failure after an overridden frame")
  {
    fixture.job.frames[1].destination = fixture.directory.string();
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 1;
  }
  SECTION("Unexpected frame callback exception")
  {
    exception = "Geometry frame callback failed";
    completed = 1;
  }
  SECTION("Primary callback survives secondary clock restoration exception")
  {
    exception = "Geometry frame callback failed";
    completed = 1;
    manager.setTimeChangedCallback([&](float time) {
      if (time == fixture.clock.time)
        throw std::runtime_error("secondary clock restoration failed");
    });
  }
  SECTION("Ordinary clock restoration exception still restores parameters")
  {
    exception = "clock restoration failed";
    manager.setTimeChangedCallback([&](float time) {
      if (time == fixture.clock.time)
        throw std::runtime_error("clock restoration failed");
    });
  }
  SECTION("Observer failure preserves metadata and independent cleanup")
  {
    enabled = false;
    geometry->parameter("radius")->setEnabled(false);
    material->removeParameter("color");
    fixture.job.overrides.push_back(
        {material.data(), "color", vsr::math::float3(1.f, 0.f, 0.f)});
    restorationDelegate =
        scene.updateDelegate().emplace<ThrowingRestoreDelegate>();
    ++fixture.delegates;
    exception = "Geometry restoration observer failed";
  }
  SECTION("Preparation callback exception after animated mutation")
  {
    exception = "Geometry preparation failed";
    completed = 0;
    callbacks = 0;
    preparationException = true;
    manager.animations().front().addCallbackBinding([](float time) {
      if (time == 0.75f)
        throw std::runtime_error("Geometry preparation failed");
    });
  }
  const std::optional<vsr::scene::Parameter> saved = missing
      ? std::nullopt
      : std::optional<vsr::scene::Parameter>(*geometry->parameter("radius"));
  size_t visited = 0;
  auto callback = [&](const vsr::app::OfflineRenderProgress &progress) {
    ++visited;
    REQUIRE(scene.objectDB().camera.size() == camerasBefore + 1);
    if (!progress.position)
      REQUIRE(geometry->parameterValueAs<float>("radius") == 0.5f);
    if (progress.position) {
      REQUIRE(geometry->parameterValueAs<float>("radius") == 0.45f);
      REQUIRE(geometry->parameter("radius")->isEnabled());
      REQUIRE(positions->dataAs<vsr::math::float3>()[0].x == 3.f);
      if (exception && *exception == "Geometry frame callback failed")
        throw std::runtime_error(*exception);
    }
    return expected != vsr::app::OfflineRenderStatus::Cancelled
        || progress.position < completed;
  };
  if (exception) {
    REQUIRE_THROWS_WITH(
        vsr::app::renderOfflineJob(fixture.ctx, fixture.job, callback),
        *exception);
  } else {
    const auto result =
        vsr::app::renderOfflineJob(fixture.ctx, fixture.job, callback);
    INFO(result.diagnostic);
    REQUIRE(result.status == expected);
    REQUIRE(result.completedFrames == completed);
  }
  REQUIRE(visited == callbacks);
  auto checkEntry = [&] {
    fixture.checkRestored();
    REQUIRE(scene.objectDB().camera.size() == camerasBefore);
    REQUIRE(positions->dataAs<vsr::math::float3>()[0].x
        == 4.f * fixture.clock.time);
    const auto *parameter = geometry->parameter("radius");
    if (missing) {
      REQUIRE_FALSE(parameter);
    } else {
      REQUIRE(parameter);
      REQUIRE(parameter->value() == original);
      REQUIRE(parameter->value().type() == original.type());
      REQUIRE(parameter->isEnabled() == enabled);
      REQUIRE(parameter->description() == saved->description());
      REQUIRE(parameter->min() == saved->min());
      REQUIRE(parameter->max() == saved->max());
      REQUIRE(parameter->usage() == saved->usage());
      REQUIRE(parameter->stringValues() == saved->stringValues());
      REQUIRE(parameter->stringSelection() == saved->stringSelection());
    }
  };
  checkEntry();
  if (restorationDelegate) {
    REQUIRE_FALSE(material->parameter("color"));
    scene.updateDelegate().erase(restorationDelegate);
    --fixture.delegates;
  }
  if (completed)
    readRedImage(fixture.job.frames[0].destination);

  // Repair only the intentionally failing inputs/callbacks and render again
  // with the original target assignments, including last-wins ordering.
  manager.setTimeChangedCallback({});
  if (preparationException)
    manager.animations().front().removeCallbackBinding(1);
  fixture.job.deviceIdentifier = "helide";
  if (unsupportedOverride)
    fixture.job.overrides.pop_back();
  fixture.job.frames[1].destination =
      (fixture.directory / "later.png").string();
  const auto repeated = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  INFO(repeated.diagnostic);
  REQUIRE(repeated.status == vsr::app::OfflineRenderStatus::Completed);
  REQUIRE(repeated.completedFrames == 2);
  readRedImage(fixture.job.frames[0].destination);
  checkEntry();
}

TEST_CASE("Generated Cameras preserve authored override targets and job reuse",
    "[App][OfflineRender][OverrideTargets]")
{
  RenderFixture fixture;
  auto &scene = fixture.ctx.vsr.scene;
  auto camera = scene.getObject<vsr::scene::Camera>(fixture.job.cameraIndex);
  const float times[] = {0.f, 1.f};
  const float heights[] = {3.f, 5.f};
  auto &manager = fixture.ctx.vsr.animationMgr;
  manager.addAnimation("authored lens motion")
      .addObjectParameterBinding(
          camera.data(), "height", ANARI_FLOAT32, heights, times, 2);
  manager.setAnimationTime(fixture.clock.time);
  fixture.clock = manager.runtimeState();
  camera->setParameter("height", 2.f)
      ->setEnabled(false)
      .setDescription("authored lens");
  fixture.job.overrides = {{camera.data(), "height", 1.f},
      {camera.data(), "height", 1.5f},
      {camera.data(), "aspect", 0.73f}};
  fixture.job.generateCamera = true;
  fixture.job.cameraIndex = vsr::core::INVALID_INDEX;
  fixture.job.globalFirstSelectedFrame = 1;
  const size_t camerasBefore = scene.objectDB().camera.size();
  auto expected = vsr::app::OfflineRenderStatus::Completed;
  bool throwCallback = false;
  ThrowingRestoreDelegate *restorationDelegate = nullptr;
  bool failSetup = false;
  bool cancelImmediately = false;
  SECTION("Completion") {}
  SECTION("Cancellation before any frame")
  {
    expected = vsr::app::OfflineRenderStatus::Cancelled;
    cancelImmediately = true;
  }
  SECTION("Cancellation after an overridden frame")
  {
    expected = vsr::app::OfflineRenderStatus::Cancelled;
  }
  SECTION("Expected setup failure after Camera generation")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    failSetup = true;
    fixture.job.deviceIdentifier = "missing_offline_library";
  }
  SECTION("Write failure after an overridden frame")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    fixture.job.frames[1].destination = fixture.directory.string();
  }
  SECTION("Unexpected callback exception after an overridden frame")
  {
    throwCallback = true;
  }
  SECTION("Primary callback exception survives a restoration exception")
  {
    throwCallback = true;
    restorationDelegate =
        scene.updateDelegate().emplace<ThrowingRestoreDelegate>();
  }
  size_t callbacks = 0;
  auto callback = [&](const vsr::app::OfflineRenderProgress &progress) {
    ++callbacks;
    REQUIRE(scene.objectDB().camera.size() == camerasBefore + 1);
    for (const auto &assignment : fixture.job.overrides)
      REQUIRE(assignment.object() == camera.data());
    if (progress.position == 1) {
      REQUIRE(camera->parameterValueAs<float>("height") == 1.5f);
      REQUIRE(camera->parameter("height")->isEnabled());
      REQUIRE(camera->parameterValueAs<float>("aspect") == 0.73f);
      if (throwCallback)
        throw std::runtime_error("authored target callback failed");
    }
    return expected != vsr::app::OfflineRenderStatus::Cancelled
        || (!cancelImmediately && progress.position == 0);
  };
  if (throwCallback) {
    REQUIRE_THROWS_WITH(
        vsr::app::renderOfflineJob(fixture.ctx, fixture.job, callback),
        "authored target callback failed");
  } else {
    const auto result =
        vsr::app::renderOfflineJob(fixture.ctx, fixture.job, callback);
    INFO(result.diagnostic);
    REQUIRE(result.status == expected);
    const size_t completed = failSetup || cancelImmediately    ? 0
        : expected == vsr::app::OfflineRenderStatus::Completed ? 2
                                                               : 1;
    REQUIRE(result.completedFrames == completed);
  }
  REQUIRE(callbacks == (failSetup ? 0 : cancelImmediately ? 1 : 2));
  if (restorationDelegate)
    scene.updateDelegate().erase(restorationDelegate);
  REQUIRE(scene.objectDB().camera.size() == camerasBefore);
  REQUIRE(camera->subtype() == vsr::core::Token("orthographic"));
  REQUIRE(camera->parameterValueAs<float>("height") == 2.f);
  REQUIRE_FALSE(camera->parameter("height")->isEnabled());
  REQUIRE(camera->parameter("height")->description() == "authored lens");
  REQUIRE_FALSE(camera->parameter("aspect"));
  fixture.checkRestored();
  if (!failSetup && !cancelImmediately)
    readRedImage(fixture.job.frames[0].destination);

  // Repair only failed destinations/device, never reconstruct the targets.
  fixture.job.deviceIdentifier = "helide";
  fixture.job.frames[1].destination =
      (fixture.directory / "second.png").string();
  // Reuse the original request, not a reconstructed engine-local copy.
  const auto repeated = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  INFO(repeated.diagnostic);
  REQUIRE(repeated.status == vsr::app::OfflineRenderStatus::Completed);
  REQUIRE(scene.objectDB().camera.size() == camerasBefore);
  REQUIRE(camera->parameterValueAs<float>("height") == 2.f);
  REQUIRE_FALSE(camera->parameter("aspect"));
  for (const auto &assignment : fixture.job.overrides)
    REQUIRE(assignment.object() == camera.data());
  readRedImage(fixture.job.frames[1].destination);
  fixture.checkRestored();
}

TEST_CASE("Offline overrides reject invalid Scene targets before generation",
    "[App][OfflineRender][OverrideTargets]")
{
  RenderFixture fixture;
  vsr::scene::Scene otherScene;
  vsr::scene::Object unpooled(ANARI_CAMERA, "orthographic");
  auto &scene = fixture.ctx.vsr.scene;
  fixture.job.generateCamera = true;
  fixture.job.cameraIndex = vsr::core::INVALID_INDEX;
  fixture.job.globalFirstSelectedFrame = 1;
  SECTION("Null target")
  {
    fixture.job.overrides = {{nullptr, "height", 1.f}};
  }
  SECTION("Default target")
  {
    fixture.job.overrides.emplace_back();
  }
  SECTION("Unpooled target")
  {
    fixture.job.overrides = {{&unpooled, "height", 1.f}};
  }
  SECTION("Foreign Scene target")
  {
    auto foreign = otherScene.createObject<vsr::scene::Camera>("orthographic");
    fixture.job.overrides = {{foreign.data(), "height", 1.f}};
  }
  SECTION("Removed target must not bind to the generated Camera")
  {
    auto camera = scene.getObject<vsr::scene::Camera>(0);
    fixture.job.overrides = {{camera.data(), "height", 1.f}};
    scene.removeObject(camera.data());
    REQUIRE_FALSE(fixture.job.overrides.front().object());
  }
  const size_t camerasBefore = scene.objectDB().camera.size();
  const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  REQUIRE(result.status == vsr::app::OfflineRenderStatus::Failed);
  REQUIRE(result.diagnostic
      == "Override target must be a live object in the caller Scene");
  REQUIRE(result.completedFrames == 0);
  REQUIRE(scene.objectDB().camera.size() == camerasBefore);
  REQUIRE_FALSE(std::filesystem::exists(fixture.job.frames[0].destination));
  fixture.checkRestored();
}

TEST_CASE("Generated offline Camera validation cleans up failed jobs",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  size_t count = fixture.ctx.vsr.scene.objectDB().camera.size();
  fixture.job.generateCamera = true;
  fixture.job.globalFirstSelectedFrame = 1;
  fixture.job.cameraIndex = vsr::core::INVALID_INDEX;
  SECTION("Conflicting selection")
  {
    fixture.job.cameraIndex = 0;
  }
  SECTION("Missing global preparation frame")
  {
    fixture.job.globalFirstSelectedFrame = -1;
  }
  SECTION("Out of timeline preparation frame")
  {
    fixture.job.globalFirstSelectedFrame = 100;
  }
  SECTION("Incompatible lens")
  {
    fixture.job.cameraParameters = {{"height", 2.f}};
  }
  SECTION("Invalid geometric override")
  {
    fixture.job.cameraParameters = {{"up", vsr::math::float3(0.f, 0.f, 1.f)}};
  }
  SECTION("Invalid numeric override")
  {
    fixture.job.cameraParameters = {{"fovy", 0.f}};
  }
  SECTION("Invalid look-at override")
  {
    fixture.job.cameraLookAt =
        vsr::math::float3(0.f, 0.f, std::numeric_limits<float>::infinity());
  }
  SECTION("Device failure")
  {
    fixture.job.deviceIdentifier = "missing_offline_library";
  }
  SECTION("Empty scene bounds")
  {
    fixture.ctx.vsr.scene.removeAllObjects();
    count = fixture.ctx.vsr.scene.objectDB().camera.size();
  }
  SECTION("Animation preparation exception propagates after cleanup")
  {
    fixture.ctx.vsr.animationMgr.addAnimation("throws").addCallbackBinding(
        [](float) { throw std::runtime_error("cannot evaluate"); });
    REQUIRE_THROWS_WITH(vsr::app::renderOfflineJob(fixture.ctx, fixture.job),
        "cannot evaluate");
    REQUIRE(fixture.ctx.vsr.scene.objectDB().camera.size() == count);
    fixture.checkRestored();
    return;
  }
  const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  INFO(result.diagnostic);
  REQUIRE(result.status == vsr::app::OfflineRenderStatus::Failed);
  REQUIRE(fixture.ctx.vsr.scene.objectDB().camera.size() == count);
  fixture.checkRestored();
}

TEST_CASE(
    "Offline animation produces fresh sparse frames", "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto &manager = fixture.ctx.vsr.animationMgr;
  auto &scene = fixture.ctx.vsr.scene;
  auto camera = scene.getObject<vsr::scene::Camera>(fixture.job.cameraIndex);
  manager.setAnimationTotalFrames(9);
  fixture.job.frames[1].frameIndex = 7;
  auto &animation = manager.addAnimation("motion");
  const float times[] = {0.f, 1.f};
  std::vector<float> evaluated;
  animation.addCallbackBinding([&](float time) { evaluated.push_back(time); });

  SECTION("Geometry-only animation updates a vertex Array")
  {
    auto geometry = scene.getObject<vsr::scene::Geometry>(0);
    auto *positions =
        geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
    animation.addCallbackBinding([positions](float time) {
      const vsr::math::float3 point(-0.8f + 1.6f * time, 0.5f, 0.f);
      positions->setData(&point, 1);
    });
  }
  SECTION("Camera animation preserves unspecified animated parameters")
  {
    const vsr::math::float3 positions[] = {{0.8f, 0.f, 3.f}, {-0.8f, 0.f, 3.f}};
    const vsr::math::float3 directions[] = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
    animation.addObjectParameterBinding(
        camera.data(), "position", ANARI_FLOAT32_VEC3, positions, times, 2);
    animation.addObjectParameterBinding(
        camera.data(), "direction", ANARI_FLOAT32_VEC3, directions, times, 2);
    fixture.job.overrides.push_back(
        {camera.data(), "direction", vsr::math::float3(0.f, 0.f, -1.f)});
  }
  manager.setAnimationTime(fixture.clock.time);
  fixture.clock = manager.runtimeState();
  const auto savedPosition = camera->parameter("position")->value();
  // A caller edit need not match the value authored by animation at this time.
  if (!fixture.job.overrides.empty())
    camera->setParameter("direction", vsr::math::float3(0.f, 0.2f, -1.f));
  const auto savedDirection = camera->parameter("direction")->value();
  evaluated.clear();
  const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  INFO(result.diagnostic);
  REQUIRE(result.status == vsr::app::OfflineRenderStatus::Completed);
  REQUIRE(result.completedFrames == 2);
  REQUIRE(evaluated == std::vector<float>{0.125f, 0.875f, fixture.clock.time});
  fixture.checkRestored();
  REQUIRE(camera->parameter("position")->value() == savedPosition);
  REQUIRE(camera->parameter("direction")->value() == savedDirection);
  REQUIRE(camera->subtype() == vsr::core::Token("orthographic"));
  REQUIRE(camera->parameterValueAs<float>("height") == 2.f);
  REQUIRE_FALSE(camera->parameter("aspect"));
  const auto first = readRedImage(fixture.job.frames[0].destination);
  const auto second = readRedImage(fixture.job.frames[1].destination);
  REQUIRE(second.centerX - first.centerX > 12.);
  size_t different = 0;
  for (size_t i = 0; i < first.mask.size(); ++i)
    different += first.mask[i] != second.mask[i];
  REQUIRE(different > 60);

  // Compare the late sequence frame with the same frame rendered alone.
  // Previous-frame accumulation would leave a second silhouette behind.
  fixture.job.frames = {{7, (fixture.directory / "alone.png").string()}};
  const auto aloneResult = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  REQUIRE(aloneResult.status == vsr::app::OfflineRenderStatus::Completed);
  const auto alone = readRedImage(fixture.job.frames[0].destination);
  different = 0;
  for (size_t i = 0; i < alone.mask.size(); ++i)
    different += alone.mask[i] != second.mask[i];
  REQUIRE(different < 12);
  fixture.checkRestored();
}

TEST_CASE("Camera controls stay authoritative after animated Camera evaluation",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto camera = fixture.ctx.vsr.scene.getObject<vsr::scene::Camera>(
      fixture.job.cameraIndex);
  auto &manager = fixture.ctx.vsr.animationMgr;
  manager.setAnimationTotalFrames(9);
  fixture.job.frames[1].frameIndex = 7;
  const float times[] = {0.f, 1.f};
  const vsr::math::float3 positions[] = {{-1.f, 0.f, 3.f}, {1.f, 0.f, 3.f}};
  const vsr::math::float3 directions[] = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
  auto &animation = manager.addAnimation("authored camera motion");
  animation.addObjectParameterBinding(
      camera.data(), "position", ANARI_FLOAT32_VEC3, positions, times, 2);
  animation.addObjectParameterBinding(
      camera.data(), "direction", ANARI_FLOAT32_VEC3, directions, times, 2);
  manager.setAnimationTime(fixture.clock.time);
  fixture.clock = manager.runtimeState();
  const auto savedPosition = camera->parameter("position")->value();
  const auto savedDirection = camera->parameter("direction")->value();
  fixture.job.cameraParameters = {
      {"position", vsr::math::float3(0.f, 0.f, 3.f)}};
  fixture.job.cameraLookAt = vsr::math::float3(0.f, 0.f, 0.f);
  const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  INFO(result.diagnostic);
  REQUIRE(result.status == vsr::app::OfflineRenderStatus::Completed);
  const auto first = readRedImage(fixture.job.frames[0].destination);
  const auto second = readRedImage(fixture.job.frames[1].destination);
  REQUIRE(std::abs(first.centerX - second.centerX) < 1.);
  REQUIRE(std::abs(int(first.count) - int(second.count)) < 12);
  REQUIRE(camera->parameter("position")->value() == savedPosition);
  REQUIRE(camera->parameter("direction")->value() == savedDirection);
  REQUIRE(camera->parameterValueAs<float>("height") == 2.f);
  fixture.checkRestored();
}

TEST_CASE("Offline overrides restore exact caller state on every exit",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto camera = fixture.ctx.vsr.scene.getObject<vsr::scene::Camera>(
      fixture.job.cameraIndex);
  camera->parameter("height")->setEnabled(false).setDescription(
      "authored lens");
  fixture.job.overrides = {{camera.data(), "height", 1.f},
      {camera.data(), "height", 2.f},
      {camera.data(), "aspect", 1.f}};
  REQUIRE_FALSE(camera->parameter("aspect"));
  vsr::app::OfflineRenderCallback callback;
  auto expected = vsr::app::OfflineRenderStatus::Completed;
  size_t completed = 2;
  std::optional<float> savedAspect;
  std::optional<std::string> exception;
  SECTION("Success") {}
  SECTION("Authored aspect and lens settings survive output sizing")
  {
    savedAspect = 0.73f;
    camera->setParameter("aspect", *savedAspect);
    camera->setParameter("focusDistance", 4.2f);
    fixture.job.width = 64;
  }
  SECTION("Cancellation after an overridden frame")
  {
    expected = vsr::app::OfflineRenderStatus::Cancelled;
    completed = 1;
    callback = [](const vsr::app::OfflineRenderProgress &p) {
      return p.position == 0;
    };
  }
  SECTION("Write failure after applying overrides")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 0;
    fixture.job.frames[0].destination = fixture.directory.string();
  }
  SECTION("Setup failure")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 0;
    fixture.job.deviceIdentifier = "missing_offline_library";
  }
  SECTION("Unsupported typed override")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 0;
    fixture.job.overrides.push_back({camera.data(), "fovy", 0.8f});
  }
  SECTION("Wrong override type")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 0;
    fixture.job.overrides.push_back({camera.data(), "height", "invalid"});
  }
  SECTION("Animation evaluation exception")
  {
    exception = "animation failed";
    completed = 1;
    fixture.ctx.vsr.animationMgr.addAnimation("throws").addCallbackBinding(
        [](float time) {
          if (time > 0.02f && time < 0.04f)
            throw std::runtime_error("animation failed");
        });
  }
  SECTION("Restoration notification exception")
  {
    exception = "restore notification failed";
    fixture.ctx.vsr.animationMgr.setTimeChangedCallback([&](float time) {
      if (time == fixture.clock.time)
        throw std::runtime_error("restore notification failed");
    });
  }
  SECTION("Exception after an overridden frame")
  {
    exception = "after overrides";
    completed = 1;
    callback = [](const vsr::app::OfflineRenderProgress &p) {
      if (p.position)
        throw std::runtime_error("after overrides");
      return true;
    };
  }
  if (exception) {
    REQUIRE_THROWS_WITH(
        vsr::app::renderOfflineJob(fixture.ctx, fixture.job, callback),
        *exception);
  } else {
    const auto result =
        vsr::app::renderOfflineJob(fixture.ctx, fixture.job, callback);
    INFO(result.diagnostic);
    REQUIRE(result.status == expected);
    REQUIRE(result.completedFrames == completed);
  }
  fixture.checkRestored();
  REQUIRE(camera->subtype() == vsr::core::Token("orthographic"));
  REQUIRE(camera->parameterValueAs<float>("height") == 2.f);
  REQUIRE_FALSE(camera->parameter("height")->isEnabled());
  REQUIRE(camera->parameter("height")->description() == "authored lens");
  REQUIRE(camera->parameterValueAs<float>("aspect") == savedAspect);
  if (savedAspect) {
    REQUIRE(camera->parameterValueAs<float>("focusDistance") == 4.2f);
    int width = 0, height = 0, channels = 0;
    const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> image(
        stbi_load(fixture.job.frames[0].destination.c_str(),
            &width,
            &height,
            &channels,
            4),
        stbi_image_free);
    REQUIRE(image);
    REQUIRE(width == 64);
    REQUIRE(height == 32);
  }
  if (completed)
    REQUIRE(std::filesystem::exists(fixture.job.frames[0].destination));
  if (completed < 2)
    REQUIRE_FALSE(std::filesystem::exists(fixture.job.frames[1].destination));
  // Pending playback time is externally observable on the next tick.
  REQUIRE(fixture.ctx.vsr.animationMgr.tick(0.09f));
  REQUIRE(fixture.ctx.vsr.animationMgr.getAnimationFrame() == 8);
}

TEST_CASE("Offline restoration notifies after restoring exact metadata",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto &scene = fixture.ctx.vsr.scene;
  auto camera = scene.getObject<vsr::scene::Camera>(fixture.job.cameraIndex);
  camera->parameter("height")->setEnabled(false).setDescription(
      "original metadata");
  fixture.job.overrides = {
      {camera.data(), "height", 1.f}, {camera.data(), "aspect", 0.73f}};
  auto *observer = scene.updateDelegate().emplace<ThrowingRestoreDelegate>();
  ++fixture.delegates;
  vsr::app::OfflineRenderCallback callback;
  std::string expected = "authored restoration observer failed";
  SECTION("Ordinary exit propagates the restoration exception") {}
  SECTION("Unwinding preserves the callback exception")
  {
    expected = "original frame exception";
    callback = [](const auto &p) {
      if (p.position)
        throw std::runtime_error("original frame exception");
      return true;
    };
  }
  REQUIRE_THROWS_WITH(
      vsr::app::renderOfflineJob(fixture.ctx, fixture.job, callback), expected);
  fixture.checkRestored();
  REQUIRE(camera->parameterValueAs<float>("height") == 2.f);
  REQUIRE_FALSE(camera->parameter("height")->isEnabled());
  REQUIRE(camera->parameter("height")->description() == "original metadata");
  REQUIRE_FALSE(camera->parameter("aspect"));
  scene.updateDelegate().erase(observer);
  --fixture.delegates;
  fixture.job.frames = {{0, (fixture.directory / "retry.png").string()}};
  REQUIRE(vsr::app::renderOfflineJob(fixture.ctx, fixture.job).status
      == vsr::app::OfflineRenderStatus::Completed);
  fixture.checkRestored();
}

TEST_CASE("Offline unwinding preserves the original exception and cleans up",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto &scene = fixture.ctx.vsr.scene;
  auto &manager = fixture.ctx.vsr.animationMgr;
  auto camera = scene.getObject<vsr::scene::Camera>(fixture.job.cameraIndex);
  camera->parameter("height")->setEnabled(false).setDescription(
      "authored lens");
  fixture.job.overrides = {
      {camera.data(), "height", 1.f}, {camera.data(), "aspect", 0.73f}};
  const auto count = scene.objectDB().camera.size();
  SECTION("Nonstandard frame exception")
  {
    bool caught = false;
    try {
      vsr::app::renderOfflineJob(fixture.ctx, fixture.job, [](const auto &p) {
        if (p.position)
          throw 42;
        return true;
      });
    } catch (int value) {
      REQUIRE(value == 42);
      caught = true;
    }
    REQUIRE(caught);
  }
  SECTION("Secondary restoration exception does not replace the primary")
  {
    manager.setTimeChangedCallback([&](float time) {
      if (time == fixture.clock.time)
        throw std::runtime_error("secondary cleanup failure");
    });
    REQUIRE_THROWS_WITH(vsr::app::renderOfflineJob(fixture.ctx,
                            fixture.job,
                            [](const auto &p) {
                              if (p.position)
                                throw std::runtime_error(
                                    "primary callback failure");
                              return true;
                            }),
        "primary callback failure");
    manager.setTimeChangedCallback({});
  }
  SECTION("Generated Camera is removed after an unexpected frame exception")
  {
    fixture.job.overrides.clear();
    fixture.job.cameraParameters = {{"fovy", vsr::math::radians(40.f)}};
    fixture.job.generateCamera = true;
    fixture.job.cameraIndex = vsr::core::INVALID_INDEX;
    fixture.job.globalFirstSelectedFrame = 1;
    REQUIRE_THROWS_WITH(vsr::app::renderOfflineJob(fixture.ctx,
                            fixture.job,
                            [](const auto &p) {
                              if (p.position)
                                throw std::runtime_error(
                                    "generated callback failure");
                              return true;
                            }),
        "generated callback failure");
  }
  fixture.checkRestored();
  REQUIRE(scene.objectDB().camera.size() == count);
  REQUIRE(camera->parameterValueAs<float>("height") == 2.f);
  REQUIRE_FALSE(camera->parameter("height")->isEnabled());
  REQUIRE(camera->parameter("height")->description() == "authored lens");
  REQUIRE_FALSE(camera->parameter("aspect"));
  REQUIRE(std::filesystem::exists(fixture.job.frames[0].destination));
  REQUIRE_FALSE(std::filesystem::exists(fixture.job.frames[1].destination));
  fixture.job.frames = {{0, (fixture.directory / "retry.png").string()}};
  const auto retry = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  INFO(retry.diagnostic);
  REQUIRE(retry.status == vsr::app::OfflineRenderStatus::Completed);
  fixture.checkRestored();
}

TEST_CASE(
    "Resolved offline jobs render real oriented PNGs", "[App][OfflineRender]")
{
  RenderFixture fixture;
  size_t callbacks = 0;
  const auto result = vsr::app::renderOfflineJob(fixture.ctx,
      fixture.job,
      [&](const vsr::app::OfflineRenderProgress &progress) {
        REQUIRE(progress.position == callbacks++);
        REQUIRE(progress.totalFrames == 2);
        REQUIRE(fixture.ctx.vsr.scene.updateDelegate().size()
            == fixture.delegates + 1);
        return true;
      });
  INFO(result.diagnostic);
  REQUIRE(result.status == vsr::app::OfflineRenderStatus::Completed);
  REQUIRE(result.completedFrames == 2);
  REQUIRE(callbacks == 2);
  fixture.checkRestored();
  for (const auto &frame : fixture.job.frames) {
    int width = 0, height = 0, channels = 0;
    const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> image(
        stbi_load(frame.destination.c_str(), &width, &height, &channels, 4),
        stbi_image_free);
    REQUIRE(image);
    REQUIRE(width == 32);
    REQUIRE(height == 32);
    int topRed = 0, bottomRed = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto *pixel = image.get() + 4 * (y * width + x);
        if (pixel[0] > pixel[1] + 20) {
          if (y < height / 2)
            ++topRed;
          else
            ++bottomRed;
        }
      }
    }
    REQUIRE(topRed > 10);
    REQUIRE(bottomRed == 0);
  }
}

TEST_CASE("Resolved offline jobs propagate cancellation and failure",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  SECTION("Cancellation after one completed frame")
  {
    const auto result = vsr::app::renderOfflineJob(
        fixture.ctx, fixture.job, [](const vsr::app::OfflineRenderProgress &p) {
          return p.position == 0;
        });
    REQUIRE(result.status == vsr::app::OfflineRenderStatus::Cancelled);
    REQUIRE(result.completedFrames == 1);
    REQUIRE(std::filesystem::exists(fixture.job.frames[0].destination));
    REQUIRE_FALSE(std::filesystem::exists(fixture.job.frames[1].destination));
  }
  SECTION("A directory is not a PNG destination")
  {
    fixture.job.frames[0].destination = fixture.directory.string();
    const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
    REQUIRE(result.status == vsr::app::OfflineRenderStatus::Failed);
    REQUIRE(result.completedFrames == 0);
    REQUIRE(result.destination == fixture.directory.string());
    REQUIRE_FALSE(result.diagnostic.empty());
  }
  SECTION("Throwing callback after animation advancement")
  {
    REQUIRE_THROWS_WITH(vsr::app::renderOfflineJob(fixture.ctx,
                            fixture.job,
                            [](const vsr::app::OfflineRenderProgress &p) {
                              if (p.position == 1)
                                throw std::runtime_error("callback failed");
                              return true;
                            }),
        "callback failed");
  }
  SECTION("Unsupported AOV cannot be successful")
  {
    vsr::app::ANARIDeviceManager devices;
    auto device = devices.loadDevice(fixture.job.deviceIdentifier);
    REQUIRE(device);
    const auto *aov = vsr::tests::unsupportedAOV(device);
    anari::release(device, device);
    if (aov) {
      INFO("Unsupported channel: " << aov->channel);
      fixture.job.aovType = aov->type;
      fixture.job.samples = 1;
      const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
      // Failure must precede visualization/writing even for one sample.
      REQUIRE(result.status == vsr::app::OfflineRenderStatus::Failed);
      REQUIRE(result.completedFrames == 0);
      REQUIRE(result.diagnostic.find(aov->channel) != std::string::npos);
      REQUIRE_FALSE(std::filesystem::exists(fixture.job.frames[0].destination));
    } else {
      WARN("SKIP unsupported-channel failure only: "
          << fixture.job.deviceIdentifier
          << " supports all tested optional AOV channels");
    }
  }
  fixture.checkRestored();
  // A second actual job on the same caller proves cleanup did not poison it.
  fixture.job.aovType = vsr::rendering::AOVType::NONE;
  fixture.job.frames = {{0, (fixture.directory / "retry.png").string()}};
  const auto retry = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  INFO(retry.diagnostic);
  REQUIRE(retry.status == vsr::app::OfflineRenderStatus::Completed);
  fixture.checkRestored();
}

TEST_CASE("Legacy sequence adapter retains naming and callback selection",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto &config = fixture.ctx.offline;
  config.renderer.rendererObjects.push_back(std::move(fixture.renderer));
  config.renderer.activeRenderer = 0;
  config.renderer.libraryName = "default@helide";
  config.camera.cameraIndex = fixture.job.cameraIndex;
  config.frame.width = 16;
  config.frame.height = 16;
  config.frame.samples = 1;
  config.frame.numFrames = 103;
  config.frame.renderSubset = true;
  config.frame.startFrame = 100;
  config.frame.endFrame = 102;
  config.frame.frameIncrement = 2;
  std::vector<int> visited;
  const auto result = vsr::app::renderAnimationSequence(fixture.ctx,
      fixture.directory.string(),
      "legacy_",
      [&](int frame, int total) {
        visited.push_back(frame);
        REQUIRE(total == 103);
        return true;
      });
  REQUIRE(result.status == vsr::app::OfflineRenderStatus::Completed);
  REQUIRE(result.completedFrames == 2);
  REQUIRE(visited == std::vector<int>{100, 102});
  REQUIRE(std::filesystem::exists(fixture.directory / "legacy_0100.png"));
  REQUIRE(std::filesystem::exists(fixture.directory / "legacy_0102.png"));
  fixture.checkRestored();
  REQUIRE_THROWS_WITH(vsr::app::renderAnimationSequence(fixture.ctx,
                          fixture.directory.string(),
                          "throwing_",
                          [](int, int) -> bool {
                            throw std::runtime_error("legacy callback failed");
                          }),
      "legacy callback failed");
  fixture.checkRestored();
  config.renderer.activeRenderer = 999;
  const auto invalid = vsr::app::renderAnimationSequence(
      fixture.ctx, fixture.directory.string(), "invalid_");
  REQUIRE(invalid.status == vsr::app::OfflineRenderStatus::Failed);
  REQUIRE_FALSE(invalid.diagnostic.empty());
  REQUIRE_FALSE(
      std::filesystem::exists(fixture.directory / "invalid_0100.png"));
  fixture.checkRestored();
}

TEST_CASE("UI sequence adapter renders and restores its caller",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  auto &ctx = fixture.ctx;
  auto &config = ctx.offline;
  config.renderer.rendererObjects.push_back(std::move(fixture.renderer));
  config.renderer.activeRenderer = 0;
  config.renderer.libraryName = "default@helide";
  config.camera.cameraIndex = fixture.job.cameraIndex;
  config.frame.width = 32;
  config.frame.height = 32;
  config.frame.samples = 2;
  config.frame.renderSubset = true;
  config.frame.startFrame = 1;
  config.frame.endFrame = 7;
  config.frame.frameIncrement = 3;
  ctx.vsr.animationMgr.setAnimationTotalFrames(9);
  auto geometry = ctx.vsr.scene.getObject<vsr::scene::Geometry>(0);
  auto *positions =
      geometry->parameterValueAsObject<vsr::scene::Array>("vertex.position");
  ctx.vsr.animationMgr.addAnimation("geometry")
      .addCallbackBinding([positions](float time) {
        const vsr::math::float3 point(-0.8f + 1.6f * time, 0.5f, 0.f);
        positions->setData(&point, 1);
      });
  fixture.clock = ctx.vsr.animationMgr.runtimeState();
  auto expected = vsr::app::OfflineRenderStatus::Completed;
  size_t completed = 3;
  bool cancel = false;
  SECTION("Success and permitted overwrite")
  {
    std::filesystem::copy_file(__FILE__, fixture.directory / "ui_0001.png");
  }
  SECTION("Cancellation after the first actual image")
  {
    expected = vsr::app::OfflineRenderStatus::Cancelled;
    completed = 1;
    cancel = true;
  }
  SECTION("Write failure is returned to the UI")
  {
    expected = vsr::app::OfflineRenderStatus::Failed;
    completed = 1;
    REQUIRE(
        std::filesystem::create_directory(fixture.directory / "ui_0004.png"));
  }
  SECTION("AOV settings reach the engine")
  {
    config.aov.aovType = vsr::rendering::AOVType::DEPTH;
    config.aov.depthMin = 0.f;
    config.aov.depthMax = 5.f;
  }
  std::vector<int> visited;
  const auto result = vsr::app::renderAnimationSequence(
      ctx, fixture.directory.string(), "ui_", [&](int frame, int total) {
        REQUIRE(total == 9);
        REQUIRE(ctx.vsr.scene.updateDelegate().size() == fixture.delegates + 1);
        visited.push_back(frame);
        return !cancel || frame == 1;
      });
  INFO(result.diagnostic);
  REQUIRE(result.status == expected);
  REQUIRE(result.completedFrames == completed);
  REQUIRE(visited.front() == 1);
  REQUIRE(visited.back() == (completed == 3 ? 7 : 4));
  if (expected == vsr::app::OfflineRenderStatus::Failed) {
    REQUIRE(result.destination == (fixture.directory / "ui_0004.png").string());
    REQUIRE_FALSE(result.diagnostic.empty());
  }
  if (config.aov.aovType == vsr::rendering::AOVType::NONE)
    REQUIRE(
        readRedImage((fixture.directory / "ui_0001.png").string()).count > 20);
  REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "ui_0002.png"));
  fixture.checkRestored();
}

TEST_CASE("Resolved offline jobs validate finite increasing depth ranges",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  fixture.job.aovType = vsr::rendering::AOVType::DEPTH;
  fixture.job.frames.resize(1);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  struct RangeCase
  {
    float lower;
    float upper;
    bool accepted;
  };
  for (const auto range : {RangeCase{-5.f, -1.f, true},
           RangeCase{-1.f, 5.f, true},
           RangeCase{1.f, 5.f, true},
           RangeCase{1.f, 1.f, false},
           RangeCase{2.f, 1.f, false},
           RangeCase{nan, 5.f, false},
           RangeCase{0.f, nan, false},
           RangeCase{-inf, 5.f, false},
           RangeCase{inf, 5.f, false},
           RangeCase{0.f, -inf, false},
           RangeCase{0.f, inf, false},
           RangeCase{-3e38f, 3e38f, false}}) {
    INFO("depth range: " << range.lower << ", " << range.upper);
    fixture.job.depthMin = range.lower;
    fixture.job.depthMax = range.upper;
    const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
    if (range.accepted) {
      REQUIRE(result.status == vsr::app::OfflineRenderStatus::Completed);
      REQUIRE(result.completedFrames == 1);
      int width = 0, height = 0, channels = 0;
      const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
          stbi_load(fixture.job.frames[0].destination.c_str(),
              &width,
              &height,
              &channels,
              4),
          stbi_image_free);
      REQUIRE(pixels);
      REQUIRE(width == 32);
      REQUIRE(height == 32);
      for (int i = 0; i < width * height; ++i) {
        REQUIRE(pixels.get()[4 * i] == pixels.get()[4 * i + 1]);
        REQUIRE(pixels.get()[4 * i] == pixels.get()[4 * i + 2]);
      }
      REQUIRE(std::filesystem::remove(fixture.job.frames[0].destination));
    } else {
      REQUIRE(result.status == vsr::app::OfflineRenderStatus::Failed);
      REQUIRE(result.diagnostic == "Invalid AOV depth range");
      REQUIRE(result.completedFrames == 0);
      REQUIRE_FALSE(std::filesystem::exists(fixture.job.frames[0].destination));
    }
    fixture.checkRestored();
  }
}

TEST_CASE("Resolved offline jobs reject invalid setup without fallback",
    "[App][OfflineRender]")
{
  RenderFixture fixture;
  SECTION("Invalid Camera")
  {
    fixture.job.cameraIndex = 9999;
  }
  SECTION("Invalid dimensions")
  {
    fixture.job.width = UINT32_MAX;
  }
  SECTION("Zero samples")
  {
    fixture.job.samples = 0;
  }
  SECTION("Invalid renderer")
  {
    fixture.job.renderer = nullptr;
  }
  SECTION("Invalid AOV")
  {
    fixture.job.aovType = static_cast<vsr::rendering::AOVType>(999);
  }
  SECTION("Overflowing image arithmetic")
  {
    fixture.job.width = 20000;
    fixture.job.height = 20000;
  }
  SECTION("Overflowing samples")
  {
    fixture.job.samples = UINT32_MAX;
  }
  SECTION("Invalid timeline frame")
  {
    fixture.job.frames[0].frameIndex = -1;
  }
  SECTION("Missing library")
  {
    fixture.job.deviceIdentifier = "vsr_missing_test_library";
  }
  SECTION("Invalid device subtype")
  {
    fixture.job.deviceIdentifier = "vsr_missing_subtype@helide";
  }
  SECTION("Invalid renderer subtype")
  {
    vsr::scene::Object unsupported(ANARI_RENDERER, "vsr_missing_renderer");
    fixture.job.renderer = &unsupported;
    const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
    REQUIRE(result.status == vsr::app::OfflineRenderStatus::Failed);
    fixture.checkRestored();
    return;
  }
  const auto result = vsr::app::renderOfflineJob(fixture.ctx, fixture.job);
  REQUIRE(result.status == vsr::app::OfflineRenderStatus::Failed);
  REQUIRE_FALSE(result.diagnostic.empty());
  REQUIRE_FALSE(std::filesystem::exists(fixture.job.frames[0].destination));
  fixture.checkRestored();
}
