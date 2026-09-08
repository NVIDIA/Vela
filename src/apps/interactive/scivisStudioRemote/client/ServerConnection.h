// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PayloadCommon.h"
#include "PlaybackMessages.h"
#include "StudioCodec.h"
#include "ViewportMessages.h"
// vsr_network
#include "vsr/network/Message.hpp"
#include "vsr/network/NetworkChannel.hpp"
// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scene {
struct Scene;
}

namespace vsr::scivis_studio {
struct Project;
}

namespace vsr::scivis_studio::client {

struct MirrorUpdateDelegate;
struct ProjectOps;

// The client's explicit Connection State toward its server (CONTEXT.md):
// Lost is involuntary and retries, Disconnected is a completed user intention.
enum class ConnectionState
{
  NeverConnected,
  Connected,
  Lost,
  Disconnected
};

const char *toString(ConnectionState state);

// Where the client's session with the server stands, from the socket up to a
// bootstrapped mirror. Idle: no socket (NeverConnected, Lost between retries,
// Disconnected). AwaitingHello: connecting, the server's Hello not yet seen.
// AwaitingBootstrap: Hellos matched and Connected, the bootstrap not begun --
// the mirror and replica are a previous session's frozen view or empty (a
// server busy with a render defers the bootstrap until the render ends).
// Bootstrapping: inside the BootstrapBegin..End bracket. Ready: BootstrapEnd
// seen on this connection, mirror and replica are the server's; the only
// phase in which an edit leaves the client. Closing: a Shutdown went out and
// the socket is only waited on, for the server's own close. Named after the
// server's SessionState where the two sides wait for the same thing:
// AwaitingHello (each awaits the peer's Hello), Bootstrapping (the same
// bracket); Ready is the client's side of the server's Established.
enum class SessionPhase
{
  Idle,
  AwaitingHello,
  AwaitingBootstrap,
  Bootstrapping,
  Ready,
  Closing
};

const char *toString(SessionPhase phase);

// Liveness and retry timings. The spec's numbers are suggestions, not
// contract; tests shrink them.
struct ConnectionTimings
{
  std::chrono::milliseconds pingAfterQuiet{5000};
  std::chrono::milliseconds lossAfterSilence{15000};
  std::chrono::milliseconds retryInitialDelay{1000};
  std::chrono::milliseconds retryMaxDelay{8000};
  // How long a loss retries on its own; 0 never does, and every reconnect is
  // then the caller's to ask for.
  std::chrono::milliseconds autoRetryFor{60000}; // then manual only
};

/*
 * The client's connection to a Studio server: TCP connect, Hello exchange,
 * the bracketed Bootstrap into the Structural Mirror and Project Replica,
 * Ping/Pong liveness, loss detection, and freeze-and-retry. UI-agnostic; the
 * client executable drives it once per frame.
 *
 * Threading: every public member runs on the UI thread. The network handlers
 * run on the channel's IO thread and only queue messages (frames into a
 * latest-wins slot), stamp the traffic clock, answer Ping, and latch the
 * disconnect; poll() does everything else, so nothing but poll() ever
 * touches the mirror. No public member blocks unboundedly.
 *
 * Connect is reconnect: every successful connection runs the full handshake
 * and bootstrap, which wholesale-replaces the mirror and replica. While Lost
 * both are kept as a frozen view until a reconnect or disconnect().
 *
 * Project Ops, Server Task tracking and Remote Browse live in projectOps():
 * replies, task events and Project Snapshots arriving on this connection are
 * dispatched there and to onProjectReplaced from poll(); declaring loss (or
 * disconnecting) fails every pending reply once with "connection lost".
 *
 * The mirror must outlive this object; it owns the MirrorUpdateDelegate that
 * is installed on the mirror for the connection's lifetime.
 *
 * Example:
 *   ServerConnection conn(&mirror);
 *   conn.onBootstrapComplete = [&] { rebuildPanels(); };
 *   conn.connect("127.0.0.1", 12345);
 *   // each UI frame:
 *   conn.poll();
 *   if (conn.takeLatestFrame(frame))
 *     upload(frame);
 */
struct ServerConnection
{
  ServerConnection(vsr::scene::Scene *mirror, ConnectionTimings timings = {});
  ~ServerConnection();

  VSR_NOT_COPYABLE(ServerConnection)
  VSR_NOT_MOVEABLE(ServerConnection)

  // Queries (valid between polls) //

