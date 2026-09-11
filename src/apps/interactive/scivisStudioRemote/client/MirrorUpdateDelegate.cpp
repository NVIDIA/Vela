// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MirrorUpdateDelegate.h"
// vsr_scivis_studio_protocol
#include "SceneEditMessages.h"
#include "SceneMessages.h"
#include "StudioCodec.h"
// vsr_network
#include "vsr/network/messages/TransferArrayData.hpp"
// vsr_scene
#include "vsr/scene/Object.hpp"
#include "vsr/scene/Parameter.hpp"
#include "vsr/scene/objects/Array.hpp"
// anari
#include <anari/anari_cpp.hpp>

namespace vsr::scivis_studio::client {

namespace {

SceneObjectRef refOf(const vsr::scene::Object *o)
{
  SceneObjectRef ref;
  ref.type = o->type();
  ref.objectIndex = o->index();
  return ref;
}

} // namespace

MirrorUpdateDelegate::MirrorUpdateDelegate(MessageSink send)
    : m_send(std::move(send))
{}

bool MirrorUpdateDelegate::enabled() const
{
  return m_enabled;
}

void MirrorUpdateDelegate::setEnabled(bool enabled)
{
  m_enabled = enabled;
}

void MirrorUpdateDelegate::signalParameterUpdated(
    const vsr::scene::Object *o, const vsr::scene::Parameter *p)
{
  if (m_enabled)
    sendParameter(o, p);
}

void MirrorUpdateDelegate::signalParameterBatchUpdated(
    const vsr::scene::Object *o,
    const std::vector<const vsr::scene::Parameter *> &ps)
{
  if (!m_enabled)
    return;
  for (const auto *p : ps)
    sendParameter(o, p);
}

void MirrorUpdateDelegate::signalParameterRemoved(
    const vsr::scene::Object *o, const vsr::scene::Parameter *p)
{
  if (!m_enabled || !m_send || !o || !p)
    return;
  protocol::RemoveObjectParameter edit;
  edit.object = refOf(o);
  edit.name = p->name().str();
  m_send(protocol::encode(edit));
}

void MirrorUpdateDelegate::signalMetadataUpdated(
    const vsr::scene::Object *o, const char *name)
{
  if (m_enabled && name)
    sendMetadata(o, {std::string(name)});
}

void MirrorUpdateDelegate::signalMetadataBatchUpdated(
    const vsr::scene::Object *o, const std::vector<std::string> &names)
{
  if (m_enabled)
    sendMetadata(o, names);
}

void MirrorUpdateDelegate::signalArrayUnmapped(const vsr::scene::Array *a)
{
  if (!m_enabled || a == nullptr || !arrayEditable(a->index()))
    return;
  // Inside a batch the array is only remembered: a widget that rewrites the
  // same samples several times before the batch closes still sends once.
  if (m_batchDepth > 0)
    m_pendingArrays[a->index()] = a;
  else
    sendArray(a);
}

void MirrorUpdateDelegate::signalUpdateBatchBegin()
{
  ++m_batchDepth;
}

void MirrorUpdateDelegate::signalUpdateBatchEnd()
{
  if (m_batchDepth > 0)
    --m_batchDepth;
  if (m_batchDepth > 0 || m_pendingArrays.empty())
    return;

  auto pending = std::move(m_pendingArrays);
  m_pendingArrays.clear();
  if (!m_enabled)
    return;
  for (const auto &[index, array] : pending) {
    if (arrayEditable(index))
      sendArray(array);
  }
}

void MirrorUpdateDelegate::setArrayEditable(size_t arrayIndex, bool editable)
{
  if (editable)
    m_editableArrays.insert(arrayIndex);
  else
    m_editableArrays.erase(arrayIndex);
}

void MirrorUpdateDelegate::clearEditableArrays()
{
  m_editableArrays.clear();
  m_pendingArrays.clear();
}

bool MirrorUpdateDelegate::arrayEditable(size_t arrayIndex) const
{
  return m_editableArrays.count(arrayIndex) != 0;
}

void MirrorUpdateDelegate::sendArray(const vsr::scene::Array *a)
{
  if (!m_send || a == nullptr || a->isProxy() || a->isEmpty())
    return;
  vsr::network::messages::TransferArrayData transfer(a);
  m_send(protocol::encodeSceneMessage<protocol::StudioMessageType::SetArrayData>(
      transfer));
}

void MirrorUpdateDelegate::sendParameter(
    const vsr::scene::Object *o, const vsr::scene::Parameter *p)
{
  if (!m_send || !o || !p)
    return;
  const auto &value = p->value();
  if (!value.valid() || anari::isArray(value.type()))
    return;
  protocol::SetObjectParameter edit;
  edit.object = refOf(o);
  edit.name = p->name().str();
  edit.value = value;
  m_send(protocol::encode(edit));
}

void MirrorUpdateDelegate::sendMetadata(
    const vsr::scene::Object *o, const std::vector<std::string> &names)
{
  if (!m_send || !o)
    return;
  protocol::SetObjectMetadata edit;
  edit.object = refOf(o);
  for (const auto &name : names) {
    protocol::ObjectMetadataEntry entry;
    entry.name = name;
    if (o->metadataHoldsArray(name)) {
      anari::DataType type = ANARI_UNKNOWN;
      const void *ptr = nullptr;
      size_t count = 0;
      o->getMetadataArray(name, &type, &ptr, &count);
      if (type == ANARI_UNKNOWN || ptr == nullptr)
        continue;
      const auto *bytes = static_cast<const std::byte *>(ptr);
      entry.arrayElementType = type;
      entry.arrayElementCount = count;
      entry.arrayData.assign(bytes, bytes + count * anari::sizeOf(type));
    } else {
      // An absent value is the removal the server applies.
      entry.value = o->getMetadataValue(name);
    }
    edit.entries.push_back(std::move(entry));
  }
  if (edit.entries.empty())
    return;
  m_send(protocol::encode(edit));
}

} // namespace vsr::scivis_studio::client
