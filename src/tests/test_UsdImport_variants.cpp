// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// Variant Selections: choosing among an asset's own variants at import, before
// any payload loads.

#if VSR_USE_USD

// catch
#include "catch.hpp"
// vsr_tests
#include "UsdTestFixtures.h"
// vsr
#include "vsr/core/DataTree.hpp"
#include "vsr/io/animation/UsdFileBinding.hpp"
#include "vsr/io/archives/AnimationManagerArchive.hpp"
// std
#include <string>

namespace {

// An asset whose `lod` Variant Set swaps between two payloads, the way
// production assets swap texture resolutions. Every file is prefixed so that
// scenarios asking whether a payload was ever opened do not see another
// scenario's layers.
struct LodAsset
{
  explicit LodAsset(const std::string &prefix)
      : high((prefix + "_high.usda").c_str(), R"(#usda 1.0
(
    defaultPrim = "Content"
)

def Xform "Content"
{
    def Sphere "HighShape"
    {
    }
}
)"),
        low((prefix + "_low.usda").c_str(), R"(#usda 1.0
(
    defaultPrim = "Content"
)

def Xform "Content" (
    variants = {
        string color = "red"
    }
    prepend variantSets = "color"
)
{
    variantSet "color" = {
        "red" {
            def Cube "RedShape"
            {
            }
        }
        "blue" {
            def Cube "BlueShape"
            {
            }
        }
    }
}
)"),
        root((prefix + ".usda").c_str(),
            R"(#usda 1.0

def Xform "Asset" (
    variants = {
        string lod = "high"
    }
    prepend variantSets = "lod"
)
{
    variantSet "lod" = {
        "high" (
            prepend payload = @./)"
                + prefix + R"(_high.usda@
        ) {
        }
        "low" (
            prepend payload = @./)"
                + prefix + R"(_low.usda@
        ) {
        }
    }
}
)")
  {}

  StageFixture high;
  StageFixture low;
  StageFixture root;
};

vsr::io::UsdImportReport importWith(vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animMgr,
    const std::string &path,
    const vsr::io::UsdVariantSelections &selections)
{
  vsr::io::UsdImportOptions options;
  options.variantSelections = selections;
  return vsr::io::import_USD(scene, animMgr, path.c_str(), {}, options);
}

} // namespace

SCENARIO("A Variant Selection decides which variant imports", "[UsdImport]")
{
  GIVEN("An asset with high and low payload variants, high selected")
  {
    LodAsset asset("vsr_test_usd_variants_pick");
    vsr::scene::Scene scene;
    vsr::animation::AnimationManager animMgr(&scene);

    WHEN("It is imported with no selections")
    {
      auto report = importWith(scene, animMgr, asset.root.path(), {});

      THEN("The Stage's own selection imports")
      {
        REQUIRE(findNode(scene.defaultLayer(), "HighShape"));
        REQUIRE_FALSE(findNode(scene.defaultLayer(), "RedShape"));
        REQUIRE(report.variantSelectedPrims == 0);
      }
    }

    WHEN("It is imported selecting the low variant")
    {
      auto report =
          importWith(scene, animMgr, asset.root.path(), {{"lod", "low"}});

      THEN("Only the low variant's content imports")
      {
        REQUIRE(findNode(scene.defaultLayer(), "RedShape"));
        REQUIRE_FALSE(findNode(scene.defaultLayer(), "HighShape"));
        REQUIRE(report.variantSelectedPrims == 1);
        REQUIRE(report.unavailableVariants.empty());
      }
    }

    WHEN("The selections reach a Variant Set the low payload authors")
    {
      auto report = importWith(scene,
          animMgr,
          asset.root.path(),
          {{"lod", "low"}, {"color", "blue"}});

      THEN("That set is selected too, once its payload has loaded")
      {
        REQUIRE(findNode(scene.defaultLayer(), "BlueShape"));
        REQUIRE_FALSE(findNode(scene.defaultLayer(), "RedShape"));
        // The payload composes onto /Asset, so both sets sit on one prim.
        REQUIRE(report.variantSelectedPrims == 1);
      }
    }

    WHEN("It is imported selecting a variant the set does not author")
    {
      auto report =
          importWith(scene, animMgr, asset.root.path(), {{"lod", "medium"}});

      THEN("The Stage's own selection stands and the miss is reported")
      {
        REQUIRE(findNode(scene.defaultLayer(), "HighShape"));
        REQUIRE(report.variantSelectedPrims == 0);
        REQUIRE(report.unavailableVariants.size() == 1);
        REQUIRE(report.unavailableVariants[0] == "/Asset {lod=medium}");
      }
    }
  }
}

