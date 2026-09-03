// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectOpDispatcher.h"
#include "ArrayHistogram.h"
#include "RemoteBrowse.h"
// vsr_scivis_studio_protocol
#include "ProjectSnapshot.h"
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
constexpr RequestPolicy policyOf()
{
  static_assert(sizeof(Request) == 0,
      "no RequestPolicy row for this ProjectRequest alternative");
  return {};
}

#define VSR_REQUEST_POLICY(Request, launches, reads, mutates)                  \
  template <>                                                                  \
  constexpr RequestPolicy policyOf<Request>()                                  \
  {                                                                            \
    return {launches, reads, mutates};                                         \
  }

// clang-format off
// One row per ProjectRequest alternative, in the variant's order:
// launchesTask, readsProjectAtDispatch, mutates.
//                                                 launches reads  mutates
// Project
VSR_REQUEST_POLICY(NewProject,                     false,   true,  true)
VSR_REQUEST_POLICY(OpenProject,                    true,    false, true)
VSR_REQUEST_POLICY(SaveProject,                    true,    false, true)
// Datasets
VSR_REQUEST_POLICY(ImportStaticDataset,            true,    false, true)
VSR_REQUEST_POLICY(ImportFileAnimationDataset,     true,    false, true)
VSR_REQUEST_POLICY(DeclareFileAnimationDataset,    false,   true,  true)
VSR_REQUEST_POLICY(ReimportDataset,                true,    false, true)
VSR_REQUEST_POLICY(RenameDataset,                  false,   true,  true)
VSR_REQUEST_POLICY(RemoveDataset,                  false,   true,  true)
VSR_REQUEST_POLICY(LoadDataset,                    true,    false, true)
VSR_REQUEST_POLICY(UnloadDataset,                  false,   true,  true)
VSR_REQUEST_POLICY(RefreshDatasetAvailability,     false,   true,  true)
VSR_REQUEST_POLICY(SaveDatasetArchive,             true,    false, true)
VSR_REQUEST_POLICY(LoadDatasetArchive,             true,    false, true)
VSR_REQUEST_POLICY(DiscoverDatasetCandidates,      false,   true,  false)
VSR_REQUEST_POLICY(IncorporateDatasetCandidate,    true,    false, true)
// Shots
VSR_REQUEST_POLICY(CreateShot,                     false,   true,  true)
VSR_REQUEST_POLICY(RemoveShot,                     false,   true,  true)
VSR_REQUEST_POLICY(UpdateShot,                     false,   true,  true)
VSR_REQUEST_POLICY(SetActiveShot,                  false,   true,  true)
VSR_REQUEST_POLICY(SetPlaying,                     false,   true,  true)
// Light rigs
VSR_REQUEST_POLICY(CreateLightRig,                 false,   true,  true)
VSR_REQUEST_POLICY(CloneLightRig,                  false,   true,  true)
VSR_REQUEST_POLICY(RemoveLightRig,                 false,   true,  true)
VSR_REQUEST_POLICY(RenameLightRig,                 false,   true,  true)
VSR_REQUEST_POLICY(AddLightToRig,                  false,   true,  true)
VSR_REQUEST_POLICY(RemoveLightFromRig,             false,   true,  true)
// Camera rigs
VSR_REQUEST_POLICY(CreateCameraRig,                false,   true,  true)
VSR_REQUEST_POLICY(RemoveCameraRig,                false,   true,  true)
VSR_REQUEST_POLICY(RenameCameraRig,                false,   true,  true)
VSR_REQUEST_POLICY(SaveCameraRigArchive,           false,   true,  true)
VSR_REQUEST_POLICY(LoadCameraRigArchive,           false,   true,  true)
VSR_REQUEST_POLICY(SaveLightRigArchive,            false,   true,  true)
VSR_REQUEST_POLICY(LoadLightRigArchive,            false,   true,  true)
// Color maps
VSR_REQUEST_POLICY(CreateColorMap,                 false,   true,  true)
VSR_REQUEST_POLICY(RenameColorMap,                 false,   true,  true)
VSR_REQUEST_POLICY(RemoveColorMap,                 false,   true,  true)
// Remote Browse, viewport and tasks
VSR_REQUEST_POLICY(ListRoots,                      false,   false, false)
VSR_REQUEST_POLICY(ListDirectory,                  false,   false, false)
VSR_REQUEST_POLICY(RequestArrayHistogram,          false,   true,  false)
VSR_REQUEST_POLICY(RenderShot,                     true,    true,  true)
VSR_REQUEST_POLICY(CancelTask,                     false,   false, false)
// clang-format on

#undef VSR_REQUEST_POLICY

} // namespace

RequestPolicy policyOf(const ProjectRequest &request)
{
  return std::visit(
      [](const auto &r) { return policyOf<std::decay_t<decltype(r)>>(); },
      request);
}

bool waitsForQueuedTasks(const ProjectRequest &request)
{
  return policyOf(request).readsProjectAtDispatch;
}

bool independentOfQueuedTasks(const ProjectRequest &request)
{
  const auto policy = policyOf(request);
  return !policy.launchesTask && !policy.readsProjectAtDispatch
      && !policy.mutates;
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
  return renderActive() && policyOf(request).mutates;
}

