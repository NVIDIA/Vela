// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/app/ApplicationDump.h"
#include "vsr/app/Context.h"
#include "vsr/app/LegacyApplicationContext.h"
#include "vsr/io/archives/SceneArchive.hpp"
// std
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

SCENARIO("Application Dumps embed required Archives without owning the root",
    "[App]")
{
  vsr::app::Context context;
  context.vsr.scene.addLayer("archived");

  vsr::core::DataTree tree;
  auto &root = tree.root();
  root["viewerState"]["theme"] = "dark";

  REQUIRE(vsr::app::serialize_ApplicationDump(context, root));

  auto *archives = root.child("archives");
  REQUIRE(archives != nullptr);
  REQUIRE(archives->child("scene") != nullptr);
  auto *animationManager = archives->child("animationManager");
  REQUIRE(animationManager != nullptr);
  REQUIRE(animationManager->child("objects") != nullptr);
  REQUIRE(animationManager->child("objects")->numChildren() == 0);
  REQUIRE(archives->child("scene")->child("animations") == nullptr);
  REQUIRE(root["viewerState"]["theme"].getValueAs<std::string>() == "dark");

  vsr::app::Context restored;
  REQUIRE(vsr::app::deserialize_ApplicationDump(restored, root));
  REQUIRE(root["viewerState"]["theme"].getValueAs<std::string>() == "dark");
}

SCENARIO("Application Dumps restore Scene before dependent animations", "[App]")
{
  vsr::app::Context source;
  auto archivedGeometry =
      source.vsr.scene.createObject<vsr::scene::Geometry>("sphere");
  archivedGeometry->setName("archived geometry");
  const float times[] = {0.f, 1.f};
  const float values[] = {1.f, 2.f};
  source.vsr.animationMgr.addAnimation("archived animation")
      .addObjectParameterBinding(
          archivedGeometry.data(), "radius", ANARI_FLOAT32, values, times, 2);

  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, tree.root()));

  vsr::app::Context target;
  auto staleGeometry =
      target.vsr.scene.createObject<vsr::scene::Geometry>("cone");
  target.vsr.animationMgr.addAnimation("stale animation")
      .addObjectParameterBinding(
          staleGeometry.data(), "radius", ANARI_FLOAT32, values, times, 2);
  target.vsr.animationMgr.play();

  REQUIRE(vsr::app::deserialize_ApplicationDump(target, tree.root()));
  REQUIRE(target.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == 1);
  auto restoredGeometry = target.vsr.scene.getObject<vsr::scene::Geometry>(0);
  REQUIRE(restoredGeometry);
  REQUIRE(restoredGeometry->name() == "archived geometry");
  REQUIRE(target.vsr.animationMgr.animations().size() == 1);
  auto &restoredAnimation = target.vsr.animationMgr.animations().front();
  REQUIRE(restoredAnimation.name() == "archived animation");
  REQUIRE(restoredAnimation.objectParameterBindings().front().target()
      == restoredGeometry.data());
  REQUIRE_FALSE(target.vsr.animationMgr.isPlaying());
}

SCENARIO("Application Dumps remap animations to dense Scene Archive indices",
    "[App]")
{
  vsr::app::Context source;
  auto removed = source.vsr.scene.createObject<vsr::scene::Geometry>("sphere");
  auto retained =
      source.vsr.scene.createObject<vsr::scene::Geometry>("cylinder");
  retained->setName("retained geometry");
  source.vsr.scene.removeObject(removed.data());
  REQUIRE(retained.index() == 1);

  auto removedTransform = source.vsr.scene.insertChildTransformNode(
      source.vsr.scene.defaultLayer()->root(),
      vsr::math::IDENTITY_MAT4,
      "removed transform");
  auto retainedTransform = source.vsr.scene.insertChildTransformNode(
      source.vsr.scene.defaultLayer()->root(),
      vsr::math::IDENTITY_MAT4,
      "retained transform");
  source.vsr.scene.removeNode(removedTransform);
  REQUIRE(retainedTransform.index() == 2);

  const float times[] = {0.f, 1.f};
  const float values[] = {1.f, 2.f};
  auto &animation = source.vsr.animationMgr.addAnimation("sparse animation");
  animation.addObjectParameterBinding(
      retained.data(), "radius", ANARI_FLOAT32, values, times, 2);
  animation.addTransformBinding(retainedTransform);

  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, tree.root()));

  vsr::app::Context target;
  REQUIRE(vsr::app::deserialize_ApplicationDump(target, tree.root()));
  auto restored = target.vsr.scene.getObject<vsr::scene::Geometry>(0);
  REQUIRE(restored);
  REQUIRE(restored->name() == "retained geometry");
  REQUIRE(target.vsr.animationMgr.animations().size() == 1);
  auto &restoredAnimation = target.vsr.animationMgr.animations().front();
  REQUIRE(restoredAnimation.objectParameterBindings().front().target()
      == restored.data());
  auto restoredTransform = target.vsr.scene.defaultLayer()->at(1);
  REQUIRE(restoredTransform);
  REQUIRE((*restoredTransform)->name() == "retained transform");
  REQUIRE(restoredAnimation.transformBindings().front().target()
      == restoredTransform);
}

