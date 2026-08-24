// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/core/Logging.hpp"
#include "vsr/io/importers.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace vsr::io {

// ---------------------------------------------------------------------------
// SDF primitive layout — must mirror SDFPrimitive in
// devices/rtx/device/gpu/gpu_objects.h byte-for-byte.
// ---------------------------------------------------------------------------

enum class SWCSdfType : uint8_t
{
  SPHERE = 0,
  PILL = 1,
  CONE_PILL = 2,
};

struct SWCSdfPrimLayout
{
  uint64_t userData{0};
  float userParams[3]{0.f, 0.f, 0.f};
  float p0[3]{0.f, 0.f, 0.f};
  float p1[3]{0.f, 0.f, 0.f};
  float r0{-1.f};
  float r1{-1.f};
  uint32_t _pad{0};
  uint64_t neighboursIndex{0};
  uint8_t numNeighbours{0};
  uint8_t type{0};
  uint8_t _pad2[6]{};
};

static_assert(sizeof(SWCSdfPrimLayout) == 72,
    "SWCSdfPrimLayout must be 72 bytes to match SDFPrimitive in gpu_objects.h");
static_assert(offsetof(SWCSdfPrimLayout, neighboursIndex) == 56,
    "SWCSdfPrimLayout::neighboursIndex offset must match SDFPrimitive");

// ---------------------------------------------------------------------------
// SWC point
// ---------------------------------------------------------------------------

struct SWCPoint
{
  int id;
  int type;
  double x, y, z;
  double radius;
  int parent;
};

// ---------------------------------------------------------------------------
// Build SDF geometry from a parsed SWC file
// ---------------------------------------------------------------------------