void ProjectOpDispatcher::runOneTask()
{
  const auto ran = m_host.tasks->runOne();
  if (ran && ran->result.projectChanged)
    sendSnapshot();
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

DatasetStatus ProjectOpDispatcher::datasetStatus(
    const Project &project, const DatasetID &id)
{
  const auto *dataset = project::findDataset(project, id);
  return dataset ? dataset->status : DatasetStatus::Unavailable;
}

void ProjectOpDispatcher::finish(
    const ProjectOpReply &reply, bool projectChanged, bool rebind)
{
  if (rebind)
    m_host.rebindActiveShot();
  m_host.flushScenePushes();
  m_host.send(encode(reply));
  if (projectChanged)
    sendSnapshot();
}

void ProjectOpDispatcher::fail(
    uint64_t requestId, const std::string &error, bool projectChanged)
{
  vsr::core::logWarning("[StudioServer] request %llu refused: %s",
      static_cast<unsigned long long>(requestId),
      error.c_str());
  finish(makeErrorReply(requestId, error), projectChanged, false);
}

void ProjectOpDispatcher::sendSnapshot()
{
  m_host.send(encode(ProjectSnapshot{project()}));
}

// Project ////////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const NewProject &req)
{
  context().createUnsavedProject();
  *m_host.uiState = nullptr;
  finish(makeOkReply(req.requestId), true, true);
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
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RenameDataset &req)
{
  std::string error;
  if (!context().renameDataset(req.datasetId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RemoveDataset &req)
{
  std::string error;
  if (!context().removeDataset(req.datasetId, req.keepAssetFile, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const UnloadDataset &req)
{
  const auto statusBefore = datasetStatus(project(), req.datasetId);
  std::string error;
  if (!context().unloadDataset(req.datasetId, &error)) {
    // A refused unload may still have re-marked the dataset.
    fail(req.requestId,
        error,
        datasetStatus(project(), req.datasetId) != statusBefore);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RefreshDatasetAvailability &req)
{
  auto *dataset = project::findDataset(project(), req.datasetId);
  if (!dataset) {
    fail(req.requestId, "dataset not found");
    return;
  }
  const auto before = dataset->status;
  context().refreshUnloadedDatasetAvailability(*dataset);
  finish(makeOkReply(req.requestId), dataset->status != before, false);
}

void ProjectOpDispatcher::handle(const DiscoverDatasetCandidates &req)
{
  DiscoverDatasetCandidatesResult candidates;
  for (const auto &candidate : context().discoverDatasetCandidates())
    candidates.candidates.push_back({candidate.file, candidate.proposedName});
  auto reply = makeOkReply(req.requestId);
  setResults(reply, candidates);
  finish(reply, false, false);
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
  finish(reply, true, true); // the new shot is the active one
}

void ProjectOpDispatcher::handle(const RemoveShot &req)
{
  const bool wasActive = project().activeShotId == req.shotId;
  std::string error;
  if (!context().removeShot(req.shotId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, wasActive);
}

void ProjectOpDispatcher::handle(const UpdateShot &req)
{
  std::string error;
  if (!context().updateShot(req.shot, &error)) {
    fail(req.requestId, error);
    return;
  }
  const bool isActive = project().activeShotId == req.shot.id;
  finish(makeOkReply(req.requestId), true, isActive);
}

void ProjectOpDispatcher::handle(const SetActiveShot &req)
{
  std::string error;
  if (!context().setActiveShot(req.shotId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, true);
}

// Play/pause is a confirmed mutation: the reply says it took, the snapshot
// carries playing plus the frame time rests on (currentFrame). The ticking
// itself belongs to the server loop.
void ProjectOpDispatcher::handle(const SetPlaying &req)
{
  std::string error;
  if (!context().setPlaying(req.shotId, req.playing, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
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
  finish(reply, true, false);
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
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RemoveLightRig &req)
{
  if (!context().removeLightRig(req.lightRigId)) {
    fail(req.requestId, "light rig not found");
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RenameLightRig &req)
{
  std::string error;
  if (!context().renameLightRig(req.lightRigId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
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
  finish(reply, true, false);
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
  finish(makeOkReply(req.requestId), true, false);
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
  finish(makeOkReply(req.requestId), false, false);
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
  finish(reply, true, false);
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
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RemoveCameraRig &req)
{
  if (!context().removeCameraRig(req.cameraRigId)) {
    fail(req.requestId, "camera rig not found");
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RenameCameraRig &req)
{
  std::string error;
  if (!context().renameCameraRig(req.cameraRigId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
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
  finish(makeOkReply(req.requestId), false, false);
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
  finish(reply, true, false);
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
  finish(reply, true, false);
}

void ProjectOpDispatcher::handle(const RenameColorMap &req)
{
  std::string error;
  if (!context().renameColorMap(req.colorMapId, req.newName, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

void ProjectOpDispatcher::handle(const RemoveColorMap &req)
{
  std::string error;
  if (!context().removeColorMap(req.colorMapId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), true, false);
}

// Remote Browse and tasks ////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const ListRoots &req)
{
  auto reply = makeOkReply(req.requestId);
  setResults(reply, listRoots(*m_host.dataRoots));
  finish(reply, false, false);
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
  finish(reply, false, false);
}

void ProjectOpDispatcher::handle(const CancelTask &req)
{
  std::string error;
  if (!m_host.tasks->cancel(req.taskId, &error)) {
    fail(req.requestId, error);
    return;
  }
  finish(makeOkReply(req.requestId), false, false);
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
  finish(reply, false, false);
}

} // namespace vsr::scivis_studio::server
