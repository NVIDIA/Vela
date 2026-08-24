// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DistributedRenderServer.hpp"
// mpi
#include <mpi.h>
// vsr_mpi
#include "vsr/mpi/ReplicatedObject.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/archives/SceneArchive.hpp"
// vsr_rendering
#include "vsr/rendering/view/ManipulatorToVSR.hpp"
// vsr_network messages
#include "vsr/network/messages/NewObject.hpp"
#include "vsr/network/messages/ParameterChange.hpp"
#include "vsr/network/messages/ParameterRemove.hpp"
#include "vsr/network/messages/RemoveObject.hpp"
#include "vsr/network/messages/TransferArrayData.hpp"
#include "vsr/network/messages/TransferLayer.hpp"
#include "vsr/network/messages/TransferScene.hpp"
// std
#include <cstring>
#include <thread>

namespace vsr::network {

///////////////////////////////////////////////////////////////////////////////
// DistState definition (delayed until after MPI_Init) ///////////////////////
///////////////////////////////////////////////////////////////////////////////

struct DistributedRenderServer::DistState
{
  vsr::mpi::ReplicatedObject<DistributedRenderServer::ControlState>
      controlState;
};

///////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor ///////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

DistributedRenderServer::DistributedRenderServer(int argc, const char **argv)
{
  vsr::core::setLogToStdout(true);
  vsr::core::logStatus("[vsrMPIServer] Parsing command line...");
  m_ctx.parseCommandLine(argc, argv);
}

DistributedRenderServer::~DistributedRenderServer()
{
  if (m_mpiInitialized)
    MPI_Finalize();
}

///////////////////////////////////////////////////////////////////////////////
// MPI helpers ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int DistributedRenderServer::rank() const
{
  return m_ctx.vsr.scene.mpiRank();
}

int DistributedRenderServer::numRanks() const
{
  return m_ctx.vsr.scene.mpiNumRanks();
}

bool DistributedRenderServer::isMain() const
{
  return rank() == 0;
}

///////////////////////////////////////////////////////////////////////////////
// run() //////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void DistributedRenderServer::run(short port)
{
  m_port = port;

  // Initialize MPI
  MPI_Init(nullptr, nullptr);
  m_mpiInitialized = true;
  {
    int r = 0, n = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &r);
    MPI_Comm_size(MPI_COMM_WORLD, &n);
    m_ctx.vsr.scene.setMpiRankInfo(r, n);
  }

  // DistState must be created after MPI_Init (ReplicatedObject reads rank)
  m_distState = std::make_unique<DistState>();

  setup_Scene();
  setup_ANARIDevice();
  setup_Camera();
  setup_ImagePipeline();

  if (isMain()) {
    setup_Messaging();
    m_server->start();
    vsr::core::logStatus(
        "[vsrMPIServer] Rank 0 listening on port %i...", int(port));
  }

  // Force an initial broadcast so all ranks have consistent state
  if (isMain()) {
    std::lock_guard lock(m_controlMutex);
    m_distState->controlState.write(); // mark as needing sync
  }