SCENARIO("Application Dumps round-trip only stable Context settings", "[App]")
{
  vsr::app::Context source;
  source.anari.setRenderIndexKind(vsr::app::RenderIndexKind::FLAT);
  source.offline.frame.width = 1920;
  source.offline.frame.height = 1080;
  source.offline.frame.colorFormat = ANARI_FLOAT32_VEC4;
  source.offline.frame.samples = 17;
  source.offline.frame.numFrames = 42;
  source.offline.frame.renderSubset = true;
  source.offline.frame.startFrame = 3;
  source.offline.frame.endFrame = 33;
  source.offline.frame.frameIncrement = 3;
  source.offline.camera.apertureRadius = 0.25f;
  source.offline.camera.focusDistance = 12.f;
  source.offline.camera.cameraIndex = 4;
  source.offline.renderer.activeRenderer = 2;
  source.offline.renderer.libraryName = "archived_library";
  auto &renderer = source.offline.renderer.rendererObjects.emplace_back(
      ANARI_RENDERER, "pathtracer");
  renderer.setName("archived renderer");
  renderer.setParameter("pixelSamples", 8);
  source.offline.output.outputDirectory = "/archived/output";
  source.offline.output.filePrefix = "beauty_";
  source.offline.aov.aovType = vsr::rendering::AOVType::DEPTH;
  source.offline.aov.depthMin = 2.f;
  source.offline.aov.depthMax = 20.f;
  source.offline.aov.edgeInvert = true;
  source.setLogVerbose(true);
  source.setLogEchoOutput(true);
  vsr::app::CameraPose pose;
  pose.name = "archived pose";
  pose.lookat = {1.f, 2.f, 3.f};
  pose.azeldist = {4.f, 5.f, 6.f};
  pose.fixedDist = 7.f;
  pose.upAxis = 2;
  pose.mode = 1;
  source.view.poses.push_back(pose);
  source.commandLine.stateFile = "source-only.vsr";
  source.vsr.sceneLoadComplete = true;

  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, tree.root()));
  REQUIRE(tree.root().child("commandLine") == nullptr);
  REQUIRE(tree.root().child("selectedNodes") == nullptr);
  REQUIRE(tree.root().child("sceneLoadComplete") == nullptr);
  REQUIRE(tree.root().child("context") == nullptr);

  vsr::app::Context target;
  target.commandLine.stateFile = "keep-me.vsr";
  target.vsr.sceneLoadComplete = false;
  REQUIRE(vsr::app::deserialize_ApplicationDump(target, tree.root()));

  REQUIRE(target.anari.renderIndexKind() == vsr::app::RenderIndexKind::FLAT);
  REQUIRE(target.offline.frame.width == 1920);
  REQUIRE(target.offline.frame.height == 1080);
  REQUIRE(target.offline.frame.colorFormat == ANARI_FLOAT32_VEC4);
  REQUIRE(target.offline.frame.samples == 17);
  REQUIRE(target.offline.frame.numFrames == 42);
  REQUIRE(target.offline.frame.renderSubset);
  REQUIRE(target.offline.frame.startFrame == 3);
  REQUIRE(target.offline.frame.endFrame == 33);
  REQUIRE(target.offline.frame.frameIncrement == 3);
  REQUIRE(target.offline.camera.apertureRadius == Approx(0.25f));
  REQUIRE(target.offline.camera.focusDistance == Approx(12.f));
  REQUIRE(target.offline.camera.cameraIndex == 4);
  REQUIRE(target.offline.renderer.activeRenderer == 2);
  REQUIRE(target.offline.renderer.libraryName == "archived_library");
  REQUIRE(target.offline.renderer.rendererObjects.size() == 1);
  REQUIRE(target.offline.renderer.rendererObjects.front().name()
      == "archived renderer");
  REQUIRE(target.offline.renderer.rendererObjects.front().subtype().str()
      == "pathtracer");
  REQUIRE(target.offline.renderer.rendererObjects.front().parameterValueAs<int>(
              "pixelSamples")
      == 8);
  REQUIRE(target.offline.output.outputDirectory == "/archived/output");
  REQUIRE(target.offline.output.filePrefix == "beauty_");
  REQUIRE(target.offline.aov.aovType == vsr::rendering::AOVType::DEPTH);
  REQUIRE(target.offline.aov.depthMin == Approx(2.f));
  REQUIRE(target.offline.aov.depthMax == Approx(20.f));
  REQUIRE(target.offline.aov.edgeInvert);
  REQUIRE(target.logVerbose());
  REQUIRE(target.logEchoOutput());
  REQUIRE(target.view.poses.size() == 1);
  REQUIRE(target.view.poses.front().name == "archived pose");
  REQUIRE(target.view.poses.front().lookat == vsr::math::float3(1.f, 2.f, 3.f));
  REQUIRE(
      target.view.poses.front().azeldist == vsr::math::float3(4.f, 5.f, 6.f));
  REQUIRE(target.view.poses.front().fixedDist == Approx(7.f));
  REQUIRE(target.view.poses.front().upAxis == 2);
  REQUIRE(target.view.poses.front().mode == 1);

  REQUIRE(target.commandLine.stateFile == "keep-me.vsr");
  REQUIRE_FALSE(target.vsr.sceneLoadComplete);
}

