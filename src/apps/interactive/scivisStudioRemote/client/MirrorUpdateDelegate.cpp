// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MirrorUpdateDelegate.h"
// vsr_scivis_studio_protocol
#include "SceneEditMessages.h"
#include "StudioCodec.h"
// vsr_scene
#include "vsr/scene/Object.hpp"
#include "vsr/scene/Parameter.hpp"
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
    if (o->metadataHoldsArray(name))
      continue; // arrays never ride this message
    // An absent value is the removal the server applies.
    edit.entries.push_back({name, o->getMetadataValue(name)});
  }
  if (edit.entries.empty())
    return;
  m_send(protocol::encode(edit));
}

} // namespace vsr::scivis_studio::client
