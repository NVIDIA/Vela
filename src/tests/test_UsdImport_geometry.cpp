// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Geometry conversion: meshes, quadrics, curves and subdivision.

#if VSR_USE_USD

// catch
#include "catch.hpp"
// vsr_tests
#include "UsdTestFixtures.h"
// std
#include <cmath>

// The first Surface placed beneath the node of this name.
static vsr::scene::Surface *surfaceUnder(
    ImportedStage &stage, const char *nodeName)
{
  auto *layer = stage.scene.defaultLayer();
  auto node = findNode(layer, nodeName);
  REQUIRE(node);
  vsr::scene::Surface *found = nullptr;
  layer->traverse(node, [&](auto &n, int) {
    if (!found && n->type() == ANARI_SURFACE)
      found = static_cast<vsr::scene::Surface *>(n->getObject());
    return !found;
  });
  return found;
}

SCENARIO("A USD Stage's meshes arrive as surfaces", "[UsdImport]")
{
  GIVEN("A Stage with a single quad mesh")
  {
    ImportedStage stage("vsr_test_usd_single_mesh.usda",
        std::string(R"(#usda 1.0

def Xform "World"
{
    def Mesh "Quad"
    {
)") + QUAD_MESH_BODY
            + R"(
    }
}
)");

    WHEN("The Stage is imported")
    {
      THEN("The mesh becomes one triangle-geometry surface")
      {
        REQUIRE(stage.report.stageOpened);
        REQUIRE(stage.scene.numberOfObjects(ANARI_SURFACE) == 1);
        REQUIRE(stage.scene.numberOfObjects(ANARI_GEOMETRY) == 1);

        auto geometry = stage.scene.getObject<vsr::scene::Geometry>(0);
        REQUIRE(geometry);
        REQUIRE(geometry->subtype() == vsr::scene::tokens::geometry::triangle);

        auto *index = geometry->parameterValueAsObject<vsr::scene::Array>(
            "primitive.index");
        REQUIRE(index != nullptr);
        REQUIRE(index->size() == 2); // a quad tessellates to two triangles

        auto *position = geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
        REQUIRE(position != nullptr);
        REQUIRE(position->size() == 4);
      }

      THEN("Nothing is silently lost")
      {
        REQUIRE(stage.report.skipped.empty());
        REQUIRE(stage.report.convertedPrims == 1);
      }
    }
  }
}

SCENARIO("Analytic quadrics stay analytic", "[UsdImport]")
{
  GIVEN("A Stage with a sphere and a cylinder")
  {
    ImportedStage stage("vsr_test_usd_quadrics.usda", R"(#usda 1.0

def Sphere "Ball"
{
    double radius = 2
}

def Cylinder "Tube"
{
    double radius = 0.5
    double height = 4
    uniform token axis = "Y"
}
)");

    WHEN("The Stage is imported")
    {
      THEN("They map onto VSR's native quadric geometry, not meshes")
      {
        REQUIRE(stage.scene.numberOfObjects(ANARI_GEOMETRY) == 2);

        auto ball = stage.scene.getObject<vsr::scene::Geometry>(0);
        REQUIRE(ball->subtype() == vsr::scene::tokens::geometry::sphere);
        REQUIRE(ball->parameterValueAs<float>("radius").value() == Approx(2.f));

        auto tube = stage.scene.getObject<vsr::scene::Geometry>(1);
        REQUIRE(tube->subtype() == vsr::scene::tokens::geometry::cylinder);

        // The spine axis is folded into the endpoints rather than a transform.
        auto *positions =
            tube->parameterValueAsObject<vsr::scene::Array>("vertex.position");
        REQUIRE(positions != nullptr);
        REQUIRE(positions->size() == 2);
        const auto *p = positions->dataAs<vsr::math::float3>();
        REQUIRE(p[0].y == Approx(-2.f));
        REQUIRE(p[1].y == Approx(2.f));
        REQUIRE(stage.report.skipped.empty());
      }
    }
  }
}

