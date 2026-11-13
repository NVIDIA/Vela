// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_tests
#include "HelideTestDevice.h"
#include "LogCapture.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/UpdateDelegate.hpp"
// catch
#include "catch.hpp"
// std
#include <cmath>
#include <string>
#include <vector>

namespace {

void requireMat4Near(const vsr::math::mat4 &actual,
    const vsr::math::mat4 &expected,
    float eps = 1e-4f)
{
  for (int c = 0; c < 4; c++) {
    for (int r = 0; r < 4; r++) {
      CAPTURE(c, r, actual[c][r], expected[c][r]);
      REQUIRE(std::abs(actual[c][r] - expected[c][r]) <= eps);
    }
  }
}

struct CountingDelegate : public vsr::scene::EmptyUpdateDelegate
{
  CountingDelegate(int *objectAddedCount) : m_objectAddedCount(objectAddedCount)
  {}

  void signalObjectAdded(const vsr::scene::Object *) override
  {
    if (m_objectAddedCount)
      (*m_objectAddedCount)++;
  }

  int *m_objectAddedCount{nullptr};
};

std::vector<size_t> rendererIndices(
    const vsr::scene::Scene &scene, vsr::core::Token deviceName)
{
  std::vector<size_t> indices;
  for (const auto &r : scene.renderersOfDevice(deviceName))
    indices.push_back(r->index());
  return indices;
}

size_t indexOrInvalid(const vsr::scene::RendererAppRef &r)
{
  return r ? r->index() : VSR_INVALID_INDEX;
}

} // namespace

static vsr::scene::LayerNodeRef findDirectChild(
    vsr::scene::LayerNodeRef parent, const std::string &name)
{
  if (!parent)
    return {};

  auto child = parent->next();
  while (child && child != parent) {
    if ((*child)->name() == name)
      return child;
    child = child->sibling();
  }

  return {};
}

SCENARIO("vsr::scene::Scene owns an intrinsic update delegate root", "[Scene]")
{
  GIVEN("A scene")
  {
    vsr::scene::Scene scene;
    auto &delegate = scene.updateDelegate();

    THEN("The scene starts with an empty MultiUpdateDelegate root")
    {
      REQUIRE(delegate.size() == 0);
    }

    THEN("Const and non-const accessors return the same delegate root")
    {
      const auto &constScene = scene;
      REQUIRE(&constScene.updateDelegate() == &delegate);
    }

    THEN(
        "Registering a child delegate observes new scene-owned objects and arrays")
    {
      int objectAddedCount = 0;
      auto *countingDelegate =
          scene.updateDelegate().emplace<CountingDelegate>(&objectAddedCount);
      auto geometry = scene.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);
      auto array = scene.createArray(ANARI_FLOAT32, 4);

      REQUIRE(geometry);
      REQUIRE(array);
      REQUIRE(countingDelegate != nullptr);
      REQUIRE(objectAddedCount == 2);
    }
  }
}

