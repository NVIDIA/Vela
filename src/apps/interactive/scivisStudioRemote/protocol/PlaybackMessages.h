// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>
#include <string>

namespace vsr::scivis_studio::protocol {

/*
 * Playback and time. The server free-runs playback and the wire speaks
 * integer frames plus a shot id (ADR 0035): SetPlaying is a sync project op,
 * SetTime is the optimistic latest-wins scrub, and TimeAdvanceWarning is the
 * server's non-modal "that frame failed to load, still playing" notice.
 * RenderShot sits here too: the offline render is a Server Task that walks
 * the shot's frames and pauses interactive frame delivery while it runs.
 *
 * Example:
 *   SetTime scrub;
 *   scrub.shotId = activeShot;
 *   scrub.frame = 42;
 *   channel.send(encode(scrub));
 */

struct SetPlaying
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SetPlaying;
  uint64_t requestId{0};
  ShotID shotId;
  bool playing{false};
};

struct SetTime
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::SetTime;
  ShotID shotId;
  int frame{0};
};

struct TimeAdvanceWarning
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::TimeAdvanceWarning;
  ShotID shotId;
  int frame{0};
  std::string message;
};

// Task-launching: answered by a ProjectOpReply carrying a TaskStartedResult.
struct RenderShot
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RenderShot;
  uint64_t requestId{0};
  ShotID shotId;
};

// Every payload is a fields() description (PayloadCommon.h). requestId,
// shotId, frame and playing are required; message is optional.

// Inlined definitions ////////////////////////////////////////////////////////

template <typename V>
void fields(V &v, SetPlaying &p)
{
  v.required("requestId", p.requestId);
  v.required("shotId", p.shotId);
  v.required("playing", p.playing);
}

template <typename V>
void fields(V &v, SetTime &t)
{
  v.required("shotId", t.shotId);
  v.required("frame", t.frame);
}

template <typename V>
void fields(V &v, TimeAdvanceWarning &w)
{
  v.required("shotId", w.shotId);
  v.required("frame", w.frame);
  v.optional("message", w.message);
}

template <typename V>
void fields(V &v, RenderShot &r)
{
  v.required("requestId", r.requestId);
  v.required("shotId", r.shotId);
}

} // namespace vsr::scivis_studio::protocol
