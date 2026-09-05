// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOpDispatcher.h"
#include "ArrayHistogram.h"
#include "RemoteBrowse.h"
// vsr_scivis_studio_protocol
#include "StudioCodec.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace vsr::scivis_studio::server {

using namespace protocol;
using vsr::network::Message;

// Request plumbing ///////////////////////////////////////////////////////////

namespace {

template <typename T>
std::optional<ProjectRequest> decodeAs(const Message &msg)
{
  auto payload = decode<T>(msg);
  if (!payload)
    return {};
  return ProjectRequest{std::move(*payload)};
}

template <size_t I>
using RequestAlternative = std::variant_alternative_t<I, ProjectRequest>;

// The alternatives are the one list of request types: each carries its
// MESSAGE_TYPE, so both the type test and the decoder fold over them.
template <size_t... I>
bool isProjectRequestTypeOf(StudioMessageType type, std::index_sequence<I...>)
{
  return ((RequestAlternative<I>::MESSAGE_TYPE == type) || ...);
}

template <size_t... I>
std::optional<ProjectRequest> decodeProjectRequestOf(
    const Message &msg, StudioMessageType type, std::index_sequence<I...>)
{
  std::optional<ProjectRequest> decoded;
  const auto tryOne = [&](auto tag) {
    using T = RequestAlternative<decltype(tag)::value>;
    if (T::MESSAGE_TYPE != type)
      return false;
    decoded = decodeAs<T>(msg);
    return true;
  };
  (tryOne(std::integral_constant<size_t, I>{}) || ...);
  return decoded;
}

constexpr auto REQUEST_ALTERNATIVES =
    std::make_index_sequence<std::variant_size_v<ProjectRequest>>{};

} // namespace

bool isProjectRequestType(StudioMessageType type)
{
  return isProjectRequestTypeOf(type, REQUEST_ALTERNATIVES);
}

std::optional<ProjectRequest> decodeProjectRequest(const Message &msg)
{
  const auto type = messageType(msg);
  if (!type)
    return {};
  return decodeProjectRequestOf(msg, *type, REQUEST_ALTERNATIVES);
}

// Request policy /////////////////////////////////////////////////////////////

