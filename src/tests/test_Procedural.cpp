// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Procedural scene generators.

// catch
#include "catch.hpp"
// vsr
#include "vsr/io/procedural.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <array>
#include <cmath>
#include <map>

using vsr::scene::Array;
using vsr::scene::Geometry;
using vsr::scene::Scene;

namespace {

Geometry *findGeometry(Scene &scene, const char *name)
{
  for (size_t i = 0; i < scene.numberOfObjects(ANARI_GEOMETRY); i++) {
    auto g = scene.getObject<Geometry>(i);
    if (g && std::string(g->name()) == name)
      return &*g;
  }
  return nullptr;
}

} // namespace

SCENARIO("generate_icosphere() builds a sphere from scratch", "[procedural]")
{
  GIVEN("A scene with a 2-subdivision icosphere and no floor")
  {
    Scene scene;
    vsr::io::generate_icosphere(scene, {}, 2, false);

    auto *geometry = findGeometry(scene, "icosphere_geometry");
    REQUIRE(geometry != nullptr);

    auto *position = geometry->parameterValueAsObject<Array>("vertex.position");
    auto *normal = geometry->parameterValueAsObject<Array>("vertex.normal");
    auto *uv = geometry->parameterValueAsObject<Array>("vertex.attribute0");
    auto *index = geometry->parameterValueAsObject<Array>("primitive.index");
    REQUIRE(position != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(uv != nullptr);
    REQUIRE(index != nullptr);

    THEN("Each subdivision quadruples the icosahedron's 20 faces")
    {
      REQUIRE(index->size() == 20 * 4 * 4);
    }

    THEN("Only the floor is omitted, so a lone surface is created")
    {
      REQUIRE(scene.numberOfObjects(ANARI_SURFACE) == 1);
    }

    THEN("Every vertex attribute array is the same length")
    {
      REQUIRE(normal->size() == position->size());
      REQUIRE(uv->size() == position->size());
    }

    THEN("Every vertex sits one unit from the sphere center")
    {
      // The sphere is lifted a radius above the floor plane, so the center is
      // at +Y rather than the origin.
      const auto *p = position->dataAs<vsr::math::float3>();
      for (size_t i = 0; i < position->size(); i++) {
        const float r = vsr::math::length(p[i] - vsr::math::float3(0, 1, 0));
        REQUIRE(r == Approx(1.f).margin(1e-5f));
      }
    }

    THEN("Normals are unit length and point away from the center")
    {
      const auto *p = position->dataAs<vsr::math::float3>();
      const auto *n = normal->dataAs<vsr::math::float3>();
      for (size_t i = 0; i < normal->size(); i++) {
        REQUIRE(vsr::math::length(n[i]) == Approx(1.f).margin(1e-5f));
        const auto outward = p[i] - vsr::math::float3(0, 1, 0);
        REQUIRE(vsr::math::dot(n[i], outward) > 0.f);
      }
    }

    THEN("Indices stay in range and no triangle is degenerate")
    {
      const auto *tri = index->dataAs<vsr::math::uint3>();
      for (size_t i = 0; i < index->size(); i++) {
        REQUIRE(tri[i].x < position->size());
        REQUIRE(tri[i].y < position->size());
        REQUIRE(tri[i].z < position->size());
        REQUIRE(tri[i].x != tri[i].y);
        REQUIRE(tri[i].y != tri[i].z);
        REQUIRE(tri[i].z != tri[i].x);
      }
    }

    THEN("No triangle straddles the longitude seam")
    {
      // Seam-spanning triangles are split during generation, so a full-turn
      // spread in u would mean a band of smeared texels around the sphere.
      const auto *tri = index->dataAs<vsr::math::uint3>();
      const auto *t = uv->dataAs<vsr::math::float2>();
      for (size_t i = 0; i < index->size(); i++) {
        const float a = t[tri[i].x].x;
        const float b = t[tri[i].y].x;
        const float c = t[tri[i].z].x;
        const float spread = std::max({a, b, c}) - std::min({a, b, c});
        REQUIRE(spread <= 0.5f);
      }
    }

    THEN("The mesh is closed: every edge is shared by exactly two triangles")
    {
      // Compare by position rather than by index, since the seam and pole
      // fixups duplicate vertices that occupy the same point in space.
      const auto *p = position->dataAs<vsr::math::float3>();
      const auto *tri = index->dataAs<vsr::math::uint3>();

      auto quantize = [&](uint32_t v) {
        const auto q = [](float f) { return int64_t(std::lround(f * 1e5f)); };
        return std::array<int64_t, 3>{q(p[v].x), q(p[v].y), q(p[v].z)};
      };

      std::map<std::pair<std::array<int64_t, 3>, std::array<int64_t, 3>>, int>
          edges;
      for (size_t i = 0; i < index->size(); i++) {
        const uint32_t v[3] = {tri[i].x, tri[i].y, tri[i].z};
        for (int e = 0; e < 3; e++) {
          auto a = quantize(v[e]);
          auto b = quantize(v[(e + 1) % 3]);
          if (b < a)
            std::swap(a, b);
          edges[{a, b}]++;
        }
      }

      for (const auto &edge : edges)
        REQUIRE(edge.second == 2);
    }
  }

  GIVEN("A scene with the default icosphere")
  {
    Scene scene;
    vsr::io::generate_icosphere(scene);

    THEN("The sphere is accompanied by a floor")
    {
      REQUIRE(scene.numberOfObjects(ANARI_SURFACE) == 2);
      REQUIRE(findGeometry(scene, "icosphere_geometry") != nullptr);
      REQUIRE(findGeometry(scene, "floor_geometry") != nullptr);
    }

    THEN("Neither surface carries geometry imported from a file")
    {
      REQUIRE(scene.numberOfObjects(ANARI_GEOMETRY) == 2);
    }
  }
}
