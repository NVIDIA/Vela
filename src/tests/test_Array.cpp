// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#ifdef VSR_USE_CUDA
#undef VSR_USE_CUDA
#define VSR_USE_CUDA 0
#endif

// catch
#include "catch.hpp"
// vsr_core
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/algorithms/computeScalarRange.hpp"
#include "vsr/scene/objects/Array.hpp"
// std
#include <memory>
#include <numeric>

SCENARIO("vsr::scene::Array interface", "[Array]")
{
  GIVEN("A default constructed Array")
  {
    vsr::scene::Array arr;

    THEN("The array is empty")
    {
      REQUIRE(arr.size() == 0);
    }

    THEN("The array element type is unknown")
    {
      REQUIRE(arr.elementType() == ANARI_UNKNOWN);
    }
  }

  GIVEN("A constructed int Array")
  {
    auto arr = vsr::scene::Array(ANARI_INT32, 4);

    THEN("The array is not empty")
    {
      REQUIRE(arr.size() == 4);
    }

    THEN("The array type/shape is correct")
    {
      REQUIRE(arr.type() == ANARI_ARRAY1D);
    }

    THEN("The array element type is correct")
    {
      REQUIRE(arr.elementType() == ANARI_INT32);
    }

    THEN("Mapping the array is not null")
    {
      void *m = arr.map();
      REQUIRE(m != nullptr);
      arr.unmap();
    }
  }

  GIVEN("A constructed a UFIXED8 Array with linear elements")
  {
    auto arr = vsr::scene::Array(ANARI_UFIXED8, 256);
    auto *begin = arr.mapAs<uint8_t>();
    auto *end = begin + 256;
    std::iota(begin, end, 0);
    arr.unmap();

    THEN("Computing the range will return the correct min + max")
    {
      auto range = vsr::scene::computeScalarRange(arr);
      REQUIRE(range.x == 0.f);
      REQUIRE(range.y == 1.f);
    }
  }
}

SCENARIO("vsr::scene::Array clone", "[Array]")
{
  auto scene = std::make_unique<vsr::scene::Scene>();

  GIVEN("A scene-owned host array with data and metadata")
  {
    auto array = scene->createArray(ANARI_INT32, 4);
    array->setName("samples");
    array->setMetadataValue("stride", 16);

    int values[4] = {2, 4, 6, 8};
    array->setData(values, 4);

    auto *clone =
        static_cast<vsr::scene::Array *>(vsr::scene::cloneObject(array.data()));

    THEN("The clone preserves array shape, storage kind, data, and metadata")
    {
      REQUIRE(clone != nullptr);
      REQUIRE(clone != array.data());
      REQUIRE(clone->type() == array->type());
      REQUIRE(clone->elementType() == array->elementType());
      REQUIRE(clone->size() == array->size());
      REQUIRE(clone->kind() == array->kind());
      REQUIRE(clone->name() == "samples_clone");
      REQUIRE(clone->getMetadataValue("stride").getAs<int>() == 16);

      const auto *cloneValues = clone->dataAs<int>();
      REQUIRE(cloneValues != nullptr);
      REQUIRE(cloneValues[0] == 2);
      REQUIRE(cloneValues[1] == 4);
      REQUIRE(cloneValues[2] == 6);
      REQUIRE(cloneValues[3] == 8);
    }

    THEN("Changing the clone data does not mutate the original")
    {
      REQUIRE(clone != nullptr);
      auto *cloneValues = clone->mapAs<int>();
      REQUIRE(cloneValues != nullptr);
      cloneValues[1] = 42;
      clone->unmap();

      REQUIRE(clone->dataAs<int>()[1] == 42);
      REQUIRE(array->dataAs<int>()[1] == 4);
    }
  }
}
