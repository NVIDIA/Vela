// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/ObjectPool.hpp"
// std
#include <string>

SCENARIO("vsr::core::ObjectPool interface tests", "[ObjectPool]")
{
  GIVEN("A default constructed ObjectPool")
  {
    vsr::core::ObjectPool<std::string> iv;

    THEN("The map should be empty")
    {
      REQUIRE(iv.size() == 0);
      REQUIRE(iv.empty());
    }

    WHEN("The map has a value added to it")
    {
      auto ref = iv.insert("hello");

      THEN("The map adds it to the underlying storage")
      {
        REQUIRE(iv.size() == 1);
        REQUIRE(!iv.empty());
      }

      THEN("The returned reference values is correct")
      {
        REQUIRE(*ref == "hello");
        REQUIRE(!ref->empty());
        REQUIRE(ref.index() == 0);
      }

      THEN("A copy of the reference is equal to the first")
      {
        auto ref2 = ref;
        REQUIRE(ref == ref2);
      }

      WHEN("Adding a second value")
      {
        auto ref2 = iv.insert("world");

        THEN("The map should contain 2 values")
        {
          REQUIRE(iv.size() == 2);
          REQUIRE(!iv.empty());
        }

        THEN("A newly added value should have a unique index")
        {
          REQUIRE(*ref2 == "world");
          REQUIRE(!ref2->empty());
          REQUIRE(ref2.index() == 1);
        }
      }

      WHEN("The added value is erased")
      {
        auto idx = ref.index();
        bool erased = iv.erase(idx);

        THEN("Erasing the value should return true")
        {
          REQUIRE(erased);
        }

        THEN("Erasing the same index again should return false")
        {
          erased = iv.erase(idx);
          REQUIRE(!erased);
        }

        THEN("The map should be empty")
        {
          REQUIRE(iv.size() == 0);
          REQUIRE(iv.empty());
        }

        THEN("A newly added value should have the same index")
        {
          auto ref2 = iv.insert("world");

          REQUIRE(iv.size() == 1);
          REQUIRE(!iv.empty());
          REQUIRE(*ref2 == "world");
          REQUIRE(!ref2->empty());
          REQUIRE(ref2.index() == 0);
        }
      }
    }
  }
}

SCENARIO("vsr::core::ObjectPool defragmentation", "[ObjectPool]")
{
  GIVEN("An ObjectPool with 5 values")
  {
    vsr::core::ObjectPool<int> iv;
    for (int i = 0; i < 5; i++)
      iv.emplace(i);

    THEN("The density should be 1.f")
    {
      REQUIRE(iv.density() == 1.f);
    }

    THEN("An out-of-bounds access should return the argument to value_or()")
    {
      REQUIRE(iv.at(1).value_or(100) != 100);
      REQUIRE(iv.at(10).value_or(100) == 100);
    }

    WHEN("1 value is erased")
    {
      iv.erase(1);

      THEN("The new size is correct")
      {
        REQUIRE(iv.size() == 4);
      }

      THEN("The capacity is the same")
      {
        REQUIRE(iv.capacity() == 5);
      }

      THEN("The density should be 0.8f")
      {
        REQUIRE(iv.density() == 0.8f);
      }
    }

    WHEN("2 values are erased")
    {
      iv.erase(1);
      iv.erase(2);

      THEN("The density should be 0.6f")
      {
        REQUIRE(iv.density() == 0.6f);
      }

      WHEN("and the ObjectPool is defragmented")
      {
        iv.defragment();

        THEN("The new size and capacity are correct")
        {
          REQUIRE(iv.size() == 3);
          REQUIRE(iv.capacity() == 3);
        }

        THEN("The density should be 1.f")
        {
          REQUIRE(iv.density() == 1.f);
        }

        THEN("The remaining values are correct")
        {
          REQUIRE(iv[0] == 0);
          REQUIRE(iv[1] == 4);
          REQUIRE(iv[2] == 3);
        }
      }
    }

    WHEN("the last 2 values are erased")
    {
      iv.erase(3);
      iv.erase(4);

      WHEN("and the ObjectPool is defragmented")
      {
        iv.defragment();

        THEN("The new size and capacity are correct")
        {
          REQUIRE(iv.size() == 3);
          REQUIRE(iv.capacity() == 3);
        }

        THEN("The density should be 1.f")
        {
          REQUIRE(iv.density() == 1.f);
        }

        THEN("The remaining values are correct")
        {
          REQUIRE(iv[0] == 0);
          REQUIRE(iv[1] == 1);
          REQUIRE(iv[2] == 2);
        }
      }
    }
  }
}

SCENARIO("vsr::core::ObjectPool placement at a chosen slot", "[ObjectPool]")
{
  GIVEN("A pool holding one value")
  {
    vsr::core::ObjectPool<std::string> pool;
    pool.insert("zero");

    WHEN("A value is placed beyond the current capacity")
    {
      auto ref = pool.insert_at(3, "three");

      THEN("It lands in that slot and the gap stays empty")
      {
        REQUIRE(ref);
        REQUIRE(ref.index() == 3);
        REQUIRE(*ref == "three");
        REQUIRE(pool.capacity() == 4);
        REQUIRE(pool.size() == 2);
        REQUIRE(pool.slot_empty(1));
        REQUIRE(pool.slot_empty(2));
        REQUIRE(!pool.at(1));
        REQUIRE(pool.at(3) == ref);
      }

      THEN("The gap slots are recycled by later plain inserts")
      {
        auto a = pool.insert("a");
        auto b = pool.insert("b");
        REQUIRE(a.index() < 3);
        REQUIRE(b.index() < 3);
        REQUIRE(a.index() != b.index());
        REQUIRE(pool.is_dense());
      }

      THEN("A gap slot can be filled by index as well")
      {
        auto one = pool.emplace_at(1, "one");
        REQUIRE(one);
        REQUIRE(one.index() == 1);
        REQUIRE(pool.size() == 3);
        auto next = pool.insert("next");
        REQUIRE(next.index() == 2);
        REQUIRE(pool.is_dense());
      }

      THEN("An occupied slot refuses the placement")
      {
        REQUIRE(!pool.insert_at(0, "clash"));
        REQUIRE(!pool.insert_at(3, "clash"));
        REQUIRE(*pool.at(0) == "zero");
        REQUIRE(*pool.at(3) == "three");
        REQUIRE(pool.size() == 2);
      }
    }

    WHEN("A slot freed by erase is filled by index")
    {
      pool.insert("one");
      pool.insert("two");
      pool.erase(1);
      auto ref = pool.insert_at(1, "again");

      THEN("The pool is dense once more")
      {
        REQUIRE(ref.index() == 1);
        REQUIRE(pool.is_dense());
        REQUIRE(pool.size() == 3);
      }
    }
  }
}
