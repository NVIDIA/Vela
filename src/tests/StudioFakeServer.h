// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "ProjectSnapshot.h"
#include "SceneMessages.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

/*
 * A Studio server reduced to its session behaviour, for the client-core
 * tests: Hello on accept, the prebuilt bootstrap on the client's Hello, Pong
 * on Ping unless silenced, and every other request handed to `onRequest` so
 * a test can script the replies. Everything runs on the server's IO thread;
 * the test reads the counters.
 *
 * Example:
 *   vsr::scene::Scene source;
 *   populateFakeScene(source);
 *   FakeStudioServer server;
 *   server.bootstrap = makeFakeBootstrap(source);
 *   server.onRequest = [&](const Message &msg) { ... server.send(reply); };
 *   connection.connect("127.0.0.1", short(server.port()));
 */

inline constexpr const char *FAKE_LAYER_NAME = "extra";
inline constexpr const char *FAKE_GEOMETRY_NAME = "bootstrapped geometry";

// One sphere under a named extra layer: enough for the mirror to be
// observably populated.
inline void populateFakeScene(vsr::scene::Scene &scene)
{
  auto *layer = scene.addLayer(FAKE_LAYER_NAME);
  auto geometry = scene.createObject<vsr::scene::Geometry>("sphere");
  geometry->setName(FAKE_GEOMETRY_NAME);
  geometry->setParameter("radius", 0.25f);
  scene.insertChildObjectNode(layer->root(), geometry);
}

// BootstrapBegin, scene, layer, frame config, snapshot; End is sent
// separately so a test can hold it back.
inline std::vector<vsr::network::Message> makeFakeBootstrap(
    vsr::scene::Scene &source)
{
  using namespace vsr::scivis_studio::protocol;
  namespace messages = vsr::network::messages;

  std::vector<vsr::network::Message> out;
  out.push_back(encode(BootstrapBegin{}));
  messages::TransferScene scene(&source, false);
  out.push_back(encodeSceneMessage(scene, StudioMessageType::TransferScene));
  messages::TransferLayer layer(&source, source.layer(FAKE_LAYER_NAME));
  out.push_back(encodeSceneMessage(layer, StudioMessageType::TransferLayer));
  FrameConfig config;
  config.width = 640;
  config.height = 480;
  out.push_back(encode(config));
  ProjectSnapshot snapshot;
  snapshot.project.name = "fake project";
  out.push_back(encode(snapshot));
  return out;
}

struct FakeStudioServer
{
  using Message = vsr::network::Message;
  using StudioMessageType = vsr::scivis_studio::protocol::StudioMessageType;

  FakeStudioServer(
      int helloVersion = vsr::scivis_studio::protocol::PROTOCOL_VERSION);
  ~FakeStudioServer();

  unsigned short port() const;
  size_t count(StudioMessageType type);
  std::vector<Message> messagesOf(StudioMessageType type);

  void send(Message msg);
  void sendBootstrapEnd();
  // The prebuilt bracket, then End unless it is being held back.
  void sendBootstrap();
  void onMessage(const Message &msg);

  // Read on the IO thread at each accept; a test may change it between two.
  std::atomic<int> helloVersion{vsr::scivis_studio::protocol::PROTOCOL_VERSION};
  std::shared_ptr<vsr::network::NetworkServer> channel;
  std::vector<Message> bootstrap; // sent on the client's Hello, in order
  std::atomic<bool> holdBootstrap{false}; // answer Hello with nothing at all
  std::atomic<bool> holdBootstrapEnd{false};
  std::atomic<bool> silent{false};
  std::atomic<int> accepts{0};
  // Every message other than Hello and Ping, on the IO thread; set it before
  // the client connects.
  std::function<void(const Message &)> onRequest;
  std::mutex mutex;
  std::vector<Message> received;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline FakeStudioServer::FakeStudioServer(int helloVersion)
    : helloVersion(helloVersion)
{
  using namespace vsr::scivis_studio::protocol;

  channel = std::make_shared<vsr::network::NetworkServer>(0);
  channel->setConnectHandler([this]() {
    accepts++;
    Hello hello;
    hello.version = this->helloVersion;
    hello.buildInfo = "fake server";
    channel->send(encode(hello));
  });
  for (int value = 1; value < vsr::network::MESSAGE_TYPE_INVALID; ++value) {
    if (!isStudioMessageType(uint8_t(value)))
      continue;
    channel->registerHandler(
        uint8_t(value), [this](const Message &msg) { onMessage(msg); });
  }
  channel->start();
}

inline FakeStudioServer::~FakeStudioServer()
{
  channel->stop();
}

inline unsigned short FakeStudioServer::port() const
{
  return channel->port();
}

inline size_t FakeStudioServer::count(StudioMessageType type)
{
  std::lock_guard lock(mutex);
  size_t n = 0;
  for (const auto &msg : received)
    n += msg.header.type == uint8_t(type);
  return n;
}

inline std::vector<vsr::network::Message> FakeStudioServer::messagesOf(
    StudioMessageType type)
{
  std::lock_guard lock(mutex);
  std::vector<Message> out;
  for (const auto &msg : received)
    if (msg.header.type == uint8_t(type))
      out.push_back(msg);
  return out;
}

inline void FakeStudioServer::send(Message msg)
{
  channel->send(std::move(msg));
}

inline void FakeStudioServer::sendBootstrapEnd()
{
  send(encode(vsr::scivis_studio::protocol::BootstrapEnd{}));
}

inline void FakeStudioServer::sendBootstrap()
{
  for (const auto &m : bootstrap)
    channel->send(Message(m));
  if (!holdBootstrapEnd)
    sendBootstrapEnd();
}

inline void FakeStudioServer::onMessage(const Message &msg)
{
  using namespace vsr::scivis_studio::protocol;

  {
    std::lock_guard lock(mutex);
    received.push_back(msg);
  }
  switch (StudioMessageType(msg.header.type)) {
  case StudioMessageType::Hello:
    if (!holdBootstrap)
      sendBootstrap();
    break;
  case StudioMessageType::Ping:
    if (!silent)
      channel->send(encode(Pong{}));
    break;
  default:
    if (onRequest)
      onRequest(msg);
    break;
  }
}
