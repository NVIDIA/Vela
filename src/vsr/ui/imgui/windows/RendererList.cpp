// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RendererList.h"
// std
#include <algorithm>

namespace vsr::ui::imgui {

using vsr::scene::Renderer;
using vsr::scene::RendererAppRef;

struct RendererList::SceneDelegate : public vsr::scene::EmptyUpdateDelegate
{
  explicit SceneDelegate(RendererList *list) : m_list(list) {}

  void signalObjectAdded(const vsr::scene::Object *o) override;
  void signalObjectRemoved(const vsr::scene::Object *o) override;
  void signalRemoveAllObjects() override;

 private:
  RendererList *m_list{nullptr};
};

void RendererList::SceneDelegate::signalObjectAdded(const vsr::scene::Object *o)
{
  if (o && o->type() == ANARI_RENDERER)
    m_list->rendererAdded(*static_cast<const Renderer *>(o));
}

void RendererList::SceneDelegate::signalObjectRemoved(
    const vsr::scene::Object *o)
{
  if (o && o->type() == ANARI_RENDERER)
    m_list->rendererRemoved(*static_cast<const Renderer *>(o));
}

void RendererList::SceneDelegate::signalRemoveAllObjects()
{
  m_list->allObjectsRemoved();
}

// RendererList definitions ///////////////////////////////////////////////////

RendererList::~RendererList()
{
  unbind();
}

bool RendererList::empty() const
{
  return m_objects.empty();
}

const std::vector<RendererAppRef> &RendererList::objects() const
{
  return m_objects;
}

RendererAppRef RendererList::current() const
{
  return m_current;
}

void RendererList::bind(vsr::scene::Scene &scene, vsr::core::Token deviceName)
{
  unbind();
  m_scene = &scene;
  m_deviceName = deviceName;
  m_delegate = scene.updateDelegate().emplace<SceneDelegate>(this);
  m_objects = scene.renderersOfDevice(deviceName);
  if (!m_objects.empty())
    m_current = m_objects.front();
}

void RendererList::unbind()
{
  if (m_scene)
    m_scene->updateDelegate().erase(m_delegate);
  m_scene = nullptr;
  m_deviceName = {};
  m_delegate = nullptr;
  m_objects.clear();
  m_current = {};
}

bool RendererList::select(size_t objectIndex)
{
  const auto pos = find(objectIndex);
  if (pos == m_objects.end())
    return false;
  m_current = *pos;
  return true;
}

void RendererList::rendererAdded(const Renderer &r)
{
  if (r.rendererDeviceName() != m_deviceName)
    return;

  // A new renderer can reuse a freed slot, so insert it in storage order.
  auto pos = std::find_if(m_objects.begin(), m_objects.end(), [&](auto &o) {
    return o->index() > r.index();
  });
  pos = m_objects.insert(pos, m_scene->getObject<Renderer>(r.index()));
  if (!m_current)
    m_current = *pos;
}

void RendererList::rendererRemoved(const Renderer &r)
{
  const auto pos = find(r.index());
  if (pos == m_objects.end())
    return;

  m_objects.erase(pos);
  if (!m_current || m_current->index() != r.index())
    return;

  const auto sameSubtype = std::find_if(m_objects.begin(),
      m_objects.end(),
      [&](auto &o) { return o->subtype() == r.subtype(); });
  if (sameSubtype != m_objects.end())
    m_current = *sameSubtype;
  else if (!m_objects.empty())
    m_current = m_objects.front();
  else
    m_current = {};
}

std::vector<RendererAppRef>::iterator RendererList::find(size_t objectIndex)
{
  return std::find_if(m_objects.begin(), m_objects.end(), [&](auto &o) {
    return o->index() == objectIndex;
  });
}

void RendererList::allObjectsRemoved()
{
  m_objects.clear();
  m_current = {};
}

} // namespace vsr::ui::imgui
