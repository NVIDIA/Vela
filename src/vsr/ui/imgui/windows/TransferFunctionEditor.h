// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"
// vsr_core
#include "vsr/core/ColorMapUtil.hpp"
#include "vsr/scene/objects/Array.hpp"
#include "vsr/scene/objects/Volume.hpp"
// std
#include <string>
#include <vector>
// SDL
#include <SDL3/SDL.h>

namespace vsr::ui::imgui {

class TransferFunctionEditor : public Window
{
 public:
  TransferFunctionEditor(Application *app, const char *name = "TF Editor");
  ~TransferFunctionEditor() override;

  void buildUI() override;

 private:
  void buildUI_selectColorMap();
  void buildUI_drawEditor();
  void buildUI_opacityScale();
  void buildUI_unitDistance();
  void buildUI_valueRange();

  std::vector<vsr::math::float4> getSampledColorsAndOpacities(
      int numSamples = 256);

  void setMap(int which = 0);
  void setObjectPtrsFromSelectedObject();
  void loadDefaultMaps();
  void loadColormap(
      const std::string &filepath, const std::string &name);

  void saveColormapTo1dt(const std::string &filepath);
  void saveColormapToParaview(const std::string &filepath);
  void getTransferFunctionFilenameFromDialog(
      std::string &filenameOut, bool save = false);
  void updateColormaps();
  void updateTfnPaletteTexture();
  void resizeTfnPaletteTexture(size_t width);

  // Data //

  vsr::scene::Volume *m_volume{nullptr};
  std::vector<vsr::scene::Volume*> m_otherVolumes;
  vsr::scene::Array *m_colorMapArray{nullptr};
  vsr::scene::Volume *m_lastColorVolume{nullptr};

  // all available transfer functions
  std::vector<std::string> m_tfnsNames;
  std::vector<std::vector<vsr::core::ColorPoint>> m_tfnsColorPoints;
  std::vector<vsr::core::OpacityPoint> m_tfnOpacityPoints;

  // parameters of currently selected transfer function
  int m_currentMap{-1};
  int m_nextMap{0};
  std::vector<vsr::core::ColorPoint> *m_tfnColorPoints{nullptr};

  // domain (value range) of transfer function
  vsr::math::float2 m_valueRange{0.f, 1.f};
  vsr::math::float2 m_defaultValueRange{0.f, 1.f};

  // texture for displaying transfer function color palette
  SDL_Texture *m_tfnPaletteTexture{nullptr};
  size_t m_tfnPaletteWidth{0};

  // New member for storing the filename of the currently loaded colormap
  std::string m_currentColormapFilename;
  std::string m_saveColormapFilename;
};

} // namespace vsr::ui::imgui
