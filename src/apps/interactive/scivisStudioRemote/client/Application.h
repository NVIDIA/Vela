// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorContext.h"
#include "StatusOverlay.h"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
// std
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::modals {
struct AddFileAnimationDatasetDialog;
struct AddStaticDatasetDialog;
struct ProjectLocationDialog;
} // namespace vsr::scivis_studio::modals

namespace vsr::scivis_studio::client {

struct StudioViewport;
struct EditorWindow;
struct TaskPanel;

// What `--host H`, `--port N` and `--connect` set; the rest of argv goes to
// the vsr_ui_imgui Application.
struct ClientCommandLine
{
  std::string host{"127.0.0.1"};
  uint16_t port{protocol::DEFAULT_PORT}; // 1..65535, as parsePort() accepts
  bool connectAtStartup{false};
};

/*
 * The SciVis Studio thin client: one ServerConnection bound to the
 * application Context's scene (the Structural Mirror), a StudioViewport
 * presenting the server's frames, the reusable panels (Layers read-only,
 * Object Editor, Database Editor, Log) browsing the mirror, and the client's
 * copies of the Studio editors (Project, Dataset Editor, Shot Editor, Light
 * Rig, Camera Rig) reading the Project Replica and sending Project Ops. The
 * Tasks panel lists Server Tasks. Nothing here touches ProjectContext,
 * persistence or files; every path is a server path chosen through Remote
 * Browse.
 *
 * Connection State drives the UI: Connected enables the menus and editors;
 * Lost freezes the last frame under a banner (auto-retry, then Retry and
 * Disconnect buttons) and makes every panel read-only; Disconnected and
 * NeverConnected are the empty home state. All network work happens in
 * ServerConnection::poll() at the start of each UI frame; reply errors go
 * to the Log window and a transient toast, never a blocking modal.
 *
 * Example:
 *   vsr::core::setLogToStdout();
 *   vsr::scivis_studio::client::Application app(argc, argv);
 *   app.run(1920, 1080, "SciVis Studio Client");
 */
class Application : public vsr::ui::imgui::Application
{
 public:
  Application(int argc = 0, const char **argv = nullptr);
  ~Application() override;

  VSR_NOT_COPYABLE(Application)
  VSR_NOT_MOVEABLE(Application)

  const ClientCommandLine &clientCommandLine() const;

  // Project actions (the File menu's and the Project window's buttons) //

  void newProject();
  void openProjectDialog();
  void saveProject();
  void saveProjectAsDialog();

 protected:
  vsr::ui::imgui::WindowArray setupWindows() override;
  void uiFrameStart() override;
  void uiMainMenuBar() override;
  void teardown() override;
  const char *getDefaultLayout() const override;

 private:
  void connect();
  void disconnect();
  void onStateChanged(ConnectionState from, ConnectionState to);
  void onMirrorReplaceBegin();
  void onBootstrapComplete();
  void onProjectReplaced();
  void onUIState(const protocol::SubtreePtr &tree);
  // Windows, layout and settings from a project's UI-state tree, as the
  // monolith applies them on open; a null tree keeps the current layout.
  void applyUIState(const protocol::SubtreePtr &tree);
  void releaseMirror();
  void enterHomeState();
  void resolveActiveShotCamera();
  std::vector<protocol::FrameEncoding> encodingPreference() const;

  // Runs `action` at once, or after the user agrees to discard a dirty
  // project.
  void requestDirtyAction(std::string message, std::function<void()> action);
  // The opaque {windows, layout, settings} tree SaveProject stores with the
  // project, in the monolith's shape so either app restores the other's.
  protocol::SubtreePtr buildUIState();

  // Notifications //

  // Log line plus toast.
  void notify(const std::string &text, bool isError);
  void onTimeAdvanceWarning(const protocol::TimeAdvanceWarning &warning);
  void onTaskEnded(const TaskRecord &task);

  void uiMenu_File();
  void uiMenu_Studio();
  void uiMenu_Client();
  void uiMenu_Server();
  void uiTaskIndicator();
  void uiLostBanner();
  void uiConfirmation();
  void uiModals();

  // Data /////////////////////////////////////////////////////////////////////

  ClientCommandLine m_options;
  std::unique_ptr<ServerConnection> m_connection;
  EditorContext m_editorContext;
  StudioViewport *m_viewport{nullptr};
  std::vector<EditorWindow *> m_editors;
  TaskPanel *m_taskPanel{nullptr};
  std::unique_ptr<modals::ProjectLocationDialog> m_projectLocationDialog;
  std::unique_ptr<modals::AddStaticDatasetDialog> m_addStaticDatasetDialog;
  std::unique_ptr<modals::AddFileAnimationDatasetDialog>
      m_addFileAnimationDialog;

  // Menu state //

  std::string m_host;
  int m_port{0}; // the connect menu's InputInt edits it; 0..65535
  protocol::FrameEncoding m_preferredEncoding{protocol::FrameEncoding::Raw};

  // The bootstrap's UIState is applied only when the client has no live
  // layout of its own: the first bootstrap out of the home state, not the
  // one a reconnect after Lost runs. Reset in enterHomeState().
  bool m_layoutLive{false};
  // --connect waits until the dock layout has settled so the bootstrap
  // reports the viewport's real size, not the undocked first-frame size.
  int m_autoConnectInFrames{-1};

  // The dirty-project question awaiting an answer; engaged while it shows.
  struct Confirmation
  {
    std::string message;
    std::function<void()> onConfirm;
  };
  std::optional<Confirmation> m_confirmation;

  StatusOverlay m_statusOverlay;
};

} // namespace vsr::scivis_studio::client