  ConnectionState state() const;
  SessionPhase phase() const;
  // Lost and still inside autoRetryFor since the loss.
  bool autoRetrying() const;
  // One line for the banner: "reconnecting...", the last error, and so on.
  const std::string &statusText() const;
  // Why the last attempt failed or the link was lost, as the reason alone
  // (statusText() is the same reason dressed for the banner). Empty until one
  // fails, and cleared by every new attempt.
  const std::string &lastFailure() const;
  const std::string &host() const;
  uint16_t port() const;
  // phase() == Bootstrapping.
  bool bootstrapping() const;
  // phase() == Ready: BootstrapEnd seen on the current connection, mirror
  // and replica are the server's.
  bool bootstrapped() const;
  // Ready and holding a replica: the one condition under which the UI may
  // send an edit or a Project Op. Lost, bootstrapping, and the wait between
  // a reconnect's Hello and its bootstrap all leave the panels read-only.
  bool canSend() const;
  // How many scene pushes the mirror has refused since this object was made.
  // An observer that samples it around a scene message (onMessage fires after
  // handling) learns whether that message was the one refused.
  uint64_t sceneRefusals() const;

  // Inbound state the UI reads //

  const protocol::FrameConfig &frameConfig() const;
  // The Project Replica; null before the first snapshot and after
  // disconnect().
  const Project *project() const;
  // Latest-wins single slot: true and the newest Frame message if one arrived
  // since the last take.
  bool takeLatestFrame(vsr::network::Message &out);
  // Header of the newest Frame handed out by takeLatestFrame(): the frame the
  // server actually rendered (Time in Motion while playing). Absent until a
  // frame was taken; kept while Lost, dropped by disconnect().
  const std::optional<protocol::FrameHeader> &lastFrameHeader() const;
  // Project Ops, Server Task records and Remote Browse; their callbacks run
  // from poll().
  ProjectOps &projectOps();
  const ProjectOps &projectOps() const;
  // The newest TimeAdvanceWarning since the last clear; onTimeAdvanceWarning
  // fires for each one as it arrives.
  const std::optional<protocol::TimeAdvanceWarning> &lastTimeAdvanceWarning()
      const;
  void clearTimeAdvanceWarning();
  // The opaque UI-state tree the bootstrap handed over; null when none.
  const protocol::SubtreePtr &uiState() const;

  // User intentions //

  // From NeverConnected, Disconnected or Lost: starts an attempt. Success
  // means Connected once the server's Hello is answered; the bootstrap then
  // follows. A failed first attempt leaves the state alone and reports in
  // statusText(); only Lost auto-retries.
  void connect(const std::string &host, uint16_t port);
  // Sends Disconnect, closes, clears mirror and replica -> Disconnected.
  void disconnect();
  // Manual retry while Lost, also after auto-retry gave up.
  void retryNow();
  // Sends Shutdown and waits in Closing for the server to close the socket;
  // that close is the completed intention (-> Disconnected), not a loss. The
  // caller keeps polling; a server that holds the socket open past
  // lossAfterSilence is left anyway.
  void sendShutdown();
  // Sends Shutdown, then disconnect(): Disconnected before this returns,
  // without waiting for the server to go.
  void shutdownServer();

  // Once per UI frame: drains inbound messages into the mirror, replica and
  // callbacks, runs the ping/loss timers and the reconnect backoff.
  void poll();

  // Outbound, fire-and-forget; dropped unless Connected //

  void setFrameConfig(uint32_t width, uint32_t height);
  void setEncodings(const std::vector<protocol::FrameEncoding> &preferred);
  void startRendering();
  void stopRendering();
  // Scrub: the latest-wins Control-State Latch slot; only the active shot's
  // id is honoured by the server.
  void setTime(const ShotID &shotId, int frame);
  // Which object the server outlines; absent clears it.
  void setOutline(const std::optional<SceneObjectRef> &objectIdentity);
  // The whole struct every time: the server resets absent fields to defaults.
  void setViewportSettings(const protocol::ViewportSettings &settings);
  template <typename T>
  void send(const T &payload);
  // An already encoded message (a payload of a type this struct has no
  // wrapper for); false when the session was not open to take it.
  bool trySend(vsr::network::Message &&msg);

  // Callbacks, invoked from poll() on the UI thread //

  // Every inbound message poll() consumed, once it has been handled, in the
  // order they were consumed -- Frames excepted, which never reach handling
  // and are taken with takeLatestFrame(). After handling, not before, so an
  // observer sees the mirror, replica and phase the message left behind.
  // For observers only: nothing here reads it.
  std::function<void(const vsr::network::Message &)> onMessage;