  // Main loop (all ranks)
  while (true) {
    // Rank 0: update server mode and connection state before broadcasting
    if (isMain()) {
      std::lock_guard lock(m_controlMutex);
      if (auto *cs = m_distState->controlState.write()) {
        if (!m_server->isConnected())
          cs->serverMode = int(ServerMode::DISCONNECTED);
        else
          cs->serverMode = m_pendingServerMode.load();
      }
    }

    syncControlState();

    auto mode = ServerMode(m_distState->controlState.read()->serverMode);

    if (mode == ServerMode::SHUTDOWN)
      break;

    if (mode == ServerMode::DISCONNECTED) {
      if (isMain() && m_previousMode != ServerMode::DISCONNECTED) {
        vsr::core::logStatus(
            "[vsrMPIServer] Listening on port %i...", int(m_port));
        m_server->restart();
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
      m_previousMode = mode;
      continue;
    }

    syncSceneOps();

    // Rank 0: send scene if requested (outside render barriers)
    if (isMain() && mode == ServerMode::SEND_SCENE) {
      vsr::core::logStatus("[vsrMPIServer] Serializing + sending scene...");
      vsr::network::messages::TransferScene sceneMsg(&m_ctx.vsr.scene);
      m_server->send(MessageType::CLIENT_RECEIVE_SCENE, std::move(sceneMsg))
          .get();
      const float time = m_ctx.vsr.animationMgr.getAnimationTime();
      m_server->send(MessageType::CLIENT_RECEIVE_TIME, &time).get();
      vsr::core::logStatus("[vsrMPIServer] Scene sent.");
      // Revert to prior rendering mode
      signalServerMode(m_previousMode == ServerMode::RENDERING
              ? ServerMode::RENDERING
              : ServerMode::PAUSED);
    }

    renderFrame();

    if (isMain() && mode == ServerMode::RENDERING)
      send_FrameBuffer();

    m_previousMode = mode;
  }

  vsr::core::logStatus("[vsrMPIServer] Shutting down...");

  if (isMain()) {
    m_server->stop();
    m_server->removeAllHandlers();
  }

  m_camera = {};
  m_ctx.anari.releaseRenderIndex(m_ctx.vsr.scene, m_device);
  m_ctx.anari.releaseAllDevices();

  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
  m_mpiInitialized = false;
}

///////////////////////////////////////////////////////////////////////////////
// Setup //////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void DistributedRenderServer::setup_Scene()
{
  auto &scene = m_ctx.vsr.scene;

  vsr::core::logStatus("[vsrMPIServer] Rank %d loading files...", rank());
  m_ctx.loadCommandLineSceneInputs();

  vsr::core::logStatus(
      "%s", vsr::scene::objectDBInfo(scene.objectDB()).c_str());

  // Tag all loaded non-global objects with this rank so the RenderIndex
  // filter can exclude unowned objects from the ANARI world.
  // Camera and renderer objects are intentionally NOT tagged (global).
  auto &db = scene.objectDB();
  auto tagPool = [&](auto &pool, anari::DataType type) {
    for (size_t i = 0; i < pool.capacity(); i++) {
      if (!pool.slot_empty(i)) {
        if (auto *obj = scene.getObject(type, i))
          obj->setMetadataValue("mpiRank", int32_t(rank()));
      }
    }
  };
  tagPool(db.surface, ANARI_SURFACE);
  tagPool(db.geometry, ANARI_GEOMETRY);
  tagPool(db.material, ANARI_MATERIAL);
  tagPool(db.sampler, ANARI_SAMPLER);
  tagPool(db.volume, ANARI_VOLUME);
  tagPool(db.field, ANARI_SPATIAL_FIELD);
  tagPool(db.light, ANARI_LIGHT);
  tagPool(db.array, ANARI_ARRAY1D); // pool type for all array dims

  vsr::core::logStatus("[vsrMPIServer] Rank %d scene setup complete.", rank());
}

void DistributedRenderServer::setup_ANARIDevice()
{
  vsr::core::logStatus(
      "[vsrMPIServer] Rank %d loading ANARI device...", rank());

  const char *libNameEnv = std::getenv("ANARI_LIBRARY");
  if (!libNameEnv) {
    vsr::core::logWarning(
        "[vsrMPIServer] ANARI_LIBRARY not set, defaulting to 'helide'");
    libNameEnv = "helide";
  }
  m_libName = libNameEnv;

  m_device = m_ctx.anari.loadDevice(m_libName);
  if (!m_device) {
    vsr::core::logError("[vsrMPIServer] Rank %d failed to load '%s' device.",
        rank(),
        m_libName.c_str());
    std::exit(EXIT_FAILURE);
  }

  auto &scene = m_ctx.vsr.scene;
  m_renderIndex = m_ctx.anari.acquireRenderIndex(scene, m_libName, m_device);

  // Install filter: only render objects owned by this rank (or global ones)
  m_renderIndex->setFilterFunction(
      [myRank = rank()](const vsr::scene::Object *obj) -> bool {
        auto v = obj->getMetadataValue("mpiRank");
        if (v.valid())
          return v.get<int32_t>() == myRank;
        return true; // no mpiRank tag → global (camera, renderer, etc.)
      });
  m_ctx.vsr.scene.updateDelegate().signalObjectFilteringChanged();

  m_camera = scene.defaultCamera();
  m_renderers = scene.renderersOfDevice(m_libName).empty()
      ? scene.createStandardRenderers(m_libName, m_device)
      : scene.renderersOfDevice(m_libName);

  // Seed ControlState with initial camera/renderer pool indices
  if (isMain()) {
    std::lock_guard lock(m_controlMutex);
    if (auto *cs = m_distState->controlState.write()) {
      cs->cameraIndex = uint64_t(m_camera->index());
      cs->rendererIndex = uint64_t(m_renderers[0]->index());
    }
  }
}

void DistributedRenderServer::setup_Camera()
{
  vsr::core::logStatus("[vsrMPIServer] Rank %d setting up camera...", rank());
  vsr::rendering::Manipulator manipulator;
  manipulator.setConfig(m_renderIndex->computeDefaultView());
  vsr::rendering::updateCameraObject(*m_camera, manipulator, true);
}

void DistributedRenderServer::setup_ImagePipeline()
{
  vsr::core::logStatus(
      "[vsrMPIServer] Rank %d setting up image pipeline...", rank());

  auto sz = m_distState->controlState.read()->frameSize;
  m_renderPipeline.setDimensions(sz.x, sz.y);

  auto *arp =
      m_renderPipeline.emplace_back<vsr::rendering::AnariSceneRenderPass>(
          m_device);
  arp->setWorld(m_renderIndex->world());
  arp->setRenderer(m_renderIndex->renderer(m_renderers[0]->index()));
  arp->setCamera(m_renderIndex->camera(m_camera->index()));
  arp->setEnableIDs(false);
  m_sceneImagePass = arp;

  // Only rank 0 needs to read back the color buffer for network transmission
  if (isMain()) {
    auto *ccbp = m_renderPipeline
                     .emplace_back<vsr::rendering::CopyFromColorBufferPass>();
    ccbp->setExternalBuffer(m_session.frame.buffers.color);
  }
}

void DistributedRenderServer::setup_Messaging()
{
  vsr::core::logStatus("[vsrMPIServer] Setting up messaging...");

  m_server = std::make_shared<NetworkServer>(m_port);

  m_server->registerHandler(MessageType::ERROR, [](const Message &msg) {
    vsr::core::logError("[vsrMPIServer] Client error: '%s'",
        vsr::network::payloadAs<char>(msg));
  });

  m_server->registerHandler(MessageType::PING, [](const Message &) {
    vsr::core::logStatus("[vsrMPIServer] PING received");
  });

  m_server->registerHandler(MessageType::DISCONNECT, [this](const Message &) {
    vsr::core::logStatus("[vsrMPIServer] Client disconnected.");
    signalServerMode(ServerMode::DISCONNECTED);
  });

  m_server->registerHandler(
      MessageType::SERVER_START_RENDERING, [this](const Message &) {
        vsr::core::logStatus("[vsrMPIServer] Starting rendering.");
        signalServerMode(ServerMode::RENDERING);
      });

  m_server->registerHandler(
      MessageType::SERVER_STOP_RENDERING, [this](const Message &) {
        vsr::core::logStatus("[vsrMPIServer] Stopping rendering.");
        signalServerMode(ServerMode::PAUSED);
        MessageFuture lastSentFrame;
        {
          std::lock_guard lock(m_frameSendMutex);
          if (m_lastSentFrame.valid())
            lastSentFrame = std::move(m_lastSentFrame);
        }
        if (lastSentFrame.valid())
          lastSentFrame.get();
      });

  m_server->registerHandler(
      MessageType::SERVER_SHUTDOWN, [this](const Message &) {
        vsr::core::logStatus("[vsrMPIServer] Shutdown requested.");
        signalServerMode(ServerMode::SHUTDOWN);
      });

  m_server->registerHandler(
      MessageType::SERVER_SET_FRAME_CONFIG, [this](const Message &msg) {
        RenderSession::Frame::Config config;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, &config)) {
          std::lock_guard lock(m_controlMutex);
          if (auto *cs = m_distState->controlState.write())
            cs->frameSize = vsr::math::int2(config.size.x, config.size.y);
        }
      });

  m_server->registerHandler(
      MessageType::SERVER_UPDATE_TIME, [this](const Message &msg) {
        float time = 0.f;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, &time)) {
          std::lock_guard lock(m_controlMutex);
          if (auto *cs = m_distState->controlState.write())
            cs->animationTime = time;
        }
      });

  m_server->registerHandler(
      MessageType::SERVER_SET_CURRENT_RENDERER, [this](const Message &msg) {
        size_t idx = 0;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, &idx)) {
          if (idx < m_renderers.size()) {
            vsr::core::logDebug(
                "[vsrMPIServer] Current renderer → index %zu", idx);
            std::lock_guard lock(m_controlMutex);
            if (auto *cs = m_distState->controlState.write())
              cs->rendererIndex = uint64_t(m_renderers[idx]->index());
          } else {
            vsr::core::logError(
                "[vsrMPIServer] Invalid renderer index %zu", idx);
          }
        }
      });

  m_server->registerHandler(
      MessageType::SERVER_SET_CURRENT_CAMERA, [this](const Message &msg) {
        size_t idx = 0;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, &idx)) {
          if (m_ctx.vsr.scene.getObject<vsr::scene::Camera>(idx)) {
            vsr::core::logDebug(
                "[vsrMPIServer] Current camera → index %zu", idx);
            std::lock_guard lock(m_controlMutex);
            if (auto *cs = m_distState->controlState.write())
              cs->cameraIndex = uint64_t(idx);
          } else {
            vsr::core::logError("[vsrMPIServer] Invalid camera index %zu", idx);
          }
        }
      });

  // Scene mutation messages: enqueue for MPI broadcast
  auto enqueueWithRouting = [this](const Message &msg) {
    enqueueSceneOp(msg, determineTargetRank(msg));
  };

  m_server->registerHandler(
      MessageType::SERVER_SET_OBJECT_PARAMETER, enqueueWithRouting);
  m_server->registerHandler(
      MessageType::SERVER_REMOVE_OBJECT_PARAMETER, enqueueWithRouting);
  m_server->registerHandler(
      MessageType::SERVER_REMOVE_OBJECT, enqueueWithRouting);
  m_server->registerHandler(
      MessageType::SERVER_SET_ARRAY_DATA, [this](const Message &msg) {
        enqueueSceneOp(msg, 0); // arrays live on rank 0
      });
  m_server->registerHandler(
      MessageType::SERVER_ADD_OBJECT, [this](const Message &msg) {
        enqueueSceneOp(msg, 0); // new objects always go to rank 0
      });
  m_server->registerHandler(MessageType::SERVER_REMOVE_ALL_OBJECTS,
      [this](const Message &msg) { enqueueSceneOp(msg, 0); });
  m_server->registerHandler(
      MessageType::SERVER_UPDATE_LAYER, [this](const Message &msg) {
        enqueueSceneOp(msg, 0); // layers are rank 0's concern
      });

  // Request/response handlers
  m_server->registerHandler(MessageType::SERVER_REQUEST_FRAME_CONFIG,
      [this, s = m_server](const Message &) {
        vsr::core::logDebug("[vsrMPIServer] Client requested frame config.");
        RenderSession::Frame::Config config;
        {
          std::lock_guard lock(m_controlMutex);
          config = m_session.frame.config;
        }
        s->send(MessageType::CLIENT_RECEIVE_FRAME_CONFIG, &config);
      });

  m_server->registerHandler(MessageType::SERVER_REQUEST_CURRENT_RENDERER,
      [this, s = m_server](const Message &) {
        vsr::core::logDebug(
            "[vsrMPIServer] Client requested current renderer.");
        auto idx = m_renderers[0]->index();
        s->send(MessageType::CLIENT_RECEIVE_CURRENT_RENDERER, &idx);
      });

  m_server->registerHandler(MessageType::SERVER_REQUEST_CURRENT_CAMERA,
      [this, s = m_server](const Message &) {
        vsr::core::logDebug("[vsrMPIServer] Client requested current camera.");
        auto idx = m_camera->index();
        s->send(MessageType::CLIENT_RECEIVE_CURRENT_CAMERA, &idx);
      });

  m_server->registerHandler(
      MessageType::SERVER_REQUEST_SCENE, [this](const Message &) {
        vsr::core::logStatus("[vsrMPIServer] Client requested scene...");
        m_server->send(MessageType::CLIENT_SCENE_TRANSFER_BEGIN);
        signalServerMode(ServerMode::SEND_SCENE);
      });

  m_server->registerHandler(
      MessageType::SERVER_SAVE_STATE_FILE, [this](const Message &msg) {
        std::string filename;
        uint32_t pos = 0;
        if (vsr::network::payloadRead(msg, pos, filename)) {
          vsr::core::logStatus(
              "[vsrMPIServer] Saving Scene Archive '%s'...", filename.c_str());
          if (!vsr::io::save_SceneArchive(
                  m_ctx.vsr.scene, filename.c_str())) {
            vsr::core::logError(
                "[vsrMPIServer] Failed to save Scene Archive '%s'",
                filename.c_str());
          }
        }
      });
}

