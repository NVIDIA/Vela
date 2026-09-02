// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client
#include "EditorContext.h"
// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PayloadCommon.h"
#include "StudioEndpoint.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
// std
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

struct StudioViewport;
struct EditorWindow;
struct TaskPanel;
struct ProjectLocationDialog;
struct AddStaticDatasetDialog;
struct AddFileAnimationDatasetDialog;

// What `--host H`, `--port N` and `--connect` set; the rest of argv goes to
// the vsr_ui_imgui Application.
struct ClientCommandLine
{
  std::string host{"127.0.0.1"};
  int port{protocol::DEFAULT_PORT}; // 1..65535, as parsePort() accepts
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
  void releaseMirror();
  void enterHomeState();
  void resolveActiveShotCamera();
  std::vector<protocol::FrameEncoding> encodingPreference() const;

  // Project actions (File menu and the Project window) //

  void newProject();
  void openProjectDialog();
  void saveProject();
  void saveProjectAsDialog();
  // Runs `action` at once, or after the user agrees to discard a dirty
  // project.
  void requestDirtyAction(std::string message, std::function<void()> action);
  // The opaque {windows, layout, settings} tree SaveProject stores with the
  // project, in the monolith's shape so either app restores the other's.
  protocol::SubtreePtr buildUIState();

  // Notifications //

  void notify(const std::string &text, bool isError);
  void watchTasks();

  void uiMenu_File();
  void uiMenu_Studio();
  void uiMenu_Client();
  void uiMenu_Server();
  void uiTaskIndicator();
  void uiLostBanner();
  void uiConfirmation();
  void uiToasts();
  void uiModals();

  // Data /////////////////////////////////////////////////////////////////////

  ClientCommandLine m_options;
  std::unique_ptr<ServerConnection> m_connection;
  EditorContext m_editorContext;
  StudioViewport *m_viewport{nullptr};
  std::vector<EditorWindow *> m_editors;
  TaskPanel *m_taskPanel{nullptr};
  std::unique_ptr<ProjectLocationDialog> m_projectLocationDialog;
  std::unique_ptr<AddStaticDatasetDialog> m_addStaticDatasetDialog;
  std::unique_ptr<AddFileAnimationDatasetDialog> m_addFileAnimationDialog;

  // Menu state //

  std::string m_host;
  int m_port{0};
  protocol::FrameEncoding m_preferredEncoding{protocol::FrameEncoding::Raw};

  // True while Lost: the panels show the frozen mirror but must not edit it.
  bool m_panelsReadOnly{false};
  // --connect waits until the dock layout has settled so the bootstrap
  // reports the viewport's real size, not the undocked first-frame size.
  int m_autoConnectInFrames{-1};

  struct Confirmation
  {
    bool open{false};
    std::string message;
    std::function<void()> onConfirm;
  } m_confirmation;

  struct Toast
  {
    std::string text;
    bool isError{false};
    double expiresAt{0.0};
  };
  std::deque<Toast> m_toasts;
  // Task states already announced, so each completion toasts once.
  std::map<uint64_t, TaskState> m_announcedTasks;
};

} // namespace vsr::scivis_studio::client
