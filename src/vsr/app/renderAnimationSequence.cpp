// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/app/renderAnimationSequence.h"
// vsr_app
#include "vsr/app/ANARIDeviceManager.h"
#include "vsr/app/Context.h"
// vsr_core
#include "vsr/core/Logging.hpp"
#include "vsr/core/TypeMacros.hpp"
// vsr_rendering
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/pipeline/saveImage.h"
// std
#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace vsr::app {

OfflineParameterOverride::OfflineParameterOverride(vsr::scene::Object *object,
    vsr::core::Token parameterName,
    vsr::core::Any parameterValue)
    : name(parameterName), value(std::move(parameterValue))
{
  if (object && object->scene()) {
    m_scene = object->scene();
    m_object = vsr::core::Any(object->type(), object->index());
  }
}

vsr::scene::Object *OfflineParameterOverride::object() const
{
  return m_scene ? m_scene->getObject(m_object) : nullptr;
}

namespace {

/*
 * Own handles before any fallible setup. Declare before the pipeline so its
 * retained references disappear before the delegate, device and library.
 * The caller Context outlives this nontransferable owner; the Scene owns the
 * registered index, whose registration this guard removes before device
 * release.
 */
struct JobResources
{
  explicit JobResources(Context &context);
  ~JobResources();
  VSR_NOT_COPYABLE(JobResources)
  VSR_NOT_MOVEABLE(JobResources)

  anari::Library loadLibrary(
      const char *name, anari::StatusCallback callback, const void *user);
  anari::Device createDevice(const char *subtype);
  anari::Renderer createRenderer(const char *subtype);
  vsr::rendering::RenderIndexAllLayers *createRenderIndex(
      const std::string &deviceIdentifier);

 private:
  Context *m_ctx{nullptr};
  anari::Library m_library{nullptr};
  anari::Device m_device{nullptr};
  anari::Renderer m_renderer{nullptr};
  vsr::rendering::RenderIndexAllLayers *m_index{nullptr};
};

JobResources::JobResources(Context &context) : m_ctx(&context) {}

JobResources::~JobResources()
{
  if (m_index)
    m_ctx->vsr.scene.updateDelegate().erase(m_index);
  if (m_renderer)
    anari::release(m_device, m_renderer);
  if (m_device)
    anari::release(m_device, m_device);
  if (m_library)
    anari::unloadLibrary(m_library);
}

anari::Library JobResources::loadLibrary(
    const char *name, anari::StatusCallback callback, const void *user)
{
  m_library = anari::loadLibrary(name, callback, user);
  return m_library;
}

anari::Device JobResources::createDevice(const char *subtype)
{
  m_device = anari::newDevice(m_library, subtype);
  return m_device;
}

anari::Renderer JobResources::createRenderer(const char *subtype)
{
  m_renderer = anari::newObject<anari::Renderer>(m_device, subtype);
  return m_renderer;
}

vsr::rendering::RenderIndexAllLayers *JobResources::createRenderIndex(
    const std::string &deviceIdentifier)
{
  m_index = m_ctx->vsr.scene.updateDelegate()
                .emplace<vsr::rendering::RenderIndexAllLayers>(
                    m_ctx->vsr.scene, deviceIdentifier, m_device);
  return m_index;
}

/*
 * Borrow the caller Context and authored targets until synchronous restoration.
 * Never transfer this guard: exactly one owner must restore state and remove
 * the generated Camera, after the job's rendering resources have been released.
 */
struct CallerStateRestore
{
  struct SavedParameter
  {
    OfflineParameterOverride assignment;
    std::optional<vsr::scene::Parameter> parameter;
  };

  explicit CallerStateRestore(Context &context);
  ~CallerStateRestore();
  VSR_NOT_COPYABLE(CallerStateRestore)
  VSR_NOT_MOVEABLE(CallerStateRestore)

  void markAnimationAdvanced();
  void setGeneratedCamera(vsr::scene::Camera *camera);
  void save(const OfflineParameterOverride &assignment);
  void restore();

 private:
  void restoreParameterSnapshot(const SavedParameter &saved);