SCENARIO("A binding rejoins its Stage with the selections it was imported with",
    "[UsdImport]")
{
  GIVEN("A deforming mesh that exists only inside a payload variant")
  {
    StageFixture deforming("vsr_test_usd_variants_deforming.usda", R"(#usda 1.0
(
    defaultPrim = "Content"
)

def Xform "Content"
{
    def Mesh "Blob"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points.timeSamples = {
            0: [(0, 0, 0), (1, 0, 0), (0, 1, 0)],
            1: [(0, 0, 0), (2, 0, 0), (0, 2, 0)],
        }
    }
}
)");
    StageFixture root("vsr_test_usd_variants_animated.usda", R"(#usda 1.0
(
    startTimeCode = 0
    endTimeCode = 1
)

def Xform "Asset" (
    variants = {
        string lod = "empty"
    }
    prepend variantSets = "lod"
)
{
    variantSet "lod" = {
        "empty" {
        }
        "animated" (
            prepend payload = @./vsr_test_usd_variants_deforming.usda@
        ) {
        }
    }
}
)");

    vsr::scene::Scene scene;
    vsr::animation::AnimationManager animMgr(&scene);
    importWith(scene, animMgr, root.path(), {{"lod", "animated"}});
    REQUIRE(animMgr.animations().size() == 1);
    REQUIRE(animMgr.animations()[0].fileBindings().size() == 1);

    WHEN("The binding is saved and reconstructed from an Archive")
    {
      vsr::core::DataTree tree;
      REQUIRE(vsr::io::serialize_AnimationManagerArchive(animMgr, tree.root()));

      vsr::animation::AnimationManager restored(&scene);
      REQUIRE(
          vsr::io::deserialize_AnimationManagerArchive(restored, tree.root()));
      REQUIRE(restored.animations().size() == 1);
      REQUIRE(restored.animations()[0].fileBindings().size() == 1);

      THEN("It carries the selections and resolves through them")
      {
        auto *binding = dynamic_cast<vsr::io::UsdFileBinding *>(
            restored.animations()[0].fileBindings()[0].get());
        REQUIRE(binding != nullptr);
        REQUIRE(binding->variantSelections()
            == vsr::io::UsdVariantSelections{{"lod", "animated"}});

        restored.setAnimationTime(1.0f);
        auto geometry = scene.getObject<vsr::scene::Geometry>(0);
        auto *positions = geometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
        REQUIRE(positions != nullptr);
        REQUIRE(positions->dataAs<vsr::math::float3>()[1].x == Approx(2.f));
      }
    }

    // The animated import's bindings hold its Stage Session open, so a second
    // import of the same file would join it if Sessions were keyed by file
    // alone -- and would see the animated variant instead of the Stage's own.
    WHEN("The same file is imported again without selections")
    {
      vsr::scene::Scene other;
      vsr::animation::AnimationManager otherAnimMgr(&other);
      auto report = importWith(other, otherAnimMgr, root.path(), {});

      THEN("It reads the Stage's own selection, not the held-open one")
      {
        REQUIRE(report.stageOpened);
        REQUIRE_FALSE(findNode(other.defaultLayer(), "Blob"));
        REQUIRE(other.numberOfObjects(ANARI_SURFACE) == 0);
      }
    }
  }
}

#endif
