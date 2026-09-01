// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PlaybackMessages.h"
#include "PayloadMacros.h"

namespace vsr::scivis_studio::protocol {

void toNode(const SetPlaying &p, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", p.requestId);
  writeChild(n, "shotId", p.shotId);
  writeChild(n, "playing", p.playing);
}

bool fromNode(const vsr::core::DataNode &n, SetPlaying &p)
{
  return readChild(n, "requestId", p.requestId)
      && readChild(n, "shotId", p.shotId) && readChild(n, "playing", p.playing);
}

void toNode(const SetTime &t, vsr::core::DataNode &n)
{
  writeChild(n, "shotId", t.shotId);
  writeChild(n, "frame", t.frame);
}

bool fromNode(const vsr::core::DataNode &n, SetTime &t)
{
  return readChild(n, "shotId", t.shotId) && readChild(n, "frame", t.frame);
}

void toNode(const TimeAdvanceWarning &w, vsr::core::DataNode &n)
{
  writeChild(n, "shotId", w.shotId);
  writeChild(n, "frame", w.frame);
  writeChild(n, "message", w.message);
}

bool fromNode(const vsr::core::DataNode &n, TimeAdvanceWarning &w)
{
  if (!readChild(n, "shotId", w.shotId) || !readChild(n, "frame", w.frame))
    return false;
  w.message = readChildOr(n, "message", std::string());
  return true;
}

VSR_STUDIO_ID_REQUEST(RenderShot, shotId)

} // namespace vsr::scivis_studio::protocol