  Context *m_ctx{nullptr};
  bool m_restored{false};
  vsr::animation::AnimationManager::RuntimeState m_clock;
  std::vector<SavedParameter> m_parameters;
  bool m_advanced{false};
  vsr::scene::Camera *m_generatedCamera{nullptr};
};

CallerStateRestore::CallerStateRestore(Context &context)
    : m_ctx(&context), m_clock(context.vsr.animationMgr.runtimeState())
{}

void CallerStateRestore::markAnimationAdvanced()
{
  m_advanced = true;
}

void CallerStateRestore::setGeneratedCamera(vsr::scene::Camera *camera)
{
  m_generatedCamera = camera;
}

void CallerStateRestore::save(const OfflineParameterOverride &assignment)
{
  if (assignment.object() == m_generatedCamera)
    return;
  for (const auto &saved : m_parameters) {
    if (saved.assignment.object() == assignment.object()
        && saved.assignment.name == assignment.name)
      return;
  }
  const auto *parameter = assignment.object()->parameter(assignment.name);
  m_parameters.push_back({assignment,
      parameter ? std::optional<vsr::scene::Parameter>(*parameter)
                : std::nullopt});
}

CallerStateRestore::~CallerStateRestore()
{
  // Normal exits call restore() explicitly so cleanup exceptions propagate.
  // This fallback runs only during unwinding: a secondary callback exception
  // must not terminate the process or replace the original exception. No
  // diagnostics are allocated here; restore() still attempts independent steps.
  try {
    restore();
  } catch (...) {
  }
}

void CallerStateRestore::restoreParameterSnapshot(const SavedParameter &saved)
{
  auto *object = saved.assignment.object();
  if (!object)
    throw std::runtime_error("Override target removed during rendering");
  if (!saved.parameter) {
    object->removeParameter(saved.assignment.name);
    return;
  }
  auto *parameter = object->parameter(saved.assignment.name);
  if (!parameter)
    parameter = &object->addParameter(saved.assignment.name);
  // Restore metadata before notifying observers, which may throw. Preserve the
  // current observer: the owner may have moved since snapshot capture.
  const auto oldValue = parameter->value();
  parameter->restoreStateFrom(*saved.parameter);
  // Use the real previous value to keep object use counts and scene delegates
  // synchronized, with a single notification after the full state is restored.
  vsr::scene::ParameterObserver *observer = object;
  observer->parameterChanged(parameter, oldValue);
}

void CallerStateRestore::restore()
{
  if (m_restored)
    return;
  m_restored = true;
  std::exception_ptr failure;
  // Defer the first cleanup exception until all independent restoration steps
  // have run. These catches do not convert programmer errors to result values.
  if (m_advanced) {
    try {
      m_ctx->vsr.animationMgr.restoreRuntimeState(m_clock);
    } catch (...) {
      failure = std::current_exception();
    }
  }
  for (const auto &saved : m_parameters) {
    try {
      restoreParameterSnapshot(saved);
    } catch (...) {
      if (!failure)
        failure = std::current_exception();
    }
  }
  if (m_generatedCamera) {
    try {
      m_ctx->vsr.scene.removeObject(m_generatedCamera);
    } catch (...) {
      if (!failure)
        failure = std::current_exception();
    }
  }
  if (failure)
    std::rethrow_exception(failure);
}

bool contains(const char **values, const std::string &value)
{
  if (!values)
    return false;
  for (; *values; ++values) {
    if (value == *values)
      return true;
  }
  return false;
}

struct DeviceDiagnostics
{
  std::mutex mutex;
  std::string error;
};

void jobStatus(const void *user,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_ERROR
      || severity == ANARI_SEVERITY_FATAL_ERROR) {
    auto &diagnostics =
        *static_cast<DeviceDiagnostics *>(const_cast<void *>(user));
    const std::lock_guard<std::mutex> lock(diagnostics.mutex);
    diagnostics.error = message;
  }
}

bool check(bool condition, const std::string &message, std::string &error)
{
  if (!condition)
    error = message;
  return condition;
}

bool validateDevice(DeviceDiagnostics &diagnostics, std::string &error)
{
  const std::lock_guard<std::mutex> lock(diagnostics.mutex);
  return check(diagnostics.error.empty(), diagnostics.error, error);
}

