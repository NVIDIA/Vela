// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Application.h"
#include "StudioViewport.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
#include "vsr/ui/imgui/windows/DatabaseEditor.h"
#include "vsr/ui/imgui/windows/LayerTree.h"
#include "vsr/ui/imgui/windows/Log.h"
#include "vsr/ui/imgui/windows/ObjectEditor.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// imgui
#include <imgui.h>
// SDL
#include <SDL3/SDL.h>
// std
#include <algorithm>
#include <cstdlib>
#include <string>

namespace vsr::scivis_studio::client {

namespace vsr_ui = vsr::ui::imgui;
using namespace protocol;

namespace {

// A reused panel that can be put in read-only mode from the outside: while
// Lost the panels keep showing the frozen mirror but must not edit it.
template <typename WindowT>
struct LockableWindow : public WindowT
{
  LockableWindow(vsr_ui::Application *app, const bool *locked)
      : WindowT(app), m_locked(locked)
  {}

  void buildUI() override
  {
    ImGui::BeginDisabled(*m_locked);
    WindowT::buildUI();
    ImGui::EndDisabled();
  }

 private:
  const bool *m_locked{nullptr};
};

const char *usage()
{
  return "scivisStudioClient [--host H] [--port N] [--connect]"
         " [vsr_ui_imgui options]";
}

void requestQuit()
{
  // The main loop owns the exit; a quit event ends it cleanly through
  // teardown() instead of std::exit() tearing down under the IO thread.
  SDL_Event event{};
  event.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&event);
}

} // namespace

// Construction ///////////////////////////////////////////////////////////////

namespace {

// argv split into the client's own options and what vsr_ui_imgui parses.
struct ParsedArguments
{
  ClientCommandLine options;
  std::vector<std::string> passThrough;
};

ParsedArguments parseArguments(int argc, const char **argv)
{
  ParsedArguments parsed;
  if (argc > 0 && argv)
    parsed.passThrough.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto valueOf = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        vsr::core::logError(
            "[Client] %s needs a value\nusage: %s", name, usage());
        return nullptr;
      }
      return argv[++i];
    };
    if (arg == "--host") {
      if (const char *v = valueOf("--host"))
        parsed.options.host = v;
    } else if (arg == "--port") {
      if (const char *v = valueOf("--port"))
        parsed.options.port = short(std::atoi(v));
    } else if (arg == "--connect") {
      parsed.options.connectAtStartup = true;
    } else if (arg == "-h" || arg == "--help") {
      vsr::core::logStatus("usage: %s", usage());
    } else if (!arg.empty() && arg[0] != '-') {
      // The base class would load a positional argument as a VSR state file;
      // the client has no local scene to load one into.
      vsr::core::logWarning(
          "[Client] ignoring positional argument '%s'\n"
          "usage: %s",
          arg.c_str(),
          usage());
    } else {
      parsed.passThrough.push_back(arg);
    }
  }
  return parsed;
}

} // namespace

Application::Application(int argc, const char **argv)
{
  auto parsed = parseArguments(argc, argv);
  m_options = parsed.options;
  m_host = m_options.host;
  m_port = int(static_cast<unsigned short>(m_options.port));

  // The base class was given no argv; feed it the arguments it owns.
  auto *ctx = appContext();
  parseCommandLine(parsed.passThrough);
  ctx->parseCommandLine(parsed.passThrough);
  // No local scene ever loads from a file here.
  ctx->commandLine.stateFile.clear();
  ctx->commandLine.loadedFromStateFile = false;
  ctx->vsr.sceneLoadComplete = false;

  m_connection = std::make_unique<ServerConnection>(&ctx->vsr.scene);
  m_connection->onStateChanged = [this](
                                     ConnectionState from, ConnectionState to) {
    onStateChanged(from, to);
  };
  m_connection->onBootstrapBegin = [this] { onBootstrapBegin(); };
  m_connection->onBootstrapComplete = [this] { onBootstrapComplete(); };
  m_connection->onServerError = [](const std::string &message) {
    vsr::core::logError("[Client] server reported: %s", message.c_str());
  };

  const auto &supported = supportedFrameEncodings();
  if (std::find(supported.begin(), supported.end(), FrameEncoding::TurboJpeg)
      != supported.end())
    m_preferredEncoding = FrameEncoding::TurboJpeg;
}

// The connection goes first: its IO thread must be joined while the mirror
// (owned by the base class Context) still exists.
Application::~Application()
{
  m_connection.reset();
}

const ClientCommandLine &Application::clientCommandLine() const
{
  return m_options;
}

// vsr_ui_imgui hooks /////////////////////////////////////////////////////////

