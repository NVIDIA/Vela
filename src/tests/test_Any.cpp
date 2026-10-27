// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/Any.hpp"
// std
#include <string>
#include <utility>

SCENARIO("vsr::core::Any self-assignment", "[Any]")
{
  GIVEN("An Any holding a float")
  {
    vsr::core::Any a(1.5f);
    auto &alias = a;

    WHEN("It is copy-assigned to itself")
    {
      a = alias;

      THEN("It keeps its value")
      {
        REQUIRE(a.is<float>());
        REQUIRE(a.get<float>() == 1.5f);
      }
    }

    WHEN("It is move-assigned to itself")
    {
      a = std::move(alias);

      THEN("It keeps its value")
      {
        REQUIRE(a.is<float>());
        REQUIRE(a.get<float>() == 1.5f);
      }
    }
  }

  GIVEN("An Any holding a string")
  {
    vsr::core::Any a(std::string("hello"));
    auto &alias = a;

    WHEN("It is copy-assigned to itself")
    {
      a = alias;

      THEN("It keeps its value")
      {
        REQUIRE(a.is<std::string>());
        REQUIRE(a.get<std::string>() == "hello");
      }
    }

    WHEN("It is move-assigned to itself")
    {
      a = std::move(alias);

      THEN("It keeps its value")
      {
        REQUIRE(a.is<std::string>());
        REQUIRE(a.get<std::string>() == "hello");
      }
    }
  }
}
