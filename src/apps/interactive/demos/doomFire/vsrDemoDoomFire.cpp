// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_ui_imgui
#include <vsr/ui/imgui/Application.h>
#include <vsr/ui/imgui/windows/LayerTree.h>
#include <vsr/ui/imgui/windows/Log.h>
#include <vsr/ui/imgui/windows/ObjectEditor.h>
#include <vsr/ui/imgui/windows/TransferFunctionEditor.h>
#include <vsr/ui/imgui/windows/Viewport.h>
// vsr_core
#include <vsr/core/ColorMapUtil.hpp>
// std
#include <vector>

#include "FireControls.h"

namespace vsr::demo {

using VSRApplication = vsr::ui::imgui::Application;
namespace vsr_ui = vsr::ui::imgui;

// The 37-entry palette of the original Doom fire, from black to white.
static const std::vector<vsr::math::float3> &doomFirePalette()
{
  static const std::vector<vsr::math::float3> palette = [] {
    const unsigned char rgb[37][3] = {{7, 7, 7},
        {31, 7, 7},
        {47, 15, 7},
        {71, 15, 7},
        {87, 23, 7},
        {103, 31, 7},
        {119, 31, 7},
        {143, 39, 7},
        {159, 47, 7},
        {175, 63, 7},
        {191, 71, 7},
        {199, 71, 7},
        {223, 79, 7},
        {223, 87, 7},
        {223, 87, 7},
        {215, 95, 7},
        {215, 95, 7},
        {215, 103, 15},
        {207, 111, 15},
        {207, 119, 15},
        {207, 127, 15},
        {207, 135, 23},
        {199, 135, 23},
        {199, 143, 23},
        {199, 151, 31},
        {191, 159, 31},
        {191, 159, 31},
        {191, 167, 39},
        {191, 167, 39},
        {191, 175, 47},
        {183, 175, 47},
        {183, 183, 47},
        {183, 183, 55},
        {207, 207, 111},
        {223, 223, 159},
        {239, 239, 199},
        {255, 255, 255}};
    std::vector<vsr::math::float3> p;
    for (auto &c : rgb)
      p.emplace_back(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f);
    return p;
  }();
  return palette;
}

class Application : public VSRApplication
{
 public:
  Application(int argc, const char *argv[]) : VSRApplication(argc, argv) {}
  ~Application() override = default;

  vsr::ui::imgui::WindowArray setupWindows() override
  {
    auto windows = VSRApplication::setupWindows();

    auto *ctx = appContext();
    auto &scene = ctx->vsr.scene;
    auto *manipulator = &ctx->view.manipulator;

    auto *log = new vsr_ui::Log(this);
    auto *viewport = new vsr_ui::Viewport(this, manipulator, "Viewport");
    auto *oeditor = new vsr_ui::ObjectEditor(this);
    auto *otree = new vsr_ui::LayerTree(this);
    auto *tfeditor = new vsr_ui::TransferFunctionEditor(this);
    auto *fire = new vsr::demo::FireControls(this);

    windows.emplace_back(viewport);
    windows.emplace_back(oeditor);
    windows.emplace_back(otree);
    windows.emplace_back(log);
    windows.emplace_back(tfeditor);
    windows.emplace_back(fire);

    setWindowArray(windows);

    ctx->setupSceneFromCommandLine(true);

    // Populate scene data //

    constexpr size_t NUM_COLORS = 256;
    std::vector<vsr::math::float4> colors(NUM_COLORS);
    const auto &palette = doomFirePalette();
    for (size_t i = 0; i < NUM_COLORS; i++) {
      const float t = float(i) / (NUM_COLORS - 1);
      const auto c = palette[size_t(t * (palette.size() - 1) + 0.5f)];
      colors[i] = vsr::math::float4(c, t); // opacity ramps up with heat
    }
    auto colorArray = scene.createArray(ANARI_FLOAT32_VEC4, NUM_COLORS);
    colorArray->setData(colors);

    auto field = scene.createObject<vsr::scene::SpatialField>(
        vsr::scene::tokens::spatial_field::structuredRegular);
    field->setName("fire_field");

    auto volume = scene.createObject<vsr::scene::Volume>(
        vsr::scene::tokens::volume::transferFunction1D);
    volume->setName("fire_volume");

    // UFIXED8 samples are normalized, so 0-255 maps to 0-1.
    vsr::math::float2 valueRange{0.f, 1.f};
    volume->setParameter("valueRange", ANARI_FLOAT32_BOX1, &valueRange);
    volume->setParameterObject("value", *field);
    volume->setParameterObject("color", *colorArray);

    auto *layer = scene.defaultLayer();
    auto volumeNode = layer->root()->insert_first_child({layer, volume});

    // Setup app //

    ctx->setSelected(volumeNode);

    fire->setField(field);

    vsr::core::logStatus(
        "%s", vsr::scene::objectDBInfo(scene.objectDB()).c_str());
    ctx->vsr.sceneLoadComplete = true;

    viewport->setLibraryToDefault();

    return windows;
  }

  const char *applicationIdentifier() const override
  {
    return "vsrDemoDoomFire";
  }

  const char *getDefaultLayout() const override
  {
    return R"layout(
[Window][MainDockSpace]
Pos=0,26
Size=1920,1054
Collapsed=0

[Window][Viewport]
Pos=549,26
Size=880,798
Collapsed=0
DockId=0x00000003,0

[Window][Log]
Pos=549,826
Size=880,254
Collapsed=0
DockId=0x0000000E,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Layers]
Pos=0,365
Size=547,195
Collapsed=0
DockId=0x0000000C,0

[Window][Object Editor]
Pos=0,562
Size=547,518
Collapsed=0
DockId=0x0000000D,0

[Window][TF Editor]
Pos=1431,26
Size=489,1054
Collapsed=0
DockId=0x00000002,0

[Window][Fire Controls]
Pos=0,26
Size=547,337
Collapsed=0
DockId=0x00000008,0

[Docking][Data]
DockSpace       ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,26 Size=1920,1054 Split=X
  DockNode      ID=0x0000000A Parent=0x80F5B4C5 SizeRef=547,1054 Split=Y Selected=0xF64741D0
    DockNode    ID=0x00000008 Parent=0x0000000A SizeRef=547,337 Selected=0xF64741D0
    DockNode    ID=0x00000009 Parent=0x0000000A SizeRef=547,715 Split=Y Selected=0xCD8384B1
      DockNode  ID=0x0000000C Parent=0x00000009 SizeRef=547,195 Selected=0xCD8384B1
      DockNode  ID=0x0000000D Parent=0x00000009 SizeRef=547,518 Selected=0x82B4C496
  DockNode      ID=0x0000000B Parent=0x80F5B4C5 SizeRef=1371,1054 Split=X
    DockNode    ID=0x00000001 Parent=0x0000000B SizeRef=880,1054 Split=Y Selected=0xC450F867
      DockNode  ID=0x00000003 Parent=0x00000001 SizeRef=821,798 CentralNode=1 Selected=0xC450F867
      DockNode  ID=0x0000000E Parent=0x00000001 SizeRef=821,254 Selected=0x139FDA3F
    DockNode    ID=0x00000002 Parent=0x0000000B SizeRef=489,1054 Selected=0x3429FA32
)layout";
  }
};

} // namespace vsr::demo

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  {
    vsr::demo::Application app(argc, argv);
    app.run(1920, 1080, "VSR Demo | Doom Fire");
  }

  return 0;
}
