// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

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
  static constexpr StudioMessageType kType = StudioMessageType::SetPlaying;
  uint64_t requestId{0};
  ShotID shotId;
  bool playing{false};
};

struct SetTime
{
  static constexpr StudioMessageType kType = StudioMessageType::SetTime;
  ShotID shotId;
  int frame{0};
};

struct TimeAdvanceWarning
{
  static constexpr StudioMessageType kType =
      StudioMessageType::TimeAdvanceWarning;
  ShotID shotId;
  int frame{0};
  std::string message;
};

// Task-launching: answered by a ProjectOpReply carrying a TaskStartedResult.
struct RenderShot
{
  static constexpr StudioMessageType kType = StudioMessageType::RenderShot;
  uint64_t requestId{0};
  ShotID shotId;
};

// requestId, shotId and playing are required.
void toNode(const SetPlaying &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SetPlaying &);

// shotId and frame are required.
void toNode(const SetTime &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SetTime &);

// shotId and frame are required; message is optional.
void toNode(const TimeAdvanceWarning &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, TimeAdvanceWarning &);

// requestId and shotId are required.
void toNode(const RenderShot &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RenderShot &);

} // namespace vsr::scivis_studio::protocol