SCENARIO("vsr::scene::Scene deep-clones layer subtrees", "[Scene]")
{
  GIVEN("A scene with a layer subtree containing object references")
  {
    vsr::scene::Scene scene;
    auto *layer = scene.defaultLayer();
    REQUIRE(layer != nullptr);

    auto material = scene.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    material->setName("linkedMaterial");

    auto light = scene.createObject<vsr::scene::Light>(
        vsr::scene::tokens::light::directional);
    light->setName("sourceLight");
    light->setParameter("irradiance", 2.f);
    light->setParameterObject("linkedMaterial", *material);

    auto group = scene.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "group");
    group->value().setInstanceParameter(
        "linkedMaterial", {material->type(), material->index()});
    scene.insertChildObjectNode(group, light, "mainLight");
    scene.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "capacityFiller");

    WHEN("The subtree is cloned with default object sharing")
    {
      auto cloneGroup = scene.cloneLayerSubtree(group, layer->root());

      THEN("The clone references the same objects")
      {
        REQUIRE(cloneGroup);
        auto cloneLightNode = findDirectChild(cloneGroup, "mainLight");
        REQUIRE(cloneLightNode);
        REQUIRE((*cloneLightNode)->getObject() == light.data());

        const auto *instanceValue =
            cloneGroup->value().getInstanceParameters().at("linkedMaterial");
        REQUIRE(instanceValue != nullptr);
        REQUIRE(instanceValue->holdsObject());
        REQUIRE(instanceValue->type() == material->type());
        REQUIRE(instanceValue->getAsObjectIndex() == material->index());
      }
    }

    WHEN("The subtree is cloned with object references cloned")
    {
      const bool cloneObjectReferences = true;
      auto cloneGroup =
          scene.cloneLayerSubtree(group, layer->root(), cloneObjectReferences);

      THEN("The clone has distinct objects and remapped object parameters")
      {
        REQUIRE(cloneGroup);
        REQUIRE(cloneGroup != group);

        auto cloneLightNode = findDirectChild(cloneGroup, "mainLight");
        REQUIRE(cloneLightNode);
        auto *cloneLight =
            dynamic_cast<vsr::scene::Light *>((*cloneLightNode)->getObject());
        REQUIRE(cloneLight != nullptr);
        REQUIRE(cloneLight != light.data());
        REQUIRE(cloneLight->parameterValueAs<float>("irradiance").value()
            == Approx(2.f));

        auto *cloneMaterial =
            cloneLight->parameterValueAsObject<vsr::scene::Material>(
                "linkedMaterial");
        REQUIRE(cloneMaterial != nullptr);
        REQUIRE(cloneMaterial != material.data());

        const auto *instanceValue =
            cloneGroup->value().getInstanceParameters().at("linkedMaterial");
        REQUIRE(instanceValue != nullptr);
        REQUIRE(instanceValue->holdsObject());
        REQUIRE(instanceValue->type() == cloneMaterial->type());
        REQUIRE(instanceValue->getAsObjectIndex() == cloneMaterial->index());

        cloneLight->setParameter("irradiance", 7.f);
        REQUIRE(light->parameterValueAs<float>("irradiance").value()
            == Approx(2.f));
      }
    }
  }
}

SCENARIO("vsr::scene::Scene clears layer references when removing an object",
    "[Scene]")
{
  GIVEN("An object referenced by nodes across multiple layers")
  {
    vsr::scene::Scene scene;
    auto *layerA = scene.defaultLayer();
    auto *layerB = scene.addLayer(vsr::core::Token("other"));

    auto light = scene.createObject<vsr::scene::Light>(
        vsr::scene::tokens::light::directional);
    light->setName("sharedLight");

    scene.insertChildObjectNode(layerA->root(), light, "instA");
    auto groupB = scene.insertChildTransformNode(
        layerB->root(), vsr::math::IDENTITY_MAT4, "groupB");
    scene.insertChildObjectNode(groupB, light, "instB");

    REQUIRE(light->useCount(vsr::scene::Object::UseKind::LAYER) == 2);

    WHEN("The object is removed")
    {
      const size_t lightIndex = light->index();
      scene.removeObject(light.data());

      THEN("All referencing layer nodes are removed, leaving no dangling refs")
      {
        REQUIRE(scene.getObject(ANARI_LIGHT, lightIndex) == nullptr);

        // No node in any layer still references the removed object.
        auto countReferencingNodes = [&](vsr::scene::Layer &layer) {
          int count = 0;
          layer.traverse(layer.root(), [&](auto &node, int) {
            if (node.value().isObject()
                && node.value().getObjectIndex() == lightIndex)
              count++;
            return true;
          });
          return count;
        };
        REQUIRE(countReferencingNodes(*layerA) == 0);
        REQUIRE(countReferencingNodes(*layerB) == 0);

        // The "instA" node is gone entirely (not left as an empty placeholder).
        REQUIRE_FALSE(findDirectChild(layerA->root(), "instA"));

        // Surrounding structure (the unrelated transform group) is preserved.
        REQUIRE(groupB.valid());
        REQUIRE((*groupB)->isTransform());
        REQUIRE_FALSE(findDirectChild(groupB, "instB"));
      }
    }
  }

  GIVEN("An object that is not referenced by any layer")
  {
    vsr::scene::Scene scene;
    auto material = scene.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    REQUIRE(material->useCount(vsr::scene::Object::UseKind::LAYER) == 0);

    WHEN("The object is removed")
    {
      const size_t idx = material->index();
      scene.removeObject(material.data());

      THEN("It is removed without issue")
      {
        REQUIRE(scene.getObject(ANARI_MATERIAL, idx) == nullptr);
      }
    }
  }
}

