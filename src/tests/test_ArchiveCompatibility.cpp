// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/animation/Animation.hpp"
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/io/archives.hpp"
#include "vsr/io/archives/detail/ArchivePlan.hpp"
#include "vsr/io/serialization/serialization_internal.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Geometry.hpp"
// std
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::string testFile(const char *name)
{
  return (std::filesystem::temp_directory_path() / name).string();
}

void removeTestFile(const std::string &filename)
{
  std::remove(filename.c_str());
}

bool saveSubtreeArchiveContent(const char *filename,
    vsr::scene::LayerNodeRef root,
    const vsr::io::SubtreeArchiveContentDesc &desc,
    std::string_view displayName = {},
    const vsr::io::SubtreeArchiveContentOptions &options = {})
{
  vsr::core::DataTree tree;
  return vsr::io::serialize_SubtreeArchiveContent(
             root, tree.root(), desc, displayName, options)
      && tree.save(filename);
}

vsr::io::SubtreeArchiveResult loadSubtreeArchiveContent(
    vsr::scene::Scene &scene,
    const char *filename,
    vsr::scene::LayerNodeRef destination,
    const vsr::io::SubtreeArchiveContentDesc &desc,
    std::string *displayName = nullptr,
    const vsr::io::SubtreeArchiveContentOptions &options = {})
{
  vsr::core::DataTree tree;
  if (!tree.load(filename))
    return {};
  return vsr::io::deserialize_SubtreeArchiveContent(
      scene, tree.root(), destination, desc, displayName, options);
}

vsr::scene::ArrayRef makeFloatArray(vsr::scene::Scene &scene,
    const char *name,
    const std::vector<float> &values)
{
  auto array = scene.createArray(ANARI_FLOAT32, values.size());
  array->setName(name);
  array->setData(values);
  return array;
}

struct UnsupportedFileBinding : vsr::animation::FileBinding
{
  explicit UnsupportedFileBinding(vsr::scene::Scene *scene);

  std::string kind() const override;
  void toDataNode(vsr::core::DataNode &) const override;
  void update(float) override;

 private:
  void addCallbackToAnimation(vsr::animation::Animation &) override;
};

UnsupportedFileBinding::UnsupportedFileBinding(vsr::scene::Scene *scene)
    : FileBinding(scene)
{}

std::string UnsupportedFileBinding::kind() const
{
  return "unsupported";
}

void UnsupportedFileBinding::toDataNode(vsr::core::DataNode &) const {}

void UnsupportedFileBinding::update(float) {}

void UnsupportedFileBinding::addCallbackToAnimation(vsr::animation::Animation &)
{}

} // namespace

SCENARIO("vsr::io camera and renderer subset serialization",
    "[ArchiveCompatibility]")
{
  GIVEN("A scene with cameras, renderers, and unrelated scene data")
  {
    vsr::scene::Scene source;

    auto defaultCamera = source.defaultCamera();
    defaultCamera->setName("shot_0_camera");
    defaultCamera->setParameter("fovy", 0.75f);
    defaultCamera->setMetadataValue("exposure", 1.5f);

    auto secondCamera = source.createObject<vsr::scene::Camera>("orthographic");
    secondCamera->setName("shot_1_camera");
    secondCamera->setParameter("height", 12.f);

    auto renderer = source.createRenderer("test_device", "pathtracer");
    renderer->setName("shot_renderer");
    renderer->setParameter("pixelSamples", 8);
    renderer->setMetadataValue("quality", 3);

    source.createObject<vsr::scene::Geometry>("sphere");
    source.addLayer("preserved_source_layer");

    vsr::core::DataTree tree;
    auto &root = tree.root();
    root["layers"]["stale"] = "remove me";
    root["animations"]["stale"] = "remove me";

    WHEN("only cameras and renderers are saved")
    {
      vsr::io::detail::serializeLegacyCameraRendererPayload(source, root);

      THEN("the output is tagged as a camera and renderer subset")
      {
        auto metadata = vsr::core::readDataTreeMetadata(root);
        REQUIRE(
            metadata.status == vsr::core::DataTreeMetadataReadStatus::Found);
        REQUIRE(metadata.metadata);
        REQUIRE(metadata.metadata->schema
            == std::string(vsr::io::schema::SCENE_CAMERAS_AND_RENDERERS));
      }

      THEN("the output contains only the camera and renderer object pools")
      {
        REQUIRE(root.child("layers") == nullptr);
        REQUIRE(root.child("animations") == nullptr);

        auto *objectDB = root.child("objectDB");
        REQUIRE(objectDB != nullptr);
        REQUIRE(objectDB->child("camera") != nullptr);
        REQUIRE(objectDB->child("renderer") != nullptr);
        REQUIRE(objectDB->child("geometry") == nullptr);
        REQUIRE(objectDB->child("material") == nullptr);
      }

      AND_WHEN("the subset is loaded into another populated scene")
      {
        vsr::scene::Scene target;
        target.defaultCamera()->setName("old_default_camera");
        auto oldCamera = target.createObject<vsr::scene::Camera>("perspective");
        oldCamera->setName("old_extra_camera");
        auto oldRenderer = target.createRenderer("old_device", "old_renderer");
        oldRenderer->setName("old_renderer");
        target.createObject<vsr::scene::Geometry>("cylinder");
        target.addLayer("keep_me");

        vsr::io::detail::tryDeserializeLegacyCameraRendererPayload(
            target, root);

        THEN("only cameras and renderers are replaced")
        {
          REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 1);
          REQUIRE(target.numberOfLayers() == 1);
          REQUIRE(target.layer("keep_me") != nullptr);

          REQUIRE(target.numberOfObjects(ANARI_CAMERA) == 2);
          REQUIRE(target.numberOfObjects(ANARI_RENDERER) == 1);
          REQUIRE(target.getObject<vsr::scene::Camera>(0)->name()
              == "shot_0_camera");
          REQUIRE(target.getObject<vsr::scene::Camera>(1)->name()
              == "shot_1_camera");
          REQUIRE(target.getObject<vsr::scene::Renderer>(0)->name()
              == "shot_renderer");
        }

        THEN("camera and renderer object data round-trips")
        {
          auto camera = target.getObject<vsr::scene::Camera>(0);
          REQUIRE(camera);
          REQUIRE(camera->subtype().str() == "perspective");
          REQUIRE(camera->parameter("fovy")->value().getAs<float>() == 0.75f);
          REQUIRE(camera->getMetadataValue("exposure").getAs<float>() == 1.5f);

          auto second = target.getObject<vsr::scene::Camera>(1);
          REQUIRE(second);
          REQUIRE(second->subtype().str() == "orthographic");
          REQUIRE(second->parameter("height")->value().getAs<float>() == 12.f);

          auto restoredRenderer = target.getObject<vsr::scene::Renderer>(0);
          REQUIRE(restoredRenderer);
          REQUIRE(restoredRenderer->subtype().str() == "pathtracer");
          REQUIRE(
              restoredRenderer->rendererDeviceName().str() == "test_device");
          REQUIRE(
              restoredRenderer->parameter("pixelSamples")->value().getAs<int>()
              == 8);
          REQUIRE(
              restoredRenderer->getMetadataValue("quality").getAs<int>() == 3);
        }
      }
    }
  }

  GIVEN("An empty camera subset")
  {
    vsr::scene::Scene scene;
    vsr::core::DataTree tree;
    tree.root()["objectDB"];

    WHEN("the subset is loaded")
    {
      vsr::io::detail::tryDeserializeLegacyCameraRendererPayload(
          scene, tree.root());

      THEN("the scene still has a default camera")
      {
        REQUIRE(scene.defaultCamera());
        REQUIRE(scene.numberOfObjects(ANARI_CAMERA) == 1);
      }
    }
  }
}

