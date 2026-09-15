// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RemoteArrayAccess.h"

namespace vsr::scivis_studio::client {

RemoteArrayAccess::RemoteArrayAccess(ServerConnection *connection)
    : m_hydration(connection)
{}

RemoteArrayAccess::~RemoteArrayAccess() = default;

bool RemoteArrayAccess::ready(vsr::scene::Array &array)
{
  if (m_hydration.hydrated(array.index()))
    return true;
  m_hydration.request(array);
  return false;
}

const char *RemoteArrayAccess::pendingReason() const
{
  const auto &error = m_hydration.lastError();
  return error.empty() ? "{fetching samples from the server...}"
                       : error.c_str();
}

void RemoteArrayAccess::dropMirrorReferences()
{
  m_hydration.dropMirrorReferences();
}

} // namespace vsr::scivis_studio::client
