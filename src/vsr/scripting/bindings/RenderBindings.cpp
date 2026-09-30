// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <sol/sol.hpp>
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/pick/PickRequest.h"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/scene/Scene.hpp"
#include "vsr/scripting/LuaBindings.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "stb_image_write.h"

namespace vsr::scripting {

struct LuaAnariDevice
{
  anari::Library library{nullptr};
  anari::Device device{nullptr};
  std::string libraryName;

  ~LuaAnariDevice()
  {
    if (device)
      anari::release(device, device);
    if (library)
      anari::unloadLibrary(library);
  }
};

struct LuaCameraSetup
{
  math::float3 position{0.f, 0.f, 5.f};
  math::float3 direction{0.f, 0.f, -1.f};
  math::float3 up{0.f, 1.f, 0.f};
  float fovy{40.f};
  float aspect{1.777f}; // 16:9
  float aperture{0.f};
  float focusDistance{1.f};
};

/*
 * Opaque renderer handed to Lua: an Image Pipeline with one ANARI scene
 * source. Holds the device so the library outlives the pipeline.
 */
struct LuaRenderer
{
  std::shared_ptr<LuaAnariDevice> device;
  std::unique_ptr<rendering::ImagePipeline> pipeline;
  rendering::AnariSceneRenderPass *source{nullptr};
  rendering::CameraView view;
};

// Writes 'pixels' (RGBA8, ANARI row order: bottom row first) to 'filename';
// the format comes from the extension. Throws on failure.
static void writeImage(
    const uint32_t *pixels, int width, int height, const std::string &filename)
{
  size_t dotPos = filename.find_last_of('.');
  if (dotPos == std::string::npos) {
    throw std::runtime_error(
        "Cannot determine image format: no file extension");
  }
  std::string ext = filename.substr(dotPos + 1);

  for (char &c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  // ANARI row order -> image row order (top row first)
  std::vector<uint32_t> flipped(size_t(width) * size_t(height));
  for (int y = 0; y < height; y++) {
    std::memcpy(&flipped[size_t(y) * width],
        &pixels[size_t(height - 1 - y) * width],
        width * sizeof(uint32_t));
  }

  int result = 0;
  if (ext == "png") {
    result = stbi_write_png(
        filename.c_str(), width, height, 4, flipped.data(), width * 4);
  } else if (ext == "jpg" || ext == "jpeg") {
    // JPEG requires RGB (no alpha)
    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; i++) {
      uint32_t pixel = flipped[i];
      rgb[i * 3 + 0] = (pixel >> 0) & 0xFF;
      rgb[i * 3 + 1] = (pixel >> 8) & 0xFF;
      rgb[i * 3 + 2] = (pixel >> 16) & 0xFF;
    }
    result = stbi_write_jpg(filename.c_str(), width, height, 3, rgb.data(), 95);
  } else if (ext == "bmp") {
    result = stbi_write_bmp(filename.c_str(), width, height, 4, flipped.data());
  } else if (ext == "tga") {
    result = stbi_write_tga(filename.c_str(), width, height, 4, flipped.data());
  } else if (ext == "ppm") {
    FILE *fp = fopen(filename.c_str(), "wb");
    if (!fp) {
      throw std::runtime_error(
          fmt::format("Failed to open file: {}", filename));
    }
    fprintf(fp, "P6\n%d %d\n255\n", width, height);
    bool writeOk = true;
    for (int i = 0; i < width * height && writeOk; i++) {
      uint32_t pixel = flipped[i];
      unsigned char rgb[3];
      rgb[0] = (pixel >> 0) & 0xFF;
      rgb[1] = (pixel >> 8) & 0xFF;
      rgb[2] = (pixel >> 16) & 0xFF;
      if (fwrite(rgb, 1, 3, fp) != 3)
        writeOk = false;
    }
    if (fclose(fp) != 0)
      writeOk = false;
    result = writeOk ? 1 : 0;
  } else {
    throw std::runtime_error(fmt::format(
        "Unsupported image format '{}'. Supported: png, jpg/jpeg, bmp, tga, ppm",
        ext));
  }

  if (result == 0) {
    throw std::runtime_error(
        fmt::format("Failed to write image '{}': {} (errno={})",
            filename,
            std::strerror(errno),
            errno));
  }
}

void registerRenderBindings(sol::state &lua)
{
  sol::table vsr = lua["vsr"];
  sol::table render = vsr["render"];

  vsr.new_usertype<LuaCameraSetup>("CameraSetup",
      sol::constructors<LuaCameraSetup()>(),
      "position",
      &LuaCameraSetup::position,
      "direction",
      &LuaCameraSetup::direction,
      "up",
      &LuaCameraSetup::up,
      "fovy",
      &LuaCameraSetup::fovy,
      "aspect",
      &LuaCameraSetup::aspect,
      "aperture",
      &LuaCameraSetup::aperture,
      "focusDistance",
      &LuaCameraSetup::focusDistance);

  vsr.new_usertype<LuaAnariDevice>("AnariDevice",
      sol::no_constructor,
      "libraryName",
      sol::readonly(&LuaAnariDevice::libraryName));

  render["loadDevice"] =
      [](const std::string &libraryName) -> std::shared_ptr<LuaAnariDevice> {
    auto statusFunc = [](const void *,
                          ANARIDevice,
                          ANARIObject,
                          anari::DataType,
                          ANARIStatusSeverity severity,
                          ANARIStatusCode,
                          const char *message) {
      if (severity == ANARI_SEVERITY_FATAL_ERROR) {
        fprintf(stderr, "[ANARI][FATAL] %s\n", message);
      } else if (severity == ANARI_SEVERITY_ERROR) {
        fprintf(stderr, "[ANARI][ERROR] %s\n", message);
      }
    };

    auto dev = std::make_shared<LuaAnariDevice>();
    dev->libraryName = libraryName;
    dev->library = anari::loadLibrary(libraryName.c_str(), statusFunc);
    if (!dev->library) {
      throw std::runtime_error(
          fmt::format("Failed to load ANARI library: {}", libraryName));
    }
    dev->device = anari::newDevice(dev->library, "default");
    if (!dev->device) {
      anari::unloadLibrary(dev->library);
      dev->library = nullptr;
      throw std::runtime_error(fmt::format(
          "Failed to create ANARI device from library: {}", libraryName));
    }
    return dev;
  };

  vsr.new_usertype<rendering::RenderIndexAllLayers>(
      "RenderIndex",
      sol::constructors<rendering::RenderIndexAllLayers(
          scene::Scene &, vsr::core::Token, anari::Device)>(),
      "populate",
      [](rendering::RenderIndexAllLayers &ri) { ri.populate(); },
      "world",
      &rendering::RenderIndexAllLayers::world,
      "device",
      &rendering::RenderIndexAllLayers::device);

  render["createRenderIndex"] = [](scene::Scene &scene,
                                    std::shared_ptr<LuaAnariDevice> dev)
      -> rendering::RenderIndexAllLayers * {
    if (!dev || !dev->device) {
      throw std::runtime_error("createRenderIndex: device handle is null");
    }
    return scene.updateDelegate().emplace<rendering::RenderIndexAllLayers>(
        scene, dev->libraryName, dev->device);
  };

  render["getWorldBounds"] = [](std::shared_ptr<LuaAnariDevice> dev,
                                 rendering::RenderIndexAllLayers *index,
                                 sol::this_state s) -> sol::table {
    if (!dev || !dev->device) {
      throw std::runtime_error("getWorldBounds: device handle is null");
    }
    if (!index) {
      throw std::runtime_error("getWorldBounds: render index handle is null");
    }

    math::float3 bounds[2] = {{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    anariGetProperty(dev->device,
        index->world(),
        "bounds",
        ANARI_FLOAT32_BOX3,
        &bounds[0],
        sizeof(bounds),
        ANARI_WAIT);

    sol::state_view lua(s);
    sol::table result = lua.create_table();
    result["min"] = bounds[0];
    result["max"] = bounds[1];
    return result;
  };

  render["createRenderer"] = [](int width,
                                 int height,
                                 std::shared_ptr<LuaAnariDevice> dev,
                                 rendering::RenderIndexAllLayers *index,
                                 const LuaCameraSetup &camera,
                                 sol::optional<sol::table> rendererParams)
      -> std::shared_ptr<LuaRenderer> {
    if (width <= 0 || height <= 0) {
      throw std::runtime_error("createRenderer: width and height must be > 0");
    }
    if (!dev || !dev->device) {
      throw std::runtime_error("createRenderer: device handle is null");
    }
    if (!index) {
      throw std::runtime_error("createRenderer: render index handle is null");
    }

    auto renderer = std::make_shared<LuaRenderer>();
    renderer->device = dev;
    renderer->pipeline =
        std::make_unique<rendering::ImagePipeline>(width, height);
    renderer->view = rendering::CameraView::perspective(camera.position,
        camera.direction,
        camera.up,
        math::radians(camera.fovy));
    renderer->view.aspect = camera.aspect; // what the ANARI camera is given

    auto cam = anari::newObject<anari::Camera>(dev->device, "perspective");
    anari::setParameter(dev->device, cam, "fovy", math::radians(camera.fovy));
    anari::setParameter(dev->device, cam, "position", camera.position);
    anari::setParameter(dev->device, cam, "direction", camera.direction);
    anari::setParameter(dev->device, cam, "up", camera.up);
    if (camera.aperture > 0.f) {
      anari::setParameter(
          dev->device, cam, "apertureRadius", camera.aperture);
      anari::setParameter(
          dev->device, cam, "focusDistance", camera.focusDistance);
    }
    anari::commitParameters(dev->device, cam);

    std::string rendererName = "default";
    if (rendererParams) {
      sol::object val = (*rendererParams)["renderer"];
      if (val.is<std::string>())
        rendererName = val.as<std::string>();
    }

    auto anariRenderer =
        anari::newObject<anari::Renderer>(dev->device, rendererName.c_str());
    if (!anariRenderer) {
      throw std::runtime_error(
          fmt::format("createRenderer: failed to create renderer subtype '{}'",
              rendererName));
    }
    if (rendererParams) {
      for (const auto &kv : *rendererParams) {
        std::string key = kv.first.as<std::string>();
        if (key == "renderer")
          continue;
        sol::object val = kv.second;
        if (val.is<bool>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<bool>());
        else if (val.is<std::string>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<std::string>());
        else if (val.is<int>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<int>());
        else if (val.is<float>() || val.is<double>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<float>());
        else if (val.is<math::float2>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<math::float2>());
        else if (val.is<math::float3>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<math::float3>());
        else if (val.is<math::float4>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<math::float4>());
        else if (val.is<math::mat4>())
          anari::setParameter(
              dev->device, anariRenderer, key.c_str(), val.as<math::mat4>());
        else if (val.is<sol::table>()) {
          sol::table t = val.as<sol::table>();
          size_t len = t.size();
          if (len == 2) {
            anari::setParameter(dev->device,
                anariRenderer,
                key.c_str(),
                math::float2(t[1], t[2]));
          } else if (len == 3) {
            anari::setParameter(dev->device,
                anariRenderer,
                key.c_str(),
                math::float3(t[1], t[2], t[3]));
          } else if (len == 4) {
            anari::setParameter(dev->device,
                anariRenderer,
                key.c_str(),
                math::float4(t[1], t[2], t[3], t[4]));
          }
        }
      }
    }
    anari::commitParameters(dev->device, anariRenderer);

    auto *source =
        renderer->pipeline->setSource<rendering::AnariSceneRenderPass>(
            dev->device);
    source->setWorld(index->world());
    source->setRenderer(anariRenderer);
    source->setCamera(cam);
    // Binding the camera sets the source's image aspect. This renderer has a
    // fixed size and honors CameraSetup's explicit aspect for both the image
    // and the view used to reconstruct picks.
    anari::setParameter(dev->device, cam, "aspect", camera.aspect);
    anari::commitParameters(dev->device, cam);
    source->setRunAsync(false);
    renderer->source = source;

    anari::release(dev->device, cam);
    anari::release(dev->device, anariRenderer);

    return renderer;
  };

  vsr.new_usertype<LuaRenderer>(
      "Renderer",
      sol::no_constructor,
      "renderToFile",
      [](LuaRenderer &r, int samples, const std::string &filename) {
        if (samples < 1)
          throw std::runtime_error("renderToFile: samples must be >= 1");

        for (int i = 0; i < samples; i++)
          r.pipeline->render();

        const auto size = r.pipeline->dimensions();
        const uint32_t *pixels = r.pipeline->getColorBuffer();
        if (!pixels)
          throw std::runtime_error("renderToFile: nothing was rendered");
        writeImage(pixels, int(size.x), int(size.y), filename);
      },
      "pick",
      [](LuaRenderer &r, int x, int y, sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        const auto size = r.pipeline->dimensions();
        if (x < 0 || y < 0 || uint32_t(x) >= size.x || uint32_t(y) >= size.y)
          throw std::runtime_error("pick: pixel outside the image");

        // Lua callers address pixels as in the saved image: row 0 on top.
        rendering::PickRequest request;
        request.pixel = {uint32_t(x), size.y - 1 - uint32_t(y)};
        request.view = r.view;

        const auto hit = rendering::pick(*r.source, request);
        if (!hit)
          return sol::lua_nil;

        sol::table t = lua.create_table();
        t["depth"] = hit->depth;
        if (hit->object) {
          t["objectType"] =
              hit->object->type == ANARI_VOLUME ? "volume" : "surface";
          t["objectIndex"] = hit->object->index;
        }
        if (hit->instanceId != ~0u)
          t["instanceId"] = hit->instanceId;
        if (hit->primitiveId != ~0u)
          t["primitiveId"] = hit->primitiveId;
        if (hit->position)
          t["position"] = *hit->position;
        return t;
      });
}

} // namespace vsr::scripting
