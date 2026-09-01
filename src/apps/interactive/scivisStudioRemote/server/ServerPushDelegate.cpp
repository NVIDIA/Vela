// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerPushDelegate.h"
// vsr_scivis_studio_protocol
#include "SceneMessages.h"
// vsr_network
#include "vsr/network/messages/NewObject.hpp"
#include "vsr/network/messages/RemoveObject.hpp"
#include "vsr/network/messages/TransferLayer.hpp"

namespace vsr::scivis_studio::server {

using namespace protocol;
namespace messages = vsr::network::messages;

ServerPushDelegate::ServerPushDelegate(vsr::scene::Scene *scene,
    SendFunction send,
    ResendSceneFunction requestSceneResend)
    : m_scene(scene),
      m_send(std::move(send)),
      m_requestSceneResend(std::move(requestSceneResend))
{}

bool ServerPushDelegate::enabled() const
{
  return m_enabled;
}

void ServerPushDelegate::setEnabled(bool enabled)
{
  m_enabled = enabled;
}

void ServerPushDelegate::signalObjectAdded(const vsr::scene::Object *obj)
{
  if (!m_enabled || !obj)
    return;
  messages::NewObject message(obj);
  m_send(encodeSceneMessage(message, StudioMessageType::ObjectAdded));
}

void ServerPushDelegate::signalObjectRemoved(const vsr::scene::Object *obj)
{
  if (!m_enabled || !obj)
    return;
  messages::RemoveObject message(obj);
  m_send(encodeSceneMessage(message, StudioMessageType::ObjectRemoved));
}

void ServerPushDelegate::signalRemoveAllObjects()
{
  if (!m_enabled)
    return;
  m_requestSceneResend();
}

void ServerPushDelegate::signalLayerAdded(const vsr::scene::Layer *layer)
{
  sendLayer(layer);
}

void ServerPushDelegate::signalLayerStructureUpdated(
    const vsr::scene::Layer *layer)
{
  sendLayer(layer);
}

void ServerPushDelegate::signalLayerTransformUpdated(
    const vsr::scene::Layer *layer)
{
  sendLayer(layer);
}

void ServerPushDelegate::sendLayer(const vsr::scene::Layer *layer)
{
  if (!m_enabled || !m_scene || !layer)
    return;
  messages::TransferLayer message(m_scene, layer);
  m_send(encodeSceneMessage(message, StudioMessageType::TransferLayer));
}

} // namespace vsr::scivis_studio::server
