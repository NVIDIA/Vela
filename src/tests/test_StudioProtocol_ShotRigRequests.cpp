// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
#include "ShotRigRequests.h"
#include "StudioCodec.h"
// std
#include <string>

using namespace vsr::scivis_studio::protocol;
using vsr::scivis_studio::SceneNodeRef;
using vsr::scivis_studio::SceneObjectRef;
using vsr::scivis_studio::Shot;

namespace {

// Encodes, decodes and hands back the payload; fails the test on a miss.
template <typename T>
T roundTrip(const T &in)
{
  const auto msg = encode(in);
  REQUIRE(msg.header.type == uint8_t(T::MESSAGE_TYPE));
  const auto out = decode<T>(msg);
  REQUIRE(out);
  return *out;
}

// Result payloads never travel alone, so they round-trip through raw bytes.
template <typename T>
T roundTripResult(const T &in)
{
  vsr::core::DataTree tree;
  toNode(in, tree.root());
  vsr::network::MessagePayload bytes;
  tree.write(bytes);
  vsr::core::DataTree copy;
  REQUIRE(copy.read(bytes));
  T out;
  REQUIRE(fromNode(copy.root(), out));
  return out;
}

// A request with only a requestId and no other child must be rejected.
template <typename T>
void requireRejectsRequestIdOnly()
{
  vsr::core::DataTree tree;
  writeChild(tree.root(), "requestId", uint64_t(1));
  T out;
  REQUIRE_FALSE(fromNode(tree.root(), out));
}

Shot makeShot()
{
  Shot shot;
  shot.id = "shot-2";
  shot.name = "Fly-through";
  shot.frameCount = 240;
  shot.fps = 30.f;
  shot.currentFrame = 17;
  shot.playing = true;
  shot.loop = false;
  shot.datasetBindings.push_back({"dataset-1", true});
  shot.datasetBindings.push_back({"dataset-4", false});
  shot.lightRigId = "lightrig-1";
  shot.cameraRigId = "camerarig-3";
  shot.camera.type = ANARI_CAMERA;
  shot.camera.objectIndex = 6;
  shot.renderSettings.width = 1920;
  shot.renderSettings.height = 1080;
  shot.renderSettings.samples = 512;
  shot.renderSettings.rendererLibrary = "visrtx";
  shot.renderSettings.rendererObjectIndex = 2;
  shot.renderSettings.rendererSubtype = "scivis";
  shot.renderSettings.outputFilePrefix = "out/frame_";
  return shot;
}

} // namespace

