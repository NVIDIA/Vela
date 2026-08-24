// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_animation
#include <vsr/animation/Animation.hpp>
#include <vsr/animation/AnimationManager.hpp>
// vsr_core
#include <vsr/core/Timer.hpp>
#include <vsr/scene/Scene.hpp>
// vsr_rendering
#include <vsr/rendering/pipeline/ImagePipeline.h>
#include <vsr/rendering/pipeline/passes/VisualizeAOVPass.h>
#include <vsr/rendering/index/RenderIndexAllLayers.hpp>
#include <vsr/rendering/view/ManipulatorToAnari.hpp>
// vsr_app
#include <vsr/app/ApplicationDump.h>
#include <vsr/app/Context.h>
// stb_image
#include "stb_image_write.h"
// std
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

// Application state //////////////////////////////////////////////////////////

static std::unique_ptr<vsr::core::DataTree> g_stateFile;
static std::unique_ptr<vsr::rendering::RenderIndexAllLayers> g_renderIndex;
static std::unique_ptr<vsr::rendering::ImagePipeline> g_renderPipeline;
static vsr::core::Timer g_timer;
static vsr::rendering::Manipulator g_manipulator;
static std::vector<vsr::rendering::CameraPose> g_cameraPoses;
static std::unique_ptr<vsr::app::Context> g_ctx;

static vsr::core::Token g_deviceName;
static anari::Library g_library{nullptr};
static anari::Device g_device{nullptr};
static anari::Camera g_camera{nullptr};

// Helper functions ///////////////////////////////////////////////////////////

