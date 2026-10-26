// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// A Static Dataset imported from USD keeps the Variant Selections it was
// imported with, so a reimport reads the same variants.

#if VSR_USE_USD

// catch
#include "catch.hpp"
// vsr_tests
#include "UsdTestFixtures.h"
// scivisStudio
#include "ProjectContext.h"
// vsr
#include "vsr/app/Context.h"

using namespace vsr::scivis_studio;

SCENARIO("SciVis Studio USD datasets import with their Variant Selections",
    "[SciVisStudio][UsdImport]")
{
  GIVEN("A Stage whose color Variant Set picks red")
  {
    StageFixture stage("vsr_test_scivis_studio_variants.usda", R"(#usda 1.0

def Xform "Asset" (
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
)");
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();

    WHEN("It is added as a static dataset selecting blue")
    {
      auto *dataset = projectContext.addStaticDataset("Asset",
          stage.path(),
          vsr::io::ImporterType::USD,
          {{"color", "blue"}});
      REQUIRE(dataset);
      REQUIRE(dataset->status == DatasetStatus::Available);
      const auto id = dataset->id;
      auto *studio = appContext.vsr.scene.layer("studio");
      REQUIRE(studio);

      THEN("Only the blue variant imports, and the selection is recorded")
      {
        REQUIRE(findNode(studio, "BlueShape"));
        REQUIRE_FALSE(findNode(studio, "RedShape"));
        const auto *setting = dataset->source.importerSettings.at(
            dataset::USD_VARIANT_SELECTIONS_SETTING);
        REQUIRE(setting);
        REQUIRE(*setting == "color=blue");
      }

      THEN("A reimport selects blue again")
      {
        std::string error;
        REQUIRE(projectContext.reimportStaticDataset(id, &error));
        REQUIRE(findNode(studio, "BlueShape"));
        REQUIRE_FALSE(findNode(studio, "RedShape"));
      }
    }

    WHEN("It is added with no selections")
    {
      auto *dataset = projectContext.addStaticDataset(
          "Asset", stage.path(), vsr::io::ImporterType::USD);
      REQUIRE(dataset);

      THEN("The Stage's own selection imports and nothing is recorded")
      {
        auto *studio = appContext.vsr.scene.layer("studio");
        REQUIRE(findNode(studio, "RedShape"));
        REQUIRE(dataset->source.importerSettings.size() == 0);
      }
    }
  }
}

#endif