SCENARIO(
    "Application Dumps read legacy context payloads with animations", "[App]")
{
  vsr::app::Context source;
  auto geometry = source.vsr.scene.createObject<vsr::scene::Geometry>("sphere");
  geometry->setName("legacy geometry");
  const float times[] = {0.f, 1.f};
  const float values[] = {1.f, 2.f};
  source.vsr.animationMgr.addAnimation("legacy animation")
      .addObjectParameterBinding(
          geometry.data(), "radius", ANARI_FLOAT32, values, times, 2);

  vsr::core::DataTree tree;
  vsr::app::detail::serializeLegacyApplicationContext(
      source, tree.root()["context"]);
  REQUIRE(tree.root()["context"]["animations"]["objects"].numChildren() == 1);

  vsr::app::Context target;
  REQUIRE(vsr::app::deserialize_ApplicationDump(target, tree.root()));
  REQUIRE(target.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == 1);
  REQUIRE(target.vsr.scene.getObject<vsr::scene::Geometry>(0)->name()
      == "legacy geometry");
  REQUIRE(target.vsr.animationMgr.animations().size() == 1);
  auto &animation = target.vsr.animationMgr.animations().front();
  REQUIRE(animation.name() == "legacy animation");
  REQUIRE(animation.objectParameterBindings().front().target()
      == target.vsr.scene.getObject<vsr::scene::Geometry>(0).data());
}

SCENARIO(
    "Application Dumps validate both Archives before replacing state", "[App]")
{
  vsr::app::Context source;
  source.vsr.scene.addLayer("archived");
  vsr::core::DataTree validTree;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, validTree.root()));

  for (const char *missingArchive : {"scene", "animationManager"}) {
    DYNAMIC_SECTION("missing " << missingArchive)
    {
      vsr::core::DataTree tree;
      tree.root() = validTree.root();
      tree.root()["archives"].remove(missingArchive);

      vsr::app::Context target;
      target.vsr.scene.addLayer("preserved");
      target.vsr.animationMgr.addAnimation("preserved animation");
      target.vsr.animationMgr.play();

      REQUIRE_FALSE(vsr::app::deserialize_ApplicationDump(target, tree.root()));
      REQUIRE(target.vsr.scene.layer("preserved") != nullptr);
      REQUIRE(target.vsr.animationMgr.animations().size() == 1);
      REQUIRE(target.vsr.animationMgr.animations().front().name()
          == "preserved animation");
      REQUIRE(target.vsr.animationMgr.isPlaying());
    }
  }
}

