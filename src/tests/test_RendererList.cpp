// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_tests
#include "LogCapture.h"
// vsr
#include "vsr/scene/Scene.hpp"
#include "windows/RendererList.h"
// std
#include <vector>

using vsr::scene::RendererAppRef;
using vsr::scene::Scene;
using vsr::ui::imgui::RendererList;

namespace {

std::vector<size_t> indicesOf(const RendererList &list)
{
  std::vector<size_t> indices;
  for (const auto &r : list.objects())
    indices.push_back(r->index());
  return indices;
}

size_t currentIndex(const RendererList &list)
{
  return list.current() ? list.current()->index() : VSR_INVALID_INDEX;
}

} // namespace

SCENARIO("vsr::ui::imgui::RendererList follows one device's renderers",
    "[RendererList]")
{
  Scene scene;
  LogCapture log;

  GIVEN("A scene with renderers of two devices")
  {
    scene.createRenderer("devA", "fast"); // 0
    scene.createRenderer("devB", "fast"); // 1
    scene.createRenderer("devA", "pathtracer"); // 2
    scene.createRenderer("devA", "fast"); // 3: a clone of 0

    RendererList list;

    THEN("An unbound list is empty")
    {
      REQUIRE(list.empty());
      REQUIRE_FALSE(list.current());
    }

    WHEN("It is bound to one device")
    {
      list.bind(scene, "devA");

      THEN("It lists that device's renderers in storage order")
      {
        REQUIRE(indicesOf(list) == std::vector<size_t>{0, 2, 3});
      }

      THEN("The first one is current")
      {
        REQUIRE(currentIndex(list) == 0);
      }

      THEN("A renderer of the list can be selected")
      {
        REQUIRE(list.select(2));
        REQUIRE(currentIndex(list) == 2);
      }

      THEN("A renderer of another device cannot be selected")
      {
        REQUIRE_FALSE(list.select(1));
        REQUIRE(currentIndex(list) == 0);
      }

      AND_WHEN("A renderer is added to that device")
      {
        scene.createRenderer("devA", "ao");

        THEN("It joins the end of the list, and the current one stays")
        {
          REQUIRE(indicesOf(list) == std::vector<size_t>{0, 2, 3, 4});
          REQUIRE(currentIndex(list) == 0);
        }
      }

      AND_WHEN("A renderer is added to another device")
      {
        scene.createRenderer("devB", "ao");

        THEN("The list is unchanged")
        {
          REQUIRE(indicesOf(list) == std::vector<size_t>{0, 2, 3});
        }
      }

      AND_WHEN("A new renderer reuses a freed slot")
      {
        scene.removeObject(scene.getObject(ANARI_RENDERER, 2));
        scene.createRenderer("devA", "ao");

        THEN("It takes that slot's place in the list")
        {
          REQUIRE(indicesOf(list) == std::vector<size_t>{0, 2, 3});
          REQUIRE(list.objects()[1]->subtype() == "ao");
        }
      }

      AND_WHEN("A renderer that is not current is removed")
      {
        list.select(2);
        scene.removeObject(scene.getObject(ANARI_RENDERER, 3));

        THEN("It leaves the list, and the current one stays")
        {
          REQUIRE(indicesOf(list) == std::vector<size_t>{0, 2});
          REQUIRE(currentIndex(list) == 2);
        }
      }

      AND_WHEN(
          "The current renderer is removed and another of its subtype"
          " remains")
      {
        list.select(3);
        scene.removeObject(scene.getObject(ANARI_RENDERER, 0));

        THEN("That renderer becomes current")
        {
          REQUIRE(indicesOf(list) == std::vector<size_t>{2, 3});
          REQUIRE(currentIndex(list) == 3);
        }
      }

      AND_WHEN("A clone that is current is removed")
      {
        list.select(3);
        scene.removeObject(scene.getObject(ANARI_RENDERER, 3));

        THEN("The renderer of its subtype becomes current")
        {
          REQUIRE(currentIndex(list) == 0);
        }
      }

      AND_WHEN(
          "The current renderer is removed and none of its subtype"
          " remains")
      {
        list.select(2);
        scene.removeObject(scene.getObject(ANARI_RENDERER, 2));

        THEN("The first renderer becomes current")
        {
          REQUIRE(indicesOf(list) == std::vector<size_t>{0, 3});
          REQUIRE(currentIndex(list) == 0);
        }
      }

      AND_WHEN("Every renderer of the device is removed")
      {
        scene.removeObject(scene.getObject(ANARI_RENDERER, 0));
        scene.removeObject(scene.getObject(ANARI_RENDERER, 2));
        scene.removeObject(scene.getObject(ANARI_RENDERER, 3));

        THEN("The list is empty and has no current renderer")
        {
          REQUIRE(list.empty());
          REQUIRE_FALSE(list.current());
        }

        AND_WHEN("A renderer is added to that device again")
        {
          scene.createRenderer("devA", "fast");

          THEN("It becomes current")
          {
            REQUIRE(indicesOf(list).size() == 1);
            REQUIRE(list.current() == list.objects()[0]);
          }
        }
      }

      AND_WHEN("Every object of the scene is removed")
      {
        scene.removeAllObjects();

        THEN("The list is empty and has no current renderer")
        {
          REQUIRE(list.empty());
          REQUIRE_FALSE(list.current());
        }

        AND_WHEN("A renderer is added to that device again")
        {
          scene.createRenderer("devA", "fast");

          THEN("It is listed and current")
          {
            REQUIRE(indicesOf(list) == std::vector<size_t>{0});
            REQUIRE(currentIndex(list) == 0);
          }
        }
      }

      AND_WHEN("It is unbound")
      {
        list.unbind();
        scene.createRenderer("devA", "ao");

        THEN("It is empty and no longer follows the scene")
        {
          REQUIRE(list.empty());
          REQUIRE_FALSE(list.current());
        }
      }

      AND_WHEN("It is bound to the other device")
      {
        list.bind(scene, "devB");

        THEN("It lists only that device's renderers")
        {
          REQUIRE(indicesOf(list) == std::vector<size_t>{1});
          REQUIRE(currentIndex(list) == 1);
        }
      }

      THEN("Removing renderers it holds logs no use count errors")
      {
        list.select(3);
        scene.removeObject(scene.getObject(ANARI_RENDERER, 3));
        scene.removeObject(scene.getObject(ANARI_RENDERER, 0));
        list.unbind();
        REQUIRE_FALSE(log.sawMessageContaining("decUseCount"));
      }
    }
  }
}