SCENARIO("vsr::io scene payload metadata validation", "[ArchiveCompatibility]")
{
  GIVEN("A serializable scene")
  {
    vsr::scene::Scene source;
    source.defaultCamera()->setName("source_camera");
    auto renderer = source.createRenderer("test_device", "pathtracer");
    renderer->setName("source_renderer");

    WHEN("a full scene is serialized")
    {
      vsr::core::DataTree tree;
      REQUIRE(vsr::io::serialize_SceneArchive(source, tree.root()));

      THEN("the output is tagged as a full scene")
      {
        auto metadata = vsr::core::readDataTreeMetadata(tree.root());
        REQUIRE(
            metadata.status == vsr::core::DataTreeMetadataReadStatus::Found);
        REQUIRE(metadata.metadata);
        REQUIRE(metadata.metadata->schema
            == std::string(vsr::io::schema::SCENE_FULL));
      }

      THEN("the camera and renderer subset loader accepts the full scene")
      {
        auto result =
            vsr::io::detail::validateLegacyCameraRendererPayload(tree.root());
        REQUIRE(result.accepted());
        REQUIRE(result.status == vsr::io::ArchiveValidationStatus::Valid);
      }
    }

    WHEN("a camera and renderer subset is loaded as a full scene")
    {
      vsr::core::DataTree subsetTree;
      vsr::io::detail::serializeLegacyCameraRendererPayload(
          source, subsetTree.root());

      vsr::scene::Scene target;
      target.createObject<vsr::scene::Geometry>("sphere");
      target.addLayer("keep_me");

      THEN("validation rejects it before mutation")
      {
        auto result =
            vsr::io::detail::validateLegacyScenePayload(subsetTree.root());
        REQUIRE(!result.accepted());
        REQUIRE(result.status
            == vsr::io::ArchiveValidationStatus::IncompatibleSchema);

        vsr::io::detail::tryDeserializeLegacyScenePayload(
            target, subsetTree.root());
        REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 1);
        REQUIRE(target.numberOfLayers() == 1);
        REQUIRE(target.layer("keep_me") != nullptr);
      }
    }

    WHEN("legacy metadata is missing but objectDB exists")
    {
      vsr::core::DataTree legacyTree;
      legacyTree.root()["objectDB"];

      THEN("validation accepts it as legacy")
      {
        auto result =
            vsr::io::detail::validateLegacyScenePayload(legacyTree.root());
        REQUIRE(result.accepted());
        REQUIRE(result.status
            == vsr::io::ArchiveValidationStatus::MissingMetadataAccepted);
      }
    }

    WHEN("the payload is missing objectDB")
    {
      vsr::core::DataTree invalidTree;
      vsr::core::writeDataTreeMetadata(
          invalidTree.root(), {1, "scene", "vsr.scene.full", 1});

      vsr::scene::Scene target;
      target.createObject<vsr::scene::Geometry>("sphere");
      target.addLayer("keep_me");

      THEN("validation rejects it before mutation")
      {
        auto result =
            vsr::io::detail::validateLegacyScenePayload(invalidTree.root());
        REQUIRE(!result.accepted());
        REQUIRE(result.status
            == vsr::io::ArchiveValidationStatus::MissingRequiredNode);

        vsr::io::detail::tryDeserializeLegacyScenePayload(
            target, invalidTree.root());
        REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 1);
        REQUIRE(target.numberOfLayers() == 1);
        REQUIRE(target.layer("keep_me") != nullptr);
      }
    }
  }
}

SCENARIO("vsr::io surface object serialization", "[ArchiveCompatibility]")
{
  GIVEN("A surface with geometry, material, sampler, array data, and metadata")
  {
    vsr::scene::Scene source;
    source.createSurface("unused_surface");

    auto positions =
        makeFloatArray(source, "positions", {1.f, 2.f, 3.f, 4.f, 5.f, 6.f});
    positions->setMetadataValue("stride", 12);

    auto texture = makeFloatArray(source, "texture", {0.25f, 0.5f, 0.75f});

    auto sampler = source.createObject<vsr::scene::Sampler>(
        vsr::scene::tokens::sampler::image1D);
    sampler->setName("albedo_sampler");
    sampler->setParameterObject("image", *texture);

    auto geometry = source.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::triangle);
    geometry->setName("mesh_geometry");
    auto *positionParam =
        geometry->setParameterObject("vertex.position", *positions);
    positionParam->setDescription("positions").setEnabled(false);
    geometry->setMetadataValue("positionBuffer",
        vsr::core::Any(positions->type(), positions->index()));

    auto material = source.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    material->setName("sampled_material");
    material->removeAllParameters();
    material->setParameterObject("color", *sampler);
    material->setParameter("roughness", 0.35f);
    material->setMetadataValue(
        "samplerRef", vsr::core::Any(sampler->type(), sampler->index()));

    auto surface = source.createSurface("root_surface", geometry, material);
    surface->setMetadataValue("priority", 9);
    surface->setMetadataValue(
        "geometryRef", vsr::core::Any(geometry->type(), geometry->index()));

    const auto filename = testFile("vsr_surface_object_roundtrip.vsr");
    removeTestFile(filename);

    WHEN("the Surface Object Archive is saved and loaded")
    {
      REQUIRE(vsr::io::save_ObjectArchive(*surface, filename.c_str()));

      vsr::core::DataTree savedTree;
      REQUIRE(savedTree.load(filename.c_str()));

      THEN("the payload is tagged as a surface object with local root index 0")
      {
        auto metadata = vsr::core::readDataTreeMetadata(savedTree.root());
        REQUIRE(
            metadata.status == vsr::core::DataTreeMetadataReadStatus::Found);
        REQUIRE(metadata.metadata);
        REQUIRE(metadata.metadata->fileType == "object");
        REQUIRE(metadata.metadata->schema
            == std::string(vsr::io::schema::OBJECT_SURFACE));

        auto *rootObject = savedTree.root().child("rootObject");
        REQUIRE(rootObject);
        REQUIRE(rootObject->getValue().type() == ANARI_SURFACE);
        REQUIRE(rootObject->getValue().getAsObjectIndex() == 0);

        auto *surfaceNode =
            savedTree.root().child("objectDB")->child("surface")->child(0);
        REQUIRE(surfaceNode);
        REQUIRE(surfaceNode->child("self")->getValue().type() == ANARI_SURFACE);
        REQUIRE(surfaceNode->child("self")->getValue().getAsObjectIndex() == 0);
      }

      vsr::scene::Scene target;
      auto existingGeometry = target.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);
      existingGeometry->setName("preexisting_geometry");
      target.addLayer("keep_me");

      auto *loaded = dynamic_cast<vsr::scene::Surface *>(
          vsr::io::load_ObjectArchive(target, filename.c_str()));

      THEN("loading appends objects without creating layers")
      {
        REQUIRE(loaded);
        REQUIRE(target.numberOfLayers() == 1);
        REQUIRE(target.layer("keep_me") != nullptr);
        REQUIRE(target.numberOfObjects(ANARI_SURFACE) == 1);
        REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 2);
        REQUIRE(target.numberOfObjects(ANARI_MATERIAL) == 2);
        REQUIRE(target.numberOfObjects(ANARI_SAMPLER) == 1);
        REQUIRE(target.numberOfObjects(ANARI_ARRAY) == 2);
      }

      THEN("surface dependencies, metadata, data, and sharing round-trip")
      {
        REQUIRE(loaded->name() == "root_surface");
        REQUIRE(loaded->getMetadataValue("priority").getAs<int>() == 9);

        auto *loadedGeometry = loaded->geometry();
        auto *loadedMaterial = loaded->material();
        REQUIRE(loadedGeometry);
        REQUIRE(loadedMaterial);
        REQUIRE(loadedGeometry->name() == "mesh_geometry");
        REQUIRE(loadedMaterial->name() == "sampled_material");

        auto geometryMetadata = loaded->getMetadataValue("geometryRef");
        REQUIRE(geometryMetadata.holdsObject());
        REQUIRE(geometryMetadata.getAsObjectIndex() == loadedGeometry->index());

        auto *positionParam = loadedGeometry->parameter("vertex.position");
        REQUIRE(positionParam);
        REQUIRE(!positionParam->isEnabled());
        REQUIRE(positionParam->description() == "positions");

        auto *loadedPositions =
            loadedGeometry->parameterValueAsObject<vsr::scene::Array>(
                "vertex.position");
        REQUIRE(loadedPositions);
        REQUIRE(loadedPositions->name() == "positions");
        REQUIRE(loadedPositions->getMetadataValue("stride").getAs<int>() == 12);
        REQUIRE(loadedPositions->size() == 6);
        const auto *positionData = loadedPositions->dataAs<float>();
        REQUIRE(positionData[0] == 1.f);
        REQUIRE(positionData[5] == 6.f);

        auto *loadedSampler =
            loadedMaterial->parameterValueAsObject<vsr::scene::Sampler>(
                "color");
        REQUIRE(loadedSampler);
        auto *loadedTexture =
            loadedSampler->parameterValueAsObject<vsr::scene::Array>("image");
        REQUIRE(loadedTexture);
        REQUIRE(loadedTexture->name() == "texture");
        REQUIRE(loadedTexture->dataAs<float>()[2] == 0.75f);

        auto samplerMetadata = loadedMaterial->getMetadataValue("samplerRef");
        REQUIRE(samplerMetadata.holdsObject());
        REQUIRE(samplerMetadata.getAsObjectIndex() == loadedSampler->index());
      }
    }

    removeTestFile(filename);
  }
}

