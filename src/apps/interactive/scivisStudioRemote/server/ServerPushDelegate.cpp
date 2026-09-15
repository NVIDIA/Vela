// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ServerPushDelegate.h"

namespace vsr::scivis_studio::server {

bool ServerPushDelegate::enabled() const
{
  return m_enabled;
}

void ServerPushDelegate::setEnabled(bool enabled)
{
  m_enabled = enabled;
}

bool ServerPushDelegate::sceneDirty() const
{
  return m_sceneDirty;
}

void ServerPushDelegate::clearSceneDirty()
{
  m_sceneDirty = false;
}

void ServerPushDelegate::signalObjectAdded(const vsr::scene::Object *obj)
{
  if (obj)
    markDirty();
}

void ServerPushDelegate::signalObjectRemoved(const vsr::scene::Object *obj)
{
  if (obj)
    markDirty();
}

void ServerPushDelegate::signalRemoveAllObjects()
{
  markDirty();
}

void ServerPushDelegate::signalLayerAdded(const vsr::scene::Layer *layer)
{
  if (layer)
    markDirty();
}

void ServerPushDelegate::signalLayerStructureUpdated(
    const vsr::scene::Layer *layer)
{
  if (layer)
    markDirty();
}

void ServerPushDelegate::signalLayerTransformUpdated(
    const vsr::scene::Layer *layer)
{
  if (layer)
    markDirty();
}

void ServerPushDelegate::markDirty()
{
  if (m_enabled)
    m_sceneDirty = true;
}

} // namespace vsr::scivis_studio::server