SCENARIO(
    "vsr::scene::Scene removes an object leaf node with its object", "[Scene]")
{
  GIVEN("A light referenced by exactly one leaf node")
  {
    vsr::scene::Scene scene;
    auto *layer = scene.defaultLayer();
    auto group = scene.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "group");
    auto light =
        scene.createObject<vsr::scene::Light>(vsr::scene::tokens::light::point);
    auto leaf = scene.insertChildObjectNode(group, light, "light");
    const size_t lightIndex = light->index();
    const size_t leafIndex = leaf.index();

    WHEN("The leaf is removed together with its object")
    {
      scene.removeNode(leaf, true);

      THEN("Both are gone once and the parent survives as an empty group")
      {
        REQUIRE(scene.getObject(ANARI_LIGHT, lightIndex) == nullptr);
        REQUIRE_FALSE(layer->at(leafIndex));
        REQUIRE(group.valid());
        REQUIRE(group->isLeaf());
        REQUIRE(findDirectChild(layer->root(), "group"));
      }
    }
  }
}

SCENARIO(
    "vsr::scene::LayerNodeData preserves singular SRT transforms", "[Scene]")
{
  GIVEN("A transform with elevation 90 and roll 270")
  {
    vsr::math::mat3 srt;
    srt[0] = vsr::math::float3(1.f, 1.f, 1.f);
    srt[1] = vsr::math::float3(0.f, 90.f, 270.f);
    srt[2] = vsr::math::float3(0.f, 0.f, 0.f);

    vsr::scene::LayerNodeData source(nullptr, srt);
    vsr::scene::LayerNodeData node(nullptr, source.getTransform());

    WHEN("The transform is exposed as UI SRT and applied back")
    {
      auto uiSrt = node.getTransformSRT();
      node.setAsTransform(uiSrt);

      THEN("The UI SRT keeps the roll and the matrix does not move")
      {
        REQUIRE(vsr::math::neql(uiSrt[1].x, 0.f, 1e-3f));
        REQUIRE(vsr::math::neql(uiSrt[1].y, 90.f, 1e-3f));
        REQUIRE(vsr::math::neql(uiSrt[1].z, 270.f, 1e-3f));
        requireMat4Near(node.getTransform(), source.getTransform());
      }
    }
  }
}

SCENARIO("vsr::scene::Scene delegate registration controls live signaling",
    "[Scene]")
{
  GIVEN("A scene with an explicitly registered child delegate")
  {
    vsr::scene::Scene scene;
    int objectAddedCount = 0;
    auto *delegate =
        scene.updateDelegate().emplace<CountingDelegate>(&objectAddedCount);

    REQUIRE(scene.updateDelegate().size() == 1);

    WHEN("A new scene object is created while the delegate is registered")
    {
      auto geometry = scene.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);

      THEN("The delegate receives the object-added signal")
      {
        REQUIRE(geometry);
        REQUIRE(objectAddedCount == 1);
      }
    }

    WHEN("The delegate is erased before more scene changes occur")
    {
      scene.updateDelegate().erase(delegate);
      auto geometry = scene.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);

      THEN("The root becomes empty and no further signals are delivered")
      {
        REQUIRE(geometry);
        REQUIRE(scene.updateDelegate().size() == 0);
        REQUIRE(objectAddedCount == 0);
      }
    }
  }
}

