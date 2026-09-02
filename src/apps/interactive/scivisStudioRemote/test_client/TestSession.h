// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "StudioCodec.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_network
#include "vsr/network/Message.hpp"
#include "vsr/network/NetworkChannel.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Any.hpp"
#include "vsr/core/TypeMacros.hpp"
#include "vsr/core/VSRMath.hpp"
// std
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::scivis_studio {
struct Project;
}

namespace vsr::scivis_studio::test_client {

// The client's Connection State (CONTEXT.md). Connected is entered with
// BootstrapEnd, when mirror and replica are the server's. Lost is involuntary:
// the Structural Mirror and Project Replica stay as a frozen view until a
// reconnect() or disconnect(). Disconnected is a completed intention: mirror
// and replica are cleared.
enum class SessionState
{
  NeverConnected,
  Connected,
  Lost,
  Disconnected
};

const char *toString(SessionState state);

// Liveness timings: a Ping after this much quiet, loss after this much
// silence. The spec's defaults; the liveness test shrinks them.
struct SessionTimings
{
  std::chrono::milliseconds pingAfterQuiet{5000};
  std::chrono::milliseconds lossAfterSilence{15000};
};

// One server message as the record stream reports it: the message name and a
// key/value summary (`Frame width=64 height=48 ...`).
struct Event
{
  std::string name;
  std::vector<std::pair<std::string, std::string>> fields;

  std::string text() const;
};

/*
 * The test client's connection to a Studio server: TCP connect, Hello
 * exchange with an exact version check, the bracketed Bootstrap into an owned
 * Structural Mirror and Project Replica, Ping/Pong liveness, loss detection,
 * and explicit disconnect, shutdown and reconnect. It is a second, independent
 * implementation of the client side of the protocol and shares no code with
 * the GUI client's session.
 *
 * Threading: every public member runs on the caller's thread. The network
 * handlers run on the channel's IO thread and only queue messages (Frames
 * into a latest-wins slot), stamp the traffic clock, answer Ping with Pong,
 * and latch the disconnect; poll() does everything else. Every received
 * message poll() consumes becomes an Event for takeEvent().
 *
 * Every wait takes a deadline and returns false with the reason when it
 * passes; nothing blocks unboundedly.
 *
 * Example:
 *   TestSession session;
 *   std::string error;
 *   if (!session.connect("127.0.0.1", 12345, 5s, &error))
 *     return fail(error);
 *   session.startRendering();
 *   session.pollUntil([&] { return session.framesReceived() > 0; }, 5s);
 *   Event event;
 *   while (session.takeEvent(event))
 *     std::cout << "EVT " << event.text() << '\n';
 */
struct TestSession
{
  explicit TestSession(SessionTimings timings = {});
  ~TestSession();

  VSR_NOT_COPYABLE(TestSession)
  VSR_NOT_MOVEABLE(TestSession)

  // Queries (valid between polls) //

  SessionState state() const;
  const std::string &host() const;
  int port() const;
  // The Structural Mirror; edits made through setParameter() and friends
  // land here as well as on the wire.
  vsr::scene::Scene &mirror();
  const vsr::scene::Scene &mirror() const;
  // The Project Replica; null before the first snapshot and once
  // Disconnected.
  const Project *project() const;
  const protocol::FrameConfig &frameConfig() const;
  // The newest Frame poll() consumed; empty until the first one.
  const std::optional<protocol::FrameHeader> &lastFrameHeader() const;
  const vsr::network::Message &lastFrame() const;
  // Frames poll() consumed (frames the slot dropped are not counted).
  size_t framesReceived() const;
  size_t errorsReceived() const;
  const std::string &lastError() const;
  // Why the last connect attempt failed or the link was Lost.
  const std::string &failure() const;

  // Session //

  // Connects, exchanges Hellos and waits for the complete Bootstrap. False
  // with the reason on refusal, version mismatch, socket loss or the deadline;
  // the state is then left as it was, and another connect() may follow at
  // once. Called on an open link it disconnect()s first.
  bool connect(const std::string &host,
      int port,
      std::chrono::milliseconds deadline,
      std::string *error = nullptr);
  // connect() again to the last host and port: a fresh handshake and
  // Bootstrap that wholesale-replace mirror and replica.
  bool reconnect(
      std::chrono::milliseconds deadline, std::string *error = nullptr);
  // Sends Disconnect, closes, clears mirror and replica -> Disconnected.
  void disconnect();
  // Sends Shutdown and waits for the server to close the socket ->
  // Disconnected. False when the socket is still open at the deadline (it is
  // then closed locally).
  bool shutdown(
      std::chrono::milliseconds deadline, std::string *error = nullptr);