///////////////////////////////////////////////////////////////////////////////
// Per-frame sync /////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void DistributedRenderServer::syncControlState()
{
  bool changed = false;
  {
    std::lock_guard lock(m_controlMutex);
    changed = m_distState->controlState.sync();
  }

  if (!changed)
    return;

  auto *cs = m_distState->controlState.read();

  // Resize pipeline and update camera aspect
  auto sz = cs->frameSize;
  if (sz.x > 0 && sz.y > 0) {
    m_renderPipeline.setDimensions(sz.x, sz.y);
    if (isMain()) {
      std::lock_guard lock(m_controlMutex);
      m_session.frame.config.size = {uint32_t(sz.x), uint32_t(sz.y)};
    }

    auto c = m_renderIndex->camera(size_t(cs->cameraIndex));
    if (m_device && c) {
      anari::setParameter(m_device, c, "aspect", float(sz.x) / float(sz.y));
      anari::commitParameters(m_device, c);
    }
  }

  // Update render pass camera / renderer
  if (m_sceneImagePass) {
    m_sceneImagePass->setCamera(m_renderIndex->camera(size_t(cs->cameraIndex)));
    m_sceneImagePass->setRenderer(
        m_renderIndex->renderer(size_t(cs->rendererIndex)));
  }

  // Propagate animation time
  m_ctx.vsr.animationMgr.setAnimationTime(cs->animationTime);
}