SCENARIO(
    "A non-convex polygon tessellates without spurious geometry", "[UsdImport]")
{
  GIVEN("A mesh with one concave five-sided face")
  {
    ImportedStage stage("vsr_test_usd_nonconvex.usda", R"(#usda 1.0

def Mesh "Arrow"
{
    int[] faceVertexCounts = [5]
    int[] faceVertexIndices = [0, 1, 2, 3, 4]
    point3f[] points = [(0, 0, 0), (2, 0, 0), (2, 2, 0), (1, 1, 0), (0, 2, 0)]
}
)");

    WHEN("The Stage is imported")
    {
      THEN("It becomes exactly n-2 triangles")
      {
        auto geometry = stage.scene.getObject<vsr::scene::Geometry>(0);
        REQUIRE(geometry);
        auto *index = geometry->parameterValueAsObject<vsr::scene::Array>(
            "primitive.index");
        REQUIRE(index != nullptr);
        REQUIRE(index->size() == 3);
      }
    }
  }
}

SCENARIO("Subdivision surfaces are refined by default", "[UsdImport]")
{
  GIVEN("A cube that explicitly declares a subdivision scheme")
  {
    const std::string subdivCube = R"(#usda 1.0

def Mesh "SubdivCube"
{
    uniform token subdivisionScheme = "catmullClark"
    int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
    int[] faceVertexIndices = [0, 1, 2, 3,  4, 7, 6, 5,  0, 4, 5, 1,
                               1, 5, 6, 2,  2, 6, 7, 3,  3, 7, 4, 0]
    point3f[] points = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
                        (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
}

def Mesh "PolygonCube"
{
    uniform token subdivisionScheme = "none"
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
}
)";

    WHEN("The Stage is imported at the default refinement level")
    {
      ImportedStage stage("vsr_test_usd_subdiv.usda", subdivCube);

      THEN("The subdivision mesh gains vertices and the polygon mesh does not")
      {
        auto subdiv = stage.scene.getObject<vsr::scene::Geometry>(0);
        auto polygon = stage.scene.getObject<vsr::scene::Geometry>(1);
        REQUIRE(subdiv);
        REQUIRE(polygon);

        auto vertexCount = [](auto geometry) {
          auto *p =
              geometry->template parameterValueAsObject<vsr::scene::Array>(
                  "vertex.position");
          return p ? p->size() : size_t(0);
        };

        // Two levels of Catmull-Clark on a cube: 8 -> 26 -> 98 vertices.
        REQUIRE(vertexCount(subdiv) == 98);
        REQUIRE(vertexCount(polygon) == 4);
      }
    }

    WHEN("Refinement is turned off")
    {
      vsr::io::UsdImportOptions options;
      options.refinementLevel = 0;
      ImportedStage stage("vsr_test_usd_subdiv.usda", subdivCube, options);

      THEN("The subdivision mesh arrives at its authored resolution")
      {
        auto subdiv = stage.scene.getObject<vsr::scene::Geometry>(0);
        auto *p = subdiv->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
        REQUIRE(p != nullptr);
        REQUIRE(p->size() == 8);
      }
    }
  }
}

SCENARIO(
    "Refinement carries face-varying primvars with the surface", "[UsdImport]")
{
  GIVEN("A subdivision mesh whose UVs are authored per face corner")
  {
    ImportedStage stage("vsr_test_usd_subdiv_uvs.usda", R"(#usda 1.0

def Mesh "SubdivQuad"
{
    uniform token subdivisionScheme = "catmullClark"
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)] (
        interpolation = "faceVarying"
    )
}
)");

    WHEN("The Stage is imported")
    {
      THEN("The UVs survive refinement rather than being dropped")
      {
        auto geometry = stage.scene.getObject<vsr::scene::Geometry>(0);
        REQUIRE(geometry);
        auto *uvs = geometry->parameterValueAsObject<vsr::scene::Array>(
            "faceVarying.attribute0");
        REQUIRE(uvs != nullptr);
        REQUIRE(uvs->size() > 4);
      }

      THEN("Nothing is reported as lost")
      {
        REQUIRE(stage.report.skipped.empty());
      }
    }
  }
}

