// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_ui_imgui
#include <vsr/ui/imgui/Application.h>
#include <vsr/ui/imgui/windows/DatabaseEditor.h>
#include <vsr/ui/imgui/windows/LayerTree.h>
#include <vsr/ui/imgui/windows/Log.h>
#include <vsr/ui/imgui/windows/ObjectEditor.h>
#include <vsr/ui/imgui/windows/Viewport.h>
// vsr_network
#include <vsr/network/messages/TransferScene.hpp>

#include "NetworkUpdateDelegate.hpp"
#include "RemoteViewport.h"

namespace vsr::demo {

using VSRApplication = vsr::ui::imgui::Application;
namespace vsr_ui = vsr::ui::imgui;

struct Application : public VSRApplication
{
  Application();
  ~Application() override;

  vsr::ui::imgui::WindowArray setupWindows() override;
  void uiMainMenuBar() override;
  void teardown() override;
  const char *getDefaultLayout() const override;

 private:
  void connect();
  void disconnect();

  vsr::network::NetworkUpdateDelegate *m_updateDelegate{nullptr};
  vsr::ui::imgui::RemoteViewport *m_viewport{nullptr};
  std::shared_ptr<vsr::network::NetworkClient> m_client;
  std::string m_host{"127.0.0.1"};
  short m_port{12345};
  std::string m_stateFileName{"state.vsr"};
  bool m_timeUpdatesEnabled{true};
};

// Application definitions ////////////////////////////////////////////////////

Application::Application()
{
  auto *ctx = appContext();

  m_client = std::make_shared<vsr::network::NetworkClient>();

  m_updateDelegate =
      ctx->vsr.scene.updateDelegate().emplace<vsr::network::NetworkUpdateDelegate>(
          &ctx->vsr.scene, m_client.get());

  ctx->vsr.animationMgr.setTimeChangedCallback([this](float time) {
    if (m_timeUpdatesEnabled)
      m_client->send(MessageType::SERVER_UPDATE_TIME, &time);
  });

  m_client->registerHandler(
      MessageType::ERROR, [](const vsr::network::Message &msg) {
        vsr::core::logError("[Client] Received error from server: '%s'",
            vsr::network::payloadAs<char>(msg));
        std::exit(1);
      });

  m_client->registerHandler(
      MessageType::PING, [](const vsr::network::Message &msg) {
        vsr::core::logStatus("[Client] Received PING from server");
      });

  m_client->registerHandler(MessageType::CLIENT_SCENE_TRANSFER_BEGIN,
      [this](const vsr::network::Message &msg) {
        vsr::core::logStatus("[Client] Server has initiated scene transfer...");
        m_updateDelegate->setEnabled(false);
      });

  m_client->registerHandler(MessageType::CLIENT_RECEIVE_SCENE,
      [this](const vsr::network::Message &msg) {
        auto &scene = appContext()->vsr.scene;
        vsr::network::messages::TransferScene sceneMsg(msg, &scene);
        sceneMsg.execute();
        m_updateDelegate->setEnabled(true);
        vsr::core::logStatus("[Client] Scene contents:");
        vsr::core::logStatus(
            "\n%s", vsr::scene::objectDBInfo(scene.objectDB()).c_str());
        vsr::core::logStatus("[Client] Requesting start of rendering...");
        m_client->send(MessageType::SERVER_START_RENDERING);
        appContext()->vsr.sceneLoadComplete = true;
      });

  m_client->registerHandler(MessageType::CLIENT_RECEIVE_TIME,
      [this](const vsr::network::Message &msg) {
        m_timeUpdatesEnabled = false;
        float time = *vsr::network::payloadAs<float>(msg);
        appContext()->vsr.animationMgr.setAnimationTime(time);
        m_timeUpdatesEnabled = true;
      });

  ctx->vsr.sceneLoadComplete = false;
}

Application::~Application()
{
  if (m_updateDelegate)
    appContext()->vsr.scene.updateDelegate().erase(m_updateDelegate);
}

vsr::ui::imgui::WindowArray Application::setupWindows()
{
  auto windows = VSRApplication::setupWindows();

  auto *ctx = appContext();
  auto *manipulator = &ctx->view.manipulator;

  auto *log = new vsr_ui::Log(this);
  m_viewport =
      new vsr_ui::RemoteViewport(this, manipulator, m_client.get(), "Viewport");
  auto *ltree = new vsr_ui::LayerTree(this);
  auto *oeditor = new vsr_ui::ObjectEditor(this);
  auto *dbeditor = new vsr_ui::DatabaseEditor(this);

  windows.emplace_back(m_viewport);
  windows.emplace_back(log);
  windows.emplace_back(ltree);
  windows.emplace_back(dbeditor);
  windows.emplace_back(oeditor);

  setWindowArray(windows);

  return windows;
}

void Application::uiMainMenuBar()
{
  // Menu //

  if (ImGui::BeginMenu("Client")) {
    ImGui::BeginDisabled(m_client->isConnected());
    if (ImGui::BeginMenu("Connect")) {
      ImGui::InputText("Host", &m_host);

      int port = m_port;
      if (ImGui::InputInt("Port", &port))
        m_port = static_cast<short>(port);

      if (ImGui::Button("Connect"))
        connect();

      ImGui::EndMenu();
    } // Connect
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::BeginDisabled(!m_client->isConnected());
    if (ImGui::MenuItem("Disconnect", "", false, true))
      disconnect();
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::MenuItem("Quit", "Esc", false, true)) {
      teardown();
      std::exit(0);
    }

    ImGui::EndMenu(); // "Client"
  }