SCENARIO("vsr::io volume object serialization", "[ArchiveCompatibility]")
{
  GIVEN("A volume with a spatial field, transfer function arrays, and metadata")
  {
    vsr::scene::Scene source;

    auto fieldData = makeFloatArray(source, "field_data", {0.f, 1.f, 2.f, 3.f});
    auto colors = makeFloatArray(source, "tf_colors", {1.f, 0.f, 0.f, 1.f});
    auto opacity = makeFloatArray(source, "tf_opacity", {0.f, 1.f});

    auto field = source.createObject<vsr::scene::SpatialField>(
        vsr::scene::tokens::spatial_field::structuredRegular);
    field->setName("density_field");
    field->setParameterObject("data", *fieldData);
    field->setMetadataValue(
        "sourceData", vsr::core::Any(fieldData->type(), fieldData->index()));

    auto sampler = source.createObject<vsr::scene::Sampler>(
        vsr::scene::tokens::sampler::image1D);
    sampler->setName("tf_sampler");
    sampler->setParameterObject("image", *colors);

    auto volume = source.createObject<vsr::scene::Volume>(
        vsr::scene::tokens::volume::transferFunction1D);
    volume->setName("root_volume");
    volume->removeAllParameters();
    volume->setParameterObject("value", *field);
    volume->setParameterObject("color", *sampler);
    volume->setParameterObject("opacity", *opacity);
    volume->setMetadataValue(
        "fieldRef", vsr::core::Any(field->type(), field->index()));

    const auto filename = testFile("vsr_volume_object_roundtrip.vsr");
    removeTestFile(filename);

    WHEN("the Volume Object Archive is saved and loaded")
    {
      REQUIRE(vsr::io::save_ObjectArchive(*volume, filename.c_str()));

      vsr::core::DataTree savedTree;
      REQUIRE(savedTree.load(filename.c_str()));
      REQUIRE(vsr::io::validate_ObjectArchive(savedTree.root()).accepted());

      vsr::scene::Scene target;
      target.createObject<vsr::scene::SpatialField>(
          vsr::scene::tokens::spatial_field::structuredRegular);

      auto *loaded = dynamic_cast<vsr::scene::Volume *>(
          vsr::io::load_ObjectArchive(target, filename.c_str()));

      THEN("the loaded volume preserves field, arrays, metadata, and refs")
      {
        REQUIRE(loaded);
        REQUIRE(loaded->name() == "root_volume");
        REQUIRE(target.numberOfObjects(ANARI_VOLUME) == 1);
        REQUIRE(target.numberOfObjects(ANARI_SPATIAL_FIELD) == 2);
        REQUIRE(target.numberOfObjects(ANARI_ARRAY) == 3);

        auto *loadedField =
            loaded->parameterValueAsObject<vsr::scene::SpatialField>("value");
        REQUIRE(loadedField);
        REQUIRE(loadedField->name() == "density_field");

        auto fieldRef = loaded->getMetadataValue("fieldRef");
        REQUIRE(fieldRef.holdsObject());
        REQUIRE(fieldRef.getAsObjectIndex() == loadedField->index());

        auto *loadedData =
            loadedField->parameterValueAsObject<vsr::scene::Array>("data");
        REQUIRE(loadedData);
        REQUIRE(loadedData->dataAs<float>()[3] == 3.f);

        auto sourceData = loadedField->getMetadataValue("sourceData");
        REQUIRE(sourceData.holdsObject());
        REQUIRE(sourceData.getAsObjectIndex() == loadedData->index());

        auto *loadedSampler =
            loaded->parameterValueAsObject<vsr::scene::Sampler>("color");
        REQUIRE(loadedSampler);
        auto *loadedColors =
            loadedSampler->parameterValueAsObject<vsr::scene::Array>("image");
        REQUIRE(loadedColors);
        REQUIRE(loadedColors->name() == "tf_colors");
        REQUIRE(loadedColors->dataAs<float>()[0] == 1.f);

        auto *loadedOpacity =
            loaded->parameterValueAsObject<vsr::scene::Array>("opacity");
        REQUIRE(loadedOpacity);
        REQUIRE(loadedOpacity->name() == "tf_opacity");
      }
    }

    removeTestFile(filename);
  }
}

