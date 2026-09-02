// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Application.h"
#include "StudioViewport.h"
#include "modals/AddFileAnimationDatasetDialog.h"
#include "modals/AddStaticDatasetDialog.h"
#include "modals/ProjectLocationDialog.h"
#include "windows/CameraRigEditor.h"
#include "windows/DatasetEditor.h"
#include "windows/LightRigEditor.h"
#include "windows/ProjectWindow.h"
#include "windows/ShotEditor.h"
#include "windows/TaskPanel.h"
// vsr_scivis_studio_client_core
#include "ProjectOps.h"
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
#include <iterator>
#include <optional>
#include <string>
#include <utility>

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

  void buildUI() override;

 private:
  const bool *m_locked{nullptr};
};

template <typename WindowT>
void LockableWindow<WindowT>::buildUI()
{
  ImGui::BeginDisabled(*m_locked);
  WindowT::buildUI();
  ImGui::EndDisabled();
}

// ImGui docking needs a couple of frames before window sizes are final.
constexpr int AUTO_CONNECT_DELAY_FRAMES = 3;

constexpr double ERROR_TOAST_SECONDS = 8.0;
constexpr double STATUS_TOAST_SECONDS = 4.0;
constexpr size_t MAX_TOASTS = 5;
constexpr const char *CONFIRMATION_POPUP = "Discard Unsaved Changes?";

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
      if (const char *v = valueOf("--port")) {
        if (!parsePort(v, parsed.options.port)) {
          vsr::core::logError(
              "[Client] --port requires an integer in 1..65535, got: %s\n"
              "usage: %s",
              v,
              usage());
        }
      }
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

// Construction ///////////////////////////////////////////////////////////////

Application::Application(int argc, const char **argv)
{
  auto parsed = parseArguments(argc, argv);
  m_options = parsed.options;
  m_host = m_options.host;
  m_port = m_options.port;

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
  m_connection->onMirrorReplaceBegin = [this] { onMirrorReplaceBegin(); };
  m_connection->onBootstrapComplete = [this] { onBootstrapComplete(); };
  m_connection->onProjectReplaced = [this] { onProjectReplaced(); };
  m_connection->onServerError = [](const std::string &message) {
    vsr::core::logError("[Client] server reported: %s", message.c_str());
  };

  m_editorContext.connection = m_connection.get();
  m_editorContext.reportError = [this](const std::string &message) {
    notify(message, true);
  };
  m_editorContext.reportStatus = [this](const std::string &message) {
    notify(message, false);
  };
  m_editorContext.actions.newProject = [this] { newProject(); };
  m_editorContext.actions.openProject = [this] { openProjectDialog(); };
  m_editorContext.actions.saveProject = [this] { saveProject(); };
  m_editorContext.actions.saveProjectAs = [this] { saveProjectAsDialog(); };

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

  auto *projectWindow = new ProjectWindow(this, &m_editorContext);
  auto *datasetEditor = new DatasetEditor(this, &m_editorContext);
  auto *shotEditor = new ShotEditor(this, &m_editorContext);
  auto *lightRigEditor = new LightRigEditor(this, &m_editorContext);
  auto *cameraRigEditor = new CameraRigEditor(this, &m_editorContext);
  m_taskPanel = new TaskPanel(this, &m_editorContext);

  m_editors = {projectWindow,
      datasetEditor,
      shotEditor,
      lightRigEditor,
      cameraRigEditor};

  windows.emplace_back(m_viewport);
  windows.emplace_back(projectWindow);
  windows.emplace_back(datasetEditor);
  windows.emplace_back(shotEditor);
  windows.emplace_back(lightRigEditor);
  windows.emplace_back(cameraRigEditor);
  windows.emplace_back(log);
  windows.emplace_back(m_taskPanel);
  windows.emplace_back(layers);
  windows.emplace_back(databaseEditor);
  windows.emplace_back(objectEditor);

  setWindowArray(windows);

  m_projectLocationDialog = std::make_unique<ProjectLocationDialog>(
      this, &m_editorContext, [this] { return buildUIState(); });
  m_addStaticDatasetDialog =
      std::make_unique<AddStaticDatasetDialog>(this, &m_editorContext);
  m_addFileAnimationDialog =
      std::make_unique<AddFileAnimationDatasetDialog>(this, &m_editorContext);

  if (m_options.connectAtStartup)
    m_autoConnectInFrames = AUTO_CONNECT_DELAY_FRAMES;

  return windows;
}

