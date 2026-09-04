// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace vsr::scivis_studio::protocol {

// Where a Studio server listens: the port both executables assume when
// --port is not given, and the one parser both use to read it, so the client
// and the server accept exactly the same spellings.
constexpr uint16_t DEFAULT_PORT = 12345;

// Accepts a decimal integer in 1..65535 and nothing else (no sign, no
// whitespace, no trailing characters). False leaves `port` untouched.
bool parsePort(const std::string &text, uint16_t &port);

// 1: milestones 1-6. 2: TaskFailed carries framesCompleted (milestone 7).
// 3: Disconnect carries a reason and is the server's farewell too.
// 4: UpdateShot's bindings are the manifest's ordered list and ProjectSnapshot
//    carries the Project's Full form inline instead of a runtime sidecar.
// 5: a SceneObjectRef is one object-reference leaf; TaskCompleted/TaskFailed
//    carry a results subtree (RenderShotResult) instead of framesCompleted;
//    ImportSubtreeDataset is its own request.
constexpr int PROTOCOL_VERSION = 5;

/*
 * Complete v1 message set of the SciVis Studio client-server protocol, as one
 * table of (enumerator, wire value, direction). The enum, toString(),
 * isStudioMessageType() and isServerToClient() are all derived from it, so
 * adding a message means adding one row here.
 *
 * Values are explicit and grouped with gaps so later versions can grow a
 * group without renumbering. 0 is unused (sentinel) and 255 is never
 * assigned: the transport's MESSAGE_TYPE_INVALID is 255 (see
 * vsr/network/Message.hpp).
 *
 * Direction: Both for the session messages that either side may send;
 * ClientToServer for requests, optimistic edits and rendering controls;
 * ServerToClient for the bootstrap bracket, project and task replies, scene
 * pushes, FrameConfig and Frame.
 *
 * Reserved, deliberately not in the enum: dataset-subtree expansion, typed
 * channel frames (depth/AOV), and an NVENC frame encoding value.
 */
// clang-format off
#define STUDIO_MESSAGE_TYPES(X)                                  \
  /* Session (1..) */                                            \
  X(Hello,                         1, Both)                      \
  X(Error,                         2, Both)                      \
  X(Ping,                          3, Both)                      \
  X(Pong,                          4, Both)                      \
  X(Disconnect,                    5, Both)                      \
  X(Shutdown,                      6, ClientToServer)            \
  X(BootstrapBegin,                7, ServerToClient)            \
  X(BootstrapEnd,                  8, ServerToClient)            \
  /* Project ops, sync or task; all carry a requestId (20..) */  \
  X(NewProject,                   20, ClientToServer)            \
  X(OpenProject,                  21, ClientToServer)            \
  X(SaveProject,                  22, ClientToServer)            \
  X(ImportStaticDataset,          23, ClientToServer)            \
  X(ImportFileAnimationDataset,   24, ClientToServer)            \
  X(DeclareFileAnimationDataset,  25, ClientToServer)            \
  X(ReimportDataset,              26, ClientToServer)            \
  X(RenameDataset,                27, ClientToServer)            \
  X(RemoveDataset,                28, ClientToServer)            \
  X(LoadDataset,                  29, ClientToServer)            \
  X(UnloadDataset,                30, ClientToServer)            \
  X(RefreshDatasetAvailability,   31, ClientToServer)            \
  X(SaveDatasetArchive,           32, ClientToServer)            \
  X(LoadDatasetArchive,           33, ClientToServer)            \
  X(DiscoverDatasetCandidates,    34, ClientToServer)            \
  X(IncorporateDatasetCandidate,  35, ClientToServer)            \
  X(CreateShot,                   36, ClientToServer)            \
  X(RemoveShot,                   37, ClientToServer)            \
  X(UpdateShot,                   38, ClientToServer)            \
  X(SetActiveShot,                39, ClientToServer)            \
  X(CreateLightRig,               40, ClientToServer)            \
  X(CloneLightRig,                41, ClientToServer)            \
  X(RemoveLightRig,               42, ClientToServer)            \
  X(RenameLightRig,               43, ClientToServer)            \
  X(AddLightToRig,                44, ClientToServer)            \
  X(RemoveLightFromRig,           45, ClientToServer)            \
  X(CreateCameraRig,              46, ClientToServer)            \
  X(RemoveCameraRig,              47, ClientToServer)            \
  X(RenameCameraRig,              48, ClientToServer)            \
  X(SaveCameraRigArchive,         49, ClientToServer)            \
  X(LoadCameraRigArchive,         50, ClientToServer)            \
  X(SaveLightRigArchive,          51, ClientToServer)            \
  X(LoadLightRigArchive,          52, ClientToServer)            \
  X(CreateColorMap,               53, ClientToServer)            \
  X(RenameColorMap,               54, ClientToServer)            \
  X(RemoveColorMap,               55, ClientToServer)            \
  X(ListRoots,                    56, ClientToServer)            \
  X(ListDirectory,                57, ClientToServer)            \
  X(SetPlaying,                   58, ClientToServer)            \
  X(RequestArrayHistogram,        59, ClientToServer)            \
  X(RenderShot,                   60, ClientToServer)            \
  X(CancelTask,                   61, ClientToServer)            \
  X(Pick,                         62, ClientToServer)            \
  X(ImportSubtreeDataset,         63, ClientToServer)            \
  /* Project/task replies (100..) */                             \
  X(ProjectOpReply,              100, ServerToClient)            \
  X(ProjectSnapshot,             101, ServerToClient)            \
  X(TaskProgress,                102, ServerToClient)            \
  X(TaskCompleted,               103, ServerToClient)            \
  X(TaskFailed,                  104, ServerToClient)            \
  X(TimeAdvanceWarning,          105, ServerToClient)            \
  X(PickReply,                   106, ServerToClient)            \
  X(UIState,                     107, ServerToClient)            \
  /* Scene pushes (120..) */                                     \
  X(TransferScene,               120, ServerToClient)            \
  X(TransferLayer,               121, ServerToClient)            \
  X(ObjectAdded,                 122, ServerToClient)            \
  X(ObjectRemoved,               123, ServerToClient)            \
  /* Optimistic edits, no reply (140..) */                       \
  X(SetObjectParameter,          140, ClientToServer)            \
  X(RemoveObjectParameter,       141, ClientToServer)            \
  X(SetNodeTransform,            142, ClientToServer)            \
  X(SetTime,                     143, ClientToServer)            \
  X(SetOutline,                  144, ClientToServer)            \
  X(ViewportSettings,            145, ClientToServer)            \
  /* Rendering and frames (160..) */                             \
  X(SetFrameConfig,              160, ClientToServer)            \
  X(FrameConfig,                 161, ServerToClient)            \
  X(SetEncodings,                162, ClientToServer)            \
  X(StartRendering,              163, ClientToServer)            \
  X(StopRendering,               164, ClientToServer)            \
  X(Frame,                       165, ServerToClient)
