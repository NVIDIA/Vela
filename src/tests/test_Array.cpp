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
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>

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

SCENARIO("vsr::scene::Array host memory alignment", "[Array]")
{
  using vsr::scene::Array;

  auto isAligned = [](const void *p) {
    return reinterpret_cast<uintptr_t>(p) % Array::HOST_DATA_ALIGNMENT == 0;
  };

  // Read the padding bytes past the last element, as Embree does with a
  // 16-byte SSE load; an ASan build reports this if the padding is missing.
  auto paddingIsReadable = [](const Array &arr) {
    auto *bytes = static_cast<const uint8_t *>(arr.data());
    auto *tail = bytes + arr.size() * arr.elementSize();
    uint8_t padding[Array::HOST_DATA_PADDING];
    std::memcpy(padding, tail, sizeof(padding));
    return true;
  };

  GIVEN("Host arrays of float3 with sizes that are not a multiple of 16 bytes")
  {
    for (size_t n : {1, 3, 5, 1000, 1001}) {
      auto arr = Array(ANARI_FLOAT32_VEC3, n);

      THEN("The data is aligned and padded for size " + std::to_string(n))
      {
        REQUIRE(isAligned(arr.data()));
        REQUIRE(paddingIsReadable(arr));
      }
    }
  }

  GIVEN("Host 2D and 3D arrays")
  {
    auto arr2 = Array(ANARI_UFIXED8_VEC3, 7, 3);
    auto arr3 = Array(ANARI_FLOAT32, 3, 3, 3);

    THEN("The data is aligned and padded")
    {
      REQUIRE(isAligned(arr2.data()));
      REQUIRE(paddingIsReadable(arr2));
      REQUIRE(isAligned(arr3.data()));
      REQUIRE(paddingIsReadable(arr3));
    }
  }

  GIVEN("A PROXY array converted to host memory")
  {
    auto arr = Array(ANARI_FLOAT32_VEC3, 5, Array::MemoryKind::PROXY);
    arr.convertProxyToHost();

    THEN("The data is aligned and padded")
    {
      REQUIRE(isAligned(arr.data()));
      REQUIRE(paddingIsReadable(arr));
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