namespace {

// Only the rows below exist: a ProjectRequest alternative without one fails
// to compile here, so adding an alternative means adding its row.
template <typename Request>
constexpr RequestKind kindOf()
{
  static_assert(sizeof(Request) == 0,
      "no RequestKind row for this ProjectRequest alternative");
  return {};
}

#define VSR_REQUEST_KIND(Request, kind)                                        \
  template <>                                                                  \
  constexpr RequestKind kindOf<Request>()                                      \
  {                                                                            \
    return RequestKind::kind;                                                  \
  }

// clang-format off
// One row per ProjectRequest alternative, in the variant's order.
// Project
VSR_REQUEST_KIND(NewProject,                     SyncMutating)
VSR_REQUEST_KIND(OpenProject,                    Task)
VSR_REQUEST_KIND(SaveProject,                    Task)
// Datasets
VSR_REQUEST_KIND(ImportStaticDataset,            Task)
VSR_REQUEST_KIND(ImportSubtreeDataset,           Task)
VSR_REQUEST_KIND(ImportFileAnimationDataset,     Task)
VSR_REQUEST_KIND(DeclareFileAnimationDataset,    SyncMutating)
VSR_REQUEST_KIND(ReimportDataset,                Task)
VSR_REQUEST_KIND(RenameDataset,                  SyncMutating)
VSR_REQUEST_KIND(RemoveDataset,                  SyncMutating)
VSR_REQUEST_KIND(LoadDataset,                    Task)
VSR_REQUEST_KIND(UnloadDataset,                  SyncMutating)
VSR_REQUEST_KIND(RefreshDatasetAvailability,     SyncMutating)
VSR_REQUEST_KIND(SaveDatasetArchive,             Task)
VSR_REQUEST_KIND(LoadDatasetArchive,             Task)
VSR_REQUEST_KIND(DiscoverDatasetCandidates,      SyncReadOnly)
VSR_REQUEST_KIND(IncorporateDatasetCandidate,    Task)
// Shots
VSR_REQUEST_KIND(CreateShot,                     SyncMutating)
VSR_REQUEST_KIND(RemoveShot,                     SyncMutating)
VSR_REQUEST_KIND(UpdateShot,                     SyncMutating)
VSR_REQUEST_KIND(SetActiveShot,                  SyncMutating)
VSR_REQUEST_KIND(SetPlaying,                     SyncMutating)
// Light rigs
VSR_REQUEST_KIND(CreateLightRig,                 SyncMutating)
VSR_REQUEST_KIND(CloneLightRig,                  SyncMutating)
VSR_REQUEST_KIND(RemoveLightRig,                 SyncMutating)
VSR_REQUEST_KIND(RenameLightRig,                 SyncMutating)
VSR_REQUEST_KIND(AddLightToRig,                  SyncMutating)
VSR_REQUEST_KIND(RemoveLightFromRig,             SyncMutating)
// Camera rigs
VSR_REQUEST_KIND(CreateCameraRig,                SyncMutating)
VSR_REQUEST_KIND(RemoveCameraRig,                SyncMutating)
VSR_REQUEST_KIND(RenameCameraRig,                SyncMutating)
VSR_REQUEST_KIND(SaveCameraRigArchive,           SyncMutating)
VSR_REQUEST_KIND(LoadCameraRigArchive,           SyncMutating)
VSR_REQUEST_KIND(SaveLightRigArchive,            SyncMutating)
VSR_REQUEST_KIND(LoadLightRigArchive,            SyncMutating)
// Color maps
VSR_REQUEST_KIND(CreateColorMap,                 SyncMutating)
VSR_REQUEST_KIND(RenameColorMap,                 SyncMutating)
VSR_REQUEST_KIND(RemoveColorMap,                 SyncMutating)
// Remote Browse, viewport and tasks
VSR_REQUEST_KIND(ListRoots,                      Independent)
VSR_REQUEST_KIND(ListDirectory,                  Independent)
VSR_REQUEST_KIND(RequestArrayHistogram,          SyncReadOnly)
VSR_REQUEST_KIND(RenderShot,                     RenderShot)
VSR_REQUEST_KIND(CancelTask,                     Independent)
// clang-format on

#undef VSR_REQUEST_KIND

constexpr bool readsProjectAtDispatch(RequestKind kind)
{
  return kind == RequestKind::SyncMutating || kind == RequestKind::SyncReadOnly
      || kind == RequestKind::RenderShot;
}

constexpr bool mutates(RequestKind kind)
{
  return kind == RequestKind::SyncMutating || kind == RequestKind::Task
      || kind == RequestKind::RenderShot;
}

} // namespace

RequestKind kindOf(const ProjectRequest &request)
{
  return std::visit(
      [](const auto &r) { return kindOf<std::decay_t<decltype(r)>>(); },
      request);
}

bool waitsForQueuedTasks(const ProjectRequest &request)
{
  return readsProjectAtDispatch(kindOf(request));
}

bool independentOfQueuedTasks(const ProjectRequest &request)
{
  return kindOf(request) == RequestKind::Independent;
}

bool mutatesProject(const ProjectRequest &request)
{
  return mutates(kindOf(request));
}

// Dispatcher /////////////////////////////////////////////////////////////////

ProjectOpDispatcher::ProjectOpDispatcher(Host host) : m_host(std::move(host)) {}

void ProjectOpDispatcher::dispatch(const ProjectRequest &request)
{
  if (refuses(request)) {
    // Pause-and-refuse: the render owns the Project and the Scene until it
    // ends; whoever asked can try again afterwards.
    const auto requestId =
        std::visit([](const auto &r) { return r.requestId; }, request);
    fail(requestId, "render in progress");
    return;
  }
  std::visit([this](const auto &r) { handle(r); }, request);
}

bool ProjectOpDispatcher::refuses(const ProjectRequest &request) const
{
  return renderActive() && mutatesProject(request);
}

bool ProjectOpDispatcher::renderActive() const
{
  return m_host.tasks->exclusivePending();
}

ProjectContext &ProjectOpDispatcher::context()
{
  return *m_host.projectContext;
}

Project &ProjectOpDispatcher::project()
{
  return context().project();
}

