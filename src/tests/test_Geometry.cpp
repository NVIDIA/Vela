// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/scene/objects/Geometry.hpp"

using vsr::scene::Geometry;

SCENARIO("vsr::Geometry interface", "[Geometry]")
{
  GIVEN("A default constructed Geometry")
  {
    Geometry obj;

    THEN("The object value type is correct")
    {
      REQUIRE(obj.type() == ANARI_GEOMETRY);
    }
  }
}