vsr_ui::WindowArray Application::setupWindows()
{
  auto windows = vsr_ui::Application::setupWindows();

  auto *ctx = appContext();

  auto *log = new vsr_ui::Log(this);
  m_viewport = new StudioViewport(
      this, &ctx->view.manipulator, m_connection.get(), "Viewport");
  auto *layers = new vsr_ui::LayerTree(this);
  layers->setReadOnly(true); // layer structure is server-push-only
  auto *objectEditor =
      new LockableWindow<vsr_ui::ObjectEditor>(this, &m_panelsReadOnly);
  auto *databaseEditor =
      new LockableWindow<vsr_ui::DatabaseEditor>(this, &m_panelsReadOnly);

  windows.emplace_back(m_viewport);
  windows.emplace_back(log);
  windows.emplace_back(layers);
  windows.emplace_back(databaseEditor);
  windows.emplace_back(objectEditor);

  setWindowArray(windows);

  if (m_options.connectAtStartup)
    connect();

  return windows;
}

void Application::uiFrameStart()
{
  // Everything the network delivered since the last frame lands in the
  // mirror, replica and callbacks here, before any panel reads them.
  m_connection->poll();

  if (ImGui::BeginMainMenuBar()) {
    uiMainMenuBar();
    ImGui::EndMainMenuBar();
  }

  if (m_connection->state() == ConnectionState::Lost)
    uiLostBanner();

  if (m_taskModal && m_taskModal->visible())
    m_taskModal->renderUI();

  if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape))
    appContext()->clearSelected();
}

void Application::uiMainMenuBar()
{
  uiMenu_Client();
  uiMenu_Server();
  uiMainMenuBar_View();
}

void Application::teardown()
{
  const auto state = m_connection->state();
  if (state == ConnectionState::Connected || state == ConnectionState::Lost)
    disconnect();
  vsr_ui::Application::teardown();
}

// Menus and banner ///////////////////////////////////////////////////////////

void Application::uiMenu_Client()
{
  if (!ImGui::BeginMenu("Client"))
    return;

  const auto state = m_connection->state();

  ImGui::BeginDisabled(state == ConnectionState::Connected);
  ImGui::SetNextItemWidth(220.f);
  ImGui::InputText("Host", &m_host);
  ImGui::SetNextItemWidth(220.f);
  if (ImGui::InputInt("Port", &m_port))
    m_port = std::clamp(m_port, 0, 65535);
  if (ImGui::MenuItem("Connect"))
    connect();
  ImGui::EndDisabled();

  ImGui::Separator();

  ImGui::BeginDisabled(
      state != ConnectionState::Connected && state != ConnectionState::Lost);
  if (ImGui::MenuItem("Disconnect"))
    disconnect();
  ImGui::EndDisabled();

  ImGui::Separator();

  if (ImGui::MenuItem("Quit", "Ctrl+Q"))
    requestQuit();

  ImGui::EndMenu();
}

void Application::uiMenu_Server()
{
  const bool connected = m_connection->state() == ConnectionState::Connected;
  ImGui::BeginDisabled(!connected);
  if (ImGui::BeginMenu("Server")) {
    if (ImGui::MenuItem("Start Rendering"))
      m_connection->startRendering();
    if (ImGui::MenuItem("Pause Rendering"))
      m_connection->stopRendering();

    ImGui::Separator();

    if (ImGui::BeginMenu("Preferred Encoding")) {
      for (auto encoding : supportedFrameEncodings()) {
        if (ImGui::RadioButton(
                toString(encoding), m_preferredEncoding == encoding)
            && m_preferredEncoding != encoding) {
          m_preferredEncoding = encoding;
          m_connection->setEncodings(encodingPreference());
        }
      }
      ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Shutdown Server")) {
      vsr::core::logStatus("[Client] asking the server to shut down");
      releaseMirror();
      m_connection->shutdownServer(); // Shutdown, then a local disconnect
      enterHomeState();
    }

    ImGui::EndMenu();
  }
  ImGui::EndDisabled();
}

void Application::uiLostBanner()
{
  const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(mainViewport->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, 0.f));
  ImGui::SetNextWindowBgAlpha(0.95f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.75f, 0.12f, 0.1f, 1.f));

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking
      | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
      | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize
      | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

  if (ImGui::Begin("##connectionLostBanner", nullptr, flags)) {
    if (m_connection->autoRetrying()) {
      ImGui::TextUnformatted("Server connection lost -- reconnecting...");
    } else {
      ImGui::TextUnformatted("Server connection lost");
      ImGui::SameLine();
      if (ImGui::SmallButton("Retry"))
        m_connection->retryNow();
      ImGui::SameLine();
      if (ImGui::SmallButton("Disconnect"))
        disconnect();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", m_connection->statusText().c_str());
  }
  ImGui::End();

  ImGui::PopStyleColor();
}

// Connection lifecycle ///////////////////////////////////////////////////////

