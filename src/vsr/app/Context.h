// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_animation
#include "vsr/animation/AnimationManager.hpp"
// vsr_core
#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/scene/Scene.hpp"
// vsr_rendering
#include "vsr/rendering/index/RenderIndex.hpp"
#include "vsr/rendering/pipeline/passes/VisualizeAOVPass.h"
#include "vsr/rendering/view/CameraPath.h"
#include "vsr/rendering/view/Manipulator.hpp"
// vsr_io
#include "vsr/io/importers.hpp"

#include "vsr/app/ANARIDeviceManager.h"
#include "vsr/app/renderAnimationSequence.h"
// std
#include <string>
#include <variant>
#include <vector>

namespace vsr::app {

using CameraPose = vsr::rendering::CameraPose;
using DeviceInitParam = std::pair<std::string, vsr::core::Any>;

struct SceneArchiveLoad
{
  std::string filename;
};

struct ForeignSceneImport
{
  vsr::io::ImportFile file;
};

/*
 * Parsed scene input intent. A Scene Archive load replaces the current Scene;
 * a foreign import adds converted content to it.
 */
using CommandLineSceneInput =
    std::variant<SceneArchiveLoad, ForeignSceneImport>;

struct CommandLineOptions
{
  bool loadedFromStateFile{false};
  std::string stateFile;
  std::vector<CommandLineSceneInput> sceneInputs;
  std::string currentLayerName{"default"};
  std::vector<vsr::io::ImportAnimationFiles> animationFilenames;
  std::vector<vsr::core::Token> animationLayerNames;
  vsr::io::ImportAnimationFiles *currentAnimationSequence{nullptr};
  vsr::io::ImporterType importerType{vsr::io::ImporterType::NONE};
  std::string cameraFile;
  std::vector<std::string> ensightFields;
  std::string vtuProperty;
  // SciVis Studio: open the project with every dataset's initial residency
  // overridden to Unloaded.
  bool openUnloaded{false};
};

struct VSRState
{
  struct StashedSelection
  {
    std::vector<vsr::scene::LayerNodeRef> nodes;
    bool shouldDeleteAfterPaste{false};
  };

  VSRState();

  // NOTE(jda) - FIX: scene must be declared before animation manager since the
  // manager needs a pointer to it, and animation manager must be destroyed
  // first...
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animationMgr;
  bool sceneLoadComplete{false};
  std::vector<vsr::scene::LayerNodeRef> selectedNodes;
  StashedSelection stashedSelection;
};

struct CameraState
{
  std::vector<CameraPose> poses;
  vsr::rendering::Manipulator manipulator;
  vsr::rendering::CameraPathSettings pathSettings;
  size_t cameraPathCameraIndex{VSR_INVALID_INDEX};
  std::string cameraPathAnimationName;
};

struct OfflineRenderSequenceConfig
{
  struct FrameSettings
  {
    uint32_t width{1024};
    uint32_t height{768};
    anari::DataType colorFormat{ANARI_UFIXED8_RGBA_SRGB};
    uint32_t samples{128};
    int numFrames{1};
    bool renderSubset{false}; // use start/end
    int startFrame{0};
    int endFrame{0};
    int frameIncrement{1};
  } frame;

  struct CameraSettings
  {
    float apertureRadius{0.f};
    float focusDistance{1.f};
    size_t cameraIndex{VSR_INVALID_INDEX};
  } camera;

  struct RenderSettings
  {
    std::vector<vsr::scene::Object> rendererObjects;
    int activeRenderer{-1};
    std::string libraryName;
  } renderer;

  struct OutputSettings
  {
    std::string outputDirectory{"./"};
    std::string filePrefix{"frame_"};
  } output;

  struct AOVSettings
  {
    vsr::rendering::AOVType aovType{vsr::rendering::AOVType::NONE};
    float depthMin{0.f};
    float depthMax{1.f};
    bool edgeInvert{false};
  } aov;

  void saveSettings(vsr::core::DataNode &root) const;
  void loadSettings(vsr::core::DataNode &root);
};

struct Context
{
  CommandLineOptions commandLine;
  VSRState vsr;
  ANARIDeviceManager anari;
  CameraState view;
  OfflineRenderSequenceConfig offline;

  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////

  Context();
  ~Context();

  // Command line parsing //

  void parseCommandLine(int argc, const char **argv); // raw main() arguments
  void parseCommandLine(std::vector<std::string> &args); // removes used args
  bool loadCommandLineSceneInputs();
  void setupSceneFromCommandLine(bool hdriOnly = false);

  // Logging //

  bool logVerbose() const;
  void setLogVerbose(bool v);
  bool logEchoOutput() const;
  void setLogEchoOutput(bool v);

  // Offline rendering //

  void setOfflineRenderingLibrary(const std::string &libName);

  // Selection //

  vsr::scene::LayerNodeRef getFirstSelected() const;
  const std::vector<vsr::scene::LayerNodeRef> &getSelectedNodes() const;
  void setSelected(vsr::scene::LayerNodeRef node);
  void setSelected(const std::vector<vsr::scene::LayerNodeRef> &nodes);
  void setSelected(const vsr::scene::Object *obj);
  void addToSelection(vsr::scene::LayerNodeRef node);
  void removeFromSelection(vsr::scene::LayerNodeRef node);
  bool isSelected(vsr::scene::LayerNodeRef node) const;
  void clearSelected();

  // Returns only parent nodes from selection (filters out children of selected
  // nodes)
  std::vector<vsr::scene::LayerNodeRef> getParentOnlySelectedNodes() const;

  // Camera poses //

  void addCurrentViewToCameraPoses(const char *name = "");
  void addTurntableCameraPoses(
      const vsr::math::float3 &azimuths, // begin, end, step
      const vsr::math::float3 &elevations, // begin, end, step
      const vsr::math::float3 &center,
      float distance,
      const char *name = "");
  void updateExistingCameraPoseFromView(CameraPose &p);
  void setCameraPose(const CameraPose &pose);
  void removeAllPoses();
  bool updateCameraPathAnimation();

  VSR_NOT_COPYABLE(Context)
  VSR_NOT_MOVEABLE(Context)

 private:
  struct LogState
  {
    bool verbose{false};
    bool echoOutput{false};
  } m_logging;
};

} // namespace vsr::app
