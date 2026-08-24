// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#define ANARI_EXTENSION_UTILITY_IMPL

#include "Context.h"
// vsr_animation
#include "vsr/animation/Animation.hpp"
// vsr_core
#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"
#include "vsr/io/importers/detail/importer_common.hpp"
#include "vsr/io/procedural.hpp"
#include "vsr/io/serialization/Object.hpp"
// std
#include <stdexcept>

namespace vsr::app {

namespace {

bool hasSceneArchiveLoad(const CommandLineOptions &commandLine)
{
  for (const auto &input : commandLine.sceneInputs) {
    if (std::holds_alternative<SceneArchiveLoad>(input))
      return true;
  }
  return false;
}

} // namespace

VSRState::VSRState() : animationMgr(&scene) {}

Context::Context() : anari(&m_logging.verbose) {}

Context::~Context()
{
  anari.releaseAllDevices();
}

void Context::parseCommandLine(int argc, const char **argv)
{
  std::vector<std::string> args(argv, argv + argc);
  parseCommandLine(args);
}

void Context::parseCommandLine(std::vector<std::string> &args)
{
  auto &importerType = this->commandLine.importerType;

  for (int i = 1; i < args.size(); i++) {
    std::string &arg = args[i];
    if (arg.empty())
      continue;

    if (arg == "-v" || arg == "--verbose")
      setLogVerbose(true);
    else if (arg == "-e" || arg == "--echoOutput")
      setLogEchoOutput(true);
    else if (arg == "--openUnloaded")
      this->commandLine.openUnloaded = true;
    else if (arg == "-l" || arg == "--layer")
      this->commandLine.currentLayerName = args[++i];
    else if (arg == "-vsr") {
      importerType = vsr::io::ImporterType::NONE;
      if (this->commandLine.loadedFromStateFile) {
        throw std::runtime_error(
            "A Scene Archive cannot be combined with an application state file");
      }
      if (hasSceneArchiveLoad(this->commandLine))
        throw std::runtime_error("Only one Scene Archive may be specified");
      if (++i >= args.size())
        throw std::runtime_error("A Scene Archive filename must follow -vsr");
      this->commandLine.sceneInputs.push_back(SceneArchiveLoad{args[i]});
    } else if (arg == "-agx")
      importerType = vsr::io::ImporterType::AGX;
    else if (arg == "-assimp")
      importerType = vsr::io::ImporterType::ASSIMP;
    else if (arg == "-assimp_flat")
      importerType = vsr::io::ImporterType::ASSIMP_FLAT;
    else if (arg == "-axyz")
      importerType = vsr::io::ImporterType::AXYZ;
    else if (arg == "-dlaf")
      importerType = vsr::io::ImporterType::DLAF;
    else if (arg == "-e57xyz")
      importerType = vsr::io::ImporterType::E57XYZ;
    else if (arg == "-ensight")
      importerType = vsr::io::ImporterType::ENSIGHT;
    else if (arg == "-ensight_fields") {
      this->commandLine.ensightFields.clear();
      auto tokens = vsr::io::splitString(args[++i], ',');
      for (auto &t : tokens)
        if (!t.empty())
          this->commandLine.ensightFields.push_back(t);
    } else if (arg == "-gltf")
      importerType = vsr::io::ImporterType::GLTF;
    else if (arg == "-hdri")
      importerType = vsr::io::ImporterType::HDRI;
    else if (arg == "-hsmesh")
      importerType = vsr::io::ImporterType::HSMESH;
    else if (arg == "-nbody")
      importerType = vsr::io::ImporterType::NBODY;
    else if (arg == "-obj")
      importerType = vsr::io::ImporterType::OBJ;
    else if (arg == "-pdb")
      importerType = vsr::io::ImporterType::PDB;
    else if (arg == "-pbrt")
      importerType = vsr::io::ImporterType::PBRT;
    else if (arg == "-ply")
      importerType = vsr::io::ImporterType::PLY;
    else if (arg == "-pointsbin") {
      this->commandLine.currentAnimationSequence = nullptr; // reset to new seq
      importerType = vsr::io::ImporterType::POINTSBIN_MULTIFILE;
    } else if (arg == "-pt")
      importerType = vsr::io::ImporterType::PT;
    else if (arg == "-silo")
      importerType = vsr::io::ImporterType::SILO;
    else if (arg == "-smesh")
      importerType = vsr::io::ImporterType::SMESH;
    else if (arg == "-smesh_animation")
      importerType = vsr::io::ImporterType::SMESH_ANIMATION;
    else if (arg == "-swc")
      importerType = vsr::io::ImporterType::SWC;
    else if (arg == "-swc_sdf")
      importerType = vsr::io::ImporterType::SWC_SDF;
    else if (arg == "-trk")
      importerType = vsr::io::ImporterType::TRK;
    else if (arg == "-usd")
      importerType = vsr::io::ImporterType::USD;
    else if (arg == "-usd_mtlx")
      importerType = vsr::io::ImporterType::USD_MTLX;
    else if (arg == "-vtp")
      importerType = vsr::io::ImporterType::VTP;
    else if (arg == "-vtu")
      importerType = vsr::io::ImporterType::VTU;
    else if (arg == "-vtu_property")
      this->commandLine.vtuProperty = args[++i];
    else if (arg == "-xyzdp")
      importerType = vsr::io::ImporterType::XYZDP;
    else if (arg == "-volume")
      importerType = vsr::io::ImporterType::VOLUME;
    else if (arg == "-volume_animation") {
      this->commandLine.currentAnimationSequence = nullptr; // reset to new seq
      importerType = vsr::io::ImporterType::VOLUME_ANIMATION;
    } else if (arg == "-blank")
      importerType = vsr::io::ImporterType::BLANK;
    else if (arg == "-xf" || arg == "--transferFunction")
      importerType = vsr::io::ImporterType::XF;
    else if (arg == "-camera" || arg == "--camera")
      this->commandLine.cameraFile = args[++i];
    else {
      if (importerType != vsr::io::ImporterType::NONE) {
        if (importerType == vsr::io::ImporterType::POINTSBIN_MULTIFILE
            || importerType == vsr::io::ImporterType::VOLUME_ANIMATION) {
          if (!this->commandLine.currentAnimationSequence) {
            this->commandLine.animationFilenames.push_back({importerType, {}});
            this->commandLine.currentAnimationSequence =
                &this->commandLine.animationFilenames.back();
            this->commandLine.animationLayerNames.push_back(
                this->commandLine.currentLayerName);
          }
          auto file = arg;
          if (importerType == vsr::io::ImporterType::VOLUME_ANIMATION
              && !this->commandLine.vtuProperty.empty())
            file += ';' + this->commandLine.vtuProperty;
          this->commandLine.currentAnimationSequence->second.push_back(file);
        } else {
          auto file = arg + ';' + this->commandLine.currentLayerName;
          if (importerType == vsr::io::ImporterType::VTU
              && !this->commandLine.vtuProperty.empty())
            file += ';' + this->commandLine.vtuProperty;
          this->commandLine.sceneInputs.push_back(
              ForeignSceneImport{{importerType, file}});
          this->commandLine.currentAnimationSequence = nullptr;
        }
      } else {
        if (hasSceneArchiveLoad(this->commandLine)) {
          throw std::runtime_error(
              "A Scene Archive cannot be combined with an application state file");
        }
        this->commandLine.stateFile = arg;
        this->commandLine.loadedFromStateFile = true;
      }
    }
  }

  this->commandLine.currentAnimationSequence = nullptr;
}

bool Context::loadCommandLineSceneInputs()
{
  bool success = true;
  std::vector<vsr::io::ImportFile> foreignImports;

  for (const auto &input : commandLine.sceneInputs) {
    if (const auto *archive = std::get_if<SceneArchiveLoad>(&input)) {
      if (!vsr::io::load_SceneArchive(vsr.scene, archive->filename.c_str())) {
        vsr::core::logError("[Context] Failed to load Scene Archive '%s'",
            archive->filename.c_str());
        success = false;
      }
    } else {
      foreignImports.push_back(std::get<ForeignSceneImport>(input).file);
    }
  }

  vsr::io::import_files(vsr.scene, vsr.animationMgr, foreignImports);
  return success;
}

void Context::setupSceneFromCommandLine(bool hdriOnly)
{
  if (hdriOnly) {
    for (const auto &input : commandLine.sceneInputs) {
      const auto *foreignImport = std::get_if<ForeignSceneImport>(&input);
      if (!foreignImport)
        continue;
      const auto &file = foreignImport->file;
      vsr::core::logStatus("...loading file '%s'", file.second.c_str());
      if (file.first == vsr::io::ImporterType::HDRI)
        vsr::io::import_HDRI(vsr.scene, vsr.animationMgr, file.second.c_str());
    }
    return;
  }

  const bool haveFiles = !commandLine.sceneInputs.empty()
      || commandLine.animationFilenames.size() > 0;
  const bool blankImport =
      !haveFiles && commandLine.importerType == vsr::io::ImporterType::BLANK;
  const bool loadFromState = commandLine.loadedFromStateFile;

  const bool generateDefaultScene =
      !(blankImport || haveFiles || loadFromState);

  if (generateDefaultScene) {
    vsr::core::logStatus("...generating default icosphere scene");
    vsr::io::generate_icosphere(vsr.scene);
  } else if (!loadFromState) {
    loadCommandLineSceneInputs();
    vsr::io::import_animations(
        vsr.scene, vsr.animationMgr, commandLine.animationFilenames);
  }
}

bool Context::logVerbose() const
{
  return m_logging.verbose;
}

void Context::setLogVerbose(bool v)
{
  m_logging.verbose = v;
}

bool Context::logEchoOutput() const
{
  return m_logging.echoOutput;
}

void Context::setLogEchoOutput(bool v)
{
  m_logging.echoOutput = v;
}

void Context::setOfflineRenderingLibrary(const std::string &libName)
{
  auto &dm = this->anari;
  auto d = dm.loadDevice(libName);
  if (!d) {
    vsr::core::logError(
        "[Context] Failed to load ANARI device for offline rendering: %s",
        libName.c_str());
    return;
  }

  this->offline.renderer.rendererObjects.clear();
  this->offline.renderer.activeRenderer = 0;
  this->offline.renderer.libraryName = libName;

  for (auto &name : vsr::scene::getANARIObjectSubtypes(d, ANARI_RENDERER)) {
    auto o = vsr::scene::parseANARIObjectInfo(d, ANARI_RENDERER, name.c_str());
    o.setName(name.c_str());
    this->offline.renderer.rendererObjects.push_back(std::move(o));
  }

  anari::release(d, d);
}

vsr::scene::LayerNodeRef Context::getFirstSelected() const
{
  return vsr.selectedNodes.empty() ? vsr::scene::LayerNodeRef{}
                                   : vsr.selectedNodes[0];
}

const std::vector<vsr::scene::LayerNodeRef> &Context::getSelectedNodes() const
{
  return vsr.selectedNodes;
}

void Context::setSelected(vsr::scene::LayerNodeRef node)
{
  setSelected(std::vector<vsr::scene::LayerNodeRef>{
      node.valid() ? node : vsr::scene::LayerNodeRef{}});
}

void Context::setSelected(const std::vector<vsr::scene::LayerNodeRef> &nodes)
{
  vsr.selectedNodes = nodes;
  vsr.scene.updateDelegate().signalObjectFilteringChanged();
}

void Context::setSelected(const vsr::scene::Object *obj)
{
  if (!obj) {
    clearSelected();
    return;
  }

  // Search all layers for first node referencing this object
  const auto &layers = vsr.scene.layers();
  for (auto &&[layerTk, state] : layers) {
    auto layer = state.ptr.get();
    vsr::scene::LayerNodeRef foundNode;
    layer->traverse_const(layer->root(), [&](const auto &node, int level) {
      if (foundNode.valid())
        return false;
      if (level > 0) {
        auto *nodeObj = node->getObject();
        if (nodeObj == obj) {
          foundNode = layer->at(node.index());
          return false;
        }
      }
      return true;
    });

    if (foundNode.valid()) {
      vsr::core::logStatus(
          "[selection] Selected object %s[%zu] as node %zu on layer %s",
          obj->name().c_str(),
          obj->index(),
          foundNode.index(),
          layerTk);
      setSelected(foundNode);
      return;
    }
  }

  vsr::core::logStatus(
      "[selection] Object not found in any layer, clearing selection");
  clearSelected();
}

void Context::addToSelection(vsr::scene::LayerNodeRef node)
{
  if (!node.valid())
    return;

  for (const auto &selected : vsr.selectedNodes) {
    if (selected == node)
      return;
  }

  vsr.selectedNodes.push_back(node);
  vsr.scene.updateDelegate().signalObjectFilteringChanged();
}

void Context::removeFromSelection(vsr::scene::LayerNodeRef node)
{
  auto it = std::find(vsr.selectedNodes.begin(), vsr.selectedNodes.end(), node);
  if (it != vsr.selectedNodes.end()) {
    vsr.selectedNodes.erase(it);
    vsr.scene.updateDelegate().signalObjectFilteringChanged();
  }
}

bool Context::isSelected(vsr::scene::LayerNodeRef node) const
{
  return std::find(vsr.selectedNodes.begin(), vsr.selectedNodes.end(), node)
      != vsr.selectedNodes.end();
}

void Context::clearSelected()
{
  if (!vsr.selectedNodes.empty()) {
    vsr.selectedNodes.clear();
    vsr.scene.updateDelegate().signalObjectFilteringChanged();
  }
}
std::vector<vsr::scene::LayerNodeRef> Context::getParentOnlySelectedNodes()
    const
{
  std::vector<vsr::scene::LayerNodeRef> parentOnly;

  for (const auto &node : vsr.selectedNodes) {
    if (!node.valid())
      continue;

    bool isChildOfSelected = false;

    // Check if any other selected node is an ancestor of this node
    for (const auto &potentialParent : vsr.selectedNodes) {
      if (!potentialParent.valid() || potentialParent == node)
        continue;

      auto current = node;
      while (current.valid()) {
        auto parentRef = current->parent();
        if (!parentRef.valid())
          break;

        if (parentRef == potentialParent) {
          isChildOfSelected = true;
          break;
        }

        current = parentRef;
      }

      if (isChildOfSelected)
        break;
    }

    if (!isChildOfSelected)
      parentOnly.push_back(node);
  }

  return parentOnly;
}
void Context::addCurrentViewToCameraPoses(const char *_name)
{
  auto azel = view.manipulator.azel();
  auto dist = view.manipulator.distance();
  vsr::math::float3 azeldist(azel.x, azel.y, dist);

  std::string name = _name;
  if (name.empty())
    name = "user_view" + std::to_string(view.poses.size());

  CameraPose pose;
  pose.name = name;
  pose.lookat = view.manipulator.at();
  pose.azeldist = azeldist;
  pose.fixedDist = view.manipulator.fixedDistance();
  pose.upAxis = static_cast<int>(view.manipulator.axis());
  pose.mode = static_cast<int>(view.manipulator.mode());

  view.poses.push_back(std::move(pose));
}

void Context::addTurntableCameraPoses(const vsr::math::float3 &azs,
    const vsr::math::float3 &els,
    const vsr::math::float3 &center,
    float dist,
    const char *_name)
{
  if (azs.z <= 0.f || els.z <= 0.f) {
    vsr::core::logError("invalid turntable azimuth/elevation step size");
    return;
  }

  std::string baseName = _name;
  if (baseName.empty())
    baseName = "turntable_view";

  int j = 0;
  for (float el = els.x; el <= els.y; el += els.z, j++) {
    int i = 0;
    for (float az = azs.x; az <= azs.y; az += azs.z, i++) {
      CameraPose pose;
      pose.name = baseName + "_" + std::to_string(i) + "_" + std::to_string(j);
      pose.lookat = center;
      pose.azeldist = {az, el, dist};
      pose.fixedDist = view.manipulator.fixedDistance();
      pose.upAxis = static_cast<int>(view.manipulator.axis());
      pose.mode = static_cast<int>(view.manipulator.mode());
      view.poses.push_back(std::move(pose));
    }
  }
}

void Context::updateExistingCameraPoseFromView(CameraPose &p)
{
  auto azel = view.manipulator.azel();
  auto dist = view.manipulator.distance();
  vsr::math::float3 azeldist(azel.x, azel.y, dist);

  p.lookat = view.manipulator.at();
  p.azeldist = azeldist;
  p.fixedDist = view.manipulator.fixedDistance();
  p.upAxis = static_cast<int>(view.manipulator.axis());
  p.mode = static_cast<int>(view.manipulator.mode());
}

bool Context::updateCameraPathAnimation()
{
  auto &scene = vsr.scene;

  if (view.poses.size() < 2) {
    vsr::core::logWarning(
        "[camera path] Need at least 2 poses to build animation");
    return false;
  }

  size_t cameraIndex = view.cameraPathCameraIndex;
  if (cameraIndex == VSR_INVALID_INDEX)
    cameraIndex = offline.camera.cameraIndex;

  auto camera = scene.getObject<vsr::scene::Camera>(cameraIndex);
  if (!camera) {
    vsr::core::logWarning("[camera path] No camera selected for animation");
    return false;
  }

  std::vector<vsr::rendering::CameraPose> samples;
  vsr::rendering::buildCameraPathSamples(
      view.poses, view.pathSettings, samples);

  if (samples.empty()) {
    vsr::core::logWarning("[camera path] No samples generated");
    return false;
  }

  offline.frame.numFrames = static_cast<int>(samples.size());
  if (offline.frame.renderSubset) {
    offline.frame.startFrame =
        std::clamp(offline.frame.startFrame, 0, offline.frame.numFrames - 1);
    offline.frame.endFrame =
        std::clamp(offline.frame.endFrame, 0, offline.frame.numFrames - 1);
  }

  // Remove existing camera path animation by name
  auto &anims = vsr.animationMgr.animations();
  for (size_t i = 0; i < anims.size(); i++) {
    if (anims[i].name() == view.cameraPathAnimationName) {
      vsr.animationMgr.removeAnimation(i);
      break;
    }
  }

  view.cameraPathAnimationName = "camera_path";

  // Build time base: linear 0..1
  std::vector<float> timeBase(samples.size());
  for (size_t i = 0; i < samples.size(); i++)
    timeBase[i] = static_cast<float>(i) / (samples.size() - 1);

  std::vector<vsr::math::float3> positions(samples.size());
  std::vector<vsr::math::float3> directions(samples.size());
  std::vector<vsr::math::float3> ups(samples.size());

  vsr::rendering::Manipulator tempManipulator;
  for (size_t i = 0; i < samples.size(); ++i) {
    tempManipulator.setConfig(samples[i]);
    positions[i] = tempManipulator.eye();
    directions[i] = tempManipulator.dir();
    ups[i] = tempManipulator.up();
  }

  const auto firstPosition = positions[0];
  const auto firstDirection = directions[0];
  const auto firstUp = ups[0];

  auto &anim = vsr.animationMgr.addAnimation(view.cameraPathAnimationName);
  anim.addObjectParameterBinding(camera.data(),
      "position",
      ANARI_FLOAT32_VEC3,
      positions.data(),
      timeBase.data(),
      samples.size());
  anim.addObjectParameterBinding(camera.data(),
      "direction",
      ANARI_FLOAT32_VEC3,
      directions.data(),
      timeBase.data(),
      samples.size());
  anim.addObjectParameterBinding(camera.data(),
      "up",
      ANARI_FLOAT32_VEC3,
      ups.data(),
      timeBase.data(),
      samples.size());

  // Seed camera parameters with the first sample for immediate feedback
  camera->setParameter("position", firstPosition);
  camera->setParameter("direction", firstDirection);
  camera->setParameter("up", firstUp);

  vsr::core::logStatus(
      "[camera path] Built animation with %zu samples for camera '%s'",
      samples.size(),
      camera->name().c_str());
  return true;
}

void Context::setCameraPose(const CameraPose &pose)
{
  view.manipulator.setConfig(pose);
  view.manipulator.setFixedDistance(pose.fixedDist);
}

void Context::removeAllPoses()
{
  view.poses.clear();
  if (!view.cameraPathAnimationName.empty()) {
    vsr::core::logStatus("[camera path] Clearing camera path animation");
    auto &anims = vsr.animationMgr.animations();
    for (size_t i = 0; i < anims.size(); i++) {
      if (anims[i].name() == view.cameraPathAnimationName) {
        vsr.animationMgr.removeAnimation(i);
        break;
      }
    }
    view.cameraPathAnimationName.clear();
  }
}

void OfflineRenderSequenceConfig::saveSettings(vsr::core::DataNode &root) const
{
  root.reset(); // clear all previous values, if they exist

  auto &frameRoot = root["frame"];
  frameRoot["width"] = frame.width;
  frameRoot["height"] = frame.height;
  frameRoot["colorFormat"] = static_cast<int>(frame.colorFormat);
  frameRoot["samples"] = frame.samples;
  frameRoot["numFrames"] = frame.numFrames;
  frameRoot["renderSubset"] = frame.renderSubset;
  frameRoot["startFrame"] = frame.startFrame;
  frameRoot["endFrame"] = frame.endFrame;
  frameRoot["frameIncrement"] = frame.frameIncrement;

  auto &cameraRoot = root["camera"];
  cameraRoot["apertureRadius"] = camera.apertureRadius;
  cameraRoot["focusDistance"] = camera.focusDistance;
  cameraRoot["cameraIndex"] = camera.cameraIndex;

  auto &rendererRoot = root["renderer"];
  rendererRoot["activeRenderer"] = renderer.activeRenderer;
  rendererRoot["libraryName"] = renderer.libraryName;

  auto &rendererObjectsRoot = rendererRoot["rendererObjects"];
  for (const auto &ro : renderer.rendererObjects)
    vsr::io::serialize_Object(ro, rendererObjectsRoot[ro.name()]);

  auto &outputRoot = root["output"];
  outputRoot["outputDirectory"] = output.outputDirectory;
  outputRoot["filePrefix"] = output.filePrefix;

  auto &aovRoot = root["aov"];
  aovRoot["aovType"] = static_cast<int>(aov.aovType);
  aovRoot["depthMin"] = aov.depthMin;
  aovRoot["depthMax"] = aov.depthMax;
  aovRoot["edgeInvert"] = aov.edgeInvert;
}

void OfflineRenderSequenceConfig::loadSettings(vsr::core::DataNode &root)
{
  auto &frameRoot = root["frame"];
  frameRoot["width"].getValue(ANARI_UINT32, &frame.width);
  frameRoot["height"].getValue(ANARI_UINT32, &frame.height);
  int colorFormat = static_cast<int>(frame.colorFormat);
  if (frameRoot["colorFormat"].getValue(ANARI_INT32, &colorFormat))
    frame.colorFormat = static_cast<anari::DataType>(colorFormat);
  frameRoot["samples"].getValue(ANARI_UINT32, &frame.samples);
  frameRoot["numFrames"].getValue(ANARI_INT32, &frame.numFrames);
  frameRoot["renderSubset"].getValue(ANARI_BOOL, &frame.renderSubset);
  frameRoot["startFrame"].getValue(ANARI_INT32, &frame.startFrame);
  frameRoot["endFrame"].getValue(ANARI_INT32, &frame.endFrame);
  frameRoot["frameIncrement"].getValue(ANARI_INT32, &frame.frameIncrement);

  auto &cameraRoot = root["camera"];
  cameraRoot["apertureRadius"].getValue(ANARI_FLOAT32, &camera.apertureRadius);
  cameraRoot["focusDistance"].getValue(ANARI_FLOAT32, &camera.focusDistance);
  cameraRoot["cameraIndex"].getValue(ANARI_UINT64, &camera.cameraIndex);

  auto &rendererRoot = root["renderer"];
  rendererRoot["activeRenderer"].getValue(
      ANARI_INT32, &renderer.activeRenderer);
  rendererRoot["libraryName"].getValue(ANARI_STRING, &renderer.libraryName);

  auto &rendererObjectsRoot = rendererRoot["rendererObjects"];
  renderer.rendererObjects.clear();
  rendererObjectsRoot.foreach_child([&](auto &node) {
    const auto *subtypeNode = node.child("subtype");
    const auto subtype = subtypeNode
        ? subtypeNode->template getValueAs<std::string>()
        : node.name();
    vsr::scene::Object ro(ANARI_RENDERER, subtype.c_str());
    vsr::io::deserialize_Object(node, ro);
    renderer.rendererObjects.push_back(std::move(ro));
  });

  auto &outputRoot = root["output"];
  outputRoot["outputDirectory"].getValue(ANARI_STRING, &output.outputDirectory);
  outputRoot["filePrefix"].getValue(ANARI_STRING, &output.filePrefix);

  auto &aovRoot = root["aov"];
  int aovTypeInt = static_cast<int>(aov.aovType);
  aovRoot["aovType"].getValue(ANARI_INT32, &aovTypeInt);
  aov.aovType = static_cast<vsr::rendering::AOVType>(aovTypeInt);
  aovRoot["depthMin"].getValue(ANARI_FLOAT32, &aov.depthMin);
  aovRoot["depthMax"].getValue(ANARI_FLOAT32, &aov.depthMax);
  aovRoot["edgeInvert"].getValue(ANARI_BOOL, &aov.edgeInvert);
}

} // namespace vsr::app
