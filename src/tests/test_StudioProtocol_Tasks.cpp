// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
#include "ProjectOpReply.h"
#include "ProjectSnapshot.h"
#include "StudioCodec.h"
#include "TaskMessages.h"
// vsr_scivis_studio_model
#include "ProjectSerialization.h"
// std
#include <string>

using namespace vsr::scivis_studio::protocol;
using namespace vsr::scivis_studio;

namespace {

// A project exercising every field the snapshot must carry, including the
// runtime-only ones the manifest form drops.
Project makeSnapshotProject()
{
  Project p;
  p.name = "Snapshot";
  p.projectDirectory = "/data/projects/snap";
  p.nextDatasetOrdinal = 5;
  p.dirty = true;

  Dataset ds;
  ds.id = "dataset_0001";
  ds.name = "Wind";
  ds.sourceKind = DatasetSourceKind::FileAnimation;
  ds.importerType = "VTK";
  ds.source.sourcePath = "/data/wind/frames.sources";
  ds.source.importerSettings.set("scalar", "velocity");
  ds.source.importerSettings.set("stride", "2");
  ds.status = DatasetStatus::Importing;
  ds.residency = DatasetResidency::Unloaded;
  ds.rootNode = {"datasets", 4};
  ds.sourceFiles.push_back({"frame_000.vtk", "/data/wind/frame_000.vtk"});
  ds.sourceFiles.push_back({"/abs/frame_001.vtk", ""});
  ds.dirty = true;
  ds.pendingExtraction = true;
  ds.declared = true;
  ds.pendingSourceListMigration = true;
  ds.persistedName = "Wind (old)";
  p.datasets.push_back(std::move(ds));

  Shot a;
  a.id = "shot_0001";
  a.name = "Intro";
  a.frameCount = 240;
  a.fps = 30.f;
  a.currentFrame = 17;
  a.playing = true;
  a.loop = false;
  a.datasetBindings.push_back({"dataset_0001", false});
  a.lightRigId = "lightRig_0001";
  a.cameraRigId = "cameraRig_0001";
  a.camera = {ANARI_CAMERA, 2};
  a.renderSettings.width = 1920;
  a.renderSettings.height = 1080;
  a.renderSettings.samples = 64;
  a.renderSettings.rendererLibrary = "visrtx";
  a.renderSettings.rendererObjectIndex = 7;
  a.renderSettings.rendererSubtype = "scivis";
  a.renderSettings.outputFilePrefix = "intro_";
  p.shots.push_back(a);

  Shot b;
  b.id = "shot_0002";
  b.name = "Outro";
  b.camera = {ANARI_CAMERA, 9};
  p.shots.push_back(b);
  p.activeShotId = a.id;

  LightRig lr;
  lr.id = "lightRig_0001";
  lr.name = "Studio Lights";
  lr.rootNode = {"lights", 11};
  lr.persistedName = "Studio Lights";
  p.lightRigs.push_back(lr);

  CameraRig cr;
  cr.id = "cameraRig_0001";
  cr.name = "Fly-through";
  cr.current.orbit.lookat = {1.f, 2.f, 3.f};
  cr.current.orbit.azeldist = {45.f, 30.f, 12.f};
  CameraKeyframe k0;
  k0.frame = 0;
  k0.name = "start";
  k0.manipulator.orbit.lookat = {0.f, 0.f, 0.f};
  k0.manipulator.orbit.azeldist = {0.f, 0.f, 10.f};
  k0.interpolationToNext = CameraInterpolation::EaseOutIn;
  CameraKeyframe k1;
  k1.frame = 120;
  k1.name = "end";
  k1.manipulator.orbit.lookat = {5.f, 6.f, 7.f};
  k1.manipulator.orbit.azeldist = {90.f, 10.f, 20.f};
  k1.interpolationToNext = CameraInterpolation::Hold;
  cr.keyframes.push_back(k0);
  cr.keyframes.push_back(k1);
  cr.persistedName = "Fly-through";
  p.cameraRigs.push_back(cr);

  p.colorMaps.push_back({"colorMap_0001", "Viridis"});
  return p;
}

void requireSamePose(
    const vsr::rendering::CameraPose &a, const vsr::rendering::CameraPose &b)
{
  REQUIRE(a.lookat.x == b.lookat.x);
  REQUIRE(a.lookat.y == b.lookat.y);
  REQUIRE(a.lookat.z == b.lookat.z);
  REQUIRE(a.azeldist.x == b.azeldist.x);
  REQUIRE(a.azeldist.y == b.azeldist.y);
  REQUIRE(a.azeldist.z == b.azeldist.z);
}

} // namespace

