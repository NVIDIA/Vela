// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>

namespace vsr::scivis_studio::protocol {

// 1: milestones 1-6. 2: TaskFailed carries framesCompleted (milestone 7).
constexpr int PROTOCOL_VERSION = 2;

/*
 * Complete v1 message set of the SciVis Studio client-server protocol. Values
 * are explicit and grouped with gaps so later versions can grow a group
 * without renumbering. 0 is unused (sentinel) and 255 is never assigned: the
 * transport's MESSAGE_TYPE_INVALID is 255 (see vsr/network/Message.hpp).
 *
 * Reserved, deliberately not in the enum: dataset-subtree expansion, typed
 * channel frames (depth/AOV), and an NVENC frame encoding value.
 */
// clang-format off
enum class StudioMessageType : uint8_t
{
  // Session (1..)
  Hello = 1,
  Error = 2,
  Ping = 3,
  Pong = 4,
  Disconnect = 5,
  Shutdown = 6,
  BootstrapBegin = 7,
  BootstrapEnd = 8,

  // Project ops, client->server, sync or task; all carry a requestId (20..)
  NewProject = 20,
  OpenProject = 21,
  SaveProject = 22,
  ImportStaticDataset = 23,
  ImportFileAnimationDataset = 24,
  DeclareFileAnimationDataset = 25,
  ReimportDataset = 26,
  RenameDataset = 27,
  RemoveDataset = 28,
  LoadDataset = 29,
  UnloadDataset = 30,
  RefreshDatasetAvailability = 31,
  SaveDatasetArchive = 32,
  LoadDatasetArchive = 33,
  DiscoverDatasetCandidates = 34,
  IncorporateDatasetCandidate = 35,
  CreateShot = 36,
  RemoveShot = 37,
  UpdateShot = 38,
  SetActiveShot = 39,
  CreateLightRig = 40,
  CloneLightRig = 41,
  RemoveLightRig = 42,
  RenameLightRig = 43,
  AddLightToRig = 44,
  RemoveLightFromRig = 45,
  CreateCameraRig = 46,
  RemoveCameraRig = 47,
  RenameCameraRig = 48,
  SaveCameraRigArchive = 49,
  LoadCameraRigArchive = 50,
  SaveLightRigArchive = 51,
  LoadLightRigArchive = 52,
  CreateColorMap = 53,
  RenameColorMap = 54,
  RemoveColorMap = 55,
  ListRoots = 56,
  ListDirectory = 57,
  SetPlaying = 58,
  RequestArrayHistogram = 59,
  RenderShot = 60,
  CancelTask = 61,
  Pick = 62,

  // Project/task, server->client (100..)
  ProjectOpReply = 100,
  ProjectSnapshot = 101,
  TaskProgress = 102,
  TaskCompleted = 103,
  TaskFailed = 104,
  TimeAdvanceWarning = 105,
  PickReply = 106,
  UIState = 107,

  // Scene, server->client (120..)
  TransferScene = 120,
  TransferLayer = 121,
  ObjectAdded = 122,
  ObjectRemoved = 123,

  // Optimistic, client->server, no reply (140..)
  SetObjectParameter = 140,
  RemoveObjectParameter = 141,
  SetNodeTransform = 142,
  SetTime = 143,
  SetOutline = 144,
  ViewportSettings = 145,

