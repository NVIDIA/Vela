// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client_core
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
// std
#include <memory>
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

struct StudioViewport;

// What `--host H`, `--port N` and `--connect` set; the rest of argv goes to
// the vsr_ui_imgui Application.
struct ClientCommandLine
{
  std::string host{"127.0.0.1"};
  short port{12345};
  bool connectAtStartup{false};
};

/*
 * The SciVis Studio thin client at viewer parity: one ServerConnection
 * bound to the application Context's scene (the Structural Mirror), a
 * StudioViewport presenting the server's frames, and the reusable panels
 * (Layers read-only, Object Editor, Database Editor, Log) browsing the
 * mirror. Nothing here touches ProjectContext, persistence or files; the
 * Project Replica is read for the active shot's camera only.
 *
 * Connection State drives the UI: Connected enables the Server menu and
 * input; Lost freezes the last frame under a banner (auto-retry, then Retry
 * and Disconnect buttons) and makes the panels read-only; Disconnected and
 * NeverConnected are the empty home state. All network work happens in
 * ServerConnection::poll() at the start of each UI frame; no network
 * callback ever exits the process.
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
  void onBootstrapBegin();
  void onBootstrapComplete();
  void releaseMirror();
  void enterHomeState();
  void resolveActiveShotCamera();
  std::vector<protocol::FrameEncoding> encodingPreference() const;

  void uiMenu_Client();
  void uiMenu_Server();
  void uiLostBanner();

  // Data /////////////////////////////////////////////////////////////////////

  ClientCommandLine m_options;
  std::unique_ptr<ServerConnection> m_connection;
  StudioViewport *m_viewport{nullptr};

  // Menu state //

  std::string m_host;
  int m_port{0};
  protocol::FrameEncoding m_preferredEncoding{protocol::FrameEncoding::Raw};

  // True while Lost: the panels show the frozen mirror but must not edit it.
  bool m_panelsReadOnly{false};
};

} // namespace vsr::scivis_studio::client