SCENARIO("vsr::io object payload validation failures", "[ArchiveCompatibility]")
{
  GIVEN("A full scene payload")
  {
    vsr::scene::Scene scene;
    vsr::core::DataTree tree;
    REQUIRE(vsr::io::serialize_SceneArchive(scene, tree.root()));
    const auto filename =
        testFile("vsr_full_scene_rejected_by_object_archive.vsr");
    removeTestFile(filename);
    REQUIRE(tree.save(filename.c_str()));

    THEN("Object Archive validation rejects it")
    {
      auto result = vsr::io::validate_ObjectArchive(tree.root());
      REQUIRE(!result.accepted());
      REQUIRE(result.status
          == vsr::io::ArchiveValidationStatus::IncompatibleSchema);

      vsr::scene::Scene target;
      const auto before = target.numberOfObjects(ANARI_MATERIAL);
      REQUIRE(vsr::io::load_ObjectArchive(target, filename.c_str()) == nullptr);
      REQUIRE(target.numberOfObjects(ANARI_MATERIAL) == before);
    }

    removeTestFile(filename);
  }

  GIVEN("A surface object file")
  {
    vsr::scene::Scene source;
    auto geometry = source.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);
    auto material = source.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = source.createSurface("surface", geometry, material);

    const auto filename = testFile("vsr_invalid_surface_object.vsr");
    removeTestFile(filename);
    REQUIRE(vsr::io::save_ObjectArchive(*surface, filename.c_str()));

    vsr::core::DataTree tree;
    REQUIRE(tree.load(filename.c_str()));

    WHEN("an extra unreferenced object is present")
    {
      auto &extra = tree.root()["objectDB"]["geometry"].append();
      vsr::io::serialize_Object(*geometry, extra);
      extra["self"] = vsr::core::Any(ANARI_GEOMETRY, size_t(1));

      THEN("validation rejects the payload")
      {
        auto result = vsr::io::validate_ObjectArchive(tree.root());
        REQUIRE(!result.accepted());
        REQUIRE(result.status
            == vsr::io::ArchiveValidationStatus::IncompatibleSchema);
      }
    }

    removeTestFile(filename);
  }

  GIVEN("A surface payload with a disallowed volume pool")
  {
    vsr::core::DataTree tree;
    auto &root = tree.root();
    vsr::core::writeDataTreeMetadata(root,
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            "object",
            std::string(vsr::io::schema::OBJECT_SURFACE),
            1});
    root["rootObject"] = vsr::core::Any(ANARI_SURFACE, size_t(0));
    auto &surfaceNode = root["objectDB"]["surface"].append();
    surfaceNode["name"] = "surface";
    surfaceNode["self"] = vsr::core::Any(ANARI_SURFACE, size_t(0));
    surfaceNode["subtype"] = "";
    auto &volumeNode = root["objectDB"]["volume"].append();
    volumeNode["name"] = "volume";
    volumeNode["self"] = vsr::core::Any(ANARI_VOLUME, size_t(0));
    volumeNode["subtype"] =
        vsr::scene::tokens::volume::transferFunction1D.c_str();

    THEN("validation rejects the disallowed pool")
    {
      auto result = vsr::io::validate_ObjectArchive(root);
      REQUIRE(!result.accepted());
      REQUIRE(result.status
          == vsr::io::ArchiveValidationStatus::IncompatibleSchema);
    }
  }
}

SCENARIO("vsr::io Object Archive save failures", "[ArchiveCompatibility]")
{
  GIVEN("An unsupported root object type")
  {
    vsr::scene::Scene scene;
    auto geometry = scene.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);

    THEN("saving fails")
    {
      REQUIRE_FALSE(vsr::io::save_ObjectArchive(
          *geometry, testFile("vsr_unsupported_object.vsr").c_str()));
    }
  }

  GIVEN("A surface reaching a proxy array")
  {
    vsr::scene::Scene scene;
    auto proxy = scene.createArrayProxy(ANARI_FLOAT32, 4);
    auto geometry = scene.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);
    geometry->setParameterObject("primitive.radius", *proxy);
    auto material = scene.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = scene.createSurface("surface", geometry, material);

    THEN("saving fails because Object Archives must be self-contained")
    {
      REQUIRE_FALSE(vsr::io::save_ObjectArchive(
          *surface, testFile("vsr_proxy_array_object.vsr").c_str()));
    }
  }

  GIVEN("A surface reaching an object-typed array")
  {
    vsr::scene::Scene scene;
    auto objectArray = scene.createArray(ANARI_SURFACE, 1);
    auto geometry = scene.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);
    geometry->setParameterObject("surface.ids", *objectArray);
    auto material = scene.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = scene.createSurface("surface", geometry, material);

    THEN("saving fails because object-valued array data cannot be remapped")
    {
      REQUIRE_FALSE(vsr::io::save_ObjectArchive(
          *surface, testFile("vsr_object_typed_array_object.vsr").c_str()));
    }
  }
}

SCENARIO("vsr::io layer subtree serialization", "[ArchiveCompatibility]")
{
  GIVEN(
      "A scene with a layer subtree referencing surfaces, a light, and overrides")
  {
    vsr::scene::Scene source;

    auto positions = makeFloatArray(source, "positions", {0.f, 1.f, 2.f});
    auto geometry = source.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::triangle);
    geometry->setName("mesh_geometry");
    geometry->setParameterObject("vertex.position", *positions);

    auto material = source.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    material->setName("mesh_material");

    auto surface = source.createSurface("mesh_surface", geometry, material);

    auto light = source.createObject<vsr::scene::Light>("directional");
    light->setName("key_light");
    light->setParameter("irradiance", 3.f);

    auto overrideMaterial = source.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    overrideMaterial->setName("override_material");

    // Build subtree: transform -> { surface (with instance params), light }
    auto *layer = source.defaultLayer();
    auto transformNode = source.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "group");
    (*transformNode)
        .value()
        .setAsTransform(vsr::math::mat3{vsr::math::float3(2.f, 2.f, 2.f),
            vsr::math::float3(10.f, 20.f, 30.f),
            vsr::math::float3(1.f, 2.f, 3.f)});

    auto surfaceNode =
        source.insertChildObjectNode(transformNode, surface, "surface_inst");
    (*surfaceNode)
        .value()
        .setInstanceParameter("opacity", vsr::core::Any(0.5f));
    (*surfaceNode)
        .value()
        .setInstanceParameter("materialOverride",
            vsr::core::Any(
                overrideMaterial->type(), overrideMaterial->index()));

    source.insertChildObjectNode(transformNode, light, "light_inst");

    const auto filename = testFile("vsr_layer_subtree_roundtrip.vsr");
    removeTestFile(filename);

    WHEN("the subtree Archive is saved")
    {
      REQUIRE(
          vsr::io::save_LayerSubtreeArchive(transformNode, filename.c_str()));

      vsr::core::DataTree savedTree;
      REQUIRE(savedTree.load(filename.c_str()));

      THEN(
          "the payload is tagged as a layer subtree with an objectDB and subtree")
      {
        auto metadata = vsr::core::readDataTreeMetadata(savedTree.root());
        REQUIRE(
            metadata.status == vsr::core::DataTreeMetadataReadStatus::Found);
        REQUIRE(metadata.metadata);
        REQUIRE(metadata.metadata->fileType == "layer-subtree");
        REQUIRE(metadata.metadata->schema
            == std::string(vsr::io::schema::LAYER_SUBTREE));
        REQUIRE(savedTree.root().child("objectDB"));
        REQUIRE(savedTree.root().child("subtree"));

        auto result = vsr::io::validate_LayerSubtreeArchive(savedTree.root());
        REQUIRE(result.accepted());
      }
    }

    WHEN("the subtree Archive is loaded under a destination node")
    {
      REQUIRE(
          vsr::io::save_LayerSubtreeArchive(transformNode, filename.c_str()));

      vsr::scene::Scene target;
      target.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);

      auto *targetLayer = target.defaultLayer();
      auto destination = target.insertChildTransformNode(
          targetLayer->root(), vsr::math::IDENTITY_MAT4, "mount");

      auto splicedRoot =
          vsr::io::load_LayerSubtreeArchive(destination, filename.c_str());

      THEN(
          "objects are appended and the subtree is grafted under the destination")
      {
        REQUIRE(splicedRoot);
        REQUIRE(target.numberOfObjects(ANARI_SURFACE) == 1);
        REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 2); // sphere + mesh
        REQUIRE(target.numberOfObjects(ANARI_MATERIAL)
            == 3); // default + mesh + override
        REQUIRE(target.numberOfObjects(ANARI_LIGHT) == 1);
        REQUIRE(target.numberOfObjects(ANARI_ARRAY) == 1);

        REQUIRE((*splicedRoot).value().isTransform());
        REQUIRE((*splicedRoot).value().name() == "group");

        // Two children: surface instance + light instance.
        int childCount = 0;
        bool sawSurfaceInstance = false;
        bool sawLight = false;
        vsr::core::Any opacity;
        vsr::core::Any materialOverride;
        targetLayer->traverse(splicedRoot, [&](auto &node, int level) {
          if (level == 1) {
            childCount++;
            auto &d = node.value();
            if (d.name() == "surface_inst") {
              sawSurfaceInstance = true;
              opacity = d.getInstanceParameters().at("opacity")
                  ? *d.getInstanceParameters().at("opacity")
                  : vsr::core::Any();
              materialOverride =
                  d.getInstanceParameters().at("materialOverride")
                  ? *d.getInstanceParameters().at("materialOverride")
                  : vsr::core::Any();
            }
            if (d.name() == "light_inst")
              sawLight = true;
          }
          return true;
        });

        REQUIRE(childCount == 2);
        REQUIRE(sawSurfaceInstance);
        REQUIRE(sawLight);

        // Instance parameters round-trip, with the object-valued one remapped
        // to the freshly created target material (not the source index).
        REQUIRE(opacity.getAs<float>() == 0.5f);
        REQUIRE(materialOverride.holdsObject());
        REQUIRE(materialOverride.type() == ANARI_MATERIAL);
        auto *remapped = target.getObject(materialOverride);
        REQUIRE(remapped);
        REQUIRE(remapped->name() == "override_material");
      }
    }

    WHEN("the subtree Archive is loaded into a fragmented scene")
    {
      REQUIRE(
          vsr::io::save_LayerSubtreeArchive(transformNode, filename.c_str()));

      vsr::scene::Scene target;

      // Create then remove objects to leave holes in the object pools without
      // defragmenting, mimicking a running viewer that has deleted objects.
      auto g0 = target.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);
      auto g1 = target.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);
      auto m0 = target.createObject<vsr::scene::Material>(
          vsr::scene::tokens::material::matte);
      target.removeObject(g0.data());
      target.removeObject(m0.data());

      auto *targetLayer = target.defaultLayer();
      auto destination = target.insertChildTransformNode(
          targetLayer->root(), vsr::math::IDENTITY_MAT4, "mount");

      auto splicedRoot =
          vsr::io::load_LayerSubtreeArchive(destination, filename.c_str());

      THEN("the load succeeds and grafts the subtree")
      {
        REQUIRE(splicedRoot);
        REQUIRE(target.numberOfObjects(ANARI_SURFACE) == 1);
        REQUIRE(target.numberOfObjects(ANARI_LIGHT) == 1);
        REQUIRE((*splicedRoot).value().name() == "group");
      }
    }
  }
}