void ProjectOpDispatcher::finish(const ProjectOpReply &reply)
{
  m_host.flushScenePushes();
  m_host.send(encode(reply));
}

void ProjectOpDispatcher::fail(uint64_t requestId, const std::string &error)
{
  vsr::core::logWarning("[StudioServer] request %llu refused: %s",
      static_cast<unsigned long long>(requestId),
      error.c_str());
  finish(makeErrorReply(requestId, error));
}

// Project ////////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const NewProject &req)
{
  context().createUnsavedProject();
  *m_host.uiState = nullptr;
  finish(makeOkReply(req.requestId));
}

// Datasets ///////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const DeclareFileAnimationDataset &req)
{
  if (req.sourceList.empty()) {
    fail(req.requestId, "source list is empty");
    return;
  }
  FileAnimationDatasetOptions options;
  options.setActiveShotFrameCount = req.setActiveShotFrameCount;
  auto *dataset = context().addDeclaredFileAnimationDataset(
      req.name, req.sourceList, req.importerType, options);
  if (!dataset) {
    fail(req.requestId, "dataset could not be declared");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, DatasetCreatedResult{dataset->id});
  finish(reply);
}

void ProjectOpDispatcher::handle(const RenameDataset &req)
{
  std::string error;
  if (!context().renameDataset(req.datasetId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const RemoveDataset &req)
{
  std::string error;
  if (!context().removeDataset(req.datasetId, req.keepAssetFile, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const UnloadDataset &req)
{
  std::string error;
  if (!context().unloadDataset(req.datasetId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const RefreshDatasetAvailability &req)
{
  auto *dataset = project::findDataset(project(), req.datasetId);
  if (!dataset) {
    fail(req.requestId, "dataset not found");
    return;
  }
  context().refreshUnloadedDatasetAvailability(*dataset);
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const DiscoverDatasetCandidates &req)
{
  DiscoverDatasetCandidatesResult candidates;
  for (const auto &candidate : context().discoverDatasetCandidates())
    candidates.candidates.push_back({candidate.file, candidate.proposedName});
  auto reply = makeOkReply(req.requestId);
  setResults(reply, candidates);
  finish(reply);
}

// Shots //////////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateShot &req)
{
  if (!context().addShot(req.name) || project().shots.empty()) {
    fail(req.requestId, "shot could not be created");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, ShotCreatedResult{project().shots.back().id});
  finish(reply);
}

void ProjectOpDispatcher::handle(const RemoveShot &req)
{
  std::string error;
  if (!context().removeShot(req.shotId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const UpdateShot &req)
{
  std::string error;
  if (!context().updateShot(req.shot, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const SetActiveShot &req)
{
  std::string error;
  if (!context().setActiveShot(req.shotId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

// Play/pause is a confirmed mutation: the reply says it took, the snapshot
// that follows carries playing plus the frame time rests on (currentFrame).
// The ticking itself belongs to the server loop.
void ProjectOpDispatcher::handle(const SetPlaying &req)
{
  std::string error;
  if (!context().setPlaying(req.shotId, req.playing, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

// Light rigs /////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateLightRig &req)
{
  auto *rig = context().createLightRig(req.name);
  if (!rig) {
    fail(req.requestId, "light rig could not be created");
    return;
  }
  // A new rig is bound to no shot: hide it like every other unbound rig.
  context().applyActiveShot();
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightRigCreatedResult{rig->id});
  finish(reply);
}

void ProjectOpDispatcher::handle(const CloneLightRig &req)
{
  if (!light_rig::findLightRig(project(), req.lightRigId)) {
    fail(req.requestId, "light rig not found");
    return;
  }
  auto *clone = context().cloneLightRig(req.lightRigId);
  if (!clone) {
    fail(req.requestId, "light rig could not be cloned");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightRigCreatedResult{clone->id});
  finish(reply);
}

void ProjectOpDispatcher::handle(const RemoveLightRig &req)
{
  if (!context().removeLightRig(req.lightRigId)) {
    fail(req.requestId, "light rig not found");
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const RenameLightRig &req)
{
  std::string error;
  if (!context().renameLightRig(req.lightRigId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const AddLightToRig &req)
{
  auto *rig = light_rig::findLightRig(project(), req.lightRigId);
  if (!rig) {
    fail(req.requestId, "light rig not found");
    return;
  }
  if (!light_rig::isKnownLightSubtype(req.subtype)) {
    fail(req.requestId, "unknown light subtype '" + req.subtype + "'");
    return;
  }
  auto node = context().addLightToRig(*rig, req.subtype);
  if (!node) {
    fail(req.requestId, "light rig has no scene node");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightAddedResult{context().refFor("studio", node)});
  finish(reply);
}

void ProjectOpDispatcher::handle(const RemoveLightFromRig &req)
{
  auto *rig = light_rig::findLightRig(project(), req.lightRigId);
  if (!rig) {
    fail(req.requestId, "light rig not found");
    return;
  }
  auto node = context().resolve(req.lightNode);
  if (!node || !context().removeLightFromRig(*rig, node)) {
    fail(req.requestId, "node is not a light of that rig");
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const SaveLightRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  if (!context().saveLightRigArchive(req.lightRigId, *file, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const LoadLightRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  auto *rig = context().loadLightRigArchive(*file, &error);
  if (!rig) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, LightRigCreatedResult{rig->id});
  finish(reply);
}

// Camera rigs ////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateCameraRig &req)
{
  auto *rig = context().createCameraRig(req.name);
  if (!rig) {
    fail(req.requestId, "camera rig could not be created");
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, CameraRigCreatedResult{rig->id});
  finish(reply);
}

void ProjectOpDispatcher::handle(const RemoveCameraRig &req)
{
  if (!context().removeCameraRig(req.cameraRigId)) {
    fail(req.requestId, "camera rig not found");
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const RenameCameraRig &req)
{
  std::string error;
  if (!context().renameCameraRig(req.cameraRigId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const SaveCameraRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  if (!context().saveCameraRigArchive(req.cameraRigId, *file, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const LoadCameraRigArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  auto *rig = context().loadCameraRigArchive(*file, &error);
  if (!rig) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, CameraRigCreatedResult{rig->id});
  finish(reply);
}

// Color maps /////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const CreateColorMap &req)
{
  auto *record = context().createColorMap(req.name);
  if (!record) {
    fail(req.requestId, "color map could not be created");
    return;
  }
  ColorMapCreatedResult result;
  result.colorMapId = record->id;
  if (auto array = context().resolveColorMapArray(record->id))
    result.object = {array->type(), array.index()};
  auto reply = makeOkReply(req.requestId);
  setResults(reply, result);
  finish(reply);
}

void ProjectOpDispatcher::handle(const RenameColorMap &req)
{
  std::string error;
  if (!context().renameColorMap(req.colorMapId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

void ProjectOpDispatcher::handle(const RemoveColorMap &req)
{
  std::string error;
  if (!context().removeColorMap(req.colorMapId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

// Remote Browse and tasks ////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const ListRoots &req)
{
  auto reply = makeOkReply(req.requestId);
  setResults(reply, listRoots(*m_host.dataRoots));
  finish(reply);
}

void ProjectOpDispatcher::handle(const ListDirectory &req)
{
  ListDirectoryResult listing;
  std::string error;
  if (!listDirectory(*m_host.dataRoots, req.directory, listing, &error)) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, listing);
  finish(reply);
}

void ProjectOpDispatcher::handle(const CancelTask &req)
{
  std::string error;
  if (!m_host.tasks->cancel(req.taskId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId));
}

// Viewport ///////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const RequestArrayHistogram &req)
{
  auto *appContext = context().appContext();
  const auto &ref = req.array;
  vsr::scene::ArrayRef array;
  if (appContext && ref.type == ANARI_ARRAY)
    array = appContext->vsr.scene.getObject<vsr::scene::Array>(ref.objectIndex);
  if (!array) {
    fail(req.requestId,
        std::string("(") + anari::toString(ref.type) + ", "
            + std::to_string(ref.objectIndex) + ") is not an array");
    return;
  }

  ArrayHistogramResult result;
  std::string error;
  if (!computeArrayHistogram(*array, req.binCount, result, &error)) {
    fail(req.requestId, error);
    return;
  }
  auto reply = makeOkReply(req.requestId);
  setResults(reply, result);
  finish(reply);
}

} // namespace vsr::scivis_studio::server