SCENARIO("vsr::scene::Scene picks a device's fallback renderer", "[Scene]")
{
  vsr::scene::Scene scene;

  GIVEN("Renderers of made-up subtypes on two devices")
  {
    scene.createRenderer("helide", "fast"); // 0
    scene.createRenderer("other", "fast"); // 1
    scene.createRenderer("helide", "slow"); // 2
    scene.createRenderer("helide", "fast"); // 3: a clone of 0

    THEN("It returns the device's lowest-index renderer of the subtype")
    {
      REQUIRE(indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "fast"))
          == 0);
      REQUIRE(indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "slow"))
          == 2);
      REQUIRE(
          indexOrInvalid(scene.rendererOfSubtypeOrFirst("other", "fast")) == 1);
    }

    THEN("Without a renderer of the subtype, it returns the device's first")
    {
      REQUIRE(
          indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "ao")) == 0);
      REQUIRE(
          indexOrInvalid(scene.rendererOfSubtypeOrFirst("other", "slow")) == 1);
    }

    THEN("Without renderers of the device, it returns empty")
    {
      REQUIRE_FALSE(scene.rendererOfSubtypeOrFirst("devX", "fast"));
    }

    THEN("By default it excludes nothing")
    {
      REQUIRE(indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "fast"))
          == 0);
    }

    THEN("An excluded renderer of the subtype yields to the next one")
    {
      REQUIRE(
          indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "fast", 0))
          == 3);
    }

    THEN("Excluding the subtype's only renderer falls back to the first")
    {
      REQUIRE(
          indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "slow", 2))
          == 0);
    }

    THEN("An excluded first renderer is not the fallback")
    {
      REQUIRE(indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "ao", 0))
          == 2);
    }

    THEN("Excluding the device's only renderer returns empty")
    {
      REQUIRE_FALSE(scene.rendererOfSubtypeOrFirst("other", "fast", 1));
    }

    THEN("Excluding another device's renderer changes nothing")
    {
      REQUIRE(
          indexOrInvalid(scene.rendererOfSubtypeOrFirst("helide", "fast", 1))
          == 0);
    }
  }
}