SCENARIO("Shot requests", "[StudioProtocol]")
{
  GIVEN("CreateShot / RemoveShot / SetActiveShot")
  {
    THEN("each round-trips")
    {
      CreateShot create;
      create.requestId = 11;
      create.name = "Intro";
      const auto c = roundTrip(create);
      REQUIRE(c.requestId == 11);
      REQUIRE(c.name == "Intro");

      RemoveShot remove;
      remove.requestId = 12;
      remove.shotId = "shot-1";
      const auto r = roundTrip(remove);
      REQUIRE(r.requestId == 12);
      REQUIRE(r.shotId == "shot-1");

      SetActiveShot active;
      active.requestId = 13;
      active.shotId = "shot-9";
      const auto a = roundTrip(active);
      REQUIRE(a.requestId == 13);
      REQUIRE(a.shotId == "shot-9");
    }

    THEN("an empty name still round-trips")
    {
      CreateShot create;
      create.requestId = 1;
      REQUIRE(roundTrip(create).name.empty());
    }

    THEN("a missing shotId is rejected")
    {
      requireRejectsRequestIdOnly<RemoveShot>();
      requireRejectsRequestIdOnly<SetActiveShot>();
    }

    THEN("a missing requestId is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "shotId", std::string("shot-1"));
      RemoveShot out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a mistyped shotId is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root(), "shotId", 3);
      SetActiveShot out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }

  GIVEN("an UpdateShot with a fully populated Shot")
  {
    UpdateShot update;
    update.requestId = 21;
    update.shot = makeShot();

    THEN("every field round-trips")
    {
      const auto out = roundTrip(update);
      REQUIRE(out.requestId == 21);
      const Shot &s = out.shot;
      REQUIRE(s.id == "shot-2");
      REQUIRE(s.name == "Fly-through");
      REQUIRE(s.frameCount == 240);
      REQUIRE(s.fps == 30.f);
      REQUIRE(s.currentFrame == 17);
      REQUIRE(s.playing == true);
      REQUIRE(s.loop == false);
      REQUIRE(s.datasetBindings.size() == 2);
      const auto *b1 =
          vsr::scivis_studio::shot::findDatasetBinding(s, "dataset-1");
      const auto *b4 =
          vsr::scivis_studio::shot::findDatasetBinding(s, "dataset-4");
      REQUIRE(b1);
      REQUIRE(b1->enabled == true);
      REQUIRE(b4);
      REQUIRE(b4->enabled == false);
      REQUIRE(s.lightRigId == "lightrig-1");
      REQUIRE(s.cameraRigId == "camerarig-3");
      REQUIRE(s.camera.type == ANARI_CAMERA);
      REQUIRE(s.camera.objectIndex == 6);
      REQUIRE(s.renderSettings.width == 1920);
      REQUIRE(s.renderSettings.height == 1080);
      REQUIRE(s.renderSettings.samples == 512);
      REQUIRE(s.renderSettings.rendererLibrary == "visrtx");
      REQUIRE(s.renderSettings.rendererObjectIndex == 2);
      REQUIRE(s.renderSettings.rendererSubtype == "scivis");
      REQUIRE(s.renderSettings.outputFilePrefix == "out/frame_");
    }

    THEN("dataset bindings are keyed by datasetId, not appended anonymously")
    {
      vsr::core::DataTree tree;
      toNode(update.shot, tree.root());
      const auto *bindings = tree.root().child("datasetBindings");
      REQUIRE(bindings);
      REQUIRE(bindings->numChildren() == 2);
      REQUIRE(bindings->child("dataset-1"));
      REQUIRE(bindings->child("dataset-4"));
      REQUIRE_FALSE(
          bindings->child("dataset-4")->child("enabled")->getValueOr(true));
    }

    THEN("a default Shot with an id round-trips to its defaults")
    {
      Shot shot;
      shot.id = "shot-0";
      UpdateShot req;
      req.shot = shot;
      const auto out = roundTrip(req).shot;
      REQUIRE(out.id == "shot-0");
      REQUIRE(out.name.empty());
      REQUIRE(out.frameCount == 120);
      REQUIRE(out.fps == 24.f);
      REQUIRE(out.currentFrame == 0);
      REQUIRE_FALSE(out.playing);
      REQUIRE(out.loop);
      REQUIRE(out.datasetBindings.empty());
      REQUIRE(out.lightRigId.empty());
      REQUIRE(out.cameraRigId.empty());
      REQUIRE(out.camera.type == ANARI_UNKNOWN);
      REQUIRE(out.camera.objectIndex == VSR_INVALID_INDEX);
      REQUIRE(out.renderSettings.width == 1024);
      REQUIRE(out.renderSettings.height == 768);
      REQUIRE(out.renderSettings.samples == 128);
      REQUIRE(out.renderSettings.rendererLibrary.empty());
      REQUIRE(out.renderSettings.rendererObjectIndex == VSR_INVALID_INDEX);
      REQUIRE(out.renderSettings.rendererSubtype == "default");
      REQUIRE(out.renderSettings.outputFilePrefix.empty());
    }

    THEN("a Shot without an id is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root()["shot"], "name", std::string("nameless"));
      UpdateShot out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a missing shot subtree is rejected")
    {
      requireRejectsRequestIdOnly<UpdateShot>();
    }

    THEN("a mistyped optional field is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "id", std::string("shot-1"));
      writeChild(tree.root(), "fps", std::string("fast"));
      Shot out;
      REQUIRE_FALSE(fromNode(tree.root(), out));

      vsr::core::DataTree badCamera;
      writeChild(badCamera.root(), "id", std::string("shot-1"));
      writeChild(
          badCamera.root()["camera"], "type", std::string("ANARI_CAMERA"));
      REQUIRE_FALSE(fromNode(badCamera.root(), out));
    }
  }
}

SCENARIO("Light rig requests", "[StudioProtocol]")
{
  GIVEN("the light rig request set")
  {
    THEN("CreateLightRig round-trips")
    {
      CreateLightRig req;
      req.requestId = 31;
      req.name = "Studio";
      const auto out = roundTrip(req);
      REQUIRE(out.requestId == 31);
      REQUIRE(out.name == "Studio");
    }

    THEN("CloneLightRig / RemoveLightRig round-trip and need an id")
    {
      CloneLightRig clone;
      clone.requestId = 32;
      clone.lightRigId = "lightrig-1";
      const auto c = roundTrip(clone);
      REQUIRE(c.requestId == 32);
      REQUIRE(c.lightRigId == "lightrig-1");

      RemoveLightRig remove;
      remove.requestId = 33;
      remove.lightRigId = "lightrig-2";
      const auto r = roundTrip(remove);
      REQUIRE(r.requestId == 33);
      REQUIRE(r.lightRigId == "lightrig-2");

      requireRejectsRequestIdOnly<CloneLightRig>();
      requireRejectsRequestIdOnly<RemoveLightRig>();
    }

    THEN("RenameLightRig round-trips and needs both id and newName")
    {
      RenameLightRig rename;
      rename.requestId = 34;
      rename.lightRigId = "lightrig-1";
      rename.newName = "Key + fill";
      const auto out = roundTrip(rename);
      REQUIRE(out.requestId == 34);
      REQUIRE(out.lightRigId == "lightrig-1");
      REQUIRE(out.newName == "Key + fill");

      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root(), "lightRigId", std::string("lightrig-1"));
      RenameLightRig noName;
      REQUIRE_FALSE(fromNode(tree.root(), noName));
    }

    THEN("AddLightToRig round-trips and needs a subtype")
    {
      AddLightToRig add;
      add.requestId = 35;
      add.lightRigId = "lightrig-1";
      add.subtype = "directional";
      const auto out = roundTrip(add);
      REQUIRE(out.requestId == 35);
      REQUIRE(out.lightRigId == "lightrig-1");
      REQUIRE(out.subtype == "directional");

      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root(), "lightRigId", std::string("lightrig-1"));
      AddLightToRig noSubtype;
      REQUIRE_FALSE(fromNode(tree.root(), noSubtype));
    }

    THEN("RemoveLightFromRig round-trips its SceneNodeRef")
    {
      RemoveLightFromRig remove;
      remove.requestId = 36;
      remove.lightRigId = "lightrig-1";
      remove.lightNode.layerName = "lights";
      remove.lightNode.nodeIndex = 4;
      const auto out = roundTrip(remove);
      REQUIRE(out.requestId == 36);
      REQUIRE(out.lightRigId == "lightrig-1");
      REQUIRE(out.lightNode.layerName == "lights");
      REQUIRE(out.lightNode.nodeIndex == 4);

      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root(), "lightRigId", std::string("lightrig-1"));
      RemoveLightFromRig noNode;
      REQUIRE_FALSE(fromNode(tree.root(), noNode));
    }

    THEN("SaveLightRigArchive / LoadLightRigArchive carry a generic path")
    {
      SaveLightRigArchive save;
      save.requestId = 37;
      save.lightRigId = "lightrig-1";
      save.file = std::filesystem::path("rigs") / "studio.vsr";
      const auto s = roundTrip(save);
      REQUIRE(s.requestId == 37);
      REQUIRE(s.lightRigId == "lightrig-1");
      REQUIRE(s.file.generic_string() == "rigs/studio.vsr");

      LoadLightRigArchive load;
      load.requestId = 38;
      load.file = "/data/rigs/studio.vsr";
      const auto l = roundTrip(load);
      REQUIRE(l.requestId == 38);
      REQUIRE(l.file.generic_string() == "/data/rigs/studio.vsr");

      requireRejectsRequestIdOnly<SaveLightRigArchive>();
      requireRejectsRequestIdOnly<LoadLightRigArchive>();
    }
  }
}