SCENARIO(
    "Width-less curves and points get a bounds-scaled radius", "[UsdImport]")
{
  // Blender hair exports commonly omit widths; without an explicit radius the
  // ANARI default of 1 world unit dwarfs most scenes.
  GIVEN("A Stage with a widthless curve, a widthed curve, and widthless points")
  {
    ImportedStage stage("vsr_test_usd_widthless_curves.usda", R"(#usda 1.0

def Xform "World"
{
    def BasisCurves "Hair"
    {
        uniform token type = "linear"
        int[] curveVertexCounts = [4]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 1, 1)]
    }

    def BasisCurves "Rope"
    {
        uniform token type = "linear"
        int[] curveVertexCounts = [2]
        point3f[] points = [(0, 0, 0), (1, 0, 0)]
        float[] widths = [0.2, 0.2] (interpolation = "vertex")
    }

    def Points "Sprinkles"
    {
        point3f[] points = [(0, 0, 0), (2, 0, 0)]
    }
}
)");

    WHEN("The Stage is imported")
    {
      THEN("The widthless curve's radius scales with its bounds")
      {
        auto geometry = findGeometry(stage.scene, "/World/Hair");
        REQUIRE(geometry);
        REQUIRE(geometry->subtype() == vsr::scene::tokens::geometry::curve);
        REQUIRE(
            geometry->parameterValueAsObject<vsr::scene::Array>("vertex.radius")
            == nullptr);

        const auto radius = geometry->parameterValueAs<float>("radius");
        REQUIRE(radius.has_value());
        REQUIRE(*radius == Approx(1e-3f * std::sqrt(3.f)));
      }

      THEN("Authored widths still become per-vertex radii")
      {
        auto geometry = findGeometry(stage.scene, "/World/Rope");
        REQUIRE(geometry);

        auto *radii = geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.radius");
        REQUIRE(radii != nullptr);
        REQUIRE(radii->size() == 2);
        REQUIRE(radii->dataAs<float>()[0] == Approx(0.1f));
        REQUIRE_FALSE(geometry->parameterValueAs<float>("radius").has_value());
      }

      THEN("Widthless points scale the same way")
      {
        auto geometry = findGeometry(stage.scene, "/World/Sprinkles");
        REQUIRE(geometry);
        REQUIRE(geometry->subtype() == vsr::scene::tokens::geometry::sphere);

        const auto radius = geometry->parameterValueAs<float>("radius");
        REQUIRE(radius.has_value());
        REQUIRE(*radius == Approx(2e-3f));
      }
    }
  }
}

SCENARIO("Conversion leaves nothing behind for geometry it does not emit",
    "[UsdImport]")
{
  GIVEN("A mesh with no points that nonetheless binds a material")
  {
    ImportedStage stage("vsr_test_usd_empty_mesh.usda", R"(#usda 1.0

def Material "Orphan"
{
    token outputs:surface.connect = </Orphan/Shader.outputs:surface>

    def Shader "Shader"
    {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (1, 0, 0)
        token outputs:surface
    }
}

def Mesh "Empty" (
    prepend apiSchemas = ["MaterialBindingAPI"]
)
{
    rel material:binding = </Orphan>
    int[] faceVertexCounts = []
    int[] faceVertexIndices = []
    point3f[] points = []
}
)");

    // A Scene creates one default Material of its own, so the count to
    // compare against is an empty Scene's rather than this one's after the
    // import.
    const auto materialsBefore =
        vsr::scene::Scene().numberOfObjects(ANARI_MATERIAL);

    WHEN("The Stage is imported")
    {
      THEN("No Surface, Geometry or Material is created for it")
      {
        REQUIRE(stage.scene.numberOfObjects(ANARI_SURFACE) == 0);
        REQUIRE(stage.scene.numberOfObjects(ANARI_GEOMETRY) == 0);
        REQUIRE(stage.scene.numberOfObjects(ANARI_MATERIAL) == materialsBefore);
      }
    }
  }

  GIVEN("A mesh whose subset claims no faces but binds its own material")
  {
    ImportedStage stage("vsr_test_usd_empty_subset.usda", R"(#usda 1.0

def Material "Used"
{
    token outputs:surface.connect = </Used/Shader.outputs:surface>

    def Shader "Shader"
    {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0, 1, 0)
        token outputs:surface
    }
}

def Material "Unused"
{
    token outputs:surface.connect = </Unused/Shader.outputs:surface>

    def Shader "Shader"
    {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0, 0, 1)
        token outputs:surface
    }
}

def Mesh "Quad"
{
    int[] faceVertexCounts = [3, 3]
    int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
    point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]

    def GeomSubset "Drawn" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [0, 1]
        rel material:binding = </Used>
    }

    def GeomSubset "Empty" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = []
        rel material:binding = </Unused>
    }
}
)");

    WHEN("The Stage is imported")
    {
      THEN("Only the material the drawn subset uses is created")
      {
        REQUIRE(stage.scene.numberOfObjects(ANARI_SURFACE) == 1);

        bool sawUnused = false;
        const auto numMaterials = stage.scene.numberOfObjects(ANARI_MATERIAL);
        for (size_t i = 0; i < numMaterials; ++i) {
          auto material = stage.scene.getObject<vsr::scene::Material>(i);
          if (material && material->name().find("Unused") != std::string::npos)
            sawUnused = true;
        }
        REQUIRE_FALSE(sawUnused);
      }
    }
  }
}