SCENARIO("vsr::scene::Scene reloads a device's renderers in place", "[Scene]")
{
  HelideTestDevice anariDevice;
  if (!anariDevice) {
    WARN("helide unavailable, skipping");
    return;
  }
  auto d = anariDevice.device;

  const auto offered = vsr::scene::getANARIObjectSubtypes(d, ANARI_RENDERER);
  // The cases below assume helide offers one subtype, so a subtype it lacks
  // can be made up and every survivor is its 'default' renderer.
  REQUIRE(offered == std::vector<std::string>{"default"});

  // What a renderer of a fresh scene looks like.
  vsr::scene::Scene reference;
  const auto fresh = reference.createStandardRenderers("helide", d).front();
  const auto freshBackground =
      fresh->parameterValueAs<vsr::math::float4>("background");
  REQUIRE(freshBackground);

  vsr::scene::Scene scene;
  LogCapture log;

  GIVEN(
      "An edited standard renderer, a clone of it, a subtype helide does not"
      " offer, and another device's renderer")
  {
    scene.createRenderer("helide", "retired"); // 0
    {
      auto other = scene.createRenderer("other", "default"); // 1
      other->setParameter("madeUp", 1.f);

      auto standard = scene.createStandardRenderers("helide", d).front(); // 2
      REQUIRE(standard->index() == 2);
      standard->setName("my renderer");
      standard->setMetadataValue("note", vsr::core::Any(7));
      standard->setParameter(
          "background", vsr::math::float4(1.f, 0.f, 0.f, 1.f));
      standard->setParameter("madeUp", 1.f);

      REQUIRE(vsr::scene::cloneObject(standard.get())->index() == 3);
      scene.createRenderer("helide", "retired"); // 4
    }
    REQUIRE(
        rendererIndices(scene, "helide") == std::vector<size_t>{0, 2, 3, 4});

    WHEN("The device's renderers are reloaded")
    {
      scene.reloadStandardRenderers("helide", d);
      auto kept = scene.getObject<vsr::scene::Renderer>(2);

      THEN("The lowest-index renderer of the offered subtype keeps its slot")
      {
        REQUIRE(kept);
        REQUIRE(kept->subtype() == "default");
        REQUIRE(rendererIndices(scene, "helide") == std::vector<size_t>{2});
      }

      THEN("Its parameters return to the device's defaults")
      {
        REQUIRE(kept->numParameters() == fresh->numParameters());
        REQUIRE_FALSE(kept->parameter("madeUp"));
        REQUIRE(kept->parameterValueAs<vsr::math::float4>("background")
            == freshBackground);
      }

      THEN("Its name and metadata are unchanged")
      {
        REQUIRE(kept->name() == "my renderer");
        REQUIRE(kept->getMetadataValue("note").getAs<int>() == 7);
      }

      THEN("The clone and the subtypes helide does not offer are removed")
      {
        REQUIRE_FALSE(scene.getObject(ANARI_RENDERER, 0));
        REQUIRE_FALSE(scene.getObject(ANARI_RENDERER, 3));
        REQUIRE_FALSE(scene.getObject(ANARI_RENDERER, 4));
      }

      THEN("The other device's renderer is untouched")
      {
        REQUIRE(rendererIndices(scene, "other") == std::vector<size_t>{1});
        auto other = scene.getObject<vsr::scene::Renderer>(1);
        REQUIRE(other->parameterValueAs<float>("madeUp") == 1.f);
      }

      THEN("No use count goes below zero")
      {
        REQUIRE_FALSE(log.sawMessageContaining("decUseCount"));
      }
    }

    WHEN("Reloading while holding the kept renderer")
    {
      auto held = scene.getObject<vsr::scene::Renderer>(2);
      const auto heldUses = held->useCount(vsr::scene::Object::UseKind::APP);
      scene.reloadStandardRenderers("helide", d);

      THEN("The held reference still names it, with its use count intact")
      {
        REQUIRE(held);
        REQUIRE(held->index() == 2);
        REQUIRE(held->useCount(vsr::scene::Object::UseKind::APP) == heldUses);
        REQUIRE_FALSE(log.sawMessageContaining("decUseCount"));
      }
    }
  }

  GIVEN("Two renderers of the offered subtype, a lower-index one first")
  {
    scene.createStandardRenderers("helide", d); // 0
    scene.createRenderer("helide", "retired"); // 1
    scene.createRenderer("helide", "default"); // 2: a clone of 0

    WHEN("The device's renderers are reloaded")
    {
      scene.reloadStandardRenderers("helide", d);

      THEN("Only the lowest-index one survives, where it was")
      {
        REQUIRE(rendererIndices(scene, "helide") == std::vector<size_t>{0});
        REQUIRE(
            scene.renderersOfDevice("helide").front()->subtype() == "default");
      }
    }
  }

  GIVEN("A device whose only renderer is of a subtype it does not offer")
  {
    scene.createRenderer("helide", "retired"); // 0

    WHEN("The device's renderers are reloaded")
    {
      scene.reloadStandardRenderers("helide", d);

      THEN("The offered subtype is created before the old one is removed")
      {
        // Created first, the new renderer could not reuse slot 0, so a stale
        // index stays detectably dead.
        REQUIRE(rendererIndices(scene, "helide") == std::vector<size_t>{1});
        REQUIRE(
            scene.getObject<vsr::scene::Renderer>(1)->subtype() == "default");
        REQUIRE_FALSE(scene.getObject(ANARI_RENDERER, 0));
      }
    }
  }

  GIVEN("A device without renderers")
  {
    WHEN("The device's renderers are reloaded")
    {
      scene.reloadStandardRenderers("helide", d);

      THEN("Its standard renderers are created")
      {
        REQUIRE(rendererIndices(scene, "helide") == std::vector<size_t>{0});
      }
    }
  }
}