SCENARIO(
    "vsr::io layer subtree animations round trip", "[ArchiveCompatibility]")
{
  vsr::scene::Scene source;
  vsr::animation::AnimationManager sourceAnimations(&source);

  auto geometry = source.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  geometry->setName("animated_geometry");
  auto material = source.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  auto alternateMaterial = source.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  alternateMaterial->setName("alternate_material");
  auto surface = source.createSurface("animated_surface", geometry, material);
  auto group = source.insertChildTransformNode(source.defaultLayer()->root(),
      vsr::math::IDENTITY_MAT4,
      "animated_group");
  source.insertChildObjectNode(group, surface, "animated_surface");

  const float times[] = {0.f, 1.f};
  const float radii[] = {0.25f, 2.f};
  auto &animation = sourceAnimations.addAnimation("dataset_animation");
  animation.addObjectParameterBinding(
      geometry.data(), "radius", ANARI_FLOAT32, radii, times, 2);
  vsr::scene::Object *materials[] = {material.data(), alternateMaterial.data()};
  animation.addObjectParameterBinding(geometry.data(),
      "animated.material",
      ANARI_MATERIAL,
      materials,
      times,
      2);
  animation.addTransformBinding(group);

  const auto filename = testFile("vsr_layer_subtree_animation.vsr");
  removeTestFile(filename);
  vsr::io::SubtreeArchiveContentOptions saveOptions;
  saveOptions.animationManager = &sourceAnimations;
  REQUIRE(saveSubtreeArchiveContent(filename.c_str(),
      group,
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All},
      {},
      saveOptions));
  vsr::core::DataTree saved;
  REQUIRE(saved.load(filename.c_str()));
  auto *serializedAnimation = saved.root()["animations"].child(0);
  REQUIRE(serializedAnimation);
  auto *serializedMaterialBinding =
      (*serializedAnimation)["objectBindings"].child(1);
  REQUIRE(serializedMaterialBinding);
  REQUIRE((*serializedMaterialBinding)["dataType"].getValueAs<int>()
      == ANARI_MATERIAL);
  anari::DataType serializedMaterialType = ANARI_UNKNOWN;
  const void *serializedMaterialData = nullptr;
  const size_t *serializedMaterialIndices = nullptr;
  size_t serializedMaterialCount = 0;
  (*serializedMaterialBinding)["data"].getValueAsArray(&serializedMaterialType,
      &serializedMaterialData,
      &serializedMaterialCount);
  serializedMaterialIndices =
      static_cast<const size_t *>(serializedMaterialData);
  REQUIRE(serializedMaterialType == ANARI_MATERIAL);
  REQUIRE(serializedMaterialCount == 2);
  REQUIRE(saved.root()["objectDB"]["material"].numChildren() == 2);
  auto savedValidation = vsr::io::validate_SubtreeArchiveContent(saved.root(),
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All});
  INFO(savedValidation.message);
  REQUIRE(savedValidation.accepted());

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  target.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::cylinder);
  auto destination =
      target.insertChildNode(target.defaultLayer()->root(), "destination");
  vsr::io::SubtreeArchiveContentOptions loadOptions;
  loadOptions.animationManager = &targetAnimations;
  auto loadedArchive = loadSubtreeArchiveContent(target,
      filename.c_str(),
      destination,
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All},
      nullptr,
      loadOptions);

  REQUIRE(loadedArchive.root);
  REQUIRE(targetAnimations.animations().size() == 1);
  auto &loadedAnimation = targetAnimations.animations().front();
  REQUIRE(loadedAnimation.name() == "dataset_animation");
  REQUIRE(loadedAnimation.objectParameterBindings().size() == 2);
  REQUIRE(loadedAnimation.objectParameterBindings().front().target());
  REQUIRE(loadedAnimation.objectParameterBindings().front().target()->name()
      == "animated_geometry");
  REQUIRE(loadedAnimation.transformBindings().size() == 1);
  REQUIRE(loadedAnimation.transformBindings().front().target());
  REQUIRE((*loadedAnimation.transformBindings().front().target())->name()
      == "animated_group");
  const auto &materialBinding = loadedAnimation.objectParameterBindings()[1];
  REQUIRE(materialBinding.data().size() == 2);
  const auto *materialIndices =
      static_cast<const size_t *>(materialBinding.data().data());
  REQUIRE(target.getObject(ANARI_MATERIAL, materialIndices[0]));
  REQUIRE(target.getObject(ANARI_MATERIAL, materialIndices[1])->name()
      == "alternate_material");

  removeTestFile(filename);
}