static void loadANARIDevice()
{
  auto statusFunc = [](const void *,
                        ANARIDevice,
                        ANARIObject,
                        ANARIDataType,
                        ANARIStatusSeverity severity,
                        ANARIStatusCode,
                        const char *message) {
    if (severity == ANARI_SEVERITY_FATAL_ERROR) {
      fprintf(stderr, "[ANARI][FATAL] %s\n", message);
      std::exit(1);
    } else if (severity == ANARI_SEVERITY_ERROR)
      fprintf(stderr, "[ANARI][ERROR] %s\n", message);
#if 0
  else if (severity == ANARI_SEVERITY_WARNING)
    fprintf(stderr, "[ANARI][WARN ] %s\n", message);
  else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING)
    fprintf(stderr, "[ANARI][PERF ] %s\n", message);
#endif
#if 0
  else if (severity == ANARI_SEVERITY_INFO)
    fprintf(stderr, "[ANARI][INFO ] %s\n", message);
  else if (severity == ANARI_SEVERITY_DEBUG)
    fprintf(stderr, "[ANARI][DEBUG] %s\n", message);
#endif
  };

  auto library = g_ctx->offline.renderer.libraryName;
  g_deviceName = library;

  printf("Loading ANARI device from '%s' library...", library.c_str());
  fflush(stdout);

  g_timer.start();
  g_library = anari::loadLibrary(library.c_str(), statusFunc);
  g_device = anari::newDevice(g_library, "default");
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static void initVSRDataTree()
{
  printf("Initializing VSR data tree...");
  fflush(stdout);

  g_timer.start();
  g_stateFile = std::make_unique<vsr::core::DataTree>();
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static void initVSRRenderIndex()
{
  printf("Initializing VSR render index...");
  fflush(stdout);

  g_timer.start();
  g_renderIndex = std::make_unique<vsr::rendering::RenderIndexAllLayers>(
      g_ctx->vsr.scene, g_deviceName, g_device);
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static bool loadState(const char *filename)
{
  printf("Loading state from '%s'...", filename);
  fflush(stdout);

  g_timer.start();
  const bool loaded = g_stateFile->load(filename);
  g_timer.end();

  printf("%s (%.2f ms)\n", loaded ? "done" : "failed", g_timer.milliseconds());
  return loaded;
}

static bool populateVSRContext()
{
  printf("Populating VSR context...");
  fflush(stdout);

  g_timer.start();
  const bool populated =
      vsr::app::deserialize_ApplicationDump(*g_ctx, g_stateFile->root());
  g_timer.end();

  printf(
      "%s (%.2f ms)\n", populated ? "done" : "failed", g_timer.milliseconds());
  return populated;
}

static void populateRenderIndex()
{
  printf("Populating VSR render index...");
  fflush(stdout);

  g_timer.start();
  g_renderIndex->populate();
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static void setupCameraManipulator()
{
  printf("Setting up camera...");
  fflush(stdout);

  g_timer.start();
  if (!g_ctx->view.poses.empty()) {
    g_cameraPoses = g_ctx->view.poses;
    printf("using %zu camera poses from file...", g_cameraPoses.size());
    fflush(stdout);
  } else {
    printf("from world bounds...");
    fflush(stdout);
    g_cameraPoses.push_back(g_renderIndex->computeDefaultView());
  }
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static void setupImagePipeline()
{
  const auto frameWidth = g_ctx->offline.frame.width;
  const auto frameHeight = g_ctx->offline.frame.height;

  printf("Setting up render pipeline (%u x %u)...", frameWidth, frameHeight);
  fflush(stdout);

  g_timer.start();
  g_renderPipeline =
      std::make_unique<vsr::rendering::ImagePipeline>(frameWidth, frameHeight);

  g_camera = anari::newObject<anari::Camera>(g_device, "perspective");
  anari::setParameter(
      g_device, g_camera, "aspect", frameWidth / float(frameHeight));
  anari::setParameter(g_device, g_camera, "fovy", anari::radians(40.f));
  anari::setParameter(g_device,
      g_camera,
      "apertureRadius",
      g_ctx->offline.camera.apertureRadius);
  anari::setParameter(
      g_device, g_camera, "focusDistance", g_ctx->offline.camera.focusDistance);
  anari::commitParameters(g_device, g_camera);

  auto activeRenderer = g_ctx->offline.renderer.activeRenderer;
  auto &ro = g_ctx->offline.renderer.rendererObjects[activeRenderer];
  auto r = anari::newObject<anari::Renderer>(g_device, ro.name().c_str());
  ro.updateAllANARIParameters(g_device, r);
  anari::commitParameters(g_device, r);

  auto *arp =
      g_renderPipeline->emplace_back<vsr::rendering::AnariSceneRenderPass>(
          g_device);
  arp->setWorld(g_renderIndex->world());
  arp->setRenderer(r);
  arp->setCamera(g_camera);
  arp->setRunAsync(false);

  // Add AOV visualization pass if enabled
  if (g_ctx->offline.aov.aovType != vsr::rendering::AOVType::NONE) {
    auto *aovPass =
        g_renderPipeline->emplace_back<vsr::rendering::VisualizeAOVPass>();
    aovPass->setAOVType(g_ctx->offline.aov.aovType);
    aovPass->setDepthRange(
        g_ctx->offline.aov.depthMin, g_ctx->offline.aov.depthMax);
    aovPass->setEdgeInvert(g_ctx->offline.aov.edgeInvert);

    // Enable necessary frame channels
    if (g_ctx->offline.aov.aovType == vsr::rendering::AOVType::ALBEDO) {
      arp->setEnableAlbedo(true);
    } else if (g_ctx->offline.aov.aovType == vsr::rendering::AOVType::NORMAL) {
      arp->setEnableNormals(true);
    } else if (g_ctx->offline.aov.aovType == vsr::rendering::AOVType::EDGES) {
      arp->setEnableIDs(true);
    }
  }

  anari::release(g_device, r);

  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

static std::string frameFilename(int i)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "vsrRender_%04d.png", i);
  return buf;
}

static void renderFrames()
{
  const auto frameWidth = g_ctx->offline.frame.width;
  const auto frameHeight = g_ctx->offline.frame.height;
  const auto frameSamples = g_ctx->offline.frame.samples;

  printf("Rendering frames (%u spp)...\n", frameSamples);
  fflush(stdout);

  stbi_flip_vertically_on_write(1);

  g_timer.start();

  // Check for camera animations
  bool hasCameraAnimation = false;
  const vsr::scene::Object *animatedCamera = nullptr;
  for (auto &anim : g_ctx->vsr.animationMgr.animations()) {
    for (auto &b : anim.objectParameterBindings()) {
      if (b.target() && b.target()->type() == ANARI_CAMERA) {
        hasCameraAnimation = true;
        animatedCamera = b.target();
        break;
      }
    }
    if (hasCameraAnimation)
      break;
  }

  if (hasCameraAnimation) {
    const int totalFrames = g_ctx->vsr.animationMgr.getAnimationTotalFrames();

    // If no animated camera, set static pose once from saved poses
    if (!animatedCamera) {
      g_manipulator.setConfig(g_cameraPoses[0]);
      vsr::rendering::updateCameraParametersPerspective(
          g_device, g_camera, g_manipulator);
      anari::commitParameters(g_device, g_camera);
    }

    printf("...animating %d frames...\n", totalFrames);

    for (int i = 0; i < totalFrames; i++) {
      g_ctx->vsr.animationMgr.setAnimationFrame(i);

      if (animatedCamera) {
        using anari::math::float3;
        if (auto v = animatedCamera->parameterValueAs<float3>("position"))
          anari::setParameter(g_device, g_camera, "position", *v);
        if (auto v = animatedCamera->parameterValueAs<float3>("direction"))
          anari::setParameter(g_device, g_camera, "direction", *v);
        if (auto v = animatedCamera->parameterValueAs<float3>("up"))
          anari::setParameter(g_device, g_camera, "up", *v);
        if (auto v = animatedCamera->parameterValueAs<float>("fovy"))
          anari::setParameter(g_device, g_camera, "fovy", *v);
        anari::commitParameters(g_device, g_camera);
      }

      printf("...frame %d / %d...\n", i, totalFrames - 1);
      fflush(stdout);

      for (int s = 0; s < frameSamples; s++)
        g_renderPipeline->render();

      auto filename = frameFilename(i);
      stbi_write_png(filename.c_str(),
          frameWidth,
          frameHeight,
          4,
          g_renderPipeline->getColorBuffer(),
          4 * frameWidth);
    }
  } else {
    // Original camera-pose turntable behavior
    for (size_t i = 0; i < g_cameraPoses.size(); i++) {
      g_manipulator.setConfig(g_cameraPoses[i]);
      vsr::rendering::updateCameraParametersPerspective(
          g_device, g_camera, g_manipulator);
      anari::commitParameters(g_device, g_camera);

      printf("...frame %zu...\n", i);
      fflush(stdout);

      for (int s = 0; s < frameSamples; s++)
        g_renderPipeline->render();

      stbi_write_png(frameFilename(i).c_str(),
          frameWidth,
          frameHeight,
          4,
          g_renderPipeline->getColorBuffer(),
          4 * frameWidth);
    }
  }

  g_timer.end();

  printf("...done (%.2f ms)\n", g_timer.milliseconds());
}

static void cleanup()
{
  printf("Cleanup objects...");
  fflush(stdout);

  g_timer.start();
  g_renderPipeline.reset();
  g_renderIndex.reset();
  g_stateFile.reset();
  anari::release(g_device, g_camera);
  anari::release(g_device, g_device);
  anari::unloadLibrary(g_library);
  g_timer.end();

  printf("done (%.2f ms)\n", g_timer.milliseconds());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  if (argc != 2) {
    printf("usage: %s <state_file.vsr>\n", argv[0]);
    return 1;
  }

  g_ctx = std::make_unique<vsr::app::Context>();

  initVSRDataTree();
  if (!loadState(argv[1]) || !populateVSRContext())
    return 1;
  loadANARIDevice();
  initVSRRenderIndex();
  populateRenderIndex();
  setupCameraManipulator();
  setupImagePipeline();
  renderFrames();
  cleanup();

  g_ctx.reset();

  return 0;
}
