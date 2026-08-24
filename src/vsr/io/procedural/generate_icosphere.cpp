// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/procedural.hpp"
#include "vsr/io/procedural/detail/checkerboard.hpp"
// std
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vsr::io {

namespace {

// A triangle mesh under construction: unit-length positions (which double as
// normals for a sphere centered at the origin) plus vertex-indexed triangles.
struct Mesh
{
  std::vector<math::float3> position;
  std::vector<math::uint3> index;
};

// The 12 vertices of a regular icosahedron, as the corners of three mutually
// orthogonal golden-ratio rectangles, normalized onto the unit sphere.
Mesh icosahedron()
{
  const float t = (1.f + std::sqrt(5.f)) / 2.f;

  Mesh m;
  m.position = {{-1, t, 0},
      {1, t, 0},
      {-1, -t, 0},
      {1, -t, 0},
      {0, -1, t},
      {0, 1, t},
      {0, -1, -t},
      {0, 1, -t},
      {t, 0, -1},
      {t, 0, 1},
      {-t, 0, -1},
      {-t, 0, 1}};
  for (auto &p : m.position)
    p = linalg::normalize(p);

  m.index = {{0, 11, 5},
      {0, 5, 1},
      {0, 1, 7},
      {0, 7, 10},
      {0, 10, 11},
      {1, 5, 9},
      {5, 11, 4},
      {11, 10, 2},
      {10, 7, 6},
      {7, 1, 8},
      {3, 9, 4},
      {3, 4, 2},
      {3, 2, 6},
      {3, 6, 8},
      {3, 8, 9},
      {4, 9, 5},
      {2, 4, 11},
      {6, 2, 10},
      {8, 6, 7},
      {9, 8, 1}};

  return m;
}

// Split every triangle into four by adding a vertex at the midpoint of each
// edge and pushing it back out to the unit sphere. The midpoint cache keys on
// the two endpoint indices so that the two triangles sharing an edge agree on
// the vertex, keeping the mesh watertight. std::unordered_map rather than
// FlatMap here because the cache reaches tens of thousands of entries at the
// subdivision levels this is used at, well past FlatMap's linear-scan range.
Mesh subdivide(const Mesh &in)
{
  Mesh out;
  out.position = in.position;
  out.index.reserve(in.index.size() * 4);

  std::unordered_map<uint64_t, uint32_t> midpoints;
  midpoints.reserve(in.index.size() * 2);

  auto midpoint = [&](uint32_t a, uint32_t b) {
    const uint64_t key =
        a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
    auto found = midpoints.find(key);
    if (found != midpoints.end())
      return found->second;

    const auto index = uint32_t(out.position.size());
    out.position.push_back(linalg::normalize(in.position[a] + in.position[b]));
    midpoints.emplace(key, index);
    return index;
  };

  for (auto tri : in.index) {
    const uint32_t ab = midpoint(tri.x, tri.y);
    const uint32_t bc = midpoint(tri.y, tri.z);
    const uint32_t ca = midpoint(tri.z, tri.x);

    out.index.push_back({tri.x, ab, ca});
    out.index.push_back({tri.y, bc, ab});
    out.index.push_back({tri.z, ca, bc});
    out.index.push_back({ab, bc, ca});
  }

  return out;
}

// Latitude/longitude texture coordinates for a point on the unit sphere.
math::float2 sphericalUV(math::float3 p)
{
  return math::float2(0.5f + std::atan2(p.z, p.x) / (2.f * float(M_PI)),
      0.5f - std::asin(p.y) / float(M_PI));
}

// Spherical UVs are discontinuous in two places, and both show up as smeared
// texels rather than as geometry errors: the longitude seam, where u jumps from
// ~1 back to ~0 across a single triangle, and the poles, where u is undefined
// because every longitude meets there. Fix both by duplicating the offending
// vertices per-triangle so each copy can carry the u the triangle needs -- the
// positions are unchanged, so the mesh stays watertight.
std::vector<math::float2> sphericalUVs(Mesh &mesh)
{
  std::vector<math::float2> uv(mesh.position.size());
  for (size_t i = 0; i < mesh.position.size(); i++)
    uv[i] = sphericalUV(mesh.position[i]);

  auto duplicate = [&](uint32_t i, math::float2 newUV) {
    const auto index = uint32_t(mesh.position.size());
    mesh.position.push_back(mesh.position[i]);
    uv.push_back(newUV);
    return index;
  };

  for (auto &tri : mesh.index) {
    uint32_t *v = &tri.x;

    // Seam: a triangle spanning it has u values nearly a full turn apart. Lift
    // the small ones by 1 so the triangle interpolates across the wrap.
    const float uMin = std::min({uv[v[0]].x, uv[v[1]].x, uv[v[2]].x});
    const float uMax = std::max({uv[v[0]].x, uv[v[1]].x, uv[v[2]].x});
    if (uMax - uMin > 0.5f) {
      for (int i = 0; i < 3; i++) {
        if (uv[v[i]].x < 0.5f)
          v[i] = duplicate(v[i], math::float2(uv[v[i]].x + 1.f, uv[v[i]].y));
      }
    }

    // Pole: give the apex the average longitude of the other two corners.
    for (int i = 0; i < 3; i++) {
      const float y = mesh.position[v[i]].y;
      if (std::abs(y) < 0.9999f)
        continue;
      const float u = 0.5f * (uv[v[(i + 1) % 3]].x + uv[v[(i + 2) % 3]].x);
      v[i] = duplicate(v[i], math::float2(u, uv[v[i]].y));
    }
  }

  return uv;
}

void addFloor(Scene &scene, LayerNodeRef location, float extent, float y)
{
  auto geometry = scene.createObject<Geometry>(tokens::geometry::triangle);
  geometry->setName("floor_geometry");

  const std::vector<math::float3> positions = {{-extent, y, extent},
      {extent, y, extent},
      {extent, y, -extent},
      {-extent, y, -extent}};
  const std::vector<math::float3> normals(4, math::float3(0.f, 1.f, 0.f));
  const std::vector<math::float2> uvs = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
  const std::vector<math::uint3> indices = {{0, 1, 2}, {0, 2, 3}};

  auto positionArray = scene.createArray(ANARI_FLOAT32_VEC3, positions.size());
  positionArray->setData(positions);
  auto normalArray = scene.createArray(ANARI_FLOAT32_VEC3, normals.size());
  normalArray->setData(normals);
  auto uvArray = scene.createArray(ANARI_FLOAT32_VEC2, uvs.size());
  uvArray->setData(uvs);
  auto indexArray = scene.createArray(ANARI_UINT32_VEC3, indices.size());
  indexArray->setData(indices);

  geometry->setParameterObject("vertex.position", *positionArray);
  geometry->setParameterObject("vertex.normal", *normalArray);
  geometry->setParameterObject("vertex.attribute0", *uvArray);
  geometry->setParameterObject("primitive.index", *indexArray);

  auto material = scene.createObject<Material>(tokens::material::matte);
  material->setName("floor_material");
  auto tex = detail::makeCheckerboardTexture(scene, 10);
  material->setParameterObject("color", *tex);

  auto surface = scene.createSurface("floor", geometry, material);
  scene.insertChildObjectNode(location, surface);
}

} // namespace

///////////////////////////////////////////////////////////////////////////////

void generate_icosphere(
    Scene &scene, LayerNodeRef location, uint32_t subdivisions, bool withFloor)
{
  if (!location)
    location = scene.defaultLayer()->root();
  auto *layer = (*location)->layer();

  auto root =
      location->insert_last_child({layer, math::IDENTITY_MAT4, "Icosphere"});

  auto mesh = icosahedron();
  for (uint32_t i = 0; i < subdivisions; i++)
    mesh = subdivide(mesh);

  auto uvs = sphericalUVs(mesh);

  // The sphere is centered a radius above the floor so it rests on it, which
  // means positions and normals differ -- normalized position is the normal.
  std::vector<math::float3> normals = mesh.position;
  for (auto &p : mesh.position)
    p.y += 1.f;

  auto geometry = scene.createObject<Geometry>(tokens::geometry::triangle);
  geometry->setName("icosphere_geometry");

  auto positionArray =
      scene.createArray(ANARI_FLOAT32_VEC3, mesh.position.size());
  positionArray->setData(mesh.position);
  auto normalArray = scene.createArray(ANARI_FLOAT32_VEC3, normals.size());
  normalArray->setData(normals);
  auto uvArray = scene.createArray(ANARI_FLOAT32_VEC2, uvs.size());
  uvArray->setData(uvs);
  auto indexArray = scene.createArray(ANARI_UINT32_VEC3, mesh.index.size());
  indexArray->setData(mesh.index);

  geometry->setParameterObject("vertex.position", *positionArray);
  geometry->setParameterObject("vertex.normal", *normalArray);
  geometry->setParameterObject("vertex.attribute0", *uvArray);
  geometry->setParameterObject("primitive.index", *indexArray);

  auto material =
      scene.createObject<Material>(tokens::material::physicallyBased);
  material->setName("icosphere_material");
  material->setParameter("baseColor", math::float3(0.f, 0.110f, 0.321f));
  material->setParameter("metallic", 0.5f);
  material->setParameter("roughness", 0.f);
  material->setParameter("clearcoat", 1.f);

  auto surface = scene.createSurface("icosphere", geometry, material);
  scene.insertChildObjectNode(root, surface);

  if (withFloor)
    addFloor(scene, root, 20.f, -1.f);
}

} // namespace vsr::io