// A Stage that repeats an asset by reference, without marking it instanceable,
// names every copy by its own path, so only the content says they are one.
SCENARIO("Geometry a Stage repeats is held once", "[UsdImport]")
{
  GIVEN("Two references to one quad asset, and a quad of its own")
  {
    ImportedStage stage("vsr_test_usd_repeated_geometry.usda",
        std::string(R"(#usda 1.0

class Xform "Asset"
{
    def Mesh "Quad"
    {
)") + QUAD_MESH_BODY
            + R"(
    }
}

def Xform "CopyA" (
    references = </Asset>
)
{
}

def Xform "CopyB" (
    references = </Asset>
)
{
}

def Xform "Other"
{
    def Mesh "Quad"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(0, 0, 0), (2, 0, 0), (2, 2, 0), (0, 2, 0)]
    }
}
)");

    auto positionOf = [&](const char *name) {
      auto geometry = findGeometry(stage.scene, name);
      REQUIRE(geometry);
      auto *array = geometry->parameterValueAsObject<vsr::scene::Array>(
          "vertex.position");
      REQUIRE(array != nullptr);
      return array;
    };

    WHEN("The Stage is imported")
    {
      THEN("The copies share one Geometry and one Surface")
      {
        REQUIRE(stage.scene.numberOfObjects(ANARI_GEOMETRY) == 2);
        REQUIRE(stage.scene.numberOfObjects(ANARI_SURFACE) == 2);
        REQUIRE(surfaceUnder(stage, "CopyA") == surfaceUnder(stage, "CopyB"));
      }

      THEN("The shared Geometry is named for the copy that built it")
      {
        REQUIRE(findGeometry(stage.scene, "/CopyA/Quad"));
        REQUIRE(!findGeometry(stage.scene, "/CopyB/Quad"));
      }

      THEN("Each copy keeps its own placement")
      {
        REQUIRE(surfaceUnder(stage, "CopyA") != nullptr);
        REQUIRE(surfaceUnder(stage, "Other") != nullptr);
        REQUIRE(surfaceUnder(stage, "Other") != surfaceUnder(stage, "CopyA"));
      }

      THEN("Different content is not shared")
      {
        REQUIRE(positionOf("/CopyA/Quad") != positionOf("/Other/Quad"));
      }
    }
  }

  GIVEN("Two references to one deforming quad asset")
  {
    ImportedStage stage("vsr_test_usd_repeated_deforming.usda", R"(#usda 1.0
(
    startTimeCode = 0
    endTimeCode = 1
)

class Xform "Asset"
{
    def Mesh "Quad"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points.timeSamples = {
            0: [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)],
            1: [(0, 0, 0), (2, 0, 0), (2, 2, 0), (0, 2, 0)],
        }
    }
}

def Xform "CopyA" (
    references = </Asset>
)
{
}

def Xform "CopyB" (
    references = </Asset>
)
{
}
)");

    WHEN("The Stage is imported")
    {
      THEN("Each copy keeps its own Geometry and Surface")
      {
        REQUIRE(findGeometry(stage.scene, "/CopyA/Quad"));
        REQUIRE(findGeometry(stage.scene, "/CopyB/Quad"));
        REQUIRE(surfaceUnder(stage, "CopyA") != surfaceUnder(stage, "CopyB"));
      }

      THEN("Each copy keeps Arrays of its own, since a scrub re-fills them")
      {
        auto copyA = findGeometry(stage.scene, "/CopyA/Quad");
        auto copyB = findGeometry(stage.scene, "/CopyB/Quad");
        REQUIRE(copyA);
        REQUIRE(copyB);
        REQUIRE(
            copyA->parameterValueAsObject<vsr::scene::Array>("vertex.position")
            != copyB->parameterValueAsObject<vsr::scene::Array>(
                "vertex.position"));
      }
    }
  }
}