void Application::uiFrameStart()
{
  if (m_autoConnectInFrames >= 0 && m_autoConnectInFrames-- == 0)
    connect();

  // Everything the network delivered since the last frame lands in the
  // mirror, replica and callbacks here, before any panel reads them.
  m_connection->poll();
  watchTasks();

  if (ImGui::BeginMainMenuBar()) {
    uiMainMenuBar();
    ImGui::EndMainMenuBar();
  }

  if (m_connection->state() == ConnectionState::Lost)
    uiLostBanner();

  if (m_taskModal && m_taskModal->visible())
    m_taskModal->renderUI();

  uiModals();
  uiConfirmation();
  uiToasts();

  const bool typing = ImGui::GetIO().WantTextInput;
  if (!typing && ImGui::IsKeyPressed(ImGuiKey_Escape))
    appContext()->clearSelected();
  if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)
      && !m_confirmation.open && !m_projectLocationDialog->visible()
      && !m_addStaticDatasetDialog->visible()
      && !m_addFileAnimationDialog->visible())
    saveProject();
}

void Application::uiMainMenuBar()
{
  uiMenu_File();
  uiMenu_Studio();
  uiMenu_Client();
  uiMenu_Server();
  uiMainMenuBar_View();
  uiTaskIndicator();
}

void Application::teardown()
{
  const auto state = m_connection->state();
  if (state == ConnectionState::Connected || state == ConnectionState::Lost)
    disconnect();
  vsr_ui::Application::teardown();
}

// Menus and banner ///////////////////////////////////////////////////////////

void Application::uiMenu_File()
{
  const bool canSend = m_editorContext.canSend();
  if (!ImGui::BeginMenu("File"))
    return;

  ImGui::BeginDisabled(!canSend);
  if (ImGui::MenuItem("New Project"))
    newProject();
  if (ImGui::MenuItem("Open Project..."))
    openProjectDialog();

  ImGui::Separator();

  if (ImGui::MenuItem("Save Project", "Ctrl+S"))
    saveProject();
  if (ImGui::MenuItem("Save Project As..."))
    saveProjectAsDialog();
  ImGui::EndDisabled();

  ImGui::EndMenu();
}

void Application::uiMenu_Studio()
{
  const bool canSend = m_editorContext.canSend();
  ImGui::BeginDisabled(!canSend);
  if (ImGui::BeginMenu("Studio")) {
    if (ImGui::BeginMenu("Add Dataset")) {
      if (ImGui::MenuItem("Static..."))
        m_addStaticDatasetDialog->show();
      if (ImGui::MenuItem("File Animation..."))
        m_addFileAnimationDialog->show();
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Add Shot")) {
      // An empty name lets the server number the shot.
      m_connection->projectOps().createShot({},
          [this](const ProjectOpReply &reply,
              const std::optional<ShotCreatedResult> &) {
            if (!reply.ok)
              notify(reply.error, true);
          });
    }
    ImGui::EndMenu();
  }
  ImGui::EndDisabled();
}

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

// Right-aligned in the menu bar while any Server Task is queued or running.
void Application::uiTaskIndicator()
{
  const auto &ops = m_connection->projectOps();
  if (!ops.tasksActive())
    return;

  const TaskRecord *shown = nullptr;
  for (const TaskRecord &task : ops.tasks()) {
    if (task.state == TaskState::Running) {
      shown = &task;
      break;
    }
    if (!shown && task.state == TaskState::Queued)
      shown = &task;
  }
  if (!shown)
    return;

  const std::string text = std::string(toString(shown->state)) + ": "
      + (shown->label.empty() ? "<task>" : shown->label);
  const float width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SameLine(ImGui::GetWindowWidth() - width
      - ImGui::GetStyle().FramePadding.x * 4.f);
  ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "%s", text.c_str());
  if (ImGui::IsItemHovered() && !shown->lastProgress.message.empty())
    ImGui::SetTooltip("%s", shown->lastProgress.message.c_str());
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