SCENARIO("ProjectOpReply payload", "[StudioProtocol]")
{
  GIVEN("an ok reply")
  {
    const auto out = decode<ProjectOpReply>(encode(makeOkReply(42)));
    REQUIRE(out);
    REQUIRE(out->requestId == 42);
    REQUIRE(out->ok);
    REQUIRE(out->error.empty());
    REQUIRE_FALSE(out->results);

    THEN("results<T>() on a reply without results is empty")
    {
      REQUIRE_FALSE(results<TaskStartedResult>(*out));
    }
  }

  GIVEN("an error reply")
  {
    const auto out =
        decode<ProjectOpReply>(encode(makeErrorReply(7, "no such dataset")));
    REQUIRE(out);
    REQUIRE(out->requestId == 7);
    REQUIRE_FALSE(out->ok);
    REQUIRE(out->error == "no such dataset");
    REQUIRE_FALSE(out->results);
  }

  GIVEN("a reply carrying a TaskStartedResult")
  {
    auto reply = makeOkReply(3);
    setResults(reply, TaskStartedResult{99});
    REQUIRE(reply.results);

    THEN("the result decodes on the receiving side")
    {
      const auto out = decode<ProjectOpReply>(encode(reply));
      REQUIRE(out);
      REQUIRE(out->results);
      const auto started = results<TaskStartedResult>(*out);
      REQUIRE(started);
      REQUIRE(started->taskId == 99);
    }

    THEN("results<T>() rejects a subtree of another shape")
    {
      auto other = makeOkReply(3);
      other.results = makeSubtree();
      other.results->root()["somethingElse"] = std::string("x");
      REQUIRE_FALSE(results<TaskStartedResult>(other));
    }
  }

  GIVEN("malformed replies")
  {
    THEN("a missing requestId or ok is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "ok", true);
      ProjectOpReply out;
      REQUIRE_FALSE(fromNode(tree.root(), out));

      vsr::core::DataTree noOk;
      writeChild(noOk.root(), "requestId", uint64_t(1));
      REQUIRE_FALSE(fromNode(noOk.root(), out));
    }

    THEN("a mistyped requestId is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", 1);
      writeChild(tree.root(), "ok", true);
      ProjectOpReply out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }
}

SCENARIO("Server Task payloads", "[StudioProtocol]")
{
  GIVEN("a TaskProgress")
  {
    TaskProgress p;
    p.taskId = 5;
    p.current = 30;
    p.total = 120;
    p.message = "frame 30";
    const auto out = decode<TaskProgress>(encode(p));
    REQUIRE(out);
    REQUIRE(out->taskId == 5);
    REQUIRE(out->current == 30);
    REQUIRE(out->total == 120);
    REQUIRE(out->message == "frame 30");

    THEN("an indeterminate progress keeps total == 0")
    {
      TaskProgress ind;
      ind.taskId = 6;
      const auto o = decode<TaskProgress>(encode(ind));
      REQUIRE(o);
      REQUIRE(o->total == 0);
      REQUIRE(o->current == 0);
      REQUIRE(o->message.empty());
    }
  }

  GIVEN("a TaskCompleted carrying a RenderShotResult")
  {
    TaskCompleted c;
    c.taskId = 8;
    c.message = "rendered";
    setResults(c, RenderShotResult{240});
    const auto out = decode<TaskCompleted>(encode(c));
    REQUIRE(out);
    REQUIRE(out->taskId == 8);
    REQUIRE(out->message == "rendered");
    const auto rendered = results<RenderShotResult>(*out);
    REQUIRE(rendered);
    REQUIRE(rendered->framesCompleted == 240);
    REQUIRE_FALSE(decode<TaskFailed>(encode(c)));

    THEN("an ending without results has none to decode")
    {
      TaskCompleted plain;
      plain.taskId = 8;
      const auto o = decode<TaskCompleted>(encode(plain));
      REQUIRE(o);
      REQUIRE_FALSE(o->results);
      REQUIRE_FALSE(results<RenderShotResult>(*o));
    }

    THEN("results<T>() rejects a subtree of another shape")
    {
      TaskCompleted other;
      other.taskId = 8;
      setResults(other, TaskStartedResult{3});
      REQUIRE_FALSE(results<RenderShotResult>(other));
    }
  }

  GIVEN("a TaskFailed carrying a RenderShotResult")
  {
    TaskFailed f;
    f.taskId = 9;
    f.error = "disk full";
    setResults(f, RenderShotResult{17});
    const auto out = decode<TaskFailed>(encode(f));
    REQUIRE(out);
    REQUIRE(out->taskId == 9);
    REQUIRE(out->error == "disk full");
    const auto rendered = results<RenderShotResult>(*out);
    REQUIRE(rendered);
    REQUIRE(rendered->framesCompleted == 17);
  }

  GIVEN("a CancelTask")
  {
    CancelTask c;
    c.requestId = 11;
    c.taskId = 9;
    const auto out = decode<CancelTask>(encode(c));
    REQUIRE(out);
    REQUIRE(out->requestId == 11);
    REQUIRE(out->taskId == 9);

    THEN("a missing taskId is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(11));
      CancelTask bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
    }
  }

  GIVEN("a task event without a taskId")
  {
    vsr::core::DataTree tree;
    writeChild(tree.root(), "message", std::string("orphan"));
    TaskProgress p;
    TaskCompleted c;
    TaskFailed f;
    REQUIRE_FALSE(fromNode(tree.root(), p));
    REQUIRE_FALSE(fromNode(tree.root(), c));
    REQUIRE_FALSE(fromNode(tree.root(), f));
  }
}

