// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "SceneMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr
#include "vsr/network/Message.hpp"
#include "vsr/network/messages/NewObject.hpp"
#include "vsr/network/messages/RemoveObject.hpp"
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <vector>

using namespace vsr::scivis_studio::protocol;
namespace messages = vsr::network::messages;

namespace {

constexpr const char *LAYER_NAME = "extra";

// A scene with one extra layer holding one geometry, plus an array with data
// so the descriptor-only transfer has something to leave behind.
void populate(vsr::scene::Scene &scene)
{
  auto *layer = scene.addLayer(LAYER_NAME);
  auto geometry = scene.createObject<vsr::scene::Geometry>("sphere");
  geometry->setName("transferred geometry");
  scene.insertChildObjectNode(layer->root(), geometry);
  auto array = scene.createArray(ANARI_FLOAT32, 4);
  array->setData(std::vector<float>{1.f, 2.f, 3.f, 4.f});
}

} // namespace

SCENARIO("Scene messages re-tag vsr::network messages", "[StudioProtocol]")
{
  GIVEN("a populated scene")
  {
    vsr::scene::Scene source;
    populate(source);

    WHEN("TransferScene is encoded with the Studio tag")
    {
      messages::TransferScene sender(&source, false);
      const auto msg =
          encodeSceneMessage(sender, StudioMessageType::TransferScene);

      THEN("the message carries the Studio type")
      {
        REQUIRE(messageType(msg) == StudioMessageType::TransferScene);
        REQUIRE(msg.header.payload_length == msg.payload.size());
        REQUIRE(msg.header.payload_length > 0);
      }

      THEN("a receiver rebuilds the layer and objects without array bytes")
      {
        vsr::scene::Scene target;
        messages::TransferScene receiver(msg, &target);
        receiver.execute();

        REQUIRE(target.layer(LAYER_NAME) != nullptr);
        REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 1);
        auto geometry = target.getObject<vsr::scene::Geometry>(0);
        REQUIRE(geometry);
        REQUIRE(geometry->name() == "transferred geometry");
        auto child = target.layer(LAYER_NAME)->root()->next();
        REQUIRE(child);
        REQUIRE((*child)->getObject() == geometry.data());

        REQUIRE(target.numberOfObjects(ANARI_ARRAY) == 1);
        auto array = target.getObject<vsr::scene::Array>(0);
        REQUIRE(array);
        REQUIRE(array->isProxy());
        REQUIRE(array->size() == 4);
      }
    }

    WHEN("TransferLayer is encoded with the Studio tag")
    {
      messages::TransferLayer sender(&source, source.layer(LAYER_NAME));
      const auto msg =
          encodeSceneMessage(sender, StudioMessageType::TransferLayer);
      REQUIRE(messageType(msg) == StudioMessageType::TransferLayer);

      THEN("a receiver holding the same objects rebuilds the layer")
      {
        // Layers reference objects by index; the receiver already has them.
        vsr::scene::Scene target;
        auto geometry = target.createObject<vsr::scene::Geometry>("sphere");
        REQUIRE(target.layer(LAYER_NAME) == nullptr);

        messages::TransferLayer receiver(msg, &target);
        receiver.execute();

        auto *layer = target.layer(LAYER_NAME);
        REQUIRE(layer != nullptr);
        auto child = layer->root()->next();
        REQUIRE(child);
        REQUIRE((*child)->getObject() == geometry.data());
      }
    }

    WHEN("ObjectAdded and ObjectRemoved are encoded with the Studio tags")
    {
      auto geometry = source.getObject<vsr::scene::Geometry>(0);
      messages::NewObject added(geometry.data());
      messages::RemoveObject removed(geometry.data());

      THEN("each carries its Studio type")
      {
        REQUIRE(messageType(
                    encodeSceneMessage(added, StudioMessageType::ObjectAdded))
            == StudioMessageType::ObjectAdded);
        REQUIRE(messageType(encodeSceneMessage(
                    removed, StudioMessageType::ObjectRemoved))
            == StudioMessageType::ObjectRemoved);
      }
    }

    WHEN("a non-scene type is requested")
    {
      messages::TransferScene sender(&source, false);
      const auto msg = encodeSceneMessage(sender, StudioMessageType::Hello);

      THEN("the message is invalid and empty")
      {
        REQUIRE(msg.header.type == vsr::network::MESSAGE_TYPE_INVALID);
        REQUIRE(msg.header.payload_length == 0);
        REQUIRE(msg.payload.empty());
        REQUIRE_FALSE(messageType(msg));
      }
    }
  }

  GIVEN("the scene message predicate")
  {
    THEN("exactly the four scene values qualify")
    {
      REQUIRE(isSceneMessageType(StudioMessageType::TransferScene));
      REQUIRE(isSceneMessageType(StudioMessageType::TransferLayer));
      REQUIRE(isSceneMessageType(StudioMessageType::ObjectAdded));
      REQUIRE(isSceneMessageType(StudioMessageType::ObjectRemoved));
      REQUIRE_FALSE(isSceneMessageType(StudioMessageType::ProjectSnapshot));
      REQUIRE_FALSE(isSceneMessageType(StudioMessageType::SetObjectParameter));
    }
  }
}