bool supportsParameter(anari::Device device,
    const vsr::scene::Object &object,
    vsr::core::Token name,
    anari::DataType type)
{
  const auto *parameters =
      static_cast<const ANARIParameter *>(anariGetObjectInfo(device,
          object.type(),
          object.subtype().c_str(),
          "parameter",
          ANARI_PARAMETER_LIST));
  for (auto *p = parameters; p && p->name; ++p) {
    if (name.str() == p->name && type == p->type)
      return true;
  }
  return false;
}

bool finite(const vsr::math::float3 &v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool validateCamera(vsr::scene::Camera &camera, std::string &error)
{
  const auto position = camera.parameterValueAs<vsr::math::float3>("position");
  const auto direction =
      camera.parameterValueAs<vsr::math::float3>("direction");
  const auto up = camera.parameterValueAs<vsr::math::float3>("up");
  if (!check(position && direction && up && finite(*position)
              && finite(*direction) && finite(*up),
          "Camera position/direction/up must be finite vectors",
          error))
    return false;
  const double x = double(direction->y) * up->z - double(direction->z) * up->y;
  const double y = double(direction->z) * up->x - double(direction->x) * up->z;
  const double z = double(direction->x) * up->y - double(direction->y) * up->x;
  if (!check(x * x + y * y + z * z > 0.,
          "Camera direction and up must be nonzero and nonparallel",
          error))
    return false;
  for (const auto *name :
      {"fovy", "height", "apertureRadius", "focusDistance"}) {
    const auto *parameter = camera.parameter(name);
    if (!parameter || !parameter->isEnabled())
      continue;
    const auto value = camera.parameterValueAs<float>(name);
    if (!check(value && std::isfinite(*value),
            std::string("Invalid Camera parameter: ") + name,
            error))
      return false;
    const std::string key = name;
    if (!check(key == "apertureRadius" ? *value >= 0.f : *value > 0.f,
            "Invalid Camera parameter: " + key,
            error))
      return false;
    if (key == "fovy"
        && !check(*value < vsr::math::radians(180.f),
            "Camera fovy must be below pi",
            error))
      return false;
  }
  return true;
}

bool fitCamera(vsr::scene::Camera &camera,
    const vsr::math::box3 &bounds,
    float aspect,
    std::string &error)
{
  if (!check(finite(bounds.lower) && finite(bounds.upper)
              && bounds.lower.x <= bounds.upper.x
              && bounds.lower.y <= bounds.upper.y
              && bounds.lower.z <= bounds.upper.z,
          "Cannot fit generated Camera: scene has no finite bounds",
          error))
    return false;
  const auto center = 0.5f * bounds.lower + 0.5f * bounds.upper;
  const auto diagonal = bounds.upper - bounds.lower;
  const double radius = 0.5
      * std::sqrt(double(diagonal.x) * diagonal.x
          + double(diagonal.y) * diagonal.y + double(diagonal.z) * diagonal.z);
  const double halfAngle =
      std::atan(std::tan(vsr::math::radians(20.f)) * std::min(1.f, aspect));
  const double distance = 1.05 * std::max(radius, 0.001) / std::sin(halfAngle);
  const vsr::math::float3 position(
      center.x, center.y, float(center.z + distance));
  if (!check(finite(center) && finite(position) && position.z > center.z,
          "Generated Camera framing exceeds supported numeric range",
          error))
    return false;
  camera.setName("generated default Camera");
  camera.setParameter("position", position);
  camera.setParameter("direction", vsr::math::float3(0.f, 0.f, -1.f));
  camera.setParameter("up", vsr::math::float3(0.f, 1.f, 0.f));
  return true;
}

const char *aovChannel(vsr::rendering::AOVType type)
{
  using vsr::rendering::AOVType;
  switch (type) {
  case AOVType::NONE:
    return nullptr;
  case AOVType::DEPTH:
    return "channel.depth";
  case AOVType::ALBEDO:
    return "channel.albedo";
  case AOVType::NORMAL:
    return "channel.normal";
  case AOVType::EDGES:
  case AOVType::OBJECT_ID:
    return "channel.objectId";
  case AOVType::PRIMITIVE_ID:
    return "channel.primitiveId";
  case AOVType::INSTANCE_ID:
    return "channel.instanceId";
  }
  throw std::runtime_error("Invalid AOV selection");
}

const char *aovExtension(vsr::rendering::AOVType type)
{
  using vsr::rendering::AOVType;
  switch (type) {
  case AOVType::NONE:
  case AOVType::DEPTH:
    return nullptr; // Color and depth are core ANARI frame channels.
  case AOVType::ALBEDO:
    return "ANARI_KHR_FRAME_CHANNEL_ALBEDO";
  case AOVType::NORMAL:
    return "ANARI_KHR_FRAME_CHANNEL_NORMAL";
  case AOVType::EDGES:
  case AOVType::OBJECT_ID:
    return "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID";
  case AOVType::PRIMITIVE_ID:
    return "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID";
  case AOVType::INSTANCE_ID:
    return "ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID";
  }
  throw std::runtime_error("Invalid AOV selection");
}

bool validateBuffer(anari::Device d,
    anari::Frame frame,
    const char *channel,
    anari::DataType type,
    const OfflineRenderJob &job,
    std::string &error)
{
  auto mapped = anari::map<void>(d, frame, channel);
  const bool valid = mapped.data && mapped.width == job.width
      && mapped.height == job.height && mapped.pixelType == type;
  anari::unmap(d, frame, channel);
  return check(valid,
      std::string("Missing or invalid requested buffer: ") + channel,
      error);
}

/*
 * Keep validation between rendering and visualization: missing channels must
 * never feed uninitialized staging buffers into an AOV or reach the writer.
 * Handles, job and diagnostics are borrowed from the enclosing synchronous job
 * and outlive this pipeline-owned pass.
 */
struct ValidateOfflineBuffersPass : public vsr::rendering::ImagePass
{
  ValidateOfflineBuffersPass(anari::Device device,
      anari::Frame frame,
      const OfflineRenderJob &job,
      DeviceDiagnostics &diagnostics);
  VSR_NOT_COPYABLE(ValidateOfflineBuffersPass)
  VSR_NOT_MOVEABLE(ValidateOfflineBuffersPass)

  const char *name() const override;
  const std::string &error() const;

 protected:
  void render(
      vsr::rendering::ImageBuffers &, vsr::rendering::FrameState &) override;

 private:
  anari::Device m_device{nullptr};
  anari::Frame m_frame{nullptr};
  const OfflineRenderJob *m_job{nullptr};
  DeviceDiagnostics *m_diagnostics{nullptr};
  std::string m_error;
};

ValidateOfflineBuffersPass::ValidateOfflineBuffersPass(anari::Device device,
    anari::Frame frame,
    const OfflineRenderJob &job,
    DeviceDiagnostics &diagnostics)
    : m_device(device), m_frame(frame), m_job(&job), m_diagnostics(&diagnostics)
{}

const char *ValidateOfflineBuffersPass::name() const
{
  return "Validate Offline Buffers";
}

const std::string &ValidateOfflineBuffersPass::error() const
{
  return m_error;
}

void ValidateOfflineBuffersPass::render(
    vsr::rendering::ImageBuffers &, vsr::rendering::FrameState &)
{
  if (validateDevice(*m_diagnostics, m_error)
      && validateBuffer(m_device,
          m_frame,
          "channel.color",
          ANARI_UFIXED8_RGBA_SRGB,
          *m_job,
          m_error)) {
    if (const auto *channel = aovChannel(m_job->aovType)) {
      using vsr::rendering::AOVType;
      const auto type = m_job->aovType == AOVType::DEPTH ? ANARI_FLOAT32
          : m_job->aovType == AOVType::ALBEDO
              || m_job->aovType == AOVType::NORMAL
          ? ANARI_FLOAT32_VEC3
          : ANARI_UINT32;
      validateBuffer(m_device, m_frame, channel, type, *m_job, m_error);
    }
  }
}

} // namespace