  std::function<void(ConnectionState from, ConnectionState to)> onStateChanged;
  // The mirror is about to be wholesale-replaced (BootstrapBegin, or a
  // TransferScene pushed outside a bootstrap), so anything pointing into it
  // (selection, cached refs) must be dropped now, before the objects go.
  std::function<void()> onMirrorReplaceBegin;
  // BootstrapEnd received: mirror and replica are fresh.
  std::function<void()> onBootstrapComplete;
  // A Project Snapshot replaced the replica (project() is new; pointers into
  // the old one are dead). Fires for the bootstrap's snapshot too, before
  // onBootstrapComplete.
  std::function<void()> onProjectReplaced;
  // A UIState arrived (uiState() holds it): inside every bootstrap, and
  // after an OpenProject completed. The tree may be null.
  std::function<void(const protocol::SubtreePtr &)> onUIState;
  std::function<void(const std::string &)> onServerError;
  // The server failed to load a frame's data and kept playing; non-modal.
  std::function<void(const protocol::TimeAdvanceWarning &)>
      onTimeAdvanceWarning;

 private:
  using Clock = std::chrono::steady_clock;

  // IO thread
  void onInbound(const vsr::network::Message &msg);
  void onChannelClosed(const boost::system::error_code &error);
  void markTraffic();

  // UI thread
  // Hellos exchanged and the socket open with nothing said in farewell:
  // AwaitingBootstrap, Bootstrapping or Ready. What trySend() needs; edits
  // need Ready.
  bool sessionOpen() const;
  void beginAttempt();
  // Closes the socket -> Idle; the mirror's delegate goes quiet with it.
  void closeChannel();
  void setState(ConnectionState to);
  void setPhase(SessionPhase to);
  // The delegate emits exactly while Ready.
  void syncDelegate();
  void setDelegateEnabled(bool enabled);
  void declareLoss(const std::string &reason);
  // What disconnect() does once the socket is closed: drop the session's
  // mirror, replica, tasks and frames -> Disconnected with `status`.
  void dropSession(const std::string &status);
  void attemptFailed(const std::string &reason);
  void scheduleRetry();
  void sendMessage(vsr::network::Message &&msg);
  // A goodbye straight to the channel, waited on for COURTESY_SEND_TIMEOUT so
  // it leaves before the socket closes and the UI thread waits no longer.
  void flushCourtesy(vsr::network::Message &&msg);
  void replyError(const std::string &text);
  void checkSendFailures();
  void handleMessage(const vsr::network::Message &msg);
  void handleHello(const vsr::network::Message &msg);
  void applySceneMessage(
      protocol::StudioMessageType type, const vsr::network::Message &msg);
  void announceMirrorReplace();
  void clearMirror();
  Clock::time_point lastTraffic() const;

  vsr::scene::Scene *m_mirror{nullptr};
  MirrorUpdateDelegate *m_delegate{nullptr};
  ConnectionTimings m_timings;
  std::shared_ptr<vsr::network::NetworkClient> m_channel;

  std::string m_host;
  uint16_t m_port{0};
  ConnectionState m_state{ConnectionState::NeverConnected};
  SessionPhase m_phase{SessionPhase::Idle};
  std::string m_status;
  std::string m_failure;
  // Scene pushes the mirror refused, counted for observers (see
  // sceneRefusals()); never reset, so a comparison across a message is safe.
  uint64_t m_sceneRefusals{0};

  Clock::time_point m_attemptStart{};
  // When the Shutdown went out: the Closing wait is bounded from here.
  Clock::time_point m_closingSince{};
  Clock::time_point m_pingSentAt{};
  // Present exactly while Lost and auto-retrying: the end of the retry
  // window (loss + autoRetryFor). Absent once it gave up, once a retry was
  // greeted, and whenever the loss was the user's intention.
  std::optional<Clock::time_point> m_retryDeadline;
  Clock::time_point m_nextRetryAt{};
  std::chrono::milliseconds m_retryDelay{0};

  protocol::FrameConfig m_frameConfig;
  std::unique_ptr<Project> m_project;
  std::unique_ptr<ProjectOps> m_projectOps;
  std::optional<protocol::TimeAdvanceWarning> m_timeAdvanceWarning;
  std::optional<protocol::FrameHeader> m_lastFrameHeader;
  protocol::SubtreePtr m_uiState;
  // The reason of the server's farewell (a Disconnect) on this connection,
  // if it sent one: the loss that follows is explained by it.
  std::string m_farewellReason;
  std::vector<vsr::network::MessageFuture> m_sendFutures;

  // Shared with the IO thread
  std::mutex m_inboundMutex;
  std::vector<vsr::network::Message> m_inbound;
  std::optional<vsr::network::Message> m_latestFrame;
  boost::system::error_code m_ioDisconnectError;
  std::atomic<bool> m_ioDisconnected{false};
  std::atomic<Clock::rep> m_lastTraffic{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline void ServerConnection::send(const T &payload)
{
  sendMessage(protocol::encode(payload));
}

} // namespace vsr::scivis_studio::client