SCENARIO("vsr::io plans subtree archive ownership", "[ArchiveCompatibility]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);

  auto geometry = scene.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  auto material = scene.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  auto surface = scene.createSurface("surface", geometry, material);
  auto dataset = scene.insertChildNode(scene.defaultLayer()->root(), "dataset");
  scene.insertChildObjectNode(dataset, surface, "surface");
  auto outside = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "outside");

  const float times[] = {0.f};
  const float values[] = {1.f};
  animations.addAnimation("owned").addObjectParameterBinding(
      geometry.data(), "radius", ANARI_FLOAT32, values, times, 1);
  animations.addAnimation("outside").addTransformBinding(outside);

  vsr::io::ArchivePlanOptions options;
  options.animationManager = &animations;
  const auto result = vsr::io::plan_SubtreeArchive(scene, dataset, options);

  INFO(result.message);
  REQUIRE(result.accepted());
  REQUIRE(result.plan.nodes.size() == 2);
  REQUIRE(result.plan.objects.size() == 3);
  REQUIRE(result.plan.ownedAnimations == std::vector<size_t>{0});
  REQUIRE(result.plan.archivedAnimations == std::vector<size_t>{0});
  REQUIRE(result.plan.containsObject(geometry.data()));
  REQUIRE_FALSE(result.plan.containsObject(nullptr));
}

SCENARIO("vsr::io archive plans reject mixed animation ownership",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  auto first = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "first");
  auto second = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "second");
  auto &animation = animations.addAnimation("mixed");
  animation.addTransformBinding(first);
  animation.addTransformBinding(second);

  vsr::io::ArchivePlanOptions options;
  options.animationManager = &animations;
  const auto result = vsr::io::plan_SubtreeArchive(scene, first, options);

  REQUIRE_FALSE(result.accepted());
  REQUIRE(result.status == vsr::io::ArchivePlanStatus::MixedAnimationTargets);
  REQUIRE_FALSE(result.message.empty());
}

SCENARIO("vsr::io archive plans reject invalid animation targets",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  auto root = scene.insertChildNode(scene.defaultLayer()->root(), "dataset");
  animations.addAnimation("invalid").addEmptyObjectParameterBinding();

  vsr::io::ArchivePlanOptions options;
  options.animationManager = &animations;
  const auto result = vsr::io::plan_SubtreeArchive(scene, root, options);

  REQUIRE_FALSE(result.accepted());
  REQUIRE(result.status == vsr::io::ArchivePlanStatus::InvalidAnimationTarget);
}

SCENARIO("vsr::io archive plans reject unsupported file bindings",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  auto root = scene.insertChildNode(scene.defaultLayer()->root(), "dataset");
  animations.addAnimation("unsupported")
      .emplaceFileBinding<UnsupportedFileBinding>(&scene);

  vsr::io::ArchivePlanOptions options;
  options.animationManager = &animations;
  options.fileBindings = vsr::io::FileBindingArchivePolicy::Omit;
  const auto result = vsr::io::plan_SubtreeArchive(scene, root, options);

  REQUIRE_FALSE(result.accepted());
  REQUIRE(result.status == vsr::io::ArchivePlanStatus::UnsupportedFileBinding);
}

SCENARIO("vsr::io accepts USD file bindings written before continuous time",
    "[ArchiveCompatibility]")
{
  // The `sampleTimes`/`timeBase` pair was a cache of what the Stage already
  // says, and was dropped when bindings started resolving at a Time Code
  // (ADR 0021). Archives that still carry it must keep validating: the fields
  // are ignored, not rejected, and no format version was bumped for them.
  GIVEN("An Animation Archive whose usdGeometry binding carries the old cache")
  {
    vsr::scene::Scene scene;
    vsr::animation::AnimationManager animations(&scene);
    auto geometry = scene.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::triangle);

    vsr::core::DataTree tree;
    auto &archive = tree.root();
    archive["name"] = std::string("legacy");
    auto &binding = archive["fileBindings"].append();
    binding["kind"] = std::string("usdGeometry");
    binding["targetIndex"] = geometry->index();
    binding["stageFile"] = std::string("/data/blob.usd");
    binding["primPath"] = std::string("/Blob");
    binding["sampleTimes"].append() = 0.f;
    binding["sampleTimes"].append() = 2.f;
    binding["timeBase"].append() = 0.f;
    binding["timeBase"].append() = 1.f;

    THEN("It still validates against the scene")
    {
      std::string message;
      REQUIRE(vsr::io::validate_AnimationArchive(animations, archive, &message));
    }

    THEN("It deserializes, dropping the fields rather than failing on them")
    {
      auto *restored =
          vsr::io::deserialize_AnimationArchive(animations, archive);
      REQUIRE(restored != nullptr);
      REQUIRE(restored->fileBindings().size() == 1);
      REQUIRE(restored->fileBindings()[0]->kind() == "usdGeometry");

      vsr::core::DataTree rewritten;
      restored->fileBindings()[0]->toDataNode(rewritten.root());
      REQUIRE(rewritten.root().child("stageFile") != nullptr);
      REQUIRE(rewritten.root().child("sampleTimes") == nullptr);
      REQUIRE(rewritten.root().child("timeBase") == nullptr);
    }
  }

  GIVEN("An Animation Archive holding a usdInstancer binding")
  {
    vsr::scene::Scene scene;
    vsr::animation::AnimationManager animations(&scene);
    auto transforms = scene.createArray(ANARI_FLOAT32_MAT4, 2);
    auto node = scene.insertChildTransformArrayNode(
        scene.defaultLayer()->root(), transforms.data(), "swarm");

    vsr::core::DataTree tree;
    auto &archive = tree.root();
    archive["name"] = std::string("swarm");
    auto &binding = archive["fileBindings"].append();
    binding["kind"] = std::string("usdInstancer");
    binding["layerName"] = std::string("default");
    binding["nodeIndex"] = node->index();
    binding["stageFile"] = std::string("/data/swarm.usd");
    binding["primPath"] = std::string("/Swarm");
    binding["prototypeIndex"] = uint64_t(0);

    THEN("The kind is a recognized part of the format")
    {
      std::string message;
      REQUIRE(vsr::io::validate_AnimationArchive(animations, archive, &message));
    }
  }
}

SCENARIO("vsr::io scene exclusion rejects mixed animation ownership",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  auto first = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "first");
  auto second = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "second");
  auto &mixed = animations.addAnimation("mixed");
  mixed.addTransformBinding(first);
  mixed.addTransformBinding(second);

  vsr::io::detail::LegacySceneSerializationOptions options;
  options.animationManager = &animations;
  options.exclusion.roots.push_back(first);
  options.exclusion.objectPolicy = vsr::io::ArchiveObjectPolicy::All;
  options.exclusion.animations =
      vsr::io::detail::LegacyExcludedAnimationPolicy::OmitOwned;
  vsr::core::DataTree tree;
  vsr::io::detail::serializeLegacyScenePayload(scene, tree.root(), options);

  bool sawFirst = false;
  bool sawSecond = false;
  tree.root()["layers"].traverse([&](vsr::core::DataNode &node, int) {
    if (node.name() == "name") {
      const auto name = node.getValueOr<std::string>("");
      sawFirst |= name == "first";
      sawSecond |= name == "second";
    }
    return true;
  });
  REQUIRE(sawFirst);
  REQUIRE(sawSecond);
  REQUIRE(tree.root()["animations"]["objects"].numChildren() == 1);
}

