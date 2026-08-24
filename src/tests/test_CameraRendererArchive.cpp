// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/DataTree.hpp"
#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/io/archives/CameraArchive.hpp"
#include "vsr/io/archives/RendererArchive.hpp"
#include "vsr/io/serialization/serialization_internal.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstdio>
#include <filesystem>

SCENARIO("Camera and Renderer Archives replace only their complete pools",
    "[CameraArchive][RendererArchive]")
{
  vsr::scene::Scene source;
  source.defaultCamera()->setName("camera zero");
  source.createObject<vsr::scene::Camera>("orthographic")
      ->setName("camera one");
  source.createRenderer("device", "pathtracer")->setName("renderer zero");

  vsr::core::DataTree cameraTree;
  vsr::core::DataTree rendererTree;
  REQUIRE(vsr::io::serialize_CameraArchive(source, cameraTree.root()));
  REQUIRE(vsr::io::serialize_RendererArchive(source, rendererTree.root()));

  auto cameraMetadata = vsr::core::readDataTreeMetadata(cameraTree.root());
  auto rendererMetadata = vsr::core::readDataTreeMetadata(rendererTree.root());
  REQUIRE(cameraMetadata.metadata);
  REQUIRE(rendererMetadata.metadata);
  REQUIRE(cameraMetadata.metadata->schema == "vsr.scene.cameras");
  REQUIRE(rendererMetadata.metadata->schema == "vsr.scene.renderers");
  REQUIRE(cameraTree.root()["objectDB"].child("renderer") == nullptr);
  REQUIRE(rendererTree.root()["objectDB"].child("camera") == nullptr);

  vsr::scene::Scene target;
  target.defaultCamera()->setName("stale camera");
  target.createRenderer("stale device", "stale renderer");
  target.createObject<vsr::scene::Geometry>("sphere");
  target.addLayer("preserved layer");

  REQUIRE(vsr::io::deserialize_CameraArchive(target, cameraTree.root()));
  REQUIRE(target.numberOfObjects(ANARI_CAMERA) == 2);
  REQUIRE(target.getObject<vsr::scene::Camera>(0)->name() == "camera zero");
  REQUIRE(target.getObject<vsr::scene::Camera>(1)->name() == "camera one");
  REQUIRE(target.numberOfObjects(ANARI_RENDERER) == 1);

  REQUIRE(vsr::io::deserialize_RendererArchive(target, rendererTree.root()));
  REQUIRE(target.numberOfObjects(ANARI_RENDERER) == 1);
  REQUIRE(target.getObject<vsr::scene::Renderer>(0)->name() == "renderer zero");
  REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 1);
  REQUIRE(target.layer("preserved layer") != nullptr);

  vsr::core::DataTree legacyTree;
  vsr::io::detail::serializeLegacyCameraRendererPayload(
      source, legacyTree.root());
  REQUIRE(vsr::io::deserialize_CameraArchive(target, legacyTree.root()));
  REQUIRE(vsr::io::deserialize_RendererArchive(target, legacyTree.root()));

  const auto cameraFile =
      (std::filesystem::temp_directory_path() / "vsr_camera_archive.vsr")
          .string();
  const auto rendererFile =
      (std::filesystem::temp_directory_path() / "vsr_renderer_archive.vsr")
          .string();
  REQUIRE(vsr::io::save_CameraArchive(source, cameraFile.c_str()));
  REQUIRE(vsr::io::save_RendererArchive(source, rendererFile.c_str()));
  vsr::scene::Scene fileTarget;
  REQUIRE(vsr::io::load_CameraArchive(fileTarget, cameraFile.c_str()));
  REQUIRE(vsr::io::load_RendererArchive(fileTarget, rendererFile.c_str()));
  std::remove(cameraFile.c_str());
  std::remove(rendererFile.c_str());

  vsr::core::DataTree invalidCameraTree;
  invalidCameraTree.root() = cameraTree.root();
  (*invalidCameraTree.root()["objectDB"]["camera"].child(0))["self"] =
      vsr::core::Any(ANARI_CAMERA, size_t(8));
  const auto camerasBefore = target.numberOfObjects(ANARI_CAMERA);
  REQUIRE_FALSE(
      vsr::io::deserialize_CameraArchive(target, invalidCameraTree.root()));
  REQUIRE(target.numberOfObjects(ANARI_CAMERA) == camerasBefore);

  vsr::core::DataTree invalidRendererTree;
  invalidRendererTree.root() = rendererTree.root();
  invalidRendererTree.root()["objectDB"]["renderer"].child(0)->remove(
      "rendererDeviceName");
  const auto renderersBefore = target.numberOfObjects(ANARI_RENDERER);
  REQUIRE_FALSE(
      vsr::io::deserialize_RendererArchive(target, invalidRendererTree.root()));
  REQUIRE(target.numberOfObjects(ANARI_RENDERER) == renderersBefore);
}
