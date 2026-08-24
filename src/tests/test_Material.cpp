// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/scene/objects/Material.hpp"

using vsr::scene::Material;

SCENARIO("vsr::scene::Material materialx", "[Material]")
{
  GIVEN("A materialx material")
  {
    Material obj(vsr::scene::tokens::material::materialx);
    THEN("It exposes source and materialName parameters")
    {
      REQUIRE(obj.parameter("source") != nullptr);
      REQUIRE(obj.parameter("materialName") != nullptr);
    }
  }
}

SCENARIO("vsr::scene::applyMaterialXStandardSurfacePreset", "[Material]")
{
  GIVEN("A materialx material with the StandardSurface preset")
  {
    Material obj(vsr::scene::tokens::material::materialx);
    vsr::scene::applyMaterialXStandardSurfacePreset(obj);
    THEN("source is an inline instantiation and curated params have defaults")
    {
      REQUIRE(obj.parameter("sourceType")->value().getString()
          == std::string("documentInline"));
      const auto source = obj.parameter("source")->value().getString();
      REQUIRE(source.find("<standard_surface") != std::string::npos);
      REQUIRE(source.find("surfacematerial") != std::string::npos);
      REQUIRE(obj.parameter("materialName")->value().getString()
          == std::string("StandardSurface"));
      REQUIRE(obj.parameter("base_color") != nullptr);
      REQUIRE(obj.parameter("base_color")->value().get<vsr::core::float3>()
          == vsr::core::float3(0.8f, 0.8f, 0.8f));
      REQUIRE(obj.parameter("specular_IOR")->value().get<float>() == 1.5f);
    }
  }
}

SCENARIO("vsr::scene::Material interface", "[Material]")
{
  GIVEN("A default constructed Material")
  {
    Material obj;

    THEN("The object value type is correct")
    {
      REQUIRE(obj.type() == ANARI_MATERIAL);
    }
  }
}