SCENARIO("subtree Archive deserialization exposes exact rollback ownership",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene source;
  vsr::animation::AnimationManager sourceAnimations(&source);
  auto geometry = source.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  auto material = source.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  auto surface = source.createSurface("surface", geometry, material);
  auto root = source.insertChildNode(source.defaultLayer()->root(), "dataset");
  source.insertChildObjectNode(root, surface, "surface");
  const float times[] = {0.f};
  const float values[] = {1.f};
  sourceAnimations.addAnimation("radius").addObjectParameterBinding(
      geometry.data(), "radius", ANARI_FLOAT32, values, times, 1);

  const auto filename = testFile("vsr_subtree_archive_ownership.vsr");
  vsr::io::SubtreeArchiveContentOptions saveOptions;
  saveOptions.animationManager = &sourceAnimations;
  REQUIRE(saveSubtreeArchiveContent(filename.c_str(),
      root,
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All},
      {},
      saveOptions));

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  vsr::io::SubtreeArchiveContentOptions loadOptions;
  loadOptions.animationManager = &targetAnimations;
  auto loaded = loadSubtreeArchiveContent(target,
      filename.c_str(),
      target.defaultLayer()->root(),
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All},
      nullptr,
      loadOptions);

  REQUIRE(loaded.valid());
  REQUIRE(loaded.root);
  REQUIRE(loaded.createdObjects.size() == 3);
  REQUIRE(loaded.createdAnimations == std::vector<size_t>{0});

  vsr::io::rollback_SubtreeArchiveContent(target, targetAnimations, loaded);
  REQUIRE_FALSE(loaded.valid());
  REQUIRE(targetAnimations.animations().empty());
  REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 0);
  REQUIRE(target.numberOfObjects(ANARI_MATERIAL) == 1); // Scene default
  REQUIRE(target.numberOfObjects(ANARI_SURFACE) == 0);

  removeTestFile(filename);
}

SCENARIO("vsr::io rejects animations spanning layer subtrees",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  auto first = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "first");
  auto second = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "second");
  auto &animation = animations.addAnimation("cross-dataset");
  animation.addTransformBinding(first);
  animation.addTransformBinding(second);

  vsr::io::SubtreeArchiveContentOptions options;
  options.animationManager = &animations;
  const auto filename = testFile("vsr_cross_subtree_animation.vsr");
  removeTestFile(filename);

  REQUIRE_FALSE(saveSubtreeArchiveContent(filename.c_str(),
      first,
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All},
      {},
      options));
  REQUIRE_FALSE(std::filesystem::exists(filename));
}

SCENARIO("vsr::io validates layer subtree animation targets",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  auto geometry = scene.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  auto material = scene.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  auto surface = scene.createSurface("surface", geometry, material);
  auto root = scene.insertChildNode(scene.defaultLayer()->root(), "dataset");
  scene.insertChildObjectNode(root, surface, "surface");
  const float times[] = {0.f};
  const float values[] = {1.f};
  animations.addAnimation("radius").addObjectParameterBinding(
      geometry.data(), "radius", ANARI_FLOAT32, values, times, 1);

  const auto filename = testFile("vsr_invalid_subtree_animation.vsr");
  vsr::io::SubtreeArchiveContentOptions options;
  options.animationManager = &animations;
  REQUIRE(saveSubtreeArchiveContent(filename.c_str(),
      root,
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All},
      {},
      options));

  vsr::core::DataTree tree;
  REQUIRE(tree.load(filename.c_str()));
  auto *animation = tree.root()["animations"].child(0);
  REQUIRE(animation);
  auto *binding = (*animation)["objectBindings"].child(0);
  REQUIRE(binding);
  (*binding)["targetIndex"] = size_t(999);
  auto validation = vsr::io::validate_SubtreeArchiveContent(tree.root(),
      {"layer-subtree",
          vsr::io::schema::LAYER_SUBTREE,
          vsr::io::ArchiveObjectPolicy::All});
  REQUIRE_FALSE(validation.accepted());
  REQUIRE_FALSE(validation.message.empty());

  removeTestFile(filename);
}

SCENARIO("legacy project payloads exclude light-rig subtrees",
    "[ArchiveCompatibility]")
{
  GIVEN("A scene with a retained surface and an excluded light-rig subtree")
  {
    vsr::scene::Scene source;

    auto positions = makeFloatArray(source, "positions", {0.f, 1.f, 2.f});
    auto geometry = source.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::triangle);
    geometry->setName("mesh_geometry");
    geometry->setParameterObject("vertex.position", *positions);
    auto material = source.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = source.createSurface("mesh_surface", geometry, material);

    auto *layer = source.addLayer("studio");
    auto datasets = source.insertChildNode(layer->root(), "datasets");
    source.insertChildObjectNode(datasets, surface, "surface_inst");

    auto rigsRoot = source.insertChildNode(layer->root(), "lightRigs");
    auto rigRoot = source.insertChildNode(rigsRoot, "rig0");
    auto light = source.createObject<vsr::scene::Light>("directional");
    light->setName("key_light");
    source.insertChildObjectNode(rigRoot, light, "key_light");

    WHEN("the scene is saved with the rig subtree excluded and reloaded")
    {
      vsr::io::detail::LegacySceneSerializationOptions options;
      options.exclusion.roots.push_back(rigRoot);

      vsr::core::DataTree tree;
      vsr::io::detail::serializeLegacyScenePayload(
          source, tree.root(), options);

      THEN("the manifest omits the light pool and the rig subtree node")
      {
        auto *objectDB = tree.root().child("objectDB");
        REQUIRE(objectDB);
        REQUIRE(objectDB->child("light") == nullptr);

        auto *rigsNode =
            tree.root()["layers"].child("studio")->child("children");
        REQUIRE(rigsNode);
        // lightRigs container is retained but its rig children are pruned.
        bool sawRig = false;
        rigsNode->foreach_child([&](vsr::core::DataNode &n) {
          if (n["name"].getValueOr<std::string>("") == "lightRigs") {
            if (auto *kids = n.child("children"))
              sawRig = kids->numChildren() > 0;
          }
        });
        REQUIRE_FALSE(sawRig);
      }

      THEN("the retained surface and its array survive the round trip")
      {
        vsr::scene::Scene target;
        vsr::io::detail::tryDeserializeLegacyScenePayload(target, tree.root());

        REQUIRE(target.numberOfObjects(ANARI_LIGHT) == 0);
        REQUIRE(target.numberOfObjects(ANARI_SURFACE) == 1);
        REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 1);
        REQUIRE(target.numberOfObjects(ANARI_ARRAY) == 1);

        auto *targetGeometry = static_cast<vsr::scene::Geometry *>(
            target.getObject(ANARI_GEOMETRY, 0));
        REQUIRE(targetGeometry);
        auto *ref = targetGeometry->parameterValueAsObject<vsr::scene::Array>(
            "vertex.position");
        REQUIRE(ref != nullptr);
        REQUIRE(ref->type() == ANARI_ARRAY1D);
      }
    }
  }

  GIVEN("A light-only array and a shared array referenced by a retained object")
  {
    vsr::scene::Scene source;

    // Retained geometry referencing its own array.
    auto positions = makeFloatArray(source, "positions", {0.f, 1.f, 2.f});
    auto geometry = source.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::triangle);
    geometry->setParameterObject("vertex.position", *positions);
    auto material = source.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = source.createSurface("mesh_surface", geometry, material);

    // An array referenced only by the (excluded) light.
    auto lightOnly = makeFloatArray(source, "light_only", {9.f});
    // An array shared between the excluded light and the retained geometry.
    auto shared = makeFloatArray(source, "shared", {1.f, 2.f});
    geometry->setParameterObject("primitive.radius", *shared);

    auto *layer = source.addLayer("studio");
    source.insertChildObjectNode(layer->root(), surface, "surface_inst");

    auto rigRoot = source.insertChildNode(layer->root(), "rig0");
    auto light = source.createObject<vsr::scene::Light>("directional");
    light->setParameterObject("rig.lightOnly", *lightOnly);
    light->setParameterObject("rig.shared", *shared);
    source.insertChildObjectNode(rigRoot, light, "key_light");

    WHEN("the scene is saved with the rig excluded and reloaded")
    {
      vsr::io::detail::LegacySceneSerializationOptions options;
      options.exclusion.roots.push_back(rigRoot);

      vsr::core::DataTree tree;
      vsr::io::detail::serializeLegacyScenePayload(
          source, tree.root(), options);

      vsr::scene::Scene target;
      vsr::io::detail::tryDeserializeLegacyScenePayload(target, tree.root());

      THEN("the light-only array is dropped but the shared array is kept")
      {
        REQUIRE(target.numberOfObjects(ANARI_LIGHT) == 0);
        // positions + shared survive; light_only is dropped.
        REQUIRE(target.numberOfObjects(ANARI_ARRAY) == 2);

        auto *targetGeometry = static_cast<vsr::scene::Geometry *>(
            target.getObject(ANARI_GEOMETRY, 0));
        REQUIRE(targetGeometry);
        REQUIRE(targetGeometry->parameterValueAsObject<vsr::scene::Array>(
                    "vertex.position")
            != nullptr);
        REQUIRE(targetGeometry->parameterValueAsObject<vsr::scene::Array>(
                    "primitive.radius")
            != nullptr);
      }
    }
  }
}