  // Rendering and frames (160..)
  SetFrameConfig = 160,
  FrameConfig = 161,
  SetEncodings = 162,
  StartRendering = 163,
  StopRendering = 164,
  Frame = 165
};
// clang-format on

// True only for values the enum defines; receivers reject anything else.
constexpr bool isStudioMessageType(uint8_t value);

// Types only the server emits: the bootstrap bracket, project and task
// replies, scene pushes, FrameConfig and Frame. A client sending one is
// confused, and a server tells it so rather than guessing.
constexpr bool isServerToClient(StudioMessageType type);

// Enumerator name, or "Unknown" for values outside the set.
const char *toString(StudioMessageType type);

// Inlined definitions ////////////////////////////////////////////////////////

constexpr bool isStudioMessageType(uint8_t value)
{
  switch (StudioMessageType(value)) {
  case StudioMessageType::Hello:
  case StudioMessageType::Error:
  case StudioMessageType::Ping:
  case StudioMessageType::Pong:
  case StudioMessageType::Disconnect:
  case StudioMessageType::Shutdown:
  case StudioMessageType::BootstrapBegin:
  case StudioMessageType::BootstrapEnd:
  case StudioMessageType::NewProject:
  case StudioMessageType::OpenProject:
  case StudioMessageType::SaveProject:
  case StudioMessageType::ImportStaticDataset:
  case StudioMessageType::ImportFileAnimationDataset:
  case StudioMessageType::DeclareFileAnimationDataset:
  case StudioMessageType::ReimportDataset:
  case StudioMessageType::RenameDataset:
  case StudioMessageType::RemoveDataset:
  case StudioMessageType::LoadDataset:
  case StudioMessageType::UnloadDataset:
  case StudioMessageType::RefreshDatasetAvailability:
  case StudioMessageType::SaveDatasetArchive:
  case StudioMessageType::LoadDatasetArchive:
  case StudioMessageType::DiscoverDatasetCandidates:
  case StudioMessageType::IncorporateDatasetCandidate:
  case StudioMessageType::CreateShot:
  case StudioMessageType::RemoveShot:
  case StudioMessageType::UpdateShot:
  case StudioMessageType::SetActiveShot:
  case StudioMessageType::CreateLightRig:
  case StudioMessageType::CloneLightRig:
  case StudioMessageType::RemoveLightRig:
  case StudioMessageType::RenameLightRig:
  case StudioMessageType::AddLightToRig:
  case StudioMessageType::RemoveLightFromRig:
  case StudioMessageType::CreateCameraRig:
  case StudioMessageType::RemoveCameraRig:
  case StudioMessageType::RenameCameraRig:
  case StudioMessageType::SaveCameraRigArchive:
  case StudioMessageType::LoadCameraRigArchive:
  case StudioMessageType::SaveLightRigArchive:
  case StudioMessageType::LoadLightRigArchive:
  case StudioMessageType::CreateColorMap:
  case StudioMessageType::RenameColorMap:
  case StudioMessageType::RemoveColorMap:
  case StudioMessageType::ListRoots:
  case StudioMessageType::ListDirectory:
  case StudioMessageType::SetPlaying:
  case StudioMessageType::RequestArrayHistogram:
  case StudioMessageType::RenderShot:
  case StudioMessageType::CancelTask:
  case StudioMessageType::Pick:
  case StudioMessageType::ProjectOpReply:
  case StudioMessageType::ProjectSnapshot:
  case StudioMessageType::TaskProgress:
  case StudioMessageType::TaskCompleted:
  case StudioMessageType::TaskFailed:
  case StudioMessageType::TimeAdvanceWarning:
  case StudioMessageType::PickReply:
  case StudioMessageType::UIState:
  case StudioMessageType::TransferScene:
  case StudioMessageType::TransferLayer:
  case StudioMessageType::ObjectAdded:
  case StudioMessageType::ObjectRemoved:
  case StudioMessageType::SetObjectParameter:
  case StudioMessageType::RemoveObjectParameter:
  case StudioMessageType::SetNodeTransform:
  case StudioMessageType::SetTime:
  case StudioMessageType::SetOutline:
  case StudioMessageType::ViewportSettings:
  case StudioMessageType::SetFrameConfig:
  case StudioMessageType::FrameConfig:
  case StudioMessageType::SetEncodings:
  case StudioMessageType::StartRendering:
  case StudioMessageType::StopRendering:
  case StudioMessageType::Frame:
    return true;
  default:
    return false;
  }
}

constexpr bool isServerToClient(StudioMessageType type)
{
  switch (type) {
  case StudioMessageType::BootstrapBegin:
  case StudioMessageType::BootstrapEnd:
  case StudioMessageType::ProjectOpReply:
  case StudioMessageType::ProjectSnapshot:
  case StudioMessageType::TaskProgress:
  case StudioMessageType::TaskCompleted:
  case StudioMessageType::TaskFailed:
  case StudioMessageType::TimeAdvanceWarning:
  case StudioMessageType::PickReply:
  case StudioMessageType::UIState:
  case StudioMessageType::TransferScene:
  case StudioMessageType::TransferLayer:
  case StudioMessageType::ObjectAdded:
  case StudioMessageType::ObjectRemoved:
  case StudioMessageType::FrameConfig:
  case StudioMessageType::Frame:
    return true;
  default:
    return false;
  }
}

} // namespace vsr::scivis_studio::protocol
