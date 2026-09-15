// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/scene/Object.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/UpdateDelegate.hpp"
// std
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

struct MockObject : public vsr::scene::Object
{
  void parameterChanged(
      const vsr::scene::Parameter *, const vsr::core::Any &) override
  {
    notified = true;
  }

  bool notified{false};
};

// Records the metadata signals, and the order the batch hooks arrive in.
struct RecordingDelegate : public vsr::scene::EmptyUpdateDelegate
{
  void signalMetadataUpdated(
      const vsr::scene::Object *, const char *name) override
  {
    metadata.emplace_back(name);
  }

  void signalMetadataBatchUpdated(const vsr::scene::Object *,
      const std::vector<std::string> &names) override
  {
    metadataBatches.push_back(names);
    order.emplace_back("metadataBatch");
  }

  void signalParameterBatchUpdated(const vsr::scene::Object *,
      const std::vector<const vsr::scene::Parameter *> &) override
  {
    order.emplace_back("parameterBatch");
  }

  std::vector<std::string> metadata;
  std::vector<std::vector<std::string>> metadataBatches;
  std::vector<std::string> order;
};

} // namespace

SCENARIO("vsr::Object interface", "[Object]")
{
  GIVEN("A default constructed Object")
  {
    MockObject obj;

    THEN("The object value type is unknown")
    {
      REQUIRE(obj.type() == ANARI_UNKNOWN);
    }

    THEN("The object has no parameters")
    {
      REQUIRE(obj.numParameters() == 0);
    }

    THEN("The object has no metadata")
    {
      REQUIRE(obj.numMetadata() == 0);
    }

    WHEN("The object is given a parameter")
    {
      obj.setParameter("test", 5);

      THEN("The object has a single parameter")
      {
        REQUIRE(obj.numParameters() == 1);
      }

      THEN("The parameter is identical through index + token access")
      {
        REQUIRE(obj.parameter("test") == &obj.parameterAt(0));
      }

      THEN("The parameter value is correct")
      {
        auto &p = obj.parameterAt(0);
        REQUIRE(p.value().is<int>());
        REQUIRE(p.value().type() == ANARI_INT32);
        REQUIRE(p.value().get<int>() == 5);
      }

      THEN("Parameter notification should have occurred on initial set")
      {
        REQUIRE(obj.notified == true);
      }

      THEN("Changing the value of the parameter should cause notification")
      {
        obj.notified = false;
        obj.parameterAt(0) = 9;
        REQUIRE(obj.notified == true);
      }

      THEN("Removing the parameter results in no more parameters on the object")
      {
        obj.removeParameter("test");
        REQUIRE(obj.numParameters() == 0);
      }
    }

    WHEN("An object metadata value is set")
    {
      obj.setMetadataValue("test_float", 5.f);

      THEN("The object now has 1 metadata on it")
      {
        REQUIRE(obj.numMetadata() == 1);
      }

      THEN("The set metadata name is correct")
      {
        REQUIRE(obj.getMetadataName(0) == std::string("test_float"));
      }

      THEN("The set metadata value is correct")
      {
        REQUIRE(obj.getMetadataValue("test_float").getAs<float>() == 5.f);
      }
    }

    WHEN("An object metadata array is set")
    {
      int arr[3] = {1, 2, 3};
      obj.setMetadataArray("test_array", ANARI_INT32, arr, 3);

      THEN("The object now has 1 metadata on it")
      {
        REQUIRE(obj.numMetadata() == 1);
      }

      THEN("The set metadata name is correct")
      {
        REQUIRE(obj.getMetadataName(0) == std::string("test_array"));
      }

      THEN("The set metadata array is correct")
      {
        const int *arr2 = nullptr;
        size_t size;
        anari::DataType type = ANARI_UNKNOWN;

        obj.getMetadataArray("test_array", &type, (const void **)&arr2, &size);

        REQUIRE(type == ANARI_INT32);
        REQUIRE(size == 3);
        REQUIRE(arr2 != nullptr);
        REQUIRE(arr2[0] == 1);
        REQUIRE(arr2[1] == 2);
        REQUIRE(arr2[2] == 3);
      }
    }
  }
}