static void buildSWCSDF(Scene &scene,
    const std::string &filename,
    LayerNodeRef location)
{
  std::ifstream file(filename);
  if (!file.is_open()) {
    logError("[import_SWC_SDF] Cannot open SWC file: %s", filename.c_str());
    return;
  }

  std::map<int, SWCPoint> points;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream iss(line);
    SWCPoint pt;
    if (iss >> pt.id >> pt.type >> pt.x >> pt.y >> pt.z >> pt.radius
        >> pt.parent)
      points[pt.id] = pt;
  }
  file.close();

  if (points.empty()) {
    logWarning("[import_SWC_SDF] No points found in '%s'", filename.c_str());
    return;
  }

  // Step 1: build one SDF primitive per SWC point
  std::map<int, size_t> pointToSdfIdx;

  struct PrimBuild
  {
    SWCSdfPrimLayout prim;
    int swcId{-1};
    int parentId{-1};
  };

  std::vector<PrimBuild> primBuilds;
  primBuilds.reserve(points.size());

  for (const auto &[id, pt] : points) {
    PrimBuild build;
    build.swcId = id;
    build.parentId = pt.parent;
    build.prim.userData = static_cast<uint64_t>(id);

    if (pt.parent == -1) {
      build.prim.p0[0] = static_cast<float>(pt.x);
      build.prim.p0[1] = static_cast<float>(pt.y);
      build.prim.p0[2] = static_cast<float>(pt.z);
      build.prim.r0 = static_cast<float>(pt.radius);
      build.prim.type = static_cast<uint8_t>(SWCSdfType::SPHERE);
    } else {
      const auto &par = points.at(pt.parent);

      float px = static_cast<float>(pt.x);
      float py = static_cast<float>(pt.y);
      float pz = static_cast<float>(pt.z);
      float qx = static_cast<float>(par.x);
      float qy = static_cast<float>(par.y);
      float qz = static_cast<float>(par.z);
      float r0 = static_cast<float>(pt.radius);
      float r1 = static_cast<float>(par.radius);

      // ConePill requires the larger radius at p0
      if (r0 < r1) {
        std::swap(px, qx);
        std::swap(py, qy);
        std::swap(pz, qz);
        std::swap(r0, r1);
      }

      build.prim.p0[0] = px;
      build.prim.p0[1] = py;
      build.prim.p0[2] = pz;
      build.prim.p1[0] = qx;
      build.prim.p1[1] = qy;
      build.prim.p1[2] = qz;
      build.prim.r0 = r0;
      build.prim.r1 = r1;
      build.prim.type = static_cast<uint8_t>(SWCSdfType::CONE_PILL);
    }

    pointToSdfIdx[id] = primBuilds.size();
    primBuilds.push_back(std::move(build));
  }

  // Step 2: children map for adjacency lookup
  std::map<int, std::vector<int>> swcChildren;
  for (const auto &[id, pt] : points)
    if (pt.parent != -1)
      swcChildren[pt.parent].push_back(id);

  // Step 3: fill neighbour buffer
  //   Neighbours = child segments + parent primitive + sibling segments
  std::vector<uint64_t> neighbourBuffer;

  for (auto &build : primBuilds) {
    const int myId = build.swcId;
    const int parentId = build.parentId;

    std::vector<uint64_t> nbrs;

    auto childIt = swcChildren.find(myId);
    if (childIt != swcChildren.end())
      for (int childId : childIt->second)
        nbrs.push_back(static_cast<uint64_t>(pointToSdfIdx.at(childId)));

    if (parentId != -1) {
      nbrs.push_back(static_cast<uint64_t>(pointToSdfIdx.at(parentId)));

      auto sibIt = swcChildren.find(parentId);
      if (sibIt != swcChildren.end())
        for (int sibId : sibIt->second)
          if (sibId != myId)
            nbrs.push_back(static_cast<uint64_t>(pointToSdfIdx.at(sibId)));
    }

    std::sort(nbrs.begin(), nbrs.end());
    nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
    nbrs.erase(std::remove(nbrs.begin(),
                   nbrs.end(),
                   static_cast<uint64_t>(pointToSdfIdx.at(myId))),
        nbrs.end());

    const uint8_t numNbrs =
        static_cast<uint8_t>(std::min(nbrs.size(), static_cast<size_t>(255)));

    build.prim.neighboursIndex = static_cast<uint64_t>(neighbourBuffer.size());
    build.prim.numNeighbours = numNbrs;

    for (uint8_t i = 0; i < numNbrs; i++)
      neighbourBuffer.push_back(nbrs[i]);
  }

  // Step 4: pack into byte arrays and upload to VSR scene
  const size_t numPrims = primBuilds.size();

  std::vector<uint8_t> sdfRawBytes(numPrims * sizeof(SWCSdfPrimLayout));
  for (size_t i = 0; i < numPrims; i++) {
    std::memcpy(sdfRawBytes.data() + i * sizeof(SWCSdfPrimLayout),
        &primBuilds[i].prim,
        sizeof(SWCSdfPrimLayout));
  }

  auto sdfArray = scene.createArray(ANARI_UINT8, sdfRawBytes.size());
  sdfArray->setData(sdfRawBytes);

  ArrayRef neighbourArray;
  if (!neighbourBuffer.empty()) {
    neighbourArray = scene.createArray(ANARI_UINT64, neighbourBuffer.size());
    neighbourArray->setData(neighbourBuffer);
  }

  logInfo(
      "[import_SWC_SDF] Built %zu SDF primitives, %zu neighbour entries from '%s'",
      numPrims,
      neighbourBuffer.size(),
      filename.c_str());

  // Step 5: create SDF geometry object with defaults
  auto sdfGeom = scene.createObject<Geometry>(tokens::geometry::sdf);
  sdfGeom->setName("sdf_geometry");

  sdfGeom->setParameterObject("primitive.sdf", *sdfArray);
  if (neighbourArray)
    sdfGeom->setParameterObject("primitive.neighbor", *neighbourArray);

  const float epsilon = 1e-5f;
  const int32_t marchIter = 16;
  const float blendFactor = 1.f;
  const float blendLerpFactor = 0.5f;
  const float omega = 1.f;
  const float noiseFactor = 0.f;
  sdfGeom->setParameter("epsilon", ANARI_FLOAT32, &epsilon);
  sdfGeom->setParameter("nbMarchIterations", ANARI_INT32, &marchIter);
  sdfGeom->setParameter("blendFactor", ANARI_FLOAT32, &blendFactor);
  sdfGeom->setParameter("blendLerpFactor", ANARI_FLOAT32, &blendLerpFactor);
  sdfGeom->setParameter("omega", ANARI_FLOAT32, &omega);
  sdfGeom->setParameter("noiseFactor", ANARI_FLOAT32, &noiseFactor);

  // Step 6: material and scene graph
  auto m = scene.createObject<Material>(tokens::material::matte);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0.0, 0.5);
  vsr::math::float3 baseColor(0.5f + static_cast<float>(dis(gen)),
      0.5f + static_cast<float>(dis(gen)),
      0.5f + static_cast<float>(dis(gen)));
  m->setParameter("color", ANARI_FLOAT32_VEC3, &baseColor);

  const std::string basename =
      std::filesystem::path(filename).filename().string();

  if (!location)
    location = scene.defaultLayer()->root();

  const auto swcLocation = scene.insertChildNode(location, basename.c_str());
  auto surface = scene.createSurface(basename.c_str(), sdfGeom, m);
  scene.insertChildObjectNode(swcLocation, surface);
}

void import_SWC_SDF(Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const char *filename,
    LayerNodeRef location)
{
  (void)animMgr;
  buildSWCSDF(scene, filename, location);
}

} // namespace vsr::io
