// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/core/Logging.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/io/importers/detail/importer_common.hpp"
// tinyply
// #define TINYPLY_IMPLEMENTATION // main ply importer defines that!
#include "tinyply.h"
// std
#include <fstream>

namespace vsr::io {

using namespace tinyply;

void import_3DGSPLY(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filename,
    LayerNodeRef location)
{
  (void)animMgr;
  std::unique_ptr<std::istream> file_stream;
  std::vector<uint8_t> byte_buffer;

  try {
    file_stream.reset(new std::ifstream(filename, std::ios::binary));

    if (!file_stream || file_stream->fail()) {
      throw std::runtime_error(
          "file_stream failed to open " + std::string(filename));
    }

    file_stream->seekg(0, std::ios::end);
    const float size_mb = file_stream->tellg() * float(1e-6);
    file_stream->seekg(0, std::ios::beg);

    PlyFile file;
    file.parse_header(*file_stream);
    std::shared_ptr<tinyply::PlyData> pos, scale, rot, opacity, sh_dc;

    pos = file.request_properties_from_element("vertex", { "x", "y", "z" });
    scale = file.request_properties_from_element("vertex", { "scale_0", "scale_1", "scale_2" });
    rot = file.request_properties_from_element("vertex", { "rot_0", "rot_1", "rot_2", "rot_3" });
    opacity = file.request_properties_from_element("vertex", { "opacity" });
    sh_dc = file.request_properties_from_element("vertex", { "f_dc_0", "f_dc_1", "f_dc_2" });

    // Higher-degree spherical harmonics (f_rest_0, f_rest_1, ...)
    std::vector<std::string> rest_names;
    int sh_cnt = 0;
    for (;;) {
      std::string prop_name = "f_rest_" + std::to_string(sh_cnt);
      bool exists = false;

      // Query header for property existence
      for (const auto& e : file.get_elements()) {
        if (e.name == "vertex") {
          for (const auto& p : e.properties) {
            if (p.name == prop_name) { exists = true; break; }
          }
        }
      }
      if (!exists) break;
      rest_names.push_back(prop_name);
      sh_cnt++;
    }

    std::shared_ptr<PlyData> sh_rest;
    if (!rest_names.empty()) {
      sh_rest = file.request_properties_from_element("vertex", rest_names);
    }

    file.read(*file_stream);

    logInfo("[import_PLY] imported data info:");

    if (pos)
      logInfo("    read %zu total positions", pos->count);
    if (scale)
      logInfo("    read %zu total scaling vectors", scale->count);
    if (rot)
      logInfo("    read %zu total rotation quaternions", rot->count);
    if (opacity)
      logInfo("    read %zu total opacities", opacity->count);
    if (sh_dc)
      logInfo("    read %zu total spherical harmonic coefficients", sh_dc->count);
    if (sh_rest)
      logInfo("    read %zu total higher order SH coefficients", sh_rest->count);

    if (!pos || !scale || !rot || !opacity || !sh_dc) {
      logWarning(
          "[import_PLY] float64 vertices not supported, import not successful");
      return;
    }

    size_t numVertices = pos->count;

    const float* posPtr = reinterpret_cast<const float*>(pos->buffer.get());
    const float* scalePtr = reinterpret_cast<const float*>(scale->buffer.get());
    const float* rotPtr = reinterpret_cast<const float*>(rot->buffer.get());
    const float* opacityPtr = reinterpret_cast<const float*>(opacity->buffer.get());
    const float* shDcPtr = reinterpret_cast<const float*>(sh_dc->buffer.get());
    const float* shRestPtr = sh_rest ? reinterpret_cast<const float*>(sh_rest->buffer.get()) : nullptr;

    struct {
      std::vector<anari::math::float3> P;
      std::vector<anari::math::float3> S;
      std::vector<anari::math::float4> Q;
      std::vector<float> opacity;
      std::vector<anari::math::float3> f_dc;
      std::vector<float> f_rest;
    } gaussians;

    for (size_t i = 0; i < numVertices; ++i) {
      anari::math::float3 P{ posPtr[i * 3], posPtr[i * 3 + 1], posPtr[i * 3 + 2] };
      anari::math::float3 S{ expf(scalePtr[i * 3]), expf(scalePtr[i * 3 + 1]), expf(scalePtr[i * 3 + 2]) };
      // wijk -> ijkw:
      anari::math::float4 Q{ rotPtr[i * 4 + 1], rotPtr[i * 4 + 2], rotPtr[i * 4 + 3], rotPtr[i * 4 ] };
      float op = 1.f / (1.f + expf(-opacityPtr[i]));
      anari::math::float3 f_dc;
      if (shDcPtr) f_dc = anari::math::float3{ shDcPtr[i * 3 + 0], shDcPtr[i * 3 + 1], shDcPtr[i * 3 + 2] };

      gaussians.P.push_back(P);
      gaussians.S.push_back(S);
      gaussians.Q.push_back(Q);
      gaussians.opacity.push_back(op);
      gaussians.f_dc.push_back(f_dc);
      for (int j = 0; j < sh_cnt; ++j) {
        float fr = shRestPtr[i * sh_cnt + j];
        gaussians.f_rest.push_back(fr);
      }
    }

    ///////////////////////////////////////////////////////////////////////////

    auto objectName = fileOf(std::string(filename)) + " (PLY file)";

    // Gaussian volume //

    auto ply_root = scene.insertChildNode(
        location ? location : scene.defaultLayer()->root(),
        fileOf(filename).c_str());
    auto *layer = (*ply_root)->layer();
    auto volume = scene.createObject<Volume>(tokens::volume::gaussianSplat);

    auto makeArray1DForMesh = [&](Token parameterName,
                                  anari::DataType type,
                                  const void *ptr,
                                  size_t size) {
      if (size > 0 && ptr) {
        auto arr = scene.createArray(type, size);
        arr->setData(ptr);
        volume->setParameterObject(parameterName, *arr);
      }
    };

    makeArray1DForMesh("vertex.position",
        ANARI_FLOAT32_VEC3,
        gaussians.P.data(),
        gaussians.P.size());

    makeArray1DForMesh("vertex.scale",
        ANARI_FLOAT32_VEC3,
        gaussians.S.data(),
        gaussians.S.size());

    makeArray1DForMesh("vertex.rotation",
        ANARI_FLOAT32_QUAT_IJKW,
        gaussians.Q.data(),
        gaussians.Q.size());

    makeArray1DForMesh("vertex.opacity",
        ANARI_FLOAT32,
        gaussians.opacity.data(),
        gaussians.opacity.size());

    makeArray1DForMesh("vertex.f_dc",
        ANARI_FLOAT32_VEC3,
        gaussians.f_dc.data(),
        gaussians.f_dc.size());

    makeArray1DForMesh("vertex.f_rest",
        ANARI_FLOAT32,
        gaussians.f_rest.data(),
        gaussians.f_rest.size());

    volume->setName(fileOf(filename).c_str());

    ply_root->insert_last_child({layer, volume});

  } catch (const std::exception &e) {
    logError("[import_PLY] caught tinyply exception: %s", e.what());
  }
}

} // namespace vsr::io
