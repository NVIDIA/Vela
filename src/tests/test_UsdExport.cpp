// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Scenes exported to USD and imported back.

#if VSR_USE_USD

// catch
#include "catch.hpp"
// vsr_tests
#include "UsdTestFixtures.h"
// vsr
#include "vsr/io/exporters.hpp"

namespace {

// Export `scene` next to the fixtures and import what was written.
struct ReimportedScene
{
  ReimportedScene(vsr::scene::Scene &exported, const char *name);
  ~ReimportedScene();

  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animMgr{&scene};
  vsr::io::UsdImportReport report;

 private:
  std::filesystem::path m_path;
};

ReimportedScene::ReimportedScene(vsr::scene::Scene &exported, const char *name)
    : m_path(fixtureDirectory() / name)
{
  vsr::io::export_SceneToUSD(exported, m_path.string().c_str());
  report = vsr::io::import_USD(scene, animMgr, m_path.string().c_str());
}

ReimportedScene::~ReimportedScene()
{
  std::error_code ec;
  std::filesystem::remove(m_path, ec);
  // The exporter writes textures to a directory named after the file.
  std::filesystem::remove_all(m_path.parent_path() / m_path.stem(), ec);
}

} // namespace

SCENARIO("Texture scale and bias survive a USD round trip", "[UsdExport]")
{
  GIVEN(
      "An imported preview surface with a decoded normal map, a packed"
      " metallic channel and a remapped roughness")
  {
    TextureFixture normal("vsr_test_usd_export_normal.tga");
    TextureFixture orm("vsr_test_usd_export_orm.tga");
    TextureFixture rough("vsr_test_usd_export_rough.tga");

    ImportedStage stage("vsr_test_usd_export_source.usda",
        R"(#usda 1.0

def Xform "World"
{
    def Material "M"
    {
        token outputs:surface.connect = </World/M/PBR.outputs:surface>

        def Shader "PBR"
        {
            uniform token info:id = "UsdPreviewSurface"
            normal3f inputs:normal.connect = </World/M/Normal.outputs:rgb>
            float inputs:metallic.connect = </World/M/Orm.outputs:g>
            float inputs:roughness.connect = </World/M/Rough.outputs:r>
            token outputs:surface
        }

        def Shader "Normal"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @vsr_test_usd_export_normal.tga@
            float4 inputs:scale = (2, 2, 2, 2)
            float4 inputs:bias = (-1, -1, -1, -1)
            float3 outputs:rgb
        }

        def Shader "Orm"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @vsr_test_usd_export_orm.tga@
            float outputs:g
        }

        def Shader "Rough"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @vsr_test_usd_export_rough.tga@
            float4 inputs:scale = (0.5, 1, 1, 1)
            float4 inputs:bias = (0.25, 0, 0, 0)
            float outputs:r
        }
    }

    def Mesh "Quad" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {)" + std::string(QUAD_MESH_BODY)
            + R"(
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)] (
            interpolation = "vertex"
        )
        rel material:binding = </World/M>
    }
}
)");
    auto *original = boundMaterial(stage.scene);

    WHEN("It is exported and imported again")
    {
      ReimportedScene reimported(stage.scene, "vsr_test_usd_export.usda");
      auto *material = boundMaterial(reimported.scene);

      THEN("Every texture still loads")
      {
        REQUIRE(!reimported.report.contains(
            vsr::io::UsdSkipReason::TEXTURE_LOAD_FAILED));
      }

      THEN("The scalar samplers read the same channel, remapped the same way")
      {
        REQUIRE(boundOutputTransform(*material, "metallic")
            == boundOutputTransform(*original, "metallic"));
        REQUIRE(boundOutputTransform(*material, "roughness")
            == boundOutputTransform(*original, "roughness"));
      }

      // The exporter writes texcoords as VSR holds them, v running down, and
      // flips v back for the lookup in a UsdTransform2d. The importer reverses
      // v on the way in, so the reimported texcoords run the other way and the
      // tangent frame derived from them is mirrored in v: the decoded normal's
      // y is negated to keep the surface shading the same.
      THEN("The normal sampler decodes alike, its y following the texcoords")
      {
        auto expected = boundOutputTransform(*original, "normal");
        expected.transform[1][1] = -expected.transform[1][1];
        expected.offset.y = -expected.offset.y;
        REQUIRE(boundOutputTransform(*material, "normal") == expected);
      }
    }
  }
}

#endif // VSR_USE_USD