// clang-format on

enum class StudioMessageType : uint8_t
{
#define X(name, value, dir) name = value,
  STUDIO_MESSAGE_TYPES(X)
#undef X
};

enum class MessageDirection
{
  Both,
  ClientToServer,
  ServerToClient
};

struct MessageTypeRow
{
  StudioMessageType type;
  const char *name;
  MessageDirection direction;
};

// clang-format off
constexpr std::array MESSAGE_TYPE_TABLE = {
#define X(name, value, dir) \
  MessageTypeRow{StudioMessageType::name, #name, MessageDirection::dir},
  STUDIO_MESSAGE_TYPES(X)
#undef X
};
// clang-format on

// True only for values the enum defines; receivers reject anything else.
constexpr bool isStudioMessageType(uint8_t value);

// Types only the server emits. A client sending one is confused, and a
// server tells it so rather than guessing. False for values outside the set.
constexpr bool isServerToClient(StudioMessageType type);

// Enumerator name, or "Unknown" for values outside the set.
const char *toString(StudioMessageType type);

// Inlined definitions ////////////////////////////////////////////////////////

// The table row for a type, or nullptr for values outside the set.
constexpr const MessageTypeRow *findMessageType(StudioMessageType type)
{
  for (const auto &row : MESSAGE_TYPE_TABLE)
    if (row.type == type)
      return &row;
  return nullptr;
}

constexpr bool isStudioMessageType(uint8_t value)
{
  return findMessageType(StudioMessageType(value)) != nullptr;
}

constexpr bool isServerToClient(StudioMessageType type)
{
  const auto *row = findMessageType(type);
  return row && row->direction == MessageDirection::ServerToClient;
}

// Table integrity: no row may take the sentinel, the transport's invalid
// value, or another row's value.
constexpr bool messageTypeTableIsWellFormed()
{
  for (size_t i = 0; i < MESSAGE_TYPE_TABLE.size(); ++i) {
    const auto v = uint8_t(MESSAGE_TYPE_TABLE[i].type);
    if (v == 0 || v == 255)
      return false;
    for (size_t j = 0; j < i; ++j)
      if (uint8_t(MESSAGE_TYPE_TABLE[j].type) == v)
        return false;
  }
  return true;
}
static_assert(messageTypeTableIsWellFormed());

} // namespace vsr::scivis_studio::protocol