// A copy's material lives under the copy's own path too, so only content says
// two copies' materials -- and the Samplers they read -- are one.
SCENARIO("Materials a Stage repeats are held once", "[UsdImport]")
{
  GIVEN("Two references to a textured asset, and a copy with its own colour")
  {
    TextureFixture texture("vsr_test_usd_repeated_material.tga");

    auto stageText = [](const char *otherRoughness) {
      return std::string(R"(#usda 1.0

class Xform "Asset"
{
    def Material "Mat"
    {
        token outputs:surface.connect = </Asset/Mat/PBR.outputs:surface>

        def Shader "PBR"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor.connect = </Asset/Mat/Tex.outputs:rgb>
            float inputs:roughness = 0.5
            token outputs:surface
        }

        def Shader "Tex"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @vsr_test_usd_repeated_material.tga@
            float3 outputs:rgb
        }
    }

    def Mesh "Quad" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {)") + QUAD_MESH_BODY
          + R"(
        rel material:binding = </Asset/Mat>
    }
}

def Xform "CopyA" (
    references = </Asset>
)
{
}

def Xform "CopyB" (
    references = </Asset>
)
{
}

def Xform "Other" (
    references = </Asset>
)
{
    over "Mat"
    {
        over "PBR"
        {
            float inputs:roughness = )"
          + otherRoughness + R"(
        }
    }
}
)";
    };

    auto materialOf = [](vsr::scene::Surface *surface) {
      REQUIRE(surface != nullptr);
      return surface->parameterValueAsObject<vsr::scene::Material>(
          vsr::scene::tokens::surface::material);
    };
    auto samplerOf = [](vsr::scene::Material *material) {
      REQUIRE(material != nullptr);
      return material->parameterValueAsObject<vsr::scene::Sampler>("baseColor");
    };

    WHEN("The Stage is imported")
    {
      ImportedStage stage(
          "vsr_test_usd_repeated_material.usda", stageText("0.9"));

      THEN("The copies share one Material and one Sampler")
      {
        auto *a = materialOf(surfaceUnder(stage, "CopyA"));
        auto *b = materialOf(surfaceUnder(stage, "CopyB"));
        REQUIRE(a == b);
        REQUIRE(samplerOf(a) != nullptr);
        REQUIRE(stage.scene.numberOfObjects(ANARI_SAMPLER) == 1);
      }

      THEN("A copy whose material differs keeps its own")
      {
        auto *a = materialOf(surfaceUnder(stage, "CopyA"));
        auto *other = materialOf(surfaceUnder(stage, "Other"));
        REQUIRE(a != other);
        REQUIRE(samplerOf(a) == samplerOf(other));
      }

      THEN("Its Surface differs too, though its Geometry is shared")
      {
        auto *a = surfaceUnder(stage, "CopyA");
        auto *other = surfaceUnder(stage, "Other");
        REQUIRE(a != other);
        REQUIRE(a->parameterValueAsObject<vsr::scene::Geometry>(
                    vsr::scene::tokens::surface::geometry)
            == other->parameterValueAsObject<vsr::scene::Geometry>(
                vsr::scene::tokens::surface::geometry));
      }
    }

    WHEN("The Stage is imported with object sharing turned off")
    {
      vsr::io::UsdImportOptions options;
      options.shareRepeatedObjects = false;
      ImportedStage stage("vsr_test_usd_repeated_material_unshared.usda",
          stageText("0.9"),
          options);

      THEN("Every copy keeps objects of its own")
      {
        auto *a = surfaceUnder(stage, "CopyA");
        auto *b = surfaceUnder(stage, "CopyB");
        REQUIRE(a != b);
        REQUIRE(materialOf(a) != materialOf(b));
        REQUIRE(samplerOf(materialOf(a)) != samplerOf(materialOf(b)));
        REQUIRE(findGeometry(stage.scene, "/CopyA/Quad")
            != findGeometry(stage.scene, "/CopyB/Quad"));
      }

      THEN("Their Arrays are still shared")
      {
        auto a = findGeometry(stage.scene, "/CopyA/Quad");
        auto b = findGeometry(stage.scene, "/CopyB/Quad");
        REQUIRE(a);
        REQUIRE(b);
        REQUIRE(a->parameterValueAsObject<vsr::scene::Array>("vertex.position")
            == b->parameterValueAsObject<vsr::scene::Array>("vertex.position"));
      }
    }
  }
}

#endif // VSR_USE_USD
