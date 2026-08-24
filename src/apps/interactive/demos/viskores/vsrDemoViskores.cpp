// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_ui_imgui
#include <vsr/ui/imgui/Application.h>
#include <vsr/ui/imgui/windows/CameraPoses.h>
#include <vsr/ui/imgui/windows/DatabaseEditor.h>
#include <vsr/ui/imgui/windows/IsosurfaceEditor.h>
#include <vsr/ui/imgui/windows/LayerTree.h>
#include <vsr/ui/imgui/windows/Log.h>
#include <vsr/ui/imgui/windows/ObjectEditor.h>
#include <vsr/ui/imgui/windows/TransferFunctionEditor.h>
#include <vsr/ui/imgui/windows/Viewport.h>
// anari_vsr
#include "anari_vsr/anariNewVsrDevice.h"
// std
#include <chrono>

#include "NodeEditor.h"
#include "NodeInfoWindow.h"

#define VSR_DEVICE_PASSTHROUGH 1

namespace vsr::demo {

using VSRApplication = vsr::ui::imgui::Application;
namespace vsr_ui = vsr::ui::imgui;

class Application : public VSRApplication
{
 public:
  Application(int argc, const char *argv[]);
  ~Application() override;

  vsr::ui::imgui::WindowArray setupWindows() override;
  void uiFrameEnd() override;
  void teardown() override;
  const char *getDefaultLayout() const override;

 private:
  void setupGraph();

  vsr::ui::imgui::Viewport *m_viewport{nullptr};
  vsr::viskores_graph::NodeEditor *m_neditor{nullptr};
  viskores::graph::ExecutionGraph m_graph;

