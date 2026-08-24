// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include <vsr/scene/Scene.hpp>
// vsr_io
#include <vsr/io/procedural.hpp>
// vsr_rendering
#include <vsr/rendering/pipeline/ImagePipeline.h>
#include <vsr/rendering/index/RenderIndexFlatRegistry.hpp>
// std
#include <cstdio>
// stb_image
#include "stb_image_write.h"

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR)
    fprintf(stderr, "[FATAL] %s\n", message);
  else if (severity == ANARI_SEVERITY_ERROR)
    fprintf(stderr, "[ERROR] %s\n", message);
  else if (severity == ANARI_SEVERITY_WARNING)
    fprintf(stderr, "[WARN ] %s\n", message);
  else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING)
    fprintf(stderr, "[PERF ] %s\n", message);
#if 0
  else if (severity == ANARI_SEVERITY_INFO)
    fprintf(stderr, "[INFO ] %s\n", message);
  else if (severity == ANARI_SEVERITY_DEBUG)
    fprintf(stderr, "[DEBUG] %s\n", message);
#endif
}

int main()
{
  // Create context //

  vsr::scene::Scene scene;

  // Populate spheres //

  vsr::io::generate_randomSpheres(scene);

  auto light = scene.createObject<vsr::scene::Light>("directional");
  light->setName("mainLight");
  light->setParameter("direction", vsr::math::float3(-1.f, 0.f, 0.f));
  light->setParameter("irradiance", 1.f);

  printf("%s\n", vsr::scene::objectDBInfo(scene.objectDB()).c_str());

  // Setup ANARI device //

  printf("create ANARI device...");
  fflush(stdout);

  anari::Library lib = anari::loadLibrary("environment", statusFunc);
  anari::Device device = anari::newDevice(lib, "default");

  printf("done!\n");

  // Setup render index //

  printf("setup render index...");
  fflush(stdout);

  vsr::rendering::RenderIndexFlatRegistry rIdx(scene, "environment", device);
  rIdx.populate();

  printf("done!\n");

  // Create camera //

  auto camera = anari::newObject<anari::Camera>(device, "perspective");

  const vsr::math::float3 eye = {0.f, 0.f, -2.f};
  const vsr::math::float3 dir = {0.f, 0.f, 1.f};
  const vsr::math::float3 up = {0.f, 1.f, 0.f};

  anari::setParameter(device, camera, "position", eye);
  anari::setParameter(device, camera, "direction", dir);
  anari::setParameter(device, camera, "up", up);

  vsr::math::uint2 imageSize = {1200, 800};
  anari::setParameter(
      device, camera, "aspect", imageSize[0] / float(imageSize[1]));

  anari::commitParameters(device, camera);

  // Create renderer //

  auto renderer = anari::newObject<anari::Renderer>(device, "default");
  const vsr::math::float4 backgroundColor = {0.1f, 0.1f, 0.1f, 1.f};
  anari::setParameter(device, renderer, "background", backgroundColor);
  anari::setParameter(device, renderer, "ambientRadiance", 0.2f);
  anari::setParameter(device, renderer, "pixelSamples", 16);
  anari::setParameter(device, renderer, "denoise", true);
  anari::commitParameters(device, renderer);

  // Setup pipeline //

  printf("setup pipeline...");
  fflush(stdout);

  vsr::rendering::ImagePipeline rpipe(imageSize.x, imageSize.y);

  auto *arp = rpipe.emplace_back<vsr::rendering::AnariSceneRenderPass>(device);
  arp->setWorld(rIdx.world());
  arp->setRenderer(renderer);
  arp->setCamera(camera);
  arp->setEnableIDs(true);
  arp->setRunAsync(false);

  anari::release(device, camera);
  anari::release(device, renderer);

  auto *hrp = rpipe.emplace_back<vsr::rendering::OutlineRenderPass>();
  hrp->setOutlineId(0);

  printf("done!\n");

  // Render frame //

  printf("render frame...");
  fflush(stdout);

  rpipe.render();

  printf("done!\n");

  stbi_flip_vertically_on_write(1);
  stbi_write_png("pipeline.png",
      imageSize.x,
      imageSize.y,
      4,
      rpipe.getColorBuffer(),
      4 * imageSize.x);

  // Cleanup remaining ANARI objets //

  anari::release(device, device);
  anari::unloadLibrary(lib);

  return 0;
}
