// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PayloadCommon.h"
#include "PlaybackMessages.h"
#include "StudioCodec.h"
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

// Liveness and retry timings. The spec's numbers are suggestions, not
// contract; tests shrink them.
struct ConnectionTimings
{
  std::chrono::milliseconds pingAfterQuiet{5000};
  std::chrono::milliseconds lossAfterSilence{15000};
  std::chrono::milliseconds retryInitialDelay{1000};
  std::chrono::milliseconds retryMaxDelay{8000};
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
  // Lost and still inside autoRetryFor since the loss.
  bool autoRetrying() const;
  // One line for the banner: "reconnecting...", the last error, and so on.
  const std::string &statusText() const;
  const std::string &host() const;
  short port() const;
  bool bootstrapping() const;

  // Inbound state the UI reads //

  const protocol::FrameConfig &frameConfig() const;
  // The Project Replica; null before the first snapshot and after
  // disconnect().
  const Project *project() const;
  // Latest-wins single slot: true and the newest Frame message if one arrived
  // since the last take.
  bool takeLatestFrame(vsr::network::Message &out);
  // Project Ops, Server Task records and Remote Browse; their callbacks run
  // from poll().
  ProjectOps &projectOps();
  const ProjectOps &projectOps() const;
  // The newest TimeAdvanceWarning since the last clear; playback is a later
  // milestone but the message may already arrive.
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
  void connect(const std::string &host, short port);
  // Sends Disconnect, closes, clears mirror and replica -> Disconnected.
  void disconnect();
  // Manual retry while Lost, also after auto-retry gave up.
  void retryNow();
  // Sends Shutdown, then disconnect().
  void shutdownServer();

  // Once per UI frame: drains inbound messages into the mirror, replica and
  // callbacks, runs the ping/loss timers and the reconnect backoff.
  void poll();

  // Outbound, fire-and-forget; dropped unless Connected //

  void setFrameConfig(uint32_t width, uint32_t height);
  void setEncodings(const std::vector<protocol::FrameEncoding> &preferred);
  void startRendering();
  void stopRendering();
  template <typename T>
  void send(const T &payload);

  // Callbacks, invoked from poll() on the UI thread //

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
  std::function<void(const std::string &)> onServerError;

 private:
  using Clock = std::chrono::steady_clock;

  // Where the current socket stands, independent of the user-facing state.
  enum class Phase
  {
    Idle,
    AwaitingHello,
    Established
  };

  // IO thread
  void onInbound(const vsr::network::Message &msg);
  void onChannelClosed(const boost::system::error_code &error);
  void markTraffic();

  // UI thread
  void beginAttempt();
  void closeChannel();
  void setState(ConnectionState to);
  // Edits leave the mirror only on an Established connection whose bootstrap
  // has completed and is not being redone; the delegate is enabled exactly
  // then.
  bool canEmitEdits() const;
  void setDelegateEnabled(bool enabled);
  void declareLoss(const std::string &reason);
  void attemptFailed(const std::string &reason);
  void scheduleRetry();
  void sendMessage(vsr::network::Message &&msg);
  // False when the message was dropped (not Established).
  bool trySend(vsr::network::Message &&msg);
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
  short m_port{0};
  ConnectionState m_state{ConnectionState::NeverConnected};
  Phase m_phase{Phase::Idle};
  std::string m_status;
  bool m_bootstrapping{false};
  // BootstrapEnd seen on this connection; until then the mirror is either
  // empty or a frozen view from an earlier session and must not emit.
  bool m_bootstrapped{false};

  Clock::time_point m_attemptStart{};
  Clock::time_point m_pingSentAt{};
  Clock::time_point m_lostAt{};
  Clock::time_point m_nextRetryAt{};
  std::chrono::milliseconds m_retryDelay{0};
  bool m_autoRetryEnabled{false};

  protocol::FrameConfig m_frameConfig;
  std::unique_ptr<Project> m_project;
  std::unique_ptr<ProjectOps> m_projectOps;
  std::optional<protocol::TimeAdvanceWarning> m_timeAdvanceWarning;
  protocol::SubtreePtr m_uiState;
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