void DistributedRenderServer::syncSceneOps()
{
  std::vector<std::byte> buf;

  if (isMain()) {
    std::vector<PendingSceneOp> ops;
    {
      std::lock_guard lock(m_opsMutex);
      ops.swap(m_pendingOps);
    }

    // Serialize: [count(u32)] ([type(u8), targetRank(i32), size(u32),
    // payload...]*)
    auto appendBytes = [&](const auto &v) {
      const auto *p = reinterpret_cast<const std::byte *>(&v);
      buf.insert(buf.end(), p, p + sizeof(v));
    };
    uint32_t count = uint32_t(ops.size());
    appendBytes(count);
    for (auto &op : ops) {
      appendBytes(op.messageType);
      appendBytes(op.targetRank);
      uint32_t payloadSize = uint32_t(op.payload.size());
      appendBytes(payloadSize);
      buf.insert(buf.end(), op.payload.begin(), op.payload.end());
    }
  }

  // Broadcast buffer size then contents
  uint32_t bufSize = uint32_t(buf.size());
  MPI_Bcast(&bufSize, 1, MPI_UINT32_T, 0, MPI_COMM_WORLD);
  if (!isMain())
    buf.resize(bufSize);
  if (bufSize > 0)
    MPI_Bcast(buf.data(), int(bufSize), MPI_BYTE, 0, MPI_COMM_WORLD);

  if (bufSize == 0)
    return;

  // Deserialize and apply ops targeted at this rank
  size_t offset = 0;
  auto readPOD = [&](auto &v) {
    std::memcpy(&v, buf.data() + offset, sizeof(v));
    offset += sizeof(v);
  };

  uint32_t count = 0;
  readPOD(count);

  auto &scene = m_ctx.vsr.scene;

  for (uint32_t i = 0; i < count; ++i) {
    uint8_t msgType = 0;
    int32_t targetRank = 0;
    uint32_t payloadSize = 0;
    readPOD(msgType);
    readPOD(targetRank);
    readPOD(payloadSize);

    // Build Message from raw payload bytes
    Message msg;
    msg.header.type = msgType;
    msg.header.payload_length = payloadSize;
    msg.payload.assign(buf.begin() + ptrdiff_t(offset),
        buf.begin() + ptrdiff_t(offset) + ptrdiff_t(payloadSize));
    offset += payloadSize;

    if (targetRank != -1 && targetRank != rank())
      continue;

    switch (msgType) {
    case MessageType::SERVER_SET_OBJECT_PARAMETER: {
      messages::ParameterChange pc(msg, &scene);
      pc.execute();
      break;
    }
    case MessageType::SERVER_REMOVE_OBJECT_PARAMETER: {
      messages::ParameterRemove pr(msg, &scene);
      pr.execute();
      break;
    }
    case MessageType::SERVER_SET_ARRAY_DATA: {
      messages::TransferArrayData ta(msg, &scene);
      ta.execute();
      break;
    }
    case MessageType::SERVER_ADD_OBJECT: {
      messages::NewObject no(msg, &scene);
      no.execute();
      break;
    }
    case MessageType::SERVER_REMOVE_OBJECT: {
      messages::RemoveObject ro(msg, &scene);
      ro.execute();
      break;
    }
    case MessageType::SERVER_REMOVE_ALL_OBJECTS: {
      scene.removeAllObjects();
      break;
    }
    case MessageType::SERVER_UPDATE_LAYER: {
      messages::TransferLayer tl(msg, &scene);
      tl.execute();
      break;
    }
    default:
      break;
    }
  }
}

