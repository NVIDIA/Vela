// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Application.h"
// scivisStudioClient
#include "RemoteBrowseDialog.h"
#include "StudioViewport.h"
#include "UICommon.h"
#include "modals/RemoteProjectActions.h"
#include "windows/CameraRigEditor.h"
#include "windows/DatasetEditor.h"
#include "windows/HistogramPanel.h"
#include "windows/LightRigEditor.h"
#include "windows/ProjectWindow.h"
#include "windows/ShotEditor.h"
#include "windows/TaskPanel.h"
#include "windows/Timeline.h"
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
#include "vsr/ui/imgui/windows/TransferFunctionEditor.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_app
#include "vsr/app/UIStateTree.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
#include "vsr/core/Logging.hpp"
// imgui
#include <imgui.h>
// SDL
#include <SDL3/SDL.h>
// std
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace vsr::scivis_studio::client {

namespace vsr_ui = vsr::ui::imgui;
using namespace protocol;

namespace {

// A reused monolith panel that edits the mirror directly: read-only whenever
// the editors are (EditorContext::canSend), so while Lost or awaiting a
// reconnect's bootstrap it keeps showing the frozen mirror without editing it.
template <typename WindowT>
struct LockableWindow : public WindowT
{
  LockableWindow(vsr_ui::Application *app, const EditorContext *context)
      : WindowT(app), m_context(context)
  {}

  void buildUI() override;

 private:
  const EditorContext *m_context{nullptr};
};

template <typename WindowT>
void LockableWindow<WindowT>::buildUI()
{
  ImGui::BeginDisabled(!m_context->canSend());
  WindowT::buildUI();
  ImGui::EndDisabled();
}

// What the mirror's object editors may offer. A value edit reaches the
// server as SetObjectParameter and comes back in the next commit's scene
// snapshot; these five have no client-to-server message at all, so the
// widget would only edit the mirror and be undone by the next snapshot.
vsr::ui::ObjectEditPolicy mirrorEditPolicy()
{
  vsr::ui::ObjectEditPolicy policy;
  policy.refuse(vsr::ui::ObjectEdit::CreateObject,
      "the server owns the scene's objects: this client has no message"
      " that creates one");
  policy.refuse(vsr::ui::ObjectEdit::SetUsageHint,
      "a parameter's usage hint is not on the wire; only its value is");
  policy.refuse(vsr::ui::ObjectEdit::SetStringList,
      "string lists and attribute bindings are not on the wire; only"
      " values are");
  policy.refuse(vsr::ui::ObjectEdit::BindArray,
      "array contents live on the server: an array-valued edit is dropped"
      " before it is sent");
  policy.refuse(vsr::ui::ObjectEdit::ClearValue,
      "clearing a parameter has no message on the wire; unset the object"
      " reference instead");
  return policy;
}

// ImGui docking needs a couple of frames before window sizes are final.
constexpr int AUTO_CONNECT_DELAY_FRAMES = 3;

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
  m_connection->onMirrorReplaceBegin = [this](MirrorReplace kind) {
    onMirrorReplaceBegin(kind);
  };
  m_connection->onBootstrapComplete = [this] { onBootstrapComplete(); };
  m_connection->onProjectReplaced = [this] { onProjectReplaced(); };
  m_connection->onServerError = [](const std::string &message) {
    vsr::core::logError("[Client] server reported: %s", message.c_str());
  };
  m_connection->onTimeAdvanceWarning = [this](
                                           const TimeAdvanceWarning &warning) {
    onTimeAdvanceWarning(warning);
  };
  m_connection->projectOps().onTaskEnded = [this](const TaskRecord &task) {
    onTaskEnded(task);
  };

  m_editorContext.connection = m_connection.get();
  m_editorContext.reportError = [this](const std::string &message) {
    notify(message, true);
  };
  m_editorContext.reportStatus = [this](const std::string &message) {
    notify(message, false);
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

  // Every object editor in this application browses the mirror, including
  // the viewport's renderer and camera menus.
  setObjectEditPolicy(mirrorEditPolicy());

  auto *ctx = appContext();

  auto *log = new vsr_ui::Log(this);
  m_viewport = new StudioViewport(
      this, &ctx->view.manipulator, m_connection.get(), "Viewport");
  auto *layers = new vsr_ui::LayerTree(this);
  m_layerTree = layers;
  // Layer structure is server-push-only.
  layers->setEditMode(vsr_ui::LayerTree::EditMode::ReadOnly);
  auto *objectEditor =
      new LockableWindow<vsr_ui::ObjectEditor>(this, &m_editorContext);
  auto *databaseEditor =
      new LockableWindow<vsr_ui::DatabaseEditor>(this, &m_editorContext);

  auto *projectWindow = new ProjectWindow(this, &m_editorContext);
  auto *datasetEditor = new DatasetEditor(this, &m_editorContext);
  auto *shotEditor = new ShotEditor(this, &m_editorContext);
  auto *lightRigEditor = new LightRigEditor(this, &m_editorContext);
  auto *cameraRigEditor = new CameraRigEditor(this, &m_editorContext);
  auto *timeline = new Timeline(this, &m_editorContext);
  auto *histogram = new HistogramPanel(this, &m_editorContext);
  m_histogram = histogram;

  // The Transfer Function editor is the stock widget; what differs here is
  // where its samples come from, which is the one thing it asks through a
  // seam (ADR 0038's companion work). Locked with the other editors: a
  // colour-ramp edit is a SetArrayData, so it needs a session to go to.
  m_arrayAccess = std::make_unique<RemoteArrayAccess>(m_connection.get());
  auto *transferFunctionEditor =
      new LockableWindow<vsr_ui::TransferFunctionEditor>(
          this, &m_editorContext);
  transferFunctionEditor->setArrayAccess(m_arrayAccess.get());
  m_taskPanel = new TaskPanel(this, &m_editorContext);

  m_editors = {projectWindow,
      datasetEditor,
      shotEditor,
      lightRigEditor,
      cameraRigEditor,
      timeline,
      histogram};

  windows.emplace_back(m_viewport);
  windows.emplace_back(projectWindow);
  windows.emplace_back(datasetEditor);
  windows.emplace_back(shotEditor);
  windows.emplace_back(lightRigEditor);
  windows.emplace_back(cameraRigEditor);
  windows.emplace_back(timeline);
  windows.emplace_back(log);
  windows.emplace_back(m_taskPanel);
  windows.emplace_back(layers);
  windows.emplace_back(databaseEditor);
  windows.emplace_back(objectEditor);
  windows.emplace_back(histogram);
  windows.emplace_back(transferFunctionEditor);

  setWindowArray(windows);

  m_projectLocationDialog =
      std::make_unique<modals::ProjectLocationDialog>(this,
          std::make_unique<RemoteBrowseProvider>(this, &m_editorContext),
          std::make_unique<RemoteProjectLocationAction>(&m_editorContext));
  m_addStaticDatasetDialog =
      std::make_unique<modals::AddStaticDatasetDialog>(this,
          std::make_unique<RemoteBrowseProvider>(this, &m_editorContext),
          std::make_unique<RemoteStaticDatasetAction>(&m_editorContext));
  m_addFileAnimationDialog =
      std::make_unique<modals::AddFileAnimationDatasetDialog>(this,
          std::make_unique<RemoteBrowseProvider>(this, &m_editorContext),
          std::make_unique<RemoteFileAnimationAction>(&m_editorContext));

  // After setWindowArray(): the restore addresses the windows by name.
  loadClientUIState();

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
  m_statusOverlay.drawToasts();

  const bool typing = ImGui::GetIO().WantTextInput;
  // getDefaultLayout() is maintained by hand: arrange the docking, press F1,
  // paste the dump over the string. The base class' key handling lives in
  // the uiFrameStart() this one replaces, so F1 is repeated here.
  if (!typing && ImGui::IsKeyPressed(ImGuiKey_F1, false))
    printf("%s\n", ImGui::SaveIniSettingsToMemory());
  if (!typing && ImGui::IsKeyPressed(ImGuiKey_Escape))
    appContext()->clearSelected();
  if (!typing && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)
      && !m_confirmation && !m_projectLocationDialog->visible()
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
  uiMenu_View();
  uiTaskIndicator();
}

void Application::teardown()
{
  saveClientUIState();
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
      m_connection->projectOps().sendForResult<ShotCreatedResult>(CreateShot{},
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

// Replaces the base class' View menu, which has the window checkboxes but
// nothing below them. Purely client-local: never gated on the connection.
void Application::uiMenu_View()
{
  if (!ImGui::BeginMenu("View"))
    return;

  for (auto *w : m_windows) {
    ImGui::PushID(w);
    ImGui::Checkbox(w->name(), w->visiblePtr());
    ImGui::PopID();
  }

  ImGui::Separator();

  // The one application setting this client exposes: it has no App Settings
  // dialog, and a font too small to read is worth a menu of its own. The
  // change applies at once and is saved with the layout.
  ImGui::SetNextItemWidth(220.f);
  if (ImGui::DragFloat("Font Scale", &uiConfig()->fontScale, 0.01f, 0.5f, 4.f))
    m_appSettingsDialog->applySettings();
  if (ImGui::MenuItem("Reset Font Scale")) {
    uiConfig()->fontScale = 1.f;
    m_appSettingsDialog->applySettings();
  }

  ImGui::Separator();

  if (ImGui::MenuItem("Restore Default Layout"))
    ImGui::LoadIniSettingsFromMemory(getDefaultLayout());

  ImGui::EndMenu();
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
  ImGui::SameLine(
      ImGui::GetWindowWidth() - width - ImGui::GetStyle().FramePadding.x * 4.f);
  ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "%s", text.c_str());
  if (ImGui::IsItemHovered() && !shown->lastProgress.message.empty())
    ImGui::SetTooltip("%s", shown->lastProgress.message.c_str());
}

void Application::uiLostBanner()
{
  switch (m_statusOverlay.drawLostBanner(
      m_connection->autoRetrying(), m_connection->statusText())) {
  case LostBannerChoice::Retry:
    m_connection->retryNow();
    break;
  case LostBannerChoice::Disconnect:
    disconnect();
    break;
  case LostBannerChoice::None:
    break;
  }
}

// Opened here, at the frame's top level, rather than where requestDirtyAction
// ran (a menu item, the Project window's button): ImGui hashes a popup id
// with whatever is pushed at the time, and this is where it is drawn.
void Application::uiConfirmation()
{
  if (!m_confirmation)
    return;
  if (!ImGui::IsPopupOpen(CONFIRMATION_POPUP))
    ImGui::OpenPopup(CONFIRMATION_POPUP);
  const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      mainViewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  const auto choice = ui::confirmModal(
      CONFIRMATION_POPUP, m_confirmation->message, "Discard", true);
  if (choice == ui::ConfirmChoice::Pending)
    return;
  // Taken out before it runs: the action may ask another question.
  Confirmation answered = std::move(*m_confirmation);
  m_confirmation.reset();
  if (choice == ui::ConfirmChoice::Confirmed && answered.onConfirm)
    answered.onConfirm();
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
  requestDirtyAction(
      "Discard unsaved changes and start a new project?", [this] {
        m_connection->projectOps().send(
            NewProject{}, [this](const ProjectOpReply &reply) {
              if (!reply.ok)
                notify(reply.error, true);
            });
      });
}

void Application::openProjectDialog()
{
  requestDirtyAction(
      "Discard unsaved changes and open another project?", [this] {
        m_projectLocationDialog->configure(
            modals::ProjectLocationMode::OpenProject);
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
  SaveProject save; // in place: no directory
  m_connection->projectOps().sendForResult<TaskStartedResult>(std::move(save),
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
  m_projectLocationDialog->configure(
      modals::ProjectLocationMode::SaveProjectAs);
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
  m_confirmation = Confirmation{std::move(message), std::move(action)};
}

// Notifications //////////////////////////////////////////////////////////////

void Application::notify(const std::string &text, bool isError)
{
  if (isError)
    vsr::core::logError("[Client] %s", text.c_str());
  else
    vsr::core::logStatus("[Client] %s", text.c_str());
  m_statusOverlay.pushToast(text, isError);
}

// Never modal: the server kept playing. The connection already wrote the Log
// line; the toast is the transient part.
void Application::onTimeAdvanceWarning(const TimeAdvanceWarning &warning)
{
  const Project *project = m_connection->project();
  const std::string shot =
      project ? project::shotLabel(*project, warning.shotId) : warning.shotId;
  m_statusOverlay.pushToast("Frame " + std::to_string(warning.frame) + " of "
          + shot + " failed to load: " + warning.message,
      true);
  m_connection->clearTimeAdvanceWarning();
}

// Each ending toasts once (ProjectOps decides what is news); the task panel
// shows the rest.
void Application::onTaskEnded(const TaskRecord &task)
{
  notify(task.describeEnding(), task.state == TaskState::Failed);
}

// Connection lifecycle ///////////////////////////////////////////////////////

void Application::connect()
{
  vsr::core::logStatus("[Client] connecting to %s:%d", m_host.c_str(), m_port);
  m_connection->connect(m_host, uint16_t(m_port));
}

void Application::disconnect()
{
  vsr::core::logStatus("[Client] disconnecting");
  releaseMirror();
  m_connection->disconnect();
  enterHomeState();
}

// Everything the UI holds into the mirror must go before the mirror is
// cleared: selection (LayerNodeRefs), the layer tree's anchor/hover/menu
// nodes and layer index, the histogram plotted from an array of the scene
// going away, and the viewport's use-counted camera and renderer refs,
// which would otherwise release against recreated slots. Dropping those
// references is all it does: whether a scene is there to browse afterwards
// is the caller's to say, and a mid-session replace leaves one behind.
void Application::releaseMirror()
{
  auto *ctx = appContext();
  ctx->clearSelected();
  if (m_layerTree)
    m_layerTree->dropSceneReferences();
  if (m_histogram)
    m_histogram->dropMirrorReferences();
  // Hydration is per array index, and an index names something else (or
  // nothing) on the other side of a mirror replacement.
  if (m_arrayAccess)
    m_arrayAccess->dropMirrorReferences();
  if (m_viewport)
    m_viewport->dropMirrorReferences();
}

void Application::onStateChanged(ConnectionState from, ConnectionState to)
{
  vsr::core::logStatus("[Client] %s -> %s", toString(from), toString(to));
  switch (to) {
  case ConnectionState::Connected:
  case ConnectionState::Lost:
    // Nothing to do: what is on screen stays (a frozen view while Lost, the
    // previous session's until a reconnect's bootstrap) and every panel reads
    // the connection's phase for whether it may edit.
    break;
  case ConnectionState::Disconnected:
  case ConnectionState::NeverConnected:
    enterHomeState();
    break;
  }
}

// Both kinds of wholesale replacement land here, and they differ in what
// they leave behind. A Bootstrap empties the mirror and refills it over the
// messages that follow, so there is no scene for the object editors to
// browse until onBootstrapComplete puts the flag back. A mid-session
// TransferScene -- what every scene-changing commit pushes -- leaves the
// whole new scene behind, so it drops references and nothing more: clearing
// the flag here would grey the object editors from the first import to the
// end of the session, with only a reconnect to bring them back.
void Application::onMirrorReplaceBegin(MirrorReplace kind)
{
  releaseMirror();
  if (kind == MirrorReplace::Bootstrap)
    appContext()->vsr.sceneLoadComplete = false;
}

// Any snapshot may have swapped the active shot or its camera object
// (NewProject, OpenProject, CreateShot, SetActiveShot, RemoveShot); the
// bootstrap's own snapshot is covered by onBootstrapComplete.
void Application::onProjectReplaced()
{
  for (auto *editor : m_editors)
    editor->onProjectReplaced();
  if (m_connection->bootstrapping())
    return;
  resolveActiveShotCamera();
}

void Application::onBootstrapComplete()
{
  appContext()->vsr.sceneLoadComplete = true;

  m_viewport->sendFrameConfig();
  m_connection->setEncodings(encodingPreference());
  m_connection->startRendering();
  m_viewport->onServerReady(); // viewport settings, then the outline

  resolveActiveShotCamera();
}

void Application::enterHomeState()
{
  auto *ctx = appContext();
  ctx->vsr.sceneLoadComplete = false;
  ctx->clearSelected();
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

// Beside the base class' application settings, and in the same shape a
// project's UI state has: `{windows, layout}` plus the one setting the View
// menu edits, `settings/fontScale`. The client is that key's only writer --
// it never calls the base class' "Save as Defaults" -- so a scale chosen
// here comes back next run without disturbing appSettings.vsr, where
// uiRounding and the rest of the application settings still live.
std::filesystem::path Application::clientUIStateFile() const
{
#ifdef _WIN32
  if (const char *appData = std::getenv("APPDATA"); appData != nullptr)
    return std::filesystem::path(appData) / "vsr" / "studioClientUI.vsr";
#else
  if (const char *home = std::getenv("HOME"); home != nullptr) {
    return std::filesystem::path(home) / ".config" / "vsr"
        / "studioClientUI.vsr";
  }
#endif

  return std::filesystem::path("studioClientUI.vsr");
}

// At exit, with the ImGui context alive and no frame open.
void Application::saveClientUIState()
{
  const auto filename = clientUIStateFile();
  const auto directory = filename.parent_path();

  try {
    if (!directory.empty())
      std::filesystem::create_directories(directory);
  } catch (const std::exception &e) {
    vsr::core::logError("[Client] failed to create config directory '%s': %s",
        directory.string().c_str(),
        e.what());
    return;
  }

  vsr::core::DataTree tree;
  auto &root = tree.root();
  auto &windows = root[vsr::app::UI_STATE_WINDOWS];
  for (auto *window : m_windows)
    window->saveSettings(windows[window->name()]);
  root[vsr::app::UI_STATE_LAYOUT] =
      std::string(ImGui::SaveIniSettingsToMemory());
  root[vsr::app::UI_STATE_SETTINGS]["fontScale"] = uiConfig()->fontScale;

  if (!tree.save(filename.string().c_str())) {
    vsr::core::logError("[Client] failed to save the layout to '%s'",
        filename.string().c_str());
    return;
  }

  vsr::core::logStatus(
      "[Client] saved the layout to '%s'", filename.string().c_str());
}

// At startup, after the base class applied the built-in default layout, so a
// file that is missing (first run) or unreadable leaves that default
// standing. --noDefaultLayout means "impose no layout" and skips this too.
void Application::loadClientUIState()
{
  if (!commandLineOptions()->useDefaultLayout)
    return;

  const auto filename = clientUIStateFile();
  if (!std::filesystem::exists(filename))
    return;

  vsr::core::DataTree tree;
  if (!tree.load(filename.string().c_str())) {
    vsr::core::logWarning("[Client] failed to load the layout from '%s'",
        filename.string().c_str());
    return;
  }

  // Loads the windows, the dock layout and fontScale into m_uiConfig; the
  // base class already applied appSettings.vsr and its own applySettings()
  // before setupWindows(), so the new scale needs pushing into ImGui here.
  applyUIStateTree(tree.root());
  m_appSettingsDialog->applySettings();
}

const char *Application::getDefaultLayout() const
{
  return R"layout(
[Window][MainDockSpace]
Pos=0,35
Size=3840,1987
Collapsed=0

[Window][Viewport]
Pos=919,35
Size=2921,1291
Collapsed=0
DockId=0x00000003,0

[Window][Log]
Pos=919,1517
Size=1482,505
Collapsed=0
DockId=0x00000008,0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Project]
Pos=0,35
Size=917,663
Collapsed=0
DockId=0x0000000B,0

[Window][Dataset Editor]
Pos=0,700
Size=917,543
Collapsed=0
DockId=0x0000000C,0

[Window][Shot Editor]
Pos=0,35
Size=917,663
Collapsed=0
DockId=0x0000000B,1

[Window][Light Rig]
Pos=0,700
Size=917,543
Collapsed=0
DockId=0x0000000C,2

[Window][Camera Rig]
Pos=0,700
Size=917,543
Collapsed=0
DockId=0x0000000C,1

[Window][Layers]
Pos=0,35
Size=917,663
Collapsed=0
DockId=0x0000000B,2

[Window][Tasks]
Pos=2403,1517
Size=1437,505
Collapsed=0
DockId=0x00000009,0

[Window][Timeline]
Pos=919,1328
Size=2921,187
Collapsed=0
DockId=0x00000004,0

[Window][Object Editor]
Pos=0,1245
Size=917,777
Collapsed=0
DockId=0x00000006,0

[Window][Database Editor]
Pos=0,1245
Size=917,777
Collapsed=0
DockId=0x00000006,1

[Window][Histogram]
Pos=0,1245
Size=917,777
Collapsed=0
DockId=0x00000006,2

[Window][TF Editor]
Pos=0,1245
Size=917,777
Collapsed=0
DockId=0x00000006,3

[Window][Remote Browse]
Pos=1505,703
Size=830,616
Collapsed=0

[Window][Add Static Dataset]
Pos=1518,886
Size=803,250
Collapsed=0

[Table][0x413D162D,1]
Column 0  Weight=1.0000

[Table][0x5BB77325,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x61093DA2,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x093FB287,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xE1C94C44,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x85DADA6C,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xD3AA420C,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xEB97C355,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xBDE75B35,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xE11CC6FC,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xB76C5E9C,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x0269BFDF,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x541927BF,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x92286960,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xB84BB7EB,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xF1FE282D,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x853097AE,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x5162ADF6,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x07C26EAB,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0x506C7953,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xF26FDE71,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Table][0xED968722,2]
Column 0  Weight=1.0000
Column 1  Weight=1.0000

[Docking][Data]
DockSpace         ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,35 Size=3840,1987 Split=X
  DockNode        ID=0x00000001 Parent=0x80F5B4C5 SizeRef=917,1054 Split=Y Selected=0xCD8384B1
    DockNode      ID=0x00000005 Parent=0x00000001 SizeRef=547,640 Split=Y Selected=0xA530E01C
      DockNode    ID=0x0000000B Parent=0x00000005 SizeRef=955,660 Selected=0x5CC9B8E1
      DockNode    ID=0x0000000C Parent=0x00000005 SizeRef=955,541 Selected=0x4192BA76
    DockNode      ID=0x00000006 Parent=0x00000001 SizeRef=547,412 Selected=0x59632AAE
  DockNode        ID=0x00000002 Parent=0x80F5B4C5 SizeRef=2921,1054 Split=Y Selected=0xC450F867
    DockNode      ID=0x00000003 Parent=0x00000002 SizeRef=1371,1284 CentralNode=1 Selected=0xC450F867
    DockNode      ID=0x0000000A Parent=0x00000002 SizeRef=1371,694 Split=Y Selected=0x139FDA3F
      DockNode    ID=0x00000004 Parent=0x0000000A SizeRef=2883,187 Selected=0x4F89F0DC
      DockNode    ID=0x00000007 Parent=0x0000000A SizeRef=2883,505 Split=X Selected=0x139FDA3F
        DockNode  ID=0x00000008 Parent=0x00000007 SizeRef=1482,505 Selected=0x139FDA3F
        DockNode  ID=0x00000009 Parent=0x00000007 SizeRef=1437,505 Selected=0x1824BFC9
)layout";
}

} // namespace vsr::scivis_studio::client