SCENARIO("Camera rig requests", "[StudioProtocol]")
{
  GIVEN("the camera rig request set")
  {
    THEN("CreateCameraRig / RemoveCameraRig round-trip")
    {
      CreateCameraRig create;
      create.requestId = 41;
      create.name = "Orbit";
      const auto c = roundTrip(create);
      REQUIRE(c.requestId == 41);
      REQUIRE(c.name == "Orbit");

      RemoveCameraRig remove;
      remove.requestId = 42;
      remove.cameraRigId = "camerarig-1";
      const auto r = roundTrip(remove);
      REQUIRE(r.requestId == 42);
      REQUIRE(r.cameraRigId == "camerarig-1");

      requireRejectsRequestIdOnly<RemoveCameraRig>();
    }

    THEN("RenameCameraRig round-trips")
    {
      RenameCameraRig rename;
      rename.requestId = 43;
      rename.cameraRigId = "camerarig-1";
      rename.newName = "Dolly";
      const auto out = roundTrip(rename);
      REQUIRE(out.requestId == 43);
      REQUIRE(out.cameraRigId == "camerarig-1");
      REQUIRE(out.newName == "Dolly");
      requireRejectsRequestIdOnly<RenameCameraRig>();
    }

    THEN("SaveCameraRigArchive / LoadCameraRigArchive round-trip")
    {
      SaveCameraRigArchive save;
      save.requestId = 44;
      save.cameraRigId = "camerarig-1";
      save.file = "cameras/orbit.vsr";
      const auto s = roundTrip(save);
      REQUIRE(s.requestId == 44);
      REQUIRE(s.cameraRigId == "camerarig-1");
      REQUIRE(s.file.generic_string() == "cameras/orbit.vsr");

      LoadCameraRigArchive load;
      load.requestId = 45;
      load.file = "cameras/orbit.vsr";
      const auto l = roundTrip(load);
      REQUIRE(l.requestId == 45);
      REQUIRE(l.file.generic_string() == "cameras/orbit.vsr");

      requireRejectsRequestIdOnly<SaveCameraRigArchive>();
      requireRejectsRequestIdOnly<LoadCameraRigArchive>();
    }
  }
}