SCENARIO(
    "Malformed Animation Manager Archives do not replace Application "
    "Dump state",
    "[App]")
{
  vsr::app::Context source;
  source.vsr.scene.addLayer("archived");
  source.anari.setRenderIndexKind(vsr::app::RenderIndexKind::FLAT);
  source.offline.frame.width = 1920;
  source.setLogVerbose(true);
  source.view.poses.push_back({});
  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, tree.root()));
  tree.root()["archives"]["animationManager"].remove("fps");

  vsr::app::Context target;
  target.vsr.scene.addLayer("preserved");
  target.vsr.animationMgr.addAnimation("preserved animation");
  target.vsr.animationMgr.setAnimationTime(0.25f);
  target.vsr.animationMgr.setAnimationIncrement(0.125f);
  target.vsr.animationMgr.setAnimationTotalFrames(41);
  target.vsr.animationMgr.setAnimationFPS(24.f);
  target.vsr.animationMgr.setLoop(false);
  target.vsr.animationMgr.play();
  target.anari.setRenderIndexKind(vsr::app::RenderIndexKind::ALL_LAYERS);
  target.offline.frame.width = 640;
  target.setLogVerbose(false);
  vsr::app::CameraPose preservedPose;
  preservedPose.name = "preserved pose";
  target.view.poses.push_back(preservedPose);

  REQUIRE_FALSE(vsr::app::deserialize_ApplicationDump(target, tree.root()));

  REQUIRE(target.vsr.scene.layer("preserved") != nullptr);
  REQUIRE(target.vsr.scene.layer("archived") == nullptr);
  REQUIRE(target.vsr.animationMgr.animations().size() == 1);
  REQUIRE(target.vsr.animationMgr.animations().front().name()
      == "preserved animation");
  REQUIRE(target.vsr.animationMgr.getAnimationTime() == Approx(0.25f));
  REQUIRE(target.vsr.animationMgr.getAnimationIncrement() == Approx(0.125f));
  REQUIRE(target.vsr.animationMgr.getAnimationTotalFrames() == 41);
  REQUIRE(target.vsr.animationMgr.getAnimationFPS() == Approx(24.f));
  REQUIRE_FALSE(target.vsr.animationMgr.isLoop());
  REQUIRE(target.vsr.animationMgr.isPlaying());
  REQUIRE(
      target.anari.renderIndexKind() == vsr::app::RenderIndexKind::ALL_LAYERS);
  REQUIRE(target.offline.frame.width == 640);
  REQUIRE_FALSE(target.logVerbose());
  REQUIRE(target.view.poses.size() == 1);
  REQUIRE(target.view.poses.front().name == "preserved pose");
}

SCENARIO("An empty Animation Manager Archive clears stale animations", "[App]")
{
  vsr::app::Context source;
  vsr::core::DataTree tree;
  REQUIRE(vsr::app::serialize_ApplicationDump(source, tree.root()));

  vsr::app::Context target;
  target.vsr.animationMgr.addAnimation("stale animation");
  target.vsr.animationMgr.play();
  REQUIRE(vsr::app::deserialize_ApplicationDump(target, tree.root()));
  REQUIRE(target.vsr.animationMgr.animations().empty());
  REQUIRE_FALSE(target.vsr.animationMgr.isPlaying());
}

SCENARIO("The VSR CLI records replacement and additive Scene inputs", "[App]")
{
  vsr::app::Context context;
  std::vector<std::string> args{
      "vsrViewer", "-vsr", "scene.vsr", "-obj", "mesh.obj"};

  context.parseCommandLine(args);

  REQUIRE(context.commandLine.sceneInputs.size() == 2);
  const auto &archive =
      std::get<vsr::app::SceneArchiveLoad>(context.commandLine.sceneInputs[0]);
  REQUIRE(archive.filename == "scene.vsr");
  const auto &foreignImport = std::get<vsr::app::ForeignSceneImport>(
      context.commandLine.sceneInputs[1]);
  REQUIRE(foreignImport.file.first == vsr::io::ImporterType::OBJ);
  REQUIRE(foreignImport.file.second == "mesh.obj;default");
}

SCENARIO("The VSR CLI rejects multiple replacement Scene Archives", "[App]")
{
  vsr::app::Context context;
  std::vector<std::string> args{
      "vsrViewer", "-vsr", "first.vsr", "-vsr", "second.vsr"};

  REQUIRE_THROWS_WITH(context.parseCommandLine(args),
      "Only one Scene Archive may be specified");
}