SCENARIO("legacy project payloads remap animations across exclusion",
    "[ArchiveCompatibility]")
{
  GIVEN("Animations whose targets shift when a light rig is excluded")
  {
    vsr::scene::Scene source;
    vsr::animation::AnimationManager animMgr(&source);

    // Array pool order: lightOnly @0 (excluded), positions @1 (retained, so it
    // shifts to @0 on reload). Exercises the object-index remap.
    auto lightOnly = makeFloatArray(source, "lightOnly", {9.f});
    auto positions = makeFloatArray(source, "positions", {0.f, 1.f, 2.f});

    auto geometry = source.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::triangle);
    geometry->setParameterObject("vertex.position", *positions);
    auto material = source.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = source.createSurface("surf", geometry, material);

    auto light = source.createObject<vsr::scene::Light>("directional");
    light->setParameterObject("rig.env", *lightOnly);

    // Layer order: excluded rig FIRST, retained "group" AFTER it, so group's
    // layer-node index shifts down on reload. Exercises the layer-node remap.
    auto *layer = source.addLayer("studio");
    auto rig0 = source.insertChildNode(layer->root(), "rig0");
    source.insertChildObjectNode(rig0, light, "key");
    auto group = source.insertChildTransformNode(
        layer->root(), vsr::math::IDENTITY_MAT4, "group");
    source.insertChildObjectNode(group, surface, "surf_inst");

    auto &anim = animMgr.addAnimation("test");
    const float timeBase[2] = {0.f, 1.f};
    const float scalarData[2] = {0.f, 1.f};
    anim.addObjectParameterBinding(
        positions.data(), "scale", ANARI_FLOAT32, scalarData, timeBase, 2);
    anim.addTransformBinding(group);

    WHEN("the scene is saved with the rig excluded and reloaded")
    {
      vsr::io::detail::LegacySceneSerializationOptions options;
      options.animationManager = &animMgr;
      options.exclusion.roots.push_back(rig0);

      vsr::core::DataTree tree;
      vsr::io::detail::serializeLegacyScenePayload(
          source, tree.root(), options);

      vsr::scene::Scene target;
      vsr::animation::AnimationManager targetMgr(&target);
      vsr::io::detail::tryDeserializeLegacyScenePayload(
          target, tree.root(), nullptr, &targetMgr);

      THEN("binding targets resolve to the correct shifted objects/nodes")
      {
        REQUIRE(target.numberOfObjects(ANARI_LIGHT) == 0);
        REQUIRE(target.numberOfObjects(ANARI_ARRAY) == 1); // lightOnly dropped
        REQUIRE(targetMgr.animations().size() == 1);

        auto &loaded = targetMgr.animations().front();
        REQUIRE(loaded.objectParameterBindings().size() == 1);
        // Without the remap, targetIndex would stay @1 and resolve to nullptr.
        REQUIRE(loaded.objectParameterBindings().front().target() != nullptr);

        REQUIRE(loaded.transformBindings().size() == 1);
        auto tbTarget = loaded.transformBindings().front().target();
        REQUIRE(tbTarget);
        REQUIRE((*tbTarget)->name() == "group");
      }
    }
  }
}

SCENARIO("vsr::io scene exclusion preserves retained animation dependencies",
    "[ArchiveCompatibility]")
{
  vsr::scene::Scene source;
  vsr::animation::AnimationManager animations(&source);

  auto removedMaterial = source.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  removedMaterial->setName("removed");
  auto dependencyMaterial = source.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  dependencyMaterial->setName("dependency");
  auto retainedMaterial = source.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  retainedMaterial->setName("retained");

  auto removedGeometry = source.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  auto dependencyGeometry = source.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  auto retainedGeometry = source.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  auto removedSurface =
      source.createSurface("removed", removedGeometry, removedMaterial);
  auto dependencySurface = source.createSurface(
      "dependency", dependencyGeometry, dependencyMaterial);
  auto retainedSurface =
      source.createSurface("retained", retainedGeometry, retainedMaterial);

  auto excluded =
      source.insertChildNode(source.defaultLayer()->root(), "excluded");
  source.insertChildObjectNode(excluded, removedSurface, "removed");
  source.insertChildObjectNode(excluded, dependencySurface, "dependency");
  source.insertChildObjectNode(
      source.defaultLayer()->root(), retainedSurface, "retained");

  const float times[] = {0.f, 1.f};
  vsr::scene::Object *keyframes[] = {
      dependencyMaterial.data(), retainedMaterial.data()};
  animations.addAnimation("retained animation")
      .addObjectParameterBinding(retainedGeometry.data(),
          "material",
          ANARI_MATERIAL,
          keyframes,
          times,
          2);

  vsr::io::detail::LegacySceneSerializationOptions options;
  options.animationManager = &animations;
  options.exclusion.roots.push_back(excluded);
  options.exclusion.objectPolicy = vsr::io::ArchiveObjectPolicy::All;
  options.exclusion.animations =
      vsr::io::detail::LegacyExcludedAnimationPolicy::OmitOwned;
  vsr::core::DataTree tree;
  vsr::io::detail::serializeLegacyScenePayload(source, tree.root(), options);

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  vsr::io::detail::tryDeserializeLegacyScenePayload(
      target, tree.root(), nullptr, &targetAnimations);

  REQUIRE(target.numberOfObjects(ANARI_SURFACE) == 1);
  REQUIRE(target.numberOfObjects(ANARI_MATERIAL) == 3);
  REQUIRE(targetAnimations.animations().size() == 1);
  const auto &binding =
      targetAnimations.animations().front().objectParameterBindings().front();
  const auto *indices = static_cast<const size_t *>(binding.data().data());
  REQUIRE(binding.data().size() == 2);
  REQUIRE(target.getObject(ANARI_MATERIAL, indices[0])->name() == "dependency");
  REQUIRE(target.getObject(ANARI_MATERIAL, indices[1])->name() == "retained");
}