SCENARIO("vsr::Object clone for scene objects", "[Object]")
{
  auto scene = std::make_unique<vsr::scene::Scene>();

  GIVEN("A scene-owned surface with parameters and metadata")
  {
    auto geometry = scene->createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);
    auto material = scene->createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = scene->createSurface("primary_surface", geometry, material);
    surface->setParameter("testFloat", 3.5f);
    surface->setMetadataValue("priority", 7);
    const int metadataArray[3] = {1, 2, 3};
    surface->setMetadataArray("bins", ANARI_INT32, metadataArray, 3);

    auto *clone = vsr::scene::cloneObject(surface.data());

    THEN(
        "The clone preserves type, subtype, references, parameters, and metadata")
    {
      REQUIRE(clone != nullptr);
      REQUIRE(clone != surface.data());
      REQUIRE(clone->type() == surface->type());
      REQUIRE(clone->subtype() == surface->subtype());
      REQUIRE(clone->name() == "primary_surface_clone");
      REQUIRE(clone->numParameters() == surface->numParameters());
      REQUIRE(clone->numMetadata() == surface->numMetadata());
      REQUIRE(
          clone->parameterValueAs<float>("testFloat").value() == Approx(3.5f));
      REQUIRE(clone->getMetadataValue("priority").getAs<int>() == 7);

      auto *cloneSurface = dynamic_cast<vsr::scene::Surface *>(clone);
      REQUIRE(cloneSurface != nullptr);
      REQUIRE(cloneSurface->geometry() == geometry.data());
      REQUIRE(cloneSurface->material() == material.data());

      anari::DataType type = ANARI_UNKNOWN;
      const int *values = nullptr;
      size_t size = 0;
      clone->getMetadataArray("bins", &type, (const void **)&values, &size);
      REQUIRE(type == ANARI_INT32);
      REQUIRE(size == 3);
      REQUIRE(values[0] == 1);
      REQUIRE(values[1] == 2);
      REQUIRE(values[2] == 3);
    }

    THEN("Changing the clone does not mutate the original")
    {
      REQUIRE(clone != nullptr);
      clone->setParameter("testFloat", 9.f);
      clone->setMetadataValue("priority", 99);

      REQUIRE(
          clone->parameterValueAs<float>("testFloat").value() == Approx(9.f));
      REQUIRE(surface->parameterValueAs<float>("testFloat").value()
          == Approx(3.5f));
      REQUIRE(clone->getMetadataValue("priority").getAs<int>() == 99);
      REQUIRE(surface->getMetadataValue("priority").getAs<int>() == 7);
    }
  }

  GIVEN("A scene-owned renderer with a device name")
  {
    auto renderer =
        scene->createRenderer("visrtx", vsr::scene::tokens::defaultToken);
    renderer->setName("main_renderer");
    renderer->setParameter("ambientRadiance", 1.25f);
    renderer->setMetadataValue("quality", 4);

    auto *clone = vsr::scene::cloneObject(renderer.get());

    THEN("The clone preserves renderer-specific state")
    {
      auto *rendererClone = dynamic_cast<vsr::scene::Renderer *>(clone);
      REQUIRE(rendererClone != nullptr);
      REQUIRE(rendererClone->rendererDeviceName() == "visrtx");
      REQUIRE(rendererClone->subtype() == renderer->subtype());
      REQUIRE(rendererClone->name() == "main_renderer_clone");
      REQUIRE(rendererClone->parameterValueAs<float>("ambientRadiance").value()
          == Approx(1.25f));
      REQUIRE(rendererClone->getMetadataValue("quality").getAs<int>() == 4);
    }
  }
}

