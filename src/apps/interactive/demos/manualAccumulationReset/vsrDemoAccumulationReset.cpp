// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_ui_imgui
#include <vsr/ui/imgui/Application.h>
#include <vsr/ui/imgui/windows/DatabaseEditor.h>
#include <vsr/ui/imgui/windows/Log.h>
#include <vsr/ui/imgui/windows/ObjectEditor.h>
#include <vsr/ui/imgui/windows/Viewport.h>
// vsr_io
#include "vsr/io/procedural.hpp"

namespace vsr::demo {

using VSRApplication = vsr::ui::imgui::Application;
namespace vsr_ui = vsr::ui::imgui;

class Application : public VSRApplication
{
 public:
  Application() = default;
  ~Application() override = default;

  vsr::ui::imgui::WindowArray setupWindows() override
  {
    auto windows = VSRApplication::setupWindows();

    auto *ctx = appContext();

    vsr::io::generate_icosphere(ctx->vsr.scene);

    std::vector<std::string> libList;
    libList.push_back("visrtx");
    libList.push_back("{none}");
    ctx->anari.setLibraryList(libList);

    auto *manipulator = &ctx->view.manipulator;
    ctx->vsr.sceneLoadComplete = true;

    auto *log = new vsr_ui::Log(this);
    m_viewport = new vsr_ui::Viewport(this, manipulator, "Viewport");

    windows.emplace_back(m_viewport);
    windows.emplace_back(log);

    setWindowArray(windows);

    m_viewport->setLibraryToDefault();

    manipulator->setConfig(vsr::math::float3(0.f, 0.f, 0.f),
        4.f,
        vsr::math::float2(315.f, 20.f));

    return windows;
  }

  void uiMainMenuBar() override
  {
    auto incrementVersion = [&](bool resetToZero = false) {
      m_accumulationVersion = resetToZero ? 0 : m_accumulationVersion + 1;
      m_viewport->setCustomFrameParameter(
          "accumulationVersion", vsr::core::Any(m_accumulationVersion));
    };

    // Menu //

    if (ImGui::BeginMenu("Accumulation Controls")) {
      if (ImGui::MenuItem("Reset", "a"))
        incrementVersion();
      if (ImGui::MenuItem("Use Automatic Reset", ""))
        incrementVersion(true);
      ImGui::Separator();
      ImGui::Text("accumulationVersion: %zu", m_accumulationVersion);
      ImGui::EndMenu();
    }

    // Keyboard shortcuts //

    if (ImGui::IsKeyPressed(ImGuiKey_A, false))
      incrementVersion();
  }

  const char *getDefaultLayout() const override
  {
    return R"layout(
[Window][MainDockSpace]
Pos=0,56
Size=3840,2206
Collapsed=0

[Window][Viewport]
Pos=0,56
Size=3840,1624
Collapsed=0
DockId=0x00000003,0

[Window][Secondary View]
Pos=1237,26
Size=683,857
Collapsed=0
DockId=0x00000004,0

[Window][Log]
Pos=0,1682
Size=3840,580
Collapsed=0
DockId=0x0000000A,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Layers]
Pos=0,25
Size=548,347
Collapsed=0
DockId=0x00000005,0

[Window][Object Editor]
Pos=0,609
Size=547,522
Collapsed=0
DockId=0x00000008,0

[Window][Scene Controls]
Pos=0,26
Size=547,581
Collapsed=0
DockId=0x00000007,0

[Window][Database Editor]
Pos=0,609
Size=547,522
Collapsed=0
DockId=0x00000008,1

[Table][0x39E9F5ED,1]
Column 0  Weight=1.0000

[Table][0x418F6C9E,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xE57DC2D0,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x65B57849,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xE53C80DF,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x7FC3FA09,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xA96A74B3,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xC00D0D97,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Docking][Data]
DockSpace       ID=0x782A6D6B Pos=0,25 Size=1920,1054 Split=X Selected=0x13926F0B
  DockNode      ID=0x00000005 Parent=0x782A6D6B SizeRef=548,626 Selected=0x1FD98235
  DockNode      ID=0x00000006 Parent=0x782A6D6B SizeRef=1370,626 CentralNode=1 Selected=0x13926F0B
DockSpace       ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,56 Size=3840,2206 Split=X
  DockNode      ID=0x00000001 Parent=0x80F5B4C5 SizeRef=547,1054 Split=Y Selected=0x6426B955
    DockNode    ID=0x00000007 Parent=0x00000001 SizeRef=547,581 Selected=0x6426B955
    DockNode    ID=0x00000008 Parent=0x00000001 SizeRef=547,522 Selected=0x82B4C496
  DockNode      ID=0x00000002 Parent=0x80F5B4C5 SizeRef=1371,1054 Split=Y Selected=0xC450F867
    DockNode    ID=0x00000009 Parent=0x00000002 SizeRef=1371,1624 Split=X Selected=0xC450F867
      DockNode  ID=0x00000003 Parent=0x00000009 SizeRef=686,857 CentralNode=1 Selected=0xC450F867
      DockNode  ID=0x00000004 Parent=0x00000009 SizeRef=683,857 Selected=0xA3219422
    DockNode    ID=0x0000000A Parent=0x00000002 SizeRef=1371,580 Selected=0x139FDA3F
)layout";
  }

 private:
  uint64_t m_accumulationVersion{0};
  vsr::ui::imgui::Viewport *m_viewport{nullptr};
};

} // namespace vsr::demo

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  {
    vsr::demo::Application app;
    app.run(1920, 1080, "VSR Demo | Manual Accumulation Reset");
  }

  return 0;
}