void DistributedRenderServer::renderFrame()
{
  MPI_Barrier(MPI_COMM_WORLD);
  m_renderPipeline.render();
  MPI_Barrier(MPI_COMM_WORLD);
}

void DistributedRenderServer::send_FrameBuffer()
{
  std::lock_guard lock(m_frameSendMutex);
  if (!is_ready<boost::system::error_code>(m_lastSentFrame)) {
    vsr::core::logDebug(
        "[vsrMPIServer] Previous frame still sending, skipping.");
    return;
  }
  m_lastSentFrame =
      m_server->send(MessageType::CLIENT_RECEIVE_FRAME_BUFFER_COLOR,
          m_session.frame.buffers.color);
}

///////////////////////////////////////////////////////////////////////////////
// Rank-0 helpers /////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void DistributedRenderServer::signalServerMode(ServerMode mode)
{
  if (m_pendingServerMode.load() == int(ServerMode::SHUTDOWN))
    return;
  m_pendingServerMode.store(int(mode));
}

// Peek at the DataTree of a structured message to determine target rank.
// Camera and renderer parameter changes go to all ranks (-1).
// Everything else goes to rank 0 only.
int DistributedRenderServer::determineTargetRank(const Message &msg)
{
  // Helper: parse a StructuredMessage and read root["o"] for object type
  struct Peek : public StructuredMessage
  {
    Peek(const Message &m) : StructuredMessage(m) {}
    void execute() override {}
  };

  try {
    switch (msg.header.type) {
    case MessageType::SERVER_SET_OBJECT_PARAMETER:
    case MessageType::SERVER_REMOVE_OBJECT_PARAMETER: {
      Peek peek(msg);
      const auto *oNode = peek.tree().root().child("o");
      if (!oNode)
        return 0;
      auto o = oNode->getValue();
      if (o.type() == ANARI_CAMERA || o.type() == ANARI_RENDERER)
        return -1; // global — apply on all ranks
      return 0;
    }
    case MessageType::SERVER_REMOVE_OBJECT: {
      Peek peek(msg);
      auto o = peek.tree().root().getValue();
      if (o.type() == ANARI_CAMERA || o.type() == ANARI_RENDERER)
        return -1;
      return 0;
    }
    default:
      return 0;
    }
  } catch (...) {
    return 0;
  }
}

void DistributedRenderServer::enqueueSceneOp(const Message &msg, int targetRank)
{
  PendingSceneOp op;
  op.messageType = msg.header.type;
  op.targetRank = int32_t(targetRank);
  op.payload = msg.payload;
  std::lock_guard lock(m_opsMutex);
  m_pendingOps.push_back(std::move(op));
}

} // namespace vsr::network