SCENARIO("vsr::Object metadata notification", "[Object]")
{
  GIVEN("An object with a recording update delegate")
  {
    MockObject obj;
    RecordingDelegate delegate;
    obj.setUpdateDelegate(&delegate);

    WHEN("A metadata value is set")
    {
      obj.setMetadataValue("manipulator.distance", 4.f);

      THEN("The delegate is told which key changed")
      {
        REQUIRE(delegate.metadata
            == std::vector<std::string>{"manipulator.distance"});
        REQUIRE(delegate.metadataBatches.empty());
      }
    }

    WHEN("A metadata array is set")
    {
      const int values[3] = {1, 2, 3};
      obj.setMetadataArray("opacityControlPoints", ANARI_INT32, values, 3);

      THEN("The delegate is told which key changed")
      {
        REQUIRE(delegate.metadata
            == std::vector<std::string>{"opacityControlPoints"});
      }
    }

    WHEN("A metadata value is removed")
    {
      obj.setMetadataValue("stale", 1);
      delegate.metadata.clear();
      obj.removeMetadata("stale");

      THEN("The delegate is told which key changed")
      {
        REQUIRE(delegate.metadata == std::vector<std::string>{"stale"});
      }
    }

    WHEN("Removing metadata from an object that has none")
    {
      obj.removeMetadata("never-set");

      THEN("The delegate hears nothing")
      {
        REQUIRE(delegate.metadata.empty());
      }
    }

    WHEN("Removing a key an object carrying other metadata never had")
    {
      obj.setMetadataValue("kept", 1);
      delegate.metadata.clear();
      obj.removeMetadata("never-set");

      THEN("The delegate hears nothing")
      {
        REQUIRE(delegate.metadata.empty());
        REQUIRE(obj.numMetadata() == 1);
      }
    }

    WHEN("Metadata holds an array")
    {
      const int values[2] = {1, 2};
      obj.setMetadataArray("bins", ANARI_INT32, values, 2);
      obj.setMetadataValue("scalar", 1.f);

      THEN("Only the array key says so")
      {
        REQUIRE(obj.metadataHoldsArray("bins"));
        REQUIRE_FALSE(obj.metadataHoldsArray("scalar"));
        REQUIRE_FALSE(obj.metadataHoldsArray("never-set"));
      }
    }

    WHEN("Metadata is written inside a parameter batch")
    {
      obj.beginParameterBatch();
      obj.setMetadataValue("manipulator.at", vsr::math::float3(1.f, 2.f, 3.f));
      obj.setMetadataValue("manipulator.distance", 4.f);
      obj.setMetadataValue("manipulator.distance", 5.f);
      obj.setParameter("position", vsr::math::float3(0.f, 0.f, 1.f));

      THEN("Nothing is signaled until the batch ends")
      {
        REQUIRE(delegate.metadata.empty());
        REQUIRE(delegate.metadataBatches.empty());
      }

      obj.endParameterBatch();

      THEN("The batch arrives once, deduplicated, with no per-key signals")
      {
        REQUIRE(delegate.metadata.empty());
        REQUIRE(delegate.metadataBatches.size() == 1);
        const auto &names = delegate.metadataBatches.front();
        REQUIRE(names.size() == 2);
        REQUIRE(std::find(names.begin(), names.end(), "manipulator.at")
            != names.end());
        REQUIRE(std::find(names.begin(), names.end(), "manipulator.distance")
            != names.end());
      }

      THEN("The metadata batch precedes the parameter batch")
      {
        REQUIRE(delegate.order
            == std::vector<std::string>{"metadataBatch", "parameterBatch"});
      }
    }

    WHEN("A batch contains no metadata")
    {
      obj.beginParameterBatch();
      obj.setParameter("position", vsr::math::float3(0.f, 0.f, 1.f));
      obj.endParameterBatch();

      THEN("No metadata batch is signaled")
      {
        REQUIRE(delegate.metadataBatches.empty());
      }
    }
  }
}
