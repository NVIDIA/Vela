// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/scene/Parameter.hpp"
// std
#include <string>

namespace {

struct MockObject : public vsr::scene::ParameterObserver
{
  void parameterChanged(
      const vsr::scene::Parameter *, const vsr::core::Any &) override
  {
    notified = true;
  }

  void removeParameter(const vsr::scene::Parameter *) override
  {
    // no-op
  }

  bool notified{false};
};

} // namespace

///////////////////////////////////////////////////////////////////////////////

TEST_CASE(
    "Parameter state restoration preserves its current observer", "[Parameter]")
{
  MockObject originalOwner, currentOwner;
  vsr::scene::Parameter saved(&originalOwner, "saved");
  saved.setValue(std::string("second"))
      .setDescription("saved metadata")
      .setMin(1)
      .setMax(9)
      .setStringValues({"first", "second"})
      .setStringSelection(0)
      .setEnabled(false);
  vsr::scene::Parameter current(&currentOwner, "current");
  current.setValue(42);
  originalOwner.notified = false;
  currentOwner.notified = false;

  current.restoreStateFrom(saved);

  REQUIRE_FALSE(originalOwner.notified);
  REQUIRE_FALSE(currentOwner.notified);
  REQUIRE(current.name() == saved.name());
  REQUIRE(current.value().get<std::string>() == "second");
  REQUIRE(current.description() == "saved metadata");
  REQUIRE(current.min().get<int>() == 1);
  REQUIRE(current.max().get<int>() == 9);
  REQUIRE(current.stringValues() == saved.stringValues());
  REQUIRE(current.stringSelection() == 0);
  REQUIRE_FALSE(current.isEnabled());

  current.setValue(std::string("first"));
  REQUIRE(currentOwner.notified);
  REQUIRE_FALSE(originalOwner.notified);

  SECTION("Self-restoration keeps the state and observer")
  {
    currentOwner.notified = false;
    current.restoreStateFrom(current);
    REQUIRE_FALSE(currentOwner.notified);
    REQUIRE(current.value().get<std::string>() == "first");
    current.setEnabled(true);
    REQUIRE(currentOwner.notified);
    REQUIRE_FALSE(originalOwner.notified);
  }
}

SCENARIO("vsr::scene::Parameter interface", "[Parameter]")
{
  GIVEN("A constructed Parameter with a value")
  {
    MockObject obj;
    vsr::scene::Parameter prop(&obj, "test_parameter");
    prop.setValue(5);

    THEN("The Parameter name is correct")
    {
      REQUIRE(prop.name() == "test_parameter");
    }

    THEN("The Parameter value type is correct")
    {
      REQUIRE(prop.value().is<int>());
      REQUIRE(!prop.value().is<float>());
    }

    THEN("The Parameter value is correct")
    {
      REQUIRE(prop.value().get<int>() == 5);
    }

    THEN("The Parameter type is correct")
    {
      REQUIRE(prop.value().type() == ANARI_INT32);
    }

    THEN("The Parameter has no min value")
    {
      REQUIRE(!prop.hasMin());
    }

    THEN("The Parameter has no max value")
    {
      REQUIRE(!prop.hasMax());
    }

    THEN("The Parameter's object observing it has been notified already")
    {
      REQUIRE(obj.notified);
    }

    THEN("Setting the Parameter's value changes the value and notifies")
    {
      prop.setValue(10);

      REQUIRE(prop.value().is<int>());
      REQUIRE(prop.value().get<int>() == 10);
      REQUIRE(obj.notified);
    }
  }

  GIVEN("A constructed vsr::scene::Parameter with a value and min/max")
  {
    MockObject obj;
    vsr::scene::Parameter prop(&obj, "test_parameter");
    prop.setValue(5)
        .setDescription("this is a test parameter")
        .setMin(0)
        .setMax(10);

    THEN("The Parameter has a min value")
    {
      REQUIRE(prop.hasMin());
      REQUIRE(prop.min().get<int>() == 0);
    }

    THEN("The Parameter has no max value")
    {
      REQUIRE(prop.hasMax());
      REQUIRE(prop.max().get<int>() == 10);
    }

    THEN("The Parameter min value can be reset through setMin({})")
    {
      prop.setMin({});
      REQUIRE(!prop.hasMin());
    }

    THEN("The Parameter max value can be reset through setMax({})")
    {
      prop.setMax({});
      REQUIRE(!prop.hasMax());
    }
  }
}

SCENARIO("vsr::core::Any recognizes strings through the generic accessors",
    "[Parameter]")
{
  // ANARITypeFor<std::string> is ANARI_UNKNOWN, so the generic is<>/get<>
  // templates silently miss ANARI_STRING unless Any specializes them. Callers
  // that reach for is<std::string>() -- the Lua bindings among them -- then
  // treat every string parameter as absent.
  GIVEN("An Any holding a string")
  {
    vsr::core::Any value = std::string("hello");

    THEN("Its type is ANARI_STRING")
    {
      REQUIRE(value.type() == ANARI_STRING);
    }

    THEN("is<std::string>() reports the string")
    {
      REQUIRE(value.is<std::string>());
      REQUIRE(!value.is<int>());
      REQUIRE(!value.is<float>());
    }

    THEN("get<std::string>() returns the string")
    {
      REQUIRE(value.get<std::string>() == "hello");
    }

    THEN("getValueOr<std::string>() returns the string")
    {
      REQUIRE(value.getValueOr<std::string>("fallback") == "hello");
    }
  }

  GIVEN("An Any holding a non-string")
  {
    vsr::core::Any value = 5;

    THEN("It is not mistaken for a string")
    {
      REQUIRE(!value.is<std::string>());
      REQUIRE(value.getValueOr<std::string>("fallback") == "fallback");
    }
  }
}
