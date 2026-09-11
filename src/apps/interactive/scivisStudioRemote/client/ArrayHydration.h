// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectOps.h"
// vsr_scene
#include "vsr/scene/objects/Array.hpp"
// std
#include <set>
#include <string>

namespace vsr::scivis_studio::client {

struct ServerConnection;

/*
 * Turning a Structural Mirror proxy array into one that can be read and
 * edited here. The mirror holds every array as a proxy -- right dimensions
 * and element type, no data -- so a panel that wants to edit one asks for
 * its samples first (RequestArrayData), fills the mirror's array in place,
 * and only then says that local writes to it may travel (SetArrayData).
 *
 * That last step is the whole point of the ordering: the fill itself is a
 * map/unmap like any other, and declaring the array editable before it would
 * send the server its own samples straight back.
 *
 * Hydration is per array index and is dropped whenever the mirror is
 * replaced, because an index names a different array, or none, afterwards.
 * One request is outstanding at a time.
 *
 * No ImGui: this is the half of the Transfer Function editor's array seam
 * that can be driven from a test.
 *
 * Example:
 *   ArrayHydration hydration(&connection);
 *   if (!hydration.hydrated(array.index()))
 *     hydration.request(array);   // ...and try again next frame
 */
struct ArrayHydration
{
  explicit ArrayHydration(ServerConnection *connection);

  bool hydrated(size_t arrayIndex) const;

  // Sends at most one request; a no-op while one is in flight, while the
  // array is already hydrated, or while the session cannot send.
  void request(vsr::scene::Array &array);

  // Empty unless the last attempt failed, in which case it says why.
  const std::string &lastError() const;

  void dropMirrorReferences();

 private:
  ServerConnection *m_connection{nullptr};
  InFlight m_request;
  std::set<size_t> m_hydrated;
  std::string m_error;
};

} // namespace vsr::scivis_studio::client