SCENARIO("Color map requests", "[StudioProtocol]")
{
  GIVEN("the color map request set")
  {
    THEN("CreateColorMap / RenameColorMap / RemoveColorMap round-trip")
    {
      CreateColorMap create;
      create.requestId = 51;
      create.name = "Viridis";
      const auto c = roundTrip(create);
      REQUIRE(c.requestId == 51);
      REQUIRE(c.name == "Viridis");

      RenameColorMap rename;
      rename.requestId = 52;
      rename.colorMapId = "colormap-1";
      rename.newName = "Inferno";
      const auto n = roundTrip(rename);
      REQUIRE(n.requestId == 52);
      REQUIRE(n.colorMapId == "colormap-1");
      REQUIRE(n.newName == "Inferno");

      RemoveColorMap remove;
      remove.requestId = 53;
      remove.colorMapId = "colormap-2";
      const auto r = roundTrip(remove);
      REQUIRE(r.requestId == 53);
      REQUIRE(r.colorMapId == "colormap-2");

      requireRejectsRequestIdOnly<RenameColorMap>();
      requireRejectsRequestIdOnly<RemoveColorMap>();
    }
  }
}

SCENARIO("Shot, rig and color map results", "[StudioProtocol]")
{
  GIVEN("the id-only results")
  {
    THEN("each round-trips and rejects an empty node")
    {
      ShotCreatedResult shot;
      shot.shotId = "shot-3";
      REQUIRE(roundTripResult(shot).shotId == "shot-3");

      LightRigCreatedResult light;
      light.lightRigId = "lightrig-3";
      REQUIRE(roundTripResult(light).lightRigId == "lightrig-3");

      CameraRigCreatedResult camera;
      camera.cameraRigId = "camerarig-3";
      REQUIRE(roundTripResult(camera).cameraRigId == "camerarig-3");

      vsr::core::DataTree empty;
      REQUIRE_FALSE(fromNode(empty.root(), shot));
      REQUIRE_FALSE(fromNode(empty.root(), light));
      REQUIRE_FALSE(fromNode(empty.root(), camera));
    }
  }

  GIVEN("LightAddedResult")
  {
    LightAddedResult result;
    result.lightNode.layerName = "lights";
    result.lightNode.nodeIndex = 9;

    THEN("it round-trips its SceneNodeRef")
    {
      const auto out = roundTripResult(result);
      REQUIRE(out.lightNode.layerName == "lights");
      REQUIRE(out.lightNode.nodeIndex == 9);
    }

    THEN("a missing lightNode is rejected")
    {
      vsr::core::DataTree empty;
      REQUIRE_FALSE(fromNode(empty.root(), result));
    }
  }

  GIVEN("ColorMapCreatedResult")
  {
    ColorMapCreatedResult result;
    result.colorMapId = "colormap-5";
    result.object.type = ANARI_ARRAY1D;
    result.object.objectIndex = 14;

    THEN("both halves round-trip")
    {
      const auto out = roundTripResult(result);
      REQUIRE(out.colorMapId == "colormap-5");
      REQUIRE(out.object.type == ANARI_ARRAY1D);
      REQUIRE(out.object.objectIndex == 14);
    }

    THEN("either half missing is rejected")
    {
      vsr::core::DataTree noObject;
      writeChild(noObject.root(), "colorMapId", std::string("colormap-5"));
      REQUIRE_FALSE(fromNode(noObject.root(), result));

      vsr::core::DataTree noId;
      writeChildNode(noId.root(), "object", result.object);
      REQUIRE_FALSE(fromNode(noId.root(), result));
    }
  }
}
