// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/Token.hpp"

SCENARIO("vsr::core::Token interface", "[Token]")
{
  GIVEN("A default constructed Token")
  {
    auto token = vsr::core::Token();

    THEN("The Token value is null")
    {
      REQUIRE(token.value() == nullptr);
    }

    THEN("A second default constructed Token will have the same value")
    {
      auto token2 = vsr::core::Token();
      REQUIRE(token == token2);
    }
  }

  GIVEN("A constructed Token from a given string")
  {
    auto token = vsr::core::Token("test1");

    THEN("The Token value is not null")
    {
      REQUIRE(token.value() != nullptr);
    }

    THEN("A second token constructed with the same string has the same value")
    {
      auto token2 = vsr::core::Token("test1");
      REQUIRE(token == token2);
    }

    THEN(
        "A second token constructed with a different string has the same "
        "value")
    {
      auto token2 = vsr::core::Token("test2");
      REQUIRE(token != token2);
    }
  }
}