SCENARIO("ProjectSnapshot payload", "[StudioProtocol]")
{
  GIVEN("a project with a dataset, two shots, rigs and a color map")
  {
    ProjectSnapshot snap;
    snap.project = makeSnapshotProject();
    const auto msg = encode(snap);
    REQUIRE(msg.header.type == uint8_t(StudioMessageType::ProjectSnapshot));

    const auto out = decode<ProjectSnapshot>(msg);
    REQUIRE(out);
    const auto &p = out->project;
    const auto &src = snap.project;

    THEN("project-level fields round-trip")
    {
      REQUIRE(p.name == src.name);
      REQUIRE(p.projectDirectory == src.projectDirectory);
      REQUIRE(p.activeShotId == "shot_0001");
      REQUIRE(p.nextDatasetOrdinal == 5);
      REQUIRE(p.dirty);
      REQUIRE(p.colorMaps.size() == 1);
      REQUIRE(p.colorMaps[0].id == "colorMap_0001");
      REQUIRE(p.colorMaps[0].name == "Viridis");
    }

    THEN("dataset manifest and runtime fields round-trip")
    {
      REQUIRE(p.datasets.size() == 1);
      const auto &d = p.datasets[0];
      const auto &s = src.datasets[0];
      REQUIRE(d.id == s.id);
      REQUIRE(d.name == s.name);
      REQUIRE(d.residency == DatasetResidency::Unloaded);
      REQUIRE(d.status == DatasetStatus::Importing);
      REQUIRE(d.sourceKind == DatasetSourceKind::FileAnimation);
      REQUIRE(d.importerType == "VTK");
      REQUIRE(d.source.sourcePath == s.source.sourcePath);
      REQUIRE(d.source.importerSettings.size() == 2);
      REQUIRE(*d.source.importerSettings.at("scalar") == "velocity");
      REQUIRE(*d.source.importerSettings.at("stride") == "2");
      REQUIRE(d.rootNode.layerName == "datasets");
      REQUIRE(d.rootNode.nodeIndex == 4);
      REQUIRE(d.sourceFiles.size() == 2);
      REQUIRE(d.sourceFiles[0].path == "frame_000.vtk");
      REQUIRE(d.sourceFiles[0].resolvedPath == "/data/wind/frame_000.vtk");
      REQUIRE(d.sourceFiles[1].path == "/abs/frame_001.vtk");
      REQUIRE(d.sourceFiles[1].resolvedPath.empty());
      REQUIRE(d.dirty);
      REQUIRE(d.pendingExtraction);
      REQUIRE(d.declared);
      REQUIRE(d.pendingSourceListMigration);
      REQUIRE(d.persistedName == "Wind (old)");
    }

    THEN("shots round-trip including playback state and camera")
    {
      REQUIRE(p.shots.size() == 2);
      const auto &a = p.shots[0];
      const auto &sa = src.shots[0];
      REQUIRE(a.id == "shot_0001");
      REQUIRE(a.name == "Intro");
      REQUIRE(a.frameCount == 240);
      REQUIRE(a.fps == 30.f);
      REQUIRE(a.currentFrame == 17);
      REQUIRE(a.playing);
      REQUIRE_FALSE(a.loop);
      REQUIRE(a.datasetBindings.size() == 1);
      REQUIRE(a.datasetBindings[0].datasetId == "dataset_0001");
      REQUIRE_FALSE(a.datasetBindings[0].enabled);
      REQUIRE(a.lightRigId == "lightRig_0001");
      REQUIRE(a.cameraRigId == "cameraRig_0001");
      REQUIRE(a.camera.type == ANARI_CAMERA);
      REQUIRE(a.camera.objectIndex == 2);
      REQUIRE(a.renderSettings.width == sa.renderSettings.width);
      REQUIRE(a.renderSettings.height == sa.renderSettings.height);
      REQUIRE(a.renderSettings.samples == sa.renderSettings.samples);
      REQUIRE(a.renderSettings.rendererLibrary == "visrtx");
      REQUIRE(a.renderSettings.rendererObjectIndex == 7);
      REQUIRE(a.renderSettings.rendererSubtype == "scivis");
      REQUIRE(a.renderSettings.outputFilePrefix == "intro_");

      const auto &b = p.shots[1];
      REQUIRE(b.id == "shot_0002");
      REQUIRE(b.name == "Outro");
      REQUIRE(b.currentFrame == 0);
      REQUIRE_FALSE(b.playing);
      REQUIRE(b.loop);
      REQUIRE(b.camera.type == ANARI_CAMERA);
      REQUIRE(b.camera.objectIndex == 9);
    }

    THEN("light rigs round-trip including the scene root node")
    {
      REQUIRE(p.lightRigs.size() == 1);
      REQUIRE(p.lightRigs[0].id == "lightRig_0001");
      REQUIRE(p.lightRigs[0].name == "Studio Lights");
      REQUIRE(p.lightRigs[0].rootNode.layerName == "lights");
      REQUIRE(p.lightRigs[0].rootNode.nodeIndex == 11);
      REQUIRE(p.lightRigs[0].persistedName == "Studio Lights");
    }

    THEN("camera rigs round-trip including pose and keyframes")
    {
      REQUIRE(p.cameraRigs.size() == 1);
      const auto &r = p.cameraRigs[0];
      const auto &sr = src.cameraRigs[0];
      REQUIRE(r.id == "cameraRig_0001");
      REQUIRE(r.name == "Fly-through");
      REQUIRE(r.persistedName == "Fly-through");
      requireSamePose(r.current.orbit, sr.current.orbit);
      REQUIRE(r.keyframes.size() == 2);
      for (size_t i = 0; i < 2; ++i) {
        REQUIRE(r.keyframes[i].frame == sr.keyframes[i].frame);
        REQUIRE(r.keyframes[i].name == sr.keyframes[i].name);
        REQUIRE(r.keyframes[i].interpolationToNext
            == sr.keyframes[i].interpolationToNext);
        requireSamePose(r.keyframes[i].manipulator.orbit,
            sr.keyframes[i].manipulator.orbit);
      }
    }

    THEN("the wire tree is the Full form: runtime fields inline, no sidecar")
    {
      vsr::core::DataTree tree;
      toNode(snap, tree.root());
      REQUIRE_FALSE(hasChild(tree.root(), "runtime"));
      const auto *project = tree.root().child("project");
      REQUIRE(project);
      REQUIRE(project->child("datasets")->child(0)->child("status"));
      REQUIRE(project->child("shots")->child(0)->child("camera"));
      REQUIRE(project->child("lightRigs")->child(0)->child("rootNode"));
      REQUIRE(project->child("cameraRigs")->child(0)->child("rig"));

      // The manifest form drops exactly those.
      vsr::core::DataTree manifest;
      projectToNode(snap.project, manifest.root(), ProjectForm::Manifest);
      const auto &m = manifest.root();
      REQUIRE_FALSE(m.child("datasets")->child(0)->child("status"));
      REQUIRE_FALSE(m.child("shots")->child(0)->child("camera"));
      REQUIRE_FALSE(m.child("lightRigs")->child(0)->child("rootNode"));
      REQUIRE_FALSE(m.child("cameraRigs")->child(0)->child("rig"));
      Project fromManifest;
      REQUIRE(nodeToProject(m, fromManifest, ProjectForm::Manifest));
      REQUIRE(fromManifest.datasets[0].status == DatasetStatus::Unavailable);
    }
  }

  GIVEN("a default project")
  {
    const auto out = decode<ProjectSnapshot>(encode(ProjectSnapshot{}));
    REQUIRE(out);
    REQUIRE(out->project.name == "Untitled");
    REQUIRE(out->project.datasets.empty());
    REQUIRE(out->project.shots.empty());
    REQUIRE_FALSE(out->project.dirty);
  }

  GIVEN("a snapshot whose project carries only the manifest fields")
  {
    vsr::core::DataTree tree;
    projectToNode(
        makeSnapshotProject(), tree.root()["project"], ProjectForm::Manifest);
    ProjectSnapshot out;

    THEN("it decodes with the manifest defaults")
    {
      REQUIRE(fromNode(tree.root(), out));
      REQUIRE(out.project.datasets.size() == 1);
      REQUIRE(out.project.datasets[0].status == DatasetStatus::Unavailable);
      REQUIRE(out.project.shots[0].currentFrame == 17);
      REQUIRE(out.project.shots[0].playing);
      REQUIRE(out.project.shots[0].camera.type == ANARI_UNKNOWN);
    }

    THEN("the source tree is not modified by decoding")
    {
      const size_t before = tree.root()["project"].numChildren();
      REQUIRE(fromNode(tree.root(), out));
      REQUIRE(tree.root()["project"].numChildren() == before);
      REQUIRE_FALSE(
          tree.root()["project"].child("datasets")->child(0)->child("status"));
    }
  }

  GIVEN("malformed snapshots")
  {
    THEN("a tree without a project child is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "runtime", 1);
      ProjectSnapshot out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("an unknown dataset status string is rejected")
    {
      vsr::core::DataTree tree;
      toNode(ProjectSnapshot{makeSnapshotProject()}, tree.root());
      (*tree.root()["project"]["datasets"].child(0))["status"] =
          std::string("Sideways");
      ProjectSnapshot out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a corrupt camera rig is rejected")
    {
      // Encodes the fixture, lets `corrupt` edit the first rig's value data,
      // and reports whether the result still decodes.
      auto decodesAfter = [](auto &&corrupt) {
        vsr::core::DataTree tree;
        toNode(ProjectSnapshot{makeSnapshotProject()}, tree.root());
        corrupt((*tree.root()["project"]["cameraRigs"].child(0))["rig"]);
        ProjectSnapshot out;
        return fromNode(tree.root(), out);
      };
      REQUIRE_FALSE(decodesAfter([](vsr::core::DataNode &rig) {
        (*rig["keyframes"].child(0))["frame"] = std::string("twelve");
      }));
      REQUIRE_FALSE(decodesAfter([](vsr::core::DataNode &rig) {
        (*rig["keyframes"].child(1))["interpolationToNext"] =
            std::string("Bounce");
      }));
      REQUIRE_FALSE(decodesAfter([](vsr::core::DataNode &rig) {
        rig["current"]["orbit"]["lookat"] = std::string("origin");
      }));
    }

    THEN("a mistyped shot camera is rejected")
    {
      vsr::core::DataTree tree;
      toNode(ProjectSnapshot{makeSnapshotProject()}, tree.root());
      (*tree.root()["project"]["shots"].child(1))["camera"]["type"] =
          std::string("ANARI_BOGUS");
      ProjectSnapshot out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }
}

SCENARIO("UIState payload", "[StudioProtocol]")
{
  GIVEN("a nested UI tree")
  {
    UIState ui;
    ui.tree = makeSubtree();
    auto &r = ui.tree->root();
    r["windows"]["Viewport"]["open"] = true;
    r["windows"]["Viewport"]["size"] = vsr::math::int2(1280, 720);
    r["windows"]["LayerTree"]["open"] = false;
    r["layout"] = std::string("[Window][Viewport]\nPos=0,0\n");
    r["settings"]["theme"] = std::string("dark");
    r["settings"]["fontScale"] = 1.25f;

    THEN("it round-trips through the codec")
    {
      const auto msg = encode(ui);
      REQUIRE(msg.header.type == uint8_t(StudioMessageType::UIState));
      const auto out = decode<UIState>(msg);
      REQUIRE(out);
      REQUIRE(out->tree);
      const auto &o = out->tree->root();
      REQUIRE(o.numChildren() == 3);
      const auto *viewport = o.child("windows")->child("Viewport");
      REQUIRE(viewport);
      REQUIRE(viewport->child("open")->getValueOr(false));
      REQUIRE(
          viewport->child("size")->getValueOr(vsr::math::int2(0, 0)).x == 1280);
      REQUIRE_FALSE(o.child("windows")
                        ->child("LayerTree")
                        ->child("open")
                        ->getValueOr(true));
      REQUIRE(o.child("layout")->getValueOr(std::string())
          == "[Window][Viewport]\nPos=0,0\n");
      REQUIRE(o.child("settings")->child("theme")->getValueOr(std::string())
          == "dark");
      REQUIRE(
          o.child("settings")->child("fontScale")->getValueOr(0.f) == 1.25f);
    }

    THEN("an absent tree decodes as null")
    {
      const auto out = decode<UIState>(encode(UIState{}));
      REQUIRE(out);
      REQUIRE_FALSE(out->tree);
    }
  }
}