void Application::uiConfirmation()
{
  if (!m_confirmation.open)
    return;

  if (!ImGui::IsPopupOpen(CONFIRMATION_POPUP))
    ImGui::OpenPopup(CONFIRMATION_POPUP);

  const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(mainViewport->GetCenter(),
      ImGuiCond_Appearing,
      ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(
          CONFIRMATION_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextWrapped("%s", m_confirmation.message.c_str());
  ImGui::Spacing();
  if (ImGui::Button("Discard")) {
    m_confirmation.open = false;
    ImGui::CloseCurrentPopup();
    auto action = std::move(m_confirmation.onConfirm);
    m_confirmation.onConfirm = nullptr;
    if (action)
      action();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_confirmation.open = false;
    m_confirmation.onConfirm = nullptr;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// Bottom-right, newest last; they expire on their own and take no input.
void Application::uiToasts()
{
  const double now = ImGui::GetTime();
  while (!m_toasts.empty() && m_toasts.front().expiresAt <= now)
    m_toasts.pop_front();
  if (m_toasts.empty())
    return;

  const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
  const ImVec2 corner(mainViewport->WorkPos.x + mainViewport->WorkSize.x - 12.f,
      mainViewport->WorkPos.y + mainViewport->WorkSize.y - 12.f);
  ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.f, 1.f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
      | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
      | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
  if (ImGui::Begin("##toasts", nullptr, flags)) {
    ImGui::PushTextWrapPos(520.f);
    for (const Toast &toast : m_toasts) {
      if (toast.isError)
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", toast.text.c_str());
      else
        ImGui::TextUnformatted(toast.text.c_str());
    }
    ImGui::PopTextWrapPos();
  }
  ImGui::End();
}

// Modals are not in the window array; their owner renders them.
void Application::uiModals()
{
  if (m_projectLocationDialog->visible())
    m_projectLocationDialog->renderUI();
  if (m_addStaticDatasetDialog->visible())
    m_addStaticDatasetDialog->renderUI();
  if (m_addFileAnimationDialog->visible())
    m_addFileAnimationDialog->renderUI();
}

// Project actions ////////////////////////////////////////////////////////////

void Application::newProject()
{
  requestDirtyAction("Discard unsaved changes and start a new project?",
      [this] {
        m_connection->projectOps().newProject([this](
                                                  const ProjectOpReply &reply) {
          if (!reply.ok)
            notify(reply.error, true);
        });
      });
}

void Application::openProjectDialog()
{
  requestDirtyAction("Discard unsaved changes and open another project?",
      [this] {
        m_projectLocationDialog->configure(ProjectLocationMode::OpenProject);
        m_projectLocationDialog->show();
      });
}

void Application::saveProject()
{
  if (!m_editorContext.canSend())
    return;
  const Project *project = m_connection->project();
  if (project->projectDirectory.empty()) {
    saveProjectAsDialog();
    return;
  }
  m_connection->projectOps().saveProject(std::nullopt,
      buildUIState(),
      [this](const ProjectOpReply &reply,
          const std::optional<TaskStartedResult> &) {
        if (!reply.ok)
          notify(reply.error, true);
      });
}

void Application::saveProjectAsDialog()
{
  if (!m_editorContext.canSend())
    return;
  m_projectLocationDialog->configure(ProjectLocationMode::SaveProjectAs);
  m_projectLocationDialog->show();
}

void Application::requestDirtyAction(
    std::string message, std::function<void()> action)
{
  if (!m_editorContext.canSend())
    return;
  const Project *project = m_connection->project();
  if (!project->dirty) {
    action();
    return;
  }
  m_confirmation.open = true;
  m_confirmation.message = std::move(message);
  m_confirmation.onConfirm = std::move(action);
}

SubtreePtr Application::buildUIState()
{
  SubtreePtr tree = makeSubtree();
  auto &root = tree->root();
  auto &windows = root["windows"];
  for (auto *window : m_windows)
    window->saveSettings(windows[window->name()]);
  root["layout"] = std::string(ImGui::SaveIniSettingsToMemory());
  auto &settings = root["settings"];
  settings["fontScale"] = m_uiConfig.fontScale;
  settings["uiRounding"] = m_uiConfig.rounding;
  return tree;
}

// Notifications //////////////////////////////////////////////////////////////

void Application::notify(const std::string &text, bool isError)
{
  if (isError)
    vsr::core::logError("[Client] %s", text.c_str());
  else
    vsr::core::logStatus("[Client] %s", text.c_str());

  Toast toast;
  toast.text = text;
  toast.isError = isError;
  toast.expiresAt = ImGui::GetTime()
      + (isError ? ERROR_TOAST_SECONDS : STATUS_TOAST_SECONDS);
  m_toasts.push_back(std::move(toast));
  while (m_toasts.size() > MAX_TOASTS)
    m_toasts.pop_front();
}

// Announces each task's completion or failure once; the task panel shows
// the rest.
void Application::watchTasks()
{
  const auto &tasks = m_connection->projectOps().tasks();

  for (auto it = m_announcedTasks.begin(); it != m_announcedTasks.end();) {
    const bool present = std::any_of(tasks.begin(),
        tasks.end(),
        [&](const TaskRecord &t) { return t.taskId == it->first; });
    it = present ? std::next(it) : m_announcedTasks.erase(it);
  }

  for (const TaskRecord &task : tasks) {
    auto it = m_announcedTasks.find(task.taskId);
    if (it != m_announcedTasks.end() && it->second == task.state)
      continue;
    m_announcedTasks[task.taskId] = task.state;
    const std::string label = task.label.empty() ? "<task>" : task.label;
    if (task.state == TaskState::Completed)
      notify(label + " completed", false);
    else if (task.state == TaskState::Failed)
      notify(label + " failed: " + task.error, true);
  }
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

void Application::onMirrorReplaceBegin()
{
  releaseMirror();
}

// Any snapshot may have swapped the active shot or its camera object
// (NewProject, OpenProject, CreateShot, SetActiveShot, RemoveShot); the
// bootstrap's own snapshot is covered by onBootstrapComplete.
void Application::onProjectReplaced()
{
  for (auto *editor : m_editors)
    editor->onProjectReplaced();
  m_addStaticDatasetDialog->onProjectReplaced();
  if (m_connection->bootstrapping())
    return;
  resolveActiveShotCamera();
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

[Window][Project]
Pos=0,56
Size=955,1341
Collapsed=0
DockId=0x00000005,0

[Window][Dataset Editor]
Pos=0,56
Size=955,1341
Collapsed=0
DockId=0x00000005,1

[Window][Shot Editor]
Pos=0,56
Size=955,1341
Collapsed=0
DockId=0x00000005,2

[Window][Light Rig]
Pos=0,56
Size=955,1341
Collapsed=0
DockId=0x00000005,3

[Window][Camera Rig]
Pos=0,56
Size=955,1341
Collapsed=0
DockId=0x00000005,4

[Window][Layers]
Pos=0,56
Size=955,1341
Collapsed=0
DockId=0x00000005,5

[Window][Tasks]
Pos=957,1741
Size=2883,521
Collapsed=0
DockId=0x0000000A,1

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
    DockNode    ID=0x00000005 Parent=0x00000001 SizeRef=547,640 Selected=0x5B7FA1DE
    DockNode    ID=0x00000006 Parent=0x00000001 SizeRef=547,412 Selected=0x82B4C496
  DockNode      ID=0x00000002 Parent=0x80F5B4C5 SizeRef=2883,1054 Split=Y Selected=0xC450F867
    DockNode    ID=0x00000003 Parent=0x00000002 SizeRef=1371,1683 CentralNode=1 Selected=0xC450F867
    DockNode    ID=0x0000000A Parent=0x00000002 SizeRef=1371,521 Selected=0x139FDA3F
)layout";
}

} // namespace vsr::scivis_studio::client
