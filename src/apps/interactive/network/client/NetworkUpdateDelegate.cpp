// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "NetworkUpdateDelegate.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_network
#include "vsr/network/messages/NewObject.hpp"
#include "vsr/network/messages/ParameterChange.hpp"
#include "vsr/network/messages/ParameterRemove.hpp"
#include "vsr/network/messages/RemoveObject.hpp"
#include "vsr/network/messages/TransferArrayData.hpp"
#include "vsr/network/messages/TransferLayer.hpp"

#include "../RenderSession.hpp"

#define CHECK_READY_OR_RETURN()                                                \
  if (!isReady(__func__))                                                      \
    return;

namespace vsr::network {

NetworkUpdateDelegate::NetworkUpdateDelegate(
    vsr::scene::Scene *scene, vsr::network::NetworkChannel *channel)
    : m_scene(scene)
{
  setNetworkChannel(channel);
}

void NetworkUpdateDelegate::setEnabled(bool enabled)
{
  m_enabled = enabled;
}

void NetworkUpdateDelegate::setNetworkChannel(
    vsr::network::NetworkChannel *channel)
{
  m_channel = channel;
}

void NetworkUpdateDelegate::signalObjectAdded(const vsr::scene::Object *o)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::NewObject(o);
  m_channel->send(MessageType::SERVER_ADD_OBJECT, std::move(msg));
}

void NetworkUpdateDelegate::signalParameterUpdated(
    const vsr::scene::Object *o, const vsr::scene::Parameter *p)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::ParameterChange(o, p);
  m_channel->send(MessageType::SERVER_SET_OBJECT_PARAMETER, std::move(msg));
}

void NetworkUpdateDelegate::signalParameterRemoved(
    const vsr::scene::Object *o, const vsr::scene::Parameter *p)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::ParameterRemove(o, p);
  m_channel->send(MessageType::SERVER_REMOVE_OBJECT_PARAMETER, std::move(msg));
}

void NetworkUpdateDelegate::signalParameterBatchUpdated(
    const vsr::scene::Object *o,
    const std::vector<const vsr::scene::Parameter *> &ps)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::ParameterChange(o, ps.data(), ps.size());
  m_channel->send(MessageType::SERVER_SET_OBJECT_PARAMETER, std::move(msg));
}

void NetworkUpdateDelegate::signalArrayMapped(const vsr::scene::Array *)
{
  CHECK_READY_OR_RETURN();
  // no-op
}

void NetworkUpdateDelegate::signalArrayUnmapped(const vsr::scene::Array *a)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::TransferArrayData(a);
  m_channel->send(MessageType::SERVER_SET_ARRAY_DATA, std::move(msg));
}

void NetworkUpdateDelegate::signalObjectParameterUseCountZero(
    const vsr::scene::Object *obj)
{
  CHECK_READY_OR_RETURN();
  // no-op
}

void NetworkUpdateDelegate::signalObjectLayerUseCountZero(
    const vsr::scene::Object *obj)
{
  CHECK_READY_OR_RETURN();
  // no-op
}

void NetworkUpdateDelegate::signalObjectRemoved(const vsr::scene::Object *o)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::RemoveObject(o);
  m_channel->send(MessageType::SERVER_REMOVE_OBJECT, std::move(msg));
}

void NetworkUpdateDelegate::signalRemoveAllObjects()
{
  CHECK_READY_OR_RETURN();
  m_channel->send(MessageType::SERVER_REMOVE_ALL_OBJECTS);
}

void NetworkUpdateDelegate::signalLayerAdded(const vsr::scene::Layer *l)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::TransferLayer(m_scene, l);
  m_channel->send(MessageType::SERVER_UPDATE_LAYER, std::move(msg));
}

void NetworkUpdateDelegate::signalLayerStructureUpdated(
    const vsr::scene::Layer *l)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::TransferLayer(m_scene, l);
  m_channel->send(MessageType::SERVER_UPDATE_LAYER, std::move(msg));
}

void NetworkUpdateDelegate::signalLayerTransformUpdated(
    const vsr::scene::Layer *l)
{
  CHECK_READY_OR_RETURN();
  auto msg = vsr::network::messages::TransferLayer(m_scene, l);
  m_channel->send(MessageType::SERVER_UPDATE_LAYER, std::move(msg));
}

void NetworkUpdateDelegate::signalLayerRemoved(const vsr::scene::Layer *)
{
  CHECK_READY_OR_RETURN();
  vsr::core::logWarning(
      "NetworkUpdateDelegate::signalLayerRemoved not implemented");
}

void NetworkUpdateDelegate::signalActiveLayersChanged()
{
  CHECK_READY_OR_RETURN();
  vsr::core::logWarning(
      "NetworkUpdateDelegate::signalActiveLayersChanged not implemented");
}

void NetworkUpdateDelegate::signalObjectFilteringChanged()
{
  CHECK_READY_OR_RETURN();
  vsr::core::logWarning(
      "NetworkUpdateDelegate::signalObjectFilteringChanged not implemented");
}

void NetworkUpdateDelegate::signalInvalidateCachedObjects()
{
  CHECK_READY_OR_RETURN();
  vsr::core::logWarning(
      "NetworkUpdateDelegate::signalInvalidateCachedObjects not implemented");
}

bool NetworkUpdateDelegate::isReady(const char *fcn) const
{
  if (!m_enabled) {
    return false;
  } else if (!m_channel) {
    vsr::core::logError("%s: no network channel", fcn);
    return false;
  }
  return true;
}

} // namespace vsr::network