OfflineRenderResult renderOfflineJob(Context &ctx,
    const OfflineRenderJob &resolvedJob,
    OfflineRenderCallback preFrameCallback)
{
  OfflineRenderResult result;
  auto &animMgr = ctx.vsr.animationMgr;
  {
    CallerStateRestore restore(ctx);
    DeviceDiagnostics deviceError;
    auto execute = [&] {
      auto valid = [&](bool condition, const std::string &message) {
        return check(condition, message, result.diagnostic);
      };
      auto job = resolvedJob;
      if (!valid(job.renderer && job.renderer->type() == ANARI_RENDERER,
              "A resolved renderer object is required"))
        return;
      auto camera =
          ctx.vsr.scene.getObject<vsr::scene::Camera>(job.cameraIndex);
      if (!valid(!job.generateCamera
                  || job.cameraIndex == vsr::core::INVALID_INDEX,
              "Generated Camera conflicts with explicit Camera selection")
          || !valid(job.generateCamera || bool(camera),
              "The selected scene Camera does not exist"))
        return;
      const uint64_t pixels = uint64_t(job.width) * job.height;
      if (!valid(job.width > 0 && job.height > 0
                  && job.width <= uint32_t(std::numeric_limits<int>::max() / 4)
                  && job.height <= uint32_t(std::numeric_limits<int>::max())
                  && pixels <= uint64_t(std::numeric_limits<int>::max()) / 16,
              "Invalid or overflowing image dimensions")
          || !valid(job.samples > 0
                  && job.samples <= uint32_t(std::numeric_limits<int>::max()),
              "Invalid sample count")
          || !valid(!job.frames.empty(), "No selected frames"))
        return;
      for (const auto &assignment : job.overrides) {
        const auto *object = assignment.object();
        if (!valid(object && object->scene() == &ctx.vsr.scene,
                "Override target must be a live object in the caller Scene")
            || !valid(
                !assignment.name.str().empty() && assignment.value.valid(),
                "Override requires a parameter name and typed value"))
          return;
      }
      if (!valid(job.aovType >= vsr::rendering::AOVType::NONE
                  && job.aovType <= vsr::rendering::AOVType::INSTANCE_ID,
              "Invalid AOV selection")
          || !valid(
              vsr::rendering::isValidDepthRange(job.depthMin, job.depthMax),
              "Invalid AOV depth range"))
        return;
      const auto *channel = aovChannel(job.aovType);
      std::set<std::string> destinations;
      for (const auto &frame : job.frames) {
        if (!valid(frame.frameIndex >= 0
                    && frame.frameIndex < animMgr.getAnimationTotalFrames(),
                "Selected frame is outside the animation timeline")
            || !valid(!frame.destination.empty(), "Empty frame destination")
            || !valid(destinations
                          .insert(std::filesystem::path(frame.destination)
                                  .lexically_normal()
                                  .string())
                          .second,
                "Duplicate frame destination"))
          return;
      }
      if (job.generateCamera) {
        if (!valid(job.globalFirstSelectedFrame >= 0
                    && job.globalFirstSelectedFrame
                        < animMgr.getAnimationTotalFrames(),
                "Generated Camera requires a valid global first selected frame"))
          return;
        // Preparation evaluates animation too: retain caller edits before it
        // can change authored targets, even if device/override setup fails.
        for (const auto &assignment : job.overrides)
          restore.save(assignment);
        restore.markAnimationAdvanced();
        animMgr.setAnimationFrame(job.globalFirstSelectedFrame);
        camera = ctx.vsr.scene.createObject<vsr::scene::Camera>(
            vsr::scene::tokens::camera::perspective);
        restore.setGeneratedCamera(camera.data());
        job.cameraIndex = camera->index();
      }
      for (const auto &parameter : job.cameraParameters)
        job.overrides.push_back(
            {camera.data(), parameter.name, parameter.value});
      if (job.cameraLookAt) {
        if (!valid(finite(*job.cameraLookAt), "Camera look-at must be finite"))
          return;
        // Direction is derived every frame from the post-animation position.
        job.overrides.push_back(
            {camera.data(), "direction", vsr::math::float3(0.f, 0.f, -1.f)});
      }
      const auto id = DeviceIdentifier::parse(job.deviceIdentifier);
      if (!valid(
              bool(id), "Invalid Device Identifier: " + job.deviceIdentifier))
        return;
      vsr::core::logStatus(
          "[offline] device=%s renderer=%s camera=%zu %ux%u %u spp, %zu frames",
          id->str().c_str(),
          job.renderer->subtype().c_str(),
          job.cameraIndex,
          job.width,
          job.height,
          job.samples,
          job.frames.size());
      JobResources resources(ctx);
      const auto library =
          resources.loadLibrary(id->library.c_str(), jobStatus, &deviceError);
      if (!valid(library != nullptr,
              "Cannot load selected ANARI library: " + id->library))
        return;
      const auto **subtypes = anariGetDeviceSubtypes(library);
      if (!valid(!subtypes || !*subtypes || contains(subtypes, id->subtype),
              "Unsupported device subtype: " + id->subtype))
        return;
      const auto d = resources.createDevice(id->subtype.c_str());
      if (!valid(d != nullptr,
              "Cannot create selected ANARI device: " + id->str()))
        return;
      anari::commitParameters(d, d);
      if (const auto *extension = aovExtension(job.aovType)) {
        if (!valid(vsr::rendering::deviceSupportsExtension(d, extension),
                std::string(
                    "Selected device does not support requested buffer: ")
                    + channel + " (requires " + extension + ")"))
          return;
      }
      if (!valid(contains(anariGetObjectSubtypes(d, ANARI_RENDERER),
                     job.renderer->subtype().str()),
              "Unsupported renderer subtype: " + job.renderer->subtype().str())
          || !valid(contains(anariGetObjectSubtypes(d, ANARI_CAMERA),
                        camera->subtype().str()),
              "Unsupported camera subtype: " + camera->subtype().str()))
        return;
      for (const auto &assignment : job.overrides) {
        if (!valid(supportsParameter(d,
                       *assignment.object(),
                       assignment.name,
                       assignment.value.type()),
                "Unsupported override parameter/type: "
                    + assignment.name.str()))
          return;
        restore.save(assignment);
      }
      const bool cameraHasAspect =
          supportsParameter(d, *camera, "aspect", ANARI_FLOAT32);
      auto *index = resources.createRenderIndex(id->str());
      index->populate();
      if (job.generateCamera
          && !fitCamera(*camera,
              index->worldBounds(),
              float(job.width) / job.height,
              result.diagnostic))
        return;
      if (!valid(index->camera(job.cameraIndex) != nullptr,
              "Cannot create selected ANARI camera"))
        return;
      const auto renderer =
          resources.createRenderer(job.renderer->subtype().c_str());
      if (!valid(renderer != nullptr, "Cannot create selected ANARI renderer"))
        return;
      for (size_t i = 0; i < job.renderer->numParameters(); ++i) {
        const auto &parameter = job.renderer->parameterAt(i);
        const auto &value = parameter.value();
        if (parameter.isEnabled() && value.holdsObject()
            && value.getAsObjectIndex() != vsr::core::INVALID_INDEX)
          if (!valid(ctx.vsr.scene.getObject(
                         value.type(), value.getAsObjectIndex()),
                  std::string("Invalid saved renderer object reference: ")
                      + job.renderer->parameterNameAt(i)))
            return;
      }
      index->updateAllANARIParameters(*job.renderer, renderer);
      anari::commitParameters(d, renderer);
      if (!validateDevice(deviceError, result.diagnostic))
        return;

      for (size_t position = 0; position < job.frames.size(); ++position) {
        const auto &frame = job.frames[position];
        result.frameIndex = frame.frameIndex;
        result.destination = frame.destination;
        restore.markAnimationAdvanced();
        if (preFrameCallback
            && !preFrameCallback(
                {frame.frameIndex, position, job.frames.size()})) {
          result.status = OfflineRenderStatus::Cancelled;
          result.diagnostic =
              "Cancelled before frame " + std::to_string(frame.frameIndex);
          break;
        }
        animMgr.setAnimationFrame(frame.frameIndex);
        for (const auto &assignment : job.overrides) {
          assignment.object()
              ->setParameter(assignment.name, assignment.value)
              ->setEnabled(true);
        }
        if (job.cameraLookAt) {
          const auto position =
              camera->parameterValueAs<vsr::math::float3>("position");
          if (!valid(bool(position), "Camera look-at requires a position"))
            return;
          camera->setParameter("direction", *job.cameraLookAt - *position);
        }
        if ((job.generateCamera || !job.cameraParameters.empty()
                || job.cameraLookAt
                || std::any_of(job.overrides.begin(),
                    job.overrides.end(),
                    [&](const auto &assignment) {
                      return assignment.object() == camera.data();
                    }))
            && !validateCamera(*camera, result.diagnostic))
          return;
        vsr::core::logStatus("[offline] frame=%d output=%s",
            frame.frameIndex,
            frame.destination.c_str());
        // A new ANARI Frame guarantees empty accumulation even when animation
        // evaluation leaves the same handles/parameters as the preceding frame.
        vsr::rendering::ImagePipeline pipeline;
        pipeline.setDimensions(job.width, job.height);
        auto *pass =
            pipeline.setSource<vsr::rendering::AnariSceneRenderPass>(d);
        pass->setRunAsync(false);
        pass->setUseImplicitAspectRatio(!cameraHasAspect);
        pass->setColorFormat(ANARI_UFIXED8_RGBA_SRGB);
        pass->setWorld(index->world());
        pass->setRenderer(renderer);
        pass->setCamera(index->camera(job.cameraIndex));
        auto *validation = pipeline.addPass<ValidateOfflineBuffersPass>(
            d, pass->getFrame(), job, deviceError);
        if (channel) {
          auto *aov = pipeline.addPass<vsr::rendering::VisualizeAOVPass>();
          aov->setAOVType(job.aovType);
          aov->setDepthRange(job.depthMin, job.depthMax);
          aov->setEdgeInvert(job.edgeInvert);
        }
        for (uint32_t sample = 0; sample < job.samples; ++sample) {
          pipeline.render();
          if (!valid(validation->error().empty(), validation->error()))
            return;
        }
        if (!valid(vsr::rendering::saveImage(pipeline, frame.destination),
                "Cannot save image to '" + frame.destination + "'"))
          return;
        ++result.completedFrames;
      }
      if (result.completedFrames == job.frames.size())
        result.status = OfflineRenderStatus::Completed;
    };
    execute();
    restore.restore();
  }
  return result;
}