  // Pumping //

  // Drains inbound messages into the mirror, replica and event queue, runs
  // the ping/loss timers and notices a lost link.
  void poll();
  // Pops the oldest unconsumed event; false when there is none.
  bool takeEvent(Event &out);
  // Polls until `done` holds or the deadline passes; false on timeout.
  bool pollUntil(
      const std::function<bool()> &done, std::chrono::milliseconds deadline);

  // Outbound (false with the reason unless Connected) //

  bool send(vsr::network::Message &&msg, std::string *error = nullptr);
  template <typename T>
  bool send(const T &payload, std::string *error = nullptr);
  // A message of an arbitrary type byte with verbatim payload bytes, for
  // probing the server's rejection paths.
  bool sendRaw(uint8_t type,
      std::vector<std::byte> payload,
      std::string *error = nullptr);
  bool ping(std::string *error = nullptr);
  bool setFrameConfig(
      uint32_t width, uint32_t height, std::string *error = nullptr);
  bool setEncodings(const std::vector<protocol::FrameEncoding> &preferred,
      std::string *error = nullptr);
  bool startRendering(std::string *error = nullptr);
  bool stopRendering(std::string *error = nullptr);
  // Optimistic scene edits: applied to the mirror and sent. False when the
  // mirror has no such object or layer. The node index of a transform is the
  // server's; the mirror is updated only when it has a transform node there.
  bool setParameter(const SceneObjectRef &object,
      const std::string &name,
      const vsr::core::Any &value,
      std::string *error = nullptr);
  bool removeParameter(const SceneObjectRef &object,
      const std::string &name,
      std::string *error = nullptr);
  bool setNodeTransform(const SceneNodeRef &node,
      const vsr::math::mat4 &transform,
      std::string *error = nullptr);

 private:
  using Clock = std::chrono::steady_clock;

  // Where the socket stands, independent of the user-facing state.
  enum class Phase
  {
    Idle,
    AwaitingHello,
    Established,
    Closing // Shutdown sent, waiting for the server to close
  };

  // IO thread
  void onInbound(const vsr::network::Message &msg);
  void onChannelClosed(const boost::system::error_code &error);
  void markTraffic();

  // Caller's thread
  void beginAttempt();
  void closeChannel();
  void setState(SessionState to);
  void linkFailed(const std::string &reason);
  void declareLoss(const std::string &reason);
  void attemptFailed(const std::string &reason);
  void finishDisconnect();
  void clearMirror();
  void checkSendFailures();
  void handleMessage(const vsr::network::Message &msg);
  void handleHello(const vsr::network::Message &msg);
  void applySceneMessage(
      protocol::StudioMessageType type, const vsr::network::Message &msg);
  void consumeFrame();
  void pushEvent(Event event);
  void replyError(const std::string &text);
  bool requireConnected(std::string *error) const;
  Clock::time_point lastTraffic() const;

  SessionTimings m_timings;
  std::shared_ptr<vsr::network::NetworkClient> m_channel;
  vsr::scene::Scene m_mirror;
  std::unique_ptr<Project> m_project;

  std::string m_host;
  int m_port{0};
  SessionState m_state{SessionState::NeverConnected};
  Phase m_phase{Phase::Idle};
  std::string m_failure; // why the last attempt failed or the link was lost
  bool m_bootstrapping{false};
  bool m_bootstrapped{false};

  Clock::time_point m_pingSentAt{};

  protocol::FrameConfig m_frameConfig;
  std::optional<protocol::FrameHeader> m_lastFrameHeader;
  vsr::network::Message m_lastFrame;
  size_t m_framesReceived{0};
  size_t m_errorsReceived{0};
  std::string m_lastError;
  std::deque<Event> m_events;
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
inline bool TestSession::send(const T &payload, std::string *error)
{
  return send(protocol::encode(payload), error);
}

} // namespace vsr::scivis_studio::test_client
