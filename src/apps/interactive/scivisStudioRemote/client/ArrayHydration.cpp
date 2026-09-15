// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ArrayHydration.h"
#include "ServerConnection.h"
// vsr_scivis_studio_protocol
#include "ViewportMessages.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <cstring>

namespace vsr::scivis_studio::client {

ArrayHydration::ArrayHydration(ServerConnection *connection)
    : m_connection(connection)
{}

bool ArrayHydration::hydrated(size_t arrayIndex) const
{
  return m_hydrated.count(arrayIndex) != 0;
}

const std::string &ArrayHydration::lastError() const
{
  return m_error;
}

void ArrayHydration::dropMirrorReferences()
{
  m_hydrated.clear();
  m_error.clear();
}

void ArrayHydration::request(vsr::scene::Array &array)
{
  if (!m_connection || !m_connection->canSend())
    return;
  if (hydrated(array.index()) || m_request.busy(m_connection->projectOps()))
    return;

  const size_t index = array.index();
  auto *scene = array.scene();

  protocol::RequestArrayData req;
  req.array = SceneObjectRef{ANARI_ARRAY, index};
  m_request.sendForResult<protocol::ArrayDataResult>(
      m_connection->projectOps(),
      std::move(req),
      [this, index, scene](const protocol::ProjectOpReply &reply,
          const std::optional<protocol::ArrayDataResult> &result) {
        if (!reply.ok) {
          m_error = reply.error;
          return;
        }
        if (!result || scene == nullptr) {
          m_error = "the reply carried no samples";
          return;
        }

        // The mirror may have been replaced while the request was in flight.
        auto target = scene->getObject<vsr::scene::Array>(index);
        if (!target || target->elementType() != result->elementType
            || target->size() != result->elementCount) {
          m_error = "the array changed while its samples were in flight";
          return;
        }

        if (target->isProxy())
          target->convertProxyToHost();
        if (!result->data.empty()) {
          std::memcpy(target->map(), result->data.data(), result->data.size());
          target->unmap();
        }

        // Only now: the fill above is itself a map/unmap, and declaring the
        // array editable first would bounce the server's samples back at it.
        m_connection->setArrayEditable(index, true);
        m_hydrated.insert(index);
        m_error.clear();
      });
}

} // namespace vsr::scivis_studio::client