OfflineRenderResult renderAnimationSequence(Context &ctx,
    const std::string &outputDir,
    const std::string &filePrefix,
    RenderSequenceCallback preFrameCallback)
{
  const auto &config = ctx.offline;
  const auto &renderers = config.renderer.rendererObjects;
  if (config.renderer.activeRenderer < 0
      || size_t(config.renderer.activeRenderer) >= renderers.size()) {
    return {OfflineRenderStatus::Failed,
        0,
        -1,
        {},
        "Invalid active offline renderer; select a device and renderer in Settings"};
  }
  OfflineRenderJob job;
  job.renderer = &renderers[config.renderer.activeRenderer];
  job.deviceIdentifier = config.renderer.libraryName;
  job.cameraIndex = config.camera.cameraIndex == vsr::core::INVALID_INDEX
      ? 0
      : config.camera.cameraIndex;
  job.width = config.frame.width;
  job.height = config.frame.height;
  job.samples = config.frame.samples;
  job.aovType = config.aov.aovType;
  job.depthMin = config.aov.depthMin;
  job.depthMax = config.aov.depthMax;
  job.edgeInvert = config.aov.edgeInvert;
  const bool hasAnimations = !ctx.vsr.animationMgr.animations().empty();
  const int total = hasAnimations
      ? ctx.vsr.animationMgr.getAnimationTotalFrames()
      : config.frame.numFrames;
  const int start = config.frame.renderSubset ? config.frame.startFrame : 0;
  const int end = config.frame.renderSubset ? config.frame.endFrame : total - 1;
  if (config.frame.frameIncrement <= 0 || start < 0 || end < start) {
    return {OfflineRenderStatus::Failed,
        0,
        -1,
        {},
        "Invalid offline frame selection; check the range and positive stride"};
  }
  for (int64_t frame = start; frame <= end;
      frame += config.frame.frameIncrement) {
    std::ostringstream name;
    name << filePrefix << std::setfill('0') << std::setw(4) << frame << ".png";
    const int timelineFrame = hasAnimations
        ? int(frame)
        : std::min(
              int(frame), ctx.vsr.animationMgr.getAnimationTotalFrames() - 1);
    job.frames.push_back({timelineFrame,
        (std::filesystem::path(outputDir) / name.str()).string()});
  }
  return renderOfflineJob(ctx, job, [&](const OfflineRenderProgress &progress) {
    return !preFrameCallback
        || preFrameCallback(
            int(int64_t(start)
                + int64_t(progress.position) * config.frame.frameIncrement),
            total);
  });
}

} // namespace vsr::app