SCENARIO("The VSR CLI rejects conflicting native state inputs", "[App]")
{
  vsr::app::Context stateFirstContext;
  std::vector<std::string> stateFirstArgs{
      "vsrViewer", "viewer_state.vsr", "-vsr", "scene.vsr"};

  REQUIRE_THROWS_WITH(stateFirstContext.parseCommandLine(stateFirstArgs),
      "A Scene Archive cannot be combined with an application state file");

  vsr::app::Context archiveFirstContext;
  std::vector<std::string> archiveFirstArgs{
      "vsrViewer", "-vsr", "scene.vsr", "viewer_state.vsr"};

  REQUIRE_THROWS_WITH(archiveFirstContext.parseCommandLine(archiveFirstArgs),
      "A Scene Archive cannot be combined with an application state file");
}

SCENARIO(
    "The VSR CLI preserves animation grouping and layer selection", "[App]")
{
  vsr::app::Context context;
  std::vector<std::string> args{"vsrViewer",
      "--layer",
      "animated",
      "-pointsbin",
      "frame0.bin",
      "frame1.bin",
      "--layer",
      "mesh",
      "-obj",
      "mesh.obj"};

  context.parseCommandLine(args);

  REQUIRE(context.commandLine.animationFilenames.size() == 1);
  REQUIRE(context.commandLine.animationFilenames.front().first
      == vsr::io::ImporterType::POINTSBIN_MULTIFILE);
  REQUIRE(context.commandLine.animationFilenames.front().second
      == std::vector<std::string>{"frame0.bin", "frame1.bin"});
  REQUIRE(context.commandLine.animationLayerNames.size() == 1);
  REQUIRE(context.commandLine.animationLayerNames.front().str() == "animated");
  REQUIRE(context.commandLine.sceneInputs.size() == 1);
  const auto &foreignImport = std::get<vsr::app::ForeignSceneImport>(
      context.commandLine.sceneInputs.front());
  REQUIRE(foreignImport.file.first == vsr::io::ImporterType::OBJ);
  REQUIRE(foreignImport.file.second == "mesh.obj;mesh");
}

SCENARIO("Scene Archive CLI inputs are loaded by VSR App", "[App]")
{
  const auto archive =
      std::filesystem::temp_directory_path() / "vsr_app_scene_archive.vsr";
  std::filesystem::remove(archive);

  vsr::scene::Scene source;
  source.addLayer("archived");
  REQUIRE(vsr::io::save_SceneArchive(source, archive.string().c_str()));

  vsr::app::Context context;
  std::vector<std::string> args{"vsrViewer", "-vsr", archive.string()};
  context.parseCommandLine(args);
  context.setupSceneFromCommandLine();

  REQUIRE(context.vsr.scene.layer("archived") != nullptr);
  std::filesystem::remove(archive);
}

SCENARIO("Scene Archive CLI load failures are reported by VSR App", "[App]")
{
  const auto archive = std::filesystem::temp_directory_path()
      / "vsr_app_missing_scene_archive.vsr";
  std::filesystem::remove(archive);

  vsr::app::Context context;
  std::vector<std::string> args{"vsrViewer", "-vsr", archive.string()};
  context.parseCommandLine(args);

  REQUIRE_FALSE(context.loadCommandLineSceneInputs());
}

SCENARIO("Scene Archive replacement precedes additive CLI imports", "[App]")
{
  const auto temp = std::filesystem::temp_directory_path();
  const auto archive = temp / "vsr_app_mixed_scene_inputs.vsr";
  const auto obj = temp / "vsr_app_mixed_scene_inputs.obj";
  std::filesystem::remove(archive);
  std::filesystem::remove(obj);

  vsr::scene::Scene source;
  source.addLayer("archived");
  REQUIRE(vsr::io::save_SceneArchive(source, archive.string().c_str()));
  {
    std::ofstream file(obj);
    file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context context;
  context.vsr.scene.addLayer("stale");
  std::vector<std::string> args{
      "vsrViewer", "-obj", obj.string(), "-vsr", archive.string()};
  context.parseCommandLine(args);
  REQUIRE(context.loadCommandLineSceneInputs());

  REQUIRE(context.vsr.scene.layer("stale") == nullptr);
  REQUIRE(context.vsr.scene.layer("archived") != nullptr);
  REQUIRE(context.vsr.scene.objectDB().geometry.size() == 1);

  std::filesystem::remove(archive);
  std::filesystem::remove(obj);
}