  ImGui::BeginDisabled(!m_client->isConnected());

  if (ImGui::BeginMenu("Server")) {
    if (ImGui::MenuItem("Start Rendering")) {
      vsr::core::logStatus("[Client] Sending START_RENDERING command");
      m_client->send(MessageType::SERVER_START_RENDERING);
    }

    if (ImGui::MenuItem("Pause Rendering")) {
      vsr::core::logStatus("[Client] Sending STOP_RENDERING command");
      m_client->send(MessageType::SERVER_STOP_RENDERING);
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("Save State File")) {
      ImGui::InputText("Filename", &m_stateFileName);
      ImGui::Separator();
      if (ImGui::Button("Save")) {
        if (!m_stateFileName.empty()) {
          vsr::core::logStatus(
              "[Client] Sending command to save state file '%s'",
              m_stateFileName.c_str());
          auto msg =
              vsr::network::makeMessage(MessageType::SERVER_SAVE_STATE_FILE);
          vsr::network::payloadWrite(msg, m_stateFileName);
          m_client->send(std::move(msg));
        }
      }
      ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Shutdown")) {
      vsr::core::logStatus("[Client] Sending SHUTDOWN command");
      m_client->send(MessageType::SERVER_SHUTDOWN).get();
      disconnect();
    }

    ImGui::EndMenu(); // "Server"
  }

  ImGui::EndDisabled();

  // Keyboard shortcuts //

  if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
    vsr::core::logStatus("[Client] Sending PING");
    m_client->send(MessageType::PING);
  }
}

void Application::teardown()
{
  disconnect();
  m_client->removeAllHandlers();
  m_viewport->setNetworkChannel(nullptr);
  VSRApplication::teardown();
}

const char *Application::getDefaultLayout() const
{
  return R"layout(
[Window][MainDockSpace]
Pos=0,56
Size=3840,2206
Collapsed=0

[Window][Viewport]
Pos=957,56
Size=2883,1683
Collapsed=0
DockId=0x00000003,0

[Window][Secondary View]
Pos=1237,26
Size=683,857
Collapsed=0
DockId=0x00000004,0

[Window][Log]
Pos=957,1741
Size=2883,521
Collapsed=0
DockId=0x0000000A,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Layers]
Pos=0,56
Size=955,1341
Collapsed=0
DockId=0x00000005,0

[Window][Object Editor]
Pos=0,1399
Size=955,863
Collapsed=0
DockId=0x00000006,0

[Window][Scene Controls]
Pos=0,26
Size=547,581
Collapsed=0
DockId=0x00000007,0

[Window][Database Editor]
Pos=0,1399
Size=955,863
Collapsed=0
DockId=0x00000006,1

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

[Table][0x413D162D,1]
Column 0  Weight=1.0000

[Docking][Data]
DockSpace       ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,56 Size=3840,2206 Split=X
  DockNode      ID=0x00000001 Parent=0x80F5B4C5 SizeRef=955,1054 Split=Y Selected=0x6426B955
    DockNode    ID=0x00000007 Parent=0x00000001 SizeRef=547,581 Selected=0x6426B955
    DockNode    ID=0x00000008 Parent=0x00000001 SizeRef=547,522 Split=Y Selected=0x8B73155F
      DockNode  ID=0x00000005 Parent=0x00000008 SizeRef=547,640 Selected=0xCD8384B1
      DockNode  ID=0x00000006 Parent=0x00000008 SizeRef=547,412 Selected=0x82B4C496
  DockNode      ID=0x00000002 Parent=0x80F5B4C5 SizeRef=2883,1054 Split=Y Selected=0xC450F867
    DockNode    ID=0x00000009 Parent=0x00000002 SizeRef=1371,1683 Split=X Selected=0xC450F867
      DockNode  ID=0x00000003 Parent=0x00000009 SizeRef=686,857 CentralNode=1 Selected=0xC450F867
      DockNode  ID=0x00000004 Parent=0x00000009 SizeRef=683,857 Selected=0xA3219422
    DockNode    ID=0x0000000A Parent=0x00000002 SizeRef=1371,521 Selected=0x139FDA3F
)layout";
}

void Application::connect()
{
  m_client->connect(m_host, m_port);
  if (m_client->isConnected()) {
    vsr::core::logStatus(
        "[Client] Connected to server at %s:%d", m_host.c_str(), m_port);
    vsr::core::logStatus("[Client] Requesting scene from server");
    m_client->send(MessageType::SERVER_REQUEST_SCENE).get();
  } else {
    vsr::core::logError("[Client] Failed to connect to server at %s:%d",
        m_host.c_str(),
        m_port);
  }
}

void Application::disconnect()
{
  vsr::core::logStatus("[Client] Disconnecting from server...");
  m_updateDelegate->setEnabled(false);
  m_viewport->disconnect();
  m_client->send(MessageType::DISCONNECT).get();
  m_client->disconnect();

  auto *ctx = appContext();
  ctx->vsr.sceneLoadComplete = false;
  ctx->clearSelected();
  auto &scene = ctx->vsr.scene;
  scene.removeAllObjects();
}

} // namespace vsr::demo

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  {
    vsr::core::setLogToStdout();
    vsr::demo::Application app;
    app.run(1920, 1080, "VSR Remote Viewer");
  }

  return 0;
}
