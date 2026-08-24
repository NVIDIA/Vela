// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifndef VERBOSE_STATUS_MESSAGES
#define VERBOSE_STATUS_MESSAGES 0
#endif

// vsr_app
#include "vsr/app/Context.h"
// vsr_mpi
#include "vsr/mpi/ReplicatedObject.hpp"
// std
#include <memory>
#include <string>

namespace vsr::mpi_viewer {

template <typename... Args>
inline void rank_printf(int rank, const char *fmt, Args &&...args)
{
#if VERBOSE_STATUS_MESSAGES
  printf("====[RANK %i]", rank);
  printf(fmt, std::forward<Args>(args)...);
  fflush(stdout);
#endif
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct LightState
{
  vsr::math::float3 hdriUp{0.f, 1.f, 0.f};
  vsr::math::float3 hdriDirection{1.f, 0.f, 0.f};
  float hdriScale{1.f};
};

struct CameraState
{
  vsr::math::float3 position{0.f, 0.f, 0.f};
  vsr::math::float3 direction{1.f, 0.f, 0.f};
  vsr::math::float3 up{0.f, 1.f, 0.f};
  float fovy{45.f};
  float aspect{1.f};
  float apertureRadius{0.f};
  float focusDistance{1.f};
};

struct RendererState
{
  vsr::math::float4 background{0.1f, 0.1f, 0.1f, 1.f};
  vsr::math::float3 ambientColor{1.f, 1.f, 1.f};
  float ambientRadiance{1.f};
  bool denoise{true};
};

struct FrameState
{
  vsr::math::int2 size{1584, 600};
  bool running{true};
};

struct AnimationState
{
  float time{0.f};
};

struct DistributedState
{
  vsr::mpi::ReplicatedObject<LightState> lights;
  vsr::mpi::ReplicatedObject<CameraState> camera;
  vsr::mpi::ReplicatedObject<RendererState> renderer;
  vsr::mpi::ReplicatedObject<FrameState> frame;
  vsr::mpi::ReplicatedObject<AnimationState> animation;
};

struct LocalState
{
  LightState lights;
  CameraState camera;
  RendererState renderer;
  FrameState frame;
  AnimationState animation;
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct DistributedSceneController
{
  DistributedSceneController();
  ~DistributedSceneController();

  void initialize(int argc, const char **argv);
  void executeFrame();
  void signalStop();
  void shutdown();
  bool isRunning() const;

  vsr::app::Context *appContext();
  DistributedState *distributedState();

  int rank() const;
  int numRanks() const;
  bool isMain() const;

  const char *anariLibraryName() const;
  anari::Device anariDevice() const;
  anari::Camera anariCamera() const;
  anari::Renderer anariRenderer() const;
  anari::Frame anariFrame() const;
  anari::Light anariHDRILight() const;

 private:
  void executeFrame_syncLights();
  void executeFrame_syncCamera();
  void executeFrame_syncRenderer();
  void executeFrame_syncFrame();
  void executeFrame_syncAnimation();
  void executeFrame_render();

  std::unique_ptr<vsr::app::Context> m_ctx;

  bool m_mpiInitialized{false};

  struct ANARIState
  {
    int gpusPerNode{1};
    std::string libraryName{"barney_mpi"};
    anari::Device device{nullptr};

    anari::Frame frame{nullptr};
    anari::Camera camera{nullptr};
    anari::Renderer renderer{nullptr};

    anari::Light hdriLight{nullptr};

    vsr::rendering::RenderIndex *renderIndex{nullptr};
  } m_anari;

  std::unique_ptr<DistributedState> m_distributedState;
};

} // namespace vsr::mpi_viewer