  anari::Device m_vsrDevice{nullptr};
  anari::Frame m_vsrFrame{nullptr};
};

// Applications definitions ///////////////////////////////////////////////////

Application::Application(int argc, const char *argv[])
    : VSRApplication(argc, argv)
{}

Application::~Application() = default;

vsr::ui::imgui::WindowArray Application::setupWindows()
{
  ImNodes::CreateContext();

  auto windows = VSRApplication::setupWindows();

  auto *ctx = appContext();

  auto *cameras = new vsr_ui::CameraPoses(this);
  auto *log = new vsr_ui::Log(this);
  m_viewport = new vsr_ui::Viewport(this, &ctx->view.manipulator, "Viewport");
  auto *dbeditor = new vsr_ui::DatabaseEditor(this);
  auto *oeditor = new vsr_ui::ObjectEditor(this);
  auto *otree = new vsr_ui::LayerTree(this);
  auto *tfeditor = new vsr_ui::TransferFunctionEditor(this);
  auto *isoeditor = new vsr_ui::IsosurfaceEditor(this);

  auto ninfo = new vsr::viskores_graph::NodeInfoWindow(this);
  m_neditor = new vsr::viskores_graph::NodeEditor(this, &m_graph, ninfo);

  windows.emplace_back(cameras);
  windows.emplace_back(m_viewport);
  windows.emplace_back(dbeditor);
  windows.emplace_back(oeditor);
  windows.emplace_back(otree);
  windows.emplace_back(log);
  windows.emplace_back(tfeditor);
  windows.emplace_back(ninfo);
  windows.emplace_back(m_neditor);

  setWindowArray(windows);

  tfeditor->hide();

  setupGraph();

  // Populate scene //

  auto populateScene = [vp = m_viewport, ctx = ctx]() {
    auto &scene = ctx->vsr.scene;

    const bool setupDefaultLight = !ctx->commandLine.loadedFromStateFile
        && scene.numberOfObjects(ANARI_LIGHT) == 0;
    if (setupDefaultLight) {
      vsr::core::logStatus("...setting up default light");

      auto light = scene.createObject<vsr::scene::Light>(
          vsr::scene::tokens::light::directional);
      light->setName("mainLight");
      light->setParameter("direction", vsr::math::float2(0.f, 240.f));

      auto *l = scene.defaultLayer();
      l->root()->insert_first_child({l, light});
    }

    ctx->vsr.sceneLoadComplete = true;

    vp->setLibraryToDefault();
  };

#if 1
  showTaskModal(populateScene, "Please Wait: Loading Scene...");
#else
  populateScene();
#endif

  return windows;
}

void Application::uiFrameEnd()
{
  m_graph.update(viskores::graph::GraphExecutionPolicy::ALL_ASYNC, [&]() {
#if VSR_DEVICE_PASSTHROUGH
    anari::render(m_vsrDevice, m_vsrFrame);
    anari::wait(m_vsrDevice, m_vsrFrame);
#if 0
    auto *ctx = appContext();
    ctx->vsr.scene.removeUnusedObjects();
#endif
#else
    auto &instances = m_graph.getANARIInstances();
    m_viewport->setExternalInstances(instances.data(), instances.size());
#endif
    m_neditor->updateNodeSummary();
  });
  VSRApplication::uiFrameEnd();
}

void Application::teardown()
{
  m_graph.sync();
  if (m_vsrDevice) {
    anari::release(m_vsrDevice, m_vsrFrame);
    anari::release(m_vsrDevice, m_vsrDevice);
  }
  ImNodes::DestroyContext();
  VSRApplication::teardown();
}

const char *Application::getDefaultLayout() const
{
  return R"layout(
[Window][MainDockSpace]
Pos=0,56
Size=3840,2206
Collapsed=0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Viewport]
Pos=823,56
Size=3017,1147
Collapsed=0
DockId=0x0000000F,0

[Window][Database Editor]
Pos=0,800
Size=821,600
Collapsed=0
DockId=0x0000000D,1

[Window][Layers]
Pos=0,56
Size=821,742
Collapsed=0
DockId=0x00000008,0

[Window][Object Editor]
Pos=0,800
Size=821,600
Collapsed=0
DockId=0x0000000D,0

[Window][Log]
Pos=823,1828
Size=3017,434
Collapsed=0
DockId=0x00000005,0

[Window][Secondary View]
Pos=1237,26
Size=683,848
Collapsed=0
DockId=0x00000007,0

[Window][Isosurface Editor]
Pos=1370,26
Size=550,1054
Collapsed=0
DockId=0x0000000C,0

[Window][TF Editor]
Pos=1370,26
Size=550,590
Collapsed=0
DockId=0x0000000B,0

[Window][Camera Poses]
Pos=0,56
Size=821,742
Collapsed=0
DockId=0x00000008,1

[Window][Node Info]
Pos=0,1402
Size=821,860
Collapsed=0
DockId=0x0000000E,0

[Window][Node Editor]
Pos=823,1205
Size=3017,621
Collapsed=0
DockId=0x00000010,0

[Window][##]
Pos=792,507
Size=336,116
Collapsed=0

[Window][##blocking_task_modal]
Pos=60,60
Size=16,72
Collapsed=0

[Table][0x44C159D3,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x9E1800B1,1]
Column 0  Weight=1.0000

[Table][0xFAE9835A,1]
Column 0  Weight=1.0000

[Table][0x413D162D,1]
Column 0  Weight=1.0000

[Table][0x34853C34,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xEEE697AB,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x50507568,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xF4075185,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Docking][Data]
DockSpace           ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,56 Size=3840,2206 Split=X
  DockNode          ID=0x00000003 Parent=0x80F5B4C5 SizeRef=1368,1054 Split=X
    DockNode        ID=0x00000001 Parent=0x00000003 SizeRef=821,1105 Split=Y Selected=0xCD8384B1
      DockNode      ID=0x00000008 Parent=0x00000001 SizeRef=547,354 Selected=0xCD8384B1
      DockNode      ID=0x00000009 Parent=0x00000001 SizeRef=547,698 Split=Y Selected=0x82B4C496
        DockNode    ID=0x0000000D Parent=0x00000009 SizeRef=547,286 Selected=0x82B4C496
        DockNode    ID=0x0000000E Parent=0x00000009 SizeRef=547,410 Selected=0x7ECBF265
    DockNode        ID=0x00000002 Parent=0x00000003 SizeRef=3017,1105 Split=Y
      DockNode      ID=0x00000004 Parent=0x00000002 SizeRef=1370,1770 Split=X Selected=0xC450F867
        DockNode    ID=0x00000006 Parent=0x00000004 SizeRef=685,848 Split=Y Selected=0xC450F867
          DockNode  ID=0x0000000F Parent=0x00000006 SizeRef=1371,1147 CentralNode=1 Selected=0xC450F867
          DockNode  ID=0x00000010 Parent=0x00000006 SizeRef=1371,621 Selected=0xA5FE7F4E
        DockNode    ID=0x00000007 Parent=0x00000004 SizeRef=683,848 Selected=0xA3219422
      DockNode      ID=0x00000005 Parent=0x00000002 SizeRef=1370,434 Selected=0x139FDA3F
  DockNode          ID=0x0000000A Parent=0x80F5B4C5 SizeRef=550,1054 Split=Y Selected=0x3429FA32
    DockNode        ID=0x0000000B Parent=0x0000000A SizeRef=550,590 Selected=0x3429FA32
    DockNode        ID=0x0000000C Parent=0x0000000A SizeRef=550,462 Selected=0xBCE6538B
)layout";
}

void Application::setupGraph()
{
#if VSR_DEVICE_PASSTHROUGH
  anari::Device d = anariNewVsrDevice();
  void *scenePtr = &appContext()->vsr.scene;
  anari::setParameter(d, d, "scene", scenePtr);
  anari::commitParameters(d, d);

  m_graph.setANARIDevice(d);

  m_vsrFrame = anari::newObject<anari::Frame>(d);
  auto vsrRenderer = anari::newObject<anari::Renderer>(d, "default");
  auto vsrCamera = anari::newObject<anari::Camera>(d, "perspective");
  auto vsrWorld = m_graph.getANARIWorld();

  anari::setParameter(d, m_vsrFrame, "renderer", vsrRenderer);
  anari::setParameter(d, m_vsrFrame, "camera", vsrCamera);
  anari::setParameter(d, m_vsrFrame, "world", vsrWorld);
  anari::commitParameters(d, m_vsrFrame);

  anari::release(d, vsrRenderer);
  anari::release(d, vsrCamera);

  m_vsrDevice = d;
#else
  m_viewport->setDeviceChangeCb([&](const std::string &libName) {
    auto &adm = appContext()->anari;
    auto &scene = appContext()->vsr.scene;
    // Use the same ANARI device for the graph as we are in the viewport
    m_graph.setANARIDevice(adm.loadDevice(libName));
    if (!libName.empty()) {
      vsr::core::logStatus(
          "[viskores] graph now using ANARI library '%s'", libName.c_str());
    }
  });
#endif
}

} // namespace vsr::demo

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  {
    vsr::demo::Application app(argc, argv);
    app.run(1920, 1080, "Viskores Demo App");
  }

  return 0;
}