void Application::connect()
{
  vsr::core::logStatus("[Client] connecting to %s:%d", m_host.c_str(), m_port);
  m_connection->connect(m_host, short(m_port));
}

void Application::disconnect()
{
  vsr::core::logStatus("[Client] disconnecting");
  releaseMirror();
  m_connection->disconnect();
  enterHomeState();
}

// Everything the UI holds into the mirror must go before the mirror is
// cleared: selection (LayerNodeRefs) and the viewport's use-counted camera
// and renderer refs, which would otherwise release against recreated slots.
void Application::releaseMirror()
{
  auto *ctx = appContext();
  ctx->clearSelected();
  ctx->vsr.sceneLoadComplete = false;
  if (m_viewport)
    m_viewport->dropMirrorReferences();
}

void Application::onStateChanged(ConnectionState from, ConnectionState to)
{
  vsr::core::logStatus("[Client] %s -> %s", toString(from), toString(to));
  switch (to) {
  case ConnectionState::Connected:
    m_panelsReadOnly = false;
    break;
  case ConnectionState::Lost:
    // Freeze in place: mirror, replica and last frame stay; edits stop.
    m_panelsReadOnly = true;
    break;
  case ConnectionState::Disconnected:
  case ConnectionState::NeverConnected:
    enterHomeState();
    break;
  }
}

void Application::onBootstrapBegin()
{
  releaseMirror();
}

void Application::onBootstrapComplete()
{
  appContext()->vsr.sceneLoadComplete = true;
  m_panelsReadOnly = false;

  m_viewport->sendFrameConfig();
  m_connection->setEncodings(encodingPreference());
  m_connection->startRendering();

  resolveActiveShotCamera();
}

void Application::enterHomeState()
{
  auto *ctx = appContext();
  ctx->vsr.sceneLoadComplete = false;
  ctx->clearSelected();
  m_panelsReadOnly = false;
  if (m_viewport)
    m_viewport->reset();
}

void Application::resolveActiveShotCamera()
{
  auto &scene = appContext()->vsr.scene;
  const Project *project = m_connection->project();
  const Shot *shot = project ? project::activeShot(*project) : nullptr;

  vsr::scene::CameraAppRef camera;
  if (shot && shot->camera.type == ANARI_CAMERA
      && shot->camera.objectIndex != VSR_INVALID_INDEX) {
    camera = scene.getObject<vsr::scene::Camera>(shot->camera.objectIndex);
  }

  if (!camera) {
    const size_t count = scene.numberOfObjects(ANARI_CAMERA);
    for (size_t i = 0; i < count && !camera; ++i)
      camera = scene.getObject<vsr::scene::Camera>(i);
    if (camera) {
      vsr::core::logWarning(
          "[Client] the active shot names no camera in the mirror; driving"
          " the first camera (index %zu) instead",
          camera->index());
    } else {
      vsr::core::logWarning(
          "[Client] the mirror holds no camera; the viewport takes no input");
    }
  }

  m_viewport->adoptCamera(camera);
  m_viewport->adoptRenderer(
      shot ? shot->renderSettings.rendererObjectIndex : VSR_INVALID_INDEX);
}

std::vector<FrameEncoding> Application::encodingPreference() const
{
  // The chosen encoding first, Raw last so the server can always fall back.
  std::vector<FrameEncoding> preference{m_preferredEncoding};
  for (auto encoding : supportedFrameEncodings()) {
    if (encoding != m_preferredEncoding && encoding != FrameEncoding::Raw)
      preference.push_back(encoding);
  }
  if (m_preferredEncoding != FrameEncoding::Raw)
    preference.push_back(FrameEncoding::Raw);
  return preference;
}

// Layout /////////////////////////////////////////////////////////////////////

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

[Window][Database Editor]
Pos=0,1399
Size=955,863
Collapsed=0
DockId=0x00000006,1

[Docking][Data]
DockSpace       ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,56 Size=3840,2206 Split=X
  DockNode      ID=0x00000001 Parent=0x80F5B4C5 SizeRef=955,1054 Split=Y Selected=0xCD8384B1
    DockNode    ID=0x00000005 Parent=0x00000001 SizeRef=547,640 Selected=0xCD8384B1
    DockNode    ID=0x00000006 Parent=0x00000001 SizeRef=547,412 Selected=0x82B4C496
  DockNode      ID=0x00000002 Parent=0x80F5B4C5 SizeRef=2883,1054 Split=Y Selected=0xC450F867
    DockNode    ID=0x00000003 Parent=0x00000002 SizeRef=1371,1683 CentralNode=1 Selected=0xC450F867
    DockNode    ID=0x0000000A Parent=0x00000002 SizeRef=1371,521 Selected=0x139FDA3F
)layout";
}

} // namespace vsr::scivis_studio::client
