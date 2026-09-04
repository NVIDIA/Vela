// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioRemoteTestHelpers.h"
#include "StudioServerTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "ArrayHistogram.h"
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PayloadCommon.h"
#include "ProjectOpReply.h"
#include "ProjectRequests.h"
#include "SceneEditMessages.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
#include "TaskMessages.h"
#include "ViewportMessages.h"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
#include "vsr/core/VSRMath.hpp"
// std
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::protocol;
using vsr::network::Message;
using namespace std::chrono_literals;

namespace {

constexpr uint32_t FRAME_WIDTH = 64;
constexpr uint32_t FRAME_HEIGHT = 48;
constexpr size_t SCALAR_COUNT = 1000;

// A Data Root holding one triangle in the z = 0 plane with corners at the
// origin, (1, 0, 0) and (0, 1, 0).
struct MeshFixture
{
  MeshFixture();
  ~MeshFixture();

  std::filesystem::path root;
  std::filesystem::path mesh;
};

MeshFixture::MeshFixture()
{
  static int counter = 0;
  root = std::filesystem::temp_directory_path()
      / ("vsr_studio_server_viewport_" + std::to_string(++counter));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  mesh = root / "triangle.obj";
  std::ofstream(mesh) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
}

MeshFixture::~MeshFixture()
{
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

// A started server with one bootstrapped client, the triangle imported and
// the shot camera looking straight at it, plus two arrays the histogram can
// be asked about: 1000 float32 values evenly spread over [0, 1] and a proxy.
struct ViewportSession
{
  explicit ViewportSession(const MeshFixture &data);

  template <typename R>
  ProjectOpReply request(R req);
  // Starts the stream; returns once the first Frame arrived.
  void startRendering();
  // Sends a Pick and waits for its reply.
  PickReply pick(int x, int y);
  // Pixels of the newest Frame.
  std::vector<std::byte> latestFramePixels();
  // Forgets every message so far and waits for three fresh Frames: the third
  // was certainly rendered after whatever was sent just before.
  std::vector<std::byte> framePixelsAfterChange();

  ServerOptions options;
  std::unique_ptr<StudioServer> server;
  std::unique_ptr<ServerLoop> loop;
  TestClient client;
  uint64_t nextRequestId{1};
  SceneObjectRef cameraRef;
  SceneObjectRef scalarArray;
  SceneObjectRef proxyArray;
  ShotID shotId;
};

ViewportSession::ViewportSession(const MeshFixture &data)
{
  options.port = 0;
  options.library = "helide";
  options.dataRoots = {data.root};
  server = std::make_unique<StudioServer>(options);
  std::string error;
  REQUIRE(server->start(&error));

  // Before run(): the last moment this thread may touch the scene.
  auto &scene = server->appContext().vsr.scene;
  auto scalars = scene.createArray(ANARI_FLOAT32, SCALAR_COUNT);
  auto *values = scalars->mapAs<float>();
  for (size_t i = 0; i < SCALAR_COUNT; ++i)
    values[i] = float(i) / float(SCALAR_COUNT - 1);
  scalars->unmap();
  scalarArray = {ANARI_ARRAY, scalars.index()};
  auto proxy = scene.createArrayProxy(ANARI_FLOAT32, 16);
  proxyArray = {ANARI_ARRAY, proxy.index()};

  const auto &project = server->projectContext().project();
  shotId = project.activeShotId;
  const auto *shot = project::activeShot(project);
  REQUIRE(shot);
  cameraRef = shot->camera;
  REQUIRE(cameraRef.objectIndex != VSR_INVALID_INDEX);

  loop = std::make_unique<ServerLoop>(server.get());
  client.connect(server->port());
  REQUIRE(client.waitForCount(StudioMessageType::Hello, 1));
  client.send(Hello{});
  REQUIRE(client.waitForCount(StudioMessageType::BootstrapEnd, 1));
  REQUIRE(waitFor(
      [&] { return server->sessionState() == SessionState::Established; }));

  ImportStaticDataset import;
  import.name = "Triangle";
  import.sourcePath = data.mesh;
  import.importerType = vsr::io::ImporterType::OBJ;
  const auto reply = request(import);
  REQUIRE(reply.ok);
  const auto started = results<TaskStartedResult>(reply);
  REQUIRE(started);
  REQUIRE(waitFor([&] {
    for (const auto &msg : client.messages()) {
      if (auto done = decode<TaskCompleted>(msg);
          done && done->taskId == started->taskId)
        return true;
    }
    return false;
  }));

  // Frame the triangle: its centroid sits on the view axis two units away,
  // so the centre pixel hits it and the corners see past it. Edits, unlike
  // project ops, do not re-sample the camera from the shot rig.
  const auto edit = [&](const char *name, vsr::core::Any value) {
    SetObjectParameter e;
    e.object = cameraRef;
    e.name = name;
    e.value = std::move(value);
    client.send(e);
  };
  edit("position", vsr::core::Any(vsr::math::float3(1.f / 3, 1.f / 3, 2.f)));
  edit("direction", vsr::core::Any(vsr::math::float3(0.f, 0.f, -1.f)));
  edit("up", vsr::core::Any(vsr::math::float3(0.f, 1.f, 0.f)));
  edit("fovy", vsr::core::Any(vsr::math::radians(40.f)));

  SetFrameConfig config;
  config.width = FRAME_WIDTH;
  config.height = FRAME_HEIGHT;
  client.send(config);
  REQUIRE(client.waitForCount(StudioMessageType::FrameConfig, 2));
  client.clear();
}

template <typename R>
ProjectOpReply ViewportSession::request(R req)
{
  req.requestId = nextRequestId++;
  client.send(req);
  const auto reply = client.waitForReply(req.requestId);
  REQUIRE(reply);
  return *reply;
}

void ViewportSession::startRendering()
{
  client.send(StartRendering{});
  REQUIRE(client.waitForCount(StudioMessageType::Frame, 1));
  const auto frame = decodeFrame(client.last(StudioMessageType::Frame));
  REQUIRE(frame);
  REQUIRE(frame->header.width == FRAME_WIDTH);
  REQUIRE(frame->header.height == FRAME_HEIGHT);
}

PickReply ViewportSession::pick(int x, int y)
{
  Pick p;
  p.requestId = nextRequestId++;
  p.x = x;
  p.y = y;
  client.send(p);
  std::optional<PickReply> reply;
  REQUIRE(waitFor([&] {
    for (const auto &msg : client.messages()) {
      if (auto r = decode<PickReply>(msg); r && r->requestId == p.requestId) {
        reply = r;
        return true;
      }
    }
    return false;
  }));
  return *reply;
}

std::vector<std::byte> ViewportSession::latestFramePixels()
{
  const auto frame = decodeFrame(client.last(StudioMessageType::Frame));
  REQUIRE(frame);
  REQUIRE(frame->header.encoding == FrameEncoding::Raw);
  return std::vector<std::byte>(frame->data, frame->data + frame->size);
}

std::vector<std::byte> ViewportSession::framePixelsAfterChange()
{
  client.clear();
  REQUIRE(client.waitForCount(StudioMessageType::Frame, 3));
  return latestFramePixels();
}

bool finite(const vsr::math::float3 &v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A ViewportSettings message whose AOV name no server knows.
Message bogusAOVSettings()
{
  vsr::core::DataTree tree;
  writeChild(tree.root(), "visualizeAOV", std::string("BOGUS"));
  Message msg;
  msg.header.type = uint8_t(StudioMessageType::ViewportSettings);
  tree.write(msg.payload);
  msg.header.payload_length = uint32_t(msg.payload.size());
  return msg;
}

} // namespace

SCENARIO("StudioServer picks against its frames", "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the pick test");
    return;
  }

  MeshFixture data;
  ViewportSession session(data);
  auto &client = session.client;

  GIVEN("a paused session looking at the triangle")
  {
    REQUIRE(session.server->sessionState() == SessionState::Established);

    THEN("a pick at the centre renders one frame and hits the surface")
    {
      const auto reply = session.pick(FRAME_WIDTH / 2, FRAME_HEIGHT / 2);
      REQUIRE(reply.hit);
      REQUIRE(reply.objectIdentity);
      REQUIRE(reply.objectIdentity->type == ANARI_SURFACE);
      REQUIRE(finite(reply.worldPosition));
      // On the triangle's plane, inside its bounds (depth is a ray length,
      // so allow for the float32 buffer).
      REQUIRE(reply.worldPosition.x == Approx(1.f / 3).margin(0.05));
      REQUIRE(reply.worldPosition.y == Approx(1.f / 3).margin(0.05));
      REQUIRE(reply.worldPosition.z == Approx(0.f).margin(0.05));
      // No Frame while paused, and the id channel is off again after.
      REQUIRE(client.count(StudioMessageType::Frame) == 0);
      REQUIRE(waitFor(
          [&] { return !session.server->viewport().idChannelEnabled(); }));

      AND_THEN("the identity names the surface the scene holds")
      {
        auto &scene = session.server->appContext().vsr.scene;
        REQUIRE(
            scene.getObject(ANARI_SURFACE, reply.objectIdentity->objectIndex));
      }
    }

    THEN("a pick at the top-left corner sees background")
    {
      const auto reply = session.pick(0, 0);
      REQUIRE_FALSE(reply.hit);
      REQUIRE_FALSE(reply.objectIdentity);
    }

    THEN("coordinates outside the frame are clamped, not refused")
    {
      const auto reply = session.pick(-100, 100000);
      REQUIRE_FALSE(reply.hit);
      REQUIRE(client.count(StudioMessageType::Error) == 0);
    }
  }

  GIVEN("a streaming session")
  {
    session.startRendering();

    THEN("picks are answered between frames and frames keep coming")
    {
      const auto before = client.count(StudioMessageType::Frame);
      const auto reply = session.pick(FRAME_WIDTH / 2, FRAME_HEIGHT / 2);
      REQUIRE(reply.hit);
      REQUIRE(reply.objectIdentity);
      REQUIRE(client.waitForCount(StudioMessageType::Frame, before + 2));
    }

    THEN("two picks in flight are latest-wins")
    {
      Pick first;
      first.requestId = session.nextRequestId++;
      first.x = int(FRAME_WIDTH / 2);
      first.y = int(FRAME_HEIGHT / 2);
      Pick second = first;
      second.requestId = session.nextRequestId++;
      second.x = 0;
      second.y = 0;
      client.send(first);
      client.send(second);

      // The second is always answered; the first only if the loop happened
      // to service it before the second landed in the latch.
      std::optional<PickReply> secondReply;
      REQUIRE(waitFor([&] {
        secondReply = client.lastDecoded<PickReply>();
        return secondReply && secondReply->requestId == second.requestId;
      }));
      REQUIRE_FALSE(secondReply->hit);
      const auto replies = client.count(StudioMessageType::PickReply);
      REQUIRE(replies >= 1);
      REQUIRE(replies <= 2);
      for (const auto &msg : client.messages()) {
        if (auto r = decode<PickReply>(msg)) {
          REQUIRE((r->requestId == first.requestId
              || r->requestId == second.requestId));
        }
      }
    }
  }
}

SCENARIO("StudioServer composites the viewport passes into its frames",
    "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the pass test");
    return;
  }

  MeshFixture data;
  ViewportSession session(data);
  auto &client = session.client;
  session.startRendering();
  const auto beauty = session.framePixelsAfterChange();
  REQUIRE(beauty.size() == FRAME_WIDTH * FRAME_HEIGHT * 4);
  REQUIRE_FALSE(session.server->viewport().idChannelEnabled());

  GIVEN("the picked surface's identity")
  {
    const auto picked = session.pick(FRAME_WIDTH / 2, FRAME_HEIGHT / 2);
    REQUIRE(picked.objectIdentity);

    WHEN("it is outlined")
    {
      SetOutline outline;
      outline.objectIdentity = picked.objectIdentity;
      client.send(outline);

      THEN("the id channel turns on and the frame changes")
      {
        REQUIRE(waitFor(
            [&] { return session.server->viewport().idChannelEnabled(); }));
        const auto outlined = session.framePixelsAfterChange();
        REQUIRE(outlined.size() == beauty.size());
        REQUIRE(outlined != beauty);

        AND_WHEN("the outline is cleared")
        {
          client.send(SetOutline{});

          THEN("the id channel turns off again")
          {
            REQUIRE(waitFor([&] {
              return !session.server->viewport().idChannelEnabled();
            }));
            REQUIRE(client.count(StudioMessageType::Error) == 0);
          }
        }

        AND_WHEN("highlighting is switched off in the settings")
        {
          ViewportSettings settings;
          settings.highlightSelection = false;
          client.send(settings);

          THEN("the outline no longer needs ids")
          {
            REQUIRE(waitFor([&] {
              return !session.server->viewport().idChannelEnabled();
            }));
          }
        }
      }
    }

    WHEN("an identity that is no surface or volume is outlined")
    {
      SetOutline outline;
      outline.objectIdentity = session.cameraRef;
      client.send(outline);

      THEN("it is treated as clear and frames keep coming")
      {
        session.framePixelsAfterChange();
        REQUIRE_FALSE(session.server->viewport().idChannelEnabled());
        REQUIRE(client.count(StudioMessageType::Error) == 0);
      }
    }
  }

  GIVEN("viewport settings")
  {
    WHEN("the DEPTH AOV is visualized")
    {
      ViewportSettings settings;
      settings.visualizeAOV = vsr::rendering::AOVType::DEPTH;
      settings.depthVisualMinimum = 1.f;
      settings.depthVisualMaximum = 3.f;
      client.send(settings);

      THEN("frames still arrive and differ from beauty")
      {
        const auto depth = session.framePixelsAfterChange();
        REQUIRE(depth.size() == beauty.size());
        REQUIRE(depth != beauty);
        REQUIRE_FALSE(session.server->viewport().idChannelEnabled());
      }
    }

    WHEN("the OBJECT_ID AOV is visualized")
    {
      ViewportSettings settings;
      settings.visualizeAOV = vsr::rendering::AOVType::OBJECT_ID;
      client.send(settings);

      THEN("the id channel is on without any outline")
      {
        REQUIRE(waitFor(
            [&] { return session.server->viewport().idChannelEnabled(); }));
        const auto ids = session.framePixelsAfterChange();
        REQUIRE(ids != beauty);

        AND_WHEN("defaults are restored")
        {
          client.send(ViewportSettings{});

          THEN("the id channel is off")
          {
            REQUIRE(waitFor([&] {
              return !session.server->viewport().idChannelEnabled();
            }));
          }
        }
      }
    }

    WHEN("the world bounds are shown")
    {
      ViewportSettings settings;
      settings.showWorldBounds = true;
      settings.worldBoundsWidth = 2;
      client.send(settings);

      THEN("frames arrive with the box drawn over the triangle")
      {
        const auto boxed = session.framePixelsAfterChange();
        REQUIRE(boxed.size() == beauty.size());
        REQUIRE(boxed != beauty);
        REQUIRE(client.count(StudioMessageType::Error) == 0);
      }
    }

    WHEN("an unknown AOV name is sent")
    {
      client.channel->send(bogusAOVSettings());

      THEN("the message is refused and the session survives")
      {
        REQUIRE(client.waitForCount(StudioMessageType::Error, 1));
        const auto error = client.lastDecoded<Error>();
        REQUIRE(error);
        REQUIRE(error->message.find("malformed ViewportSettings")
            != std::string::npos);
        session.framePixelsAfterChange();
        REQUIRE(session.server->streaming());
      }
    }
  }
}

SCENARIO("StudioServer bins scalar arrays on request", "[StudioServer]")
{
  if (!helideAvailable()) {
    WARN("helide ANARI library unavailable, skipping the histogram test");
    return;
  }

  MeshFixture data;
  ViewportSession session(data);
  auto &client = session.client;

  GIVEN("a float32 array of 1000 values evenly spread over [0, 1]")
  {
    WHEN("ten bins are requested")
    {
      RequestArrayHistogram req;
      req.array = session.scalarArray;
      req.binCount = 10;
      const auto reply = session.request(req);

      THEN("every bin holds 100, min and max are exact, no snapshot follows")
      {
        REQUIRE(reply.ok);
        const auto result = results<ArrayHistogramResult>(reply);
        REQUIRE(result);
        REQUIRE(result->bins.size() == 10);
        for (const auto count : result->bins)
          REQUIRE(count == 100);
        REQUIRE(std::accumulate(result->bins.begin(), result->bins.end(), 0u)
            == SCALAR_COUNT);
        REQUIRE(result->minValue == 0.f);
        REQUIRE(result->maxValue == 1.f);
        REQUIRE(client.count(StudioMessageType::ProjectSnapshot) == 0);
      }
    }

    WHEN("zero bins are requested")
    {
      RequestArrayHistogram req;
      req.array = session.scalarArray;
      req.binCount = 0;
      const auto reply = session.request(req);

      THEN("the count is clamped to one bin holding everything")
      {
        REQUIRE(reply.ok);
        const auto result = results<ArrayHistogramResult>(reply);
        REQUIRE(result);
        REQUIRE(result->bins == std::vector<uint64_t>{SCALAR_COUNT});
      }
    }

    WHEN("far too many bins are requested")
    {
      RequestArrayHistogram req;
      req.array = session.scalarArray;
      req.binCount = 1u << 20;
      const auto reply = session.request(req);

      THEN("the count is clamped to the maximum")
      {
        REQUIRE(reply.ok);
        const auto result = results<ArrayHistogramResult>(reply);
        REQUIRE(result);
        REQUIRE(result->bins.size() == MAX_HISTOGRAM_BINS);
      }
    }
  }

  GIVEN("arrays the server cannot bin")
  {
    THEN("the mesh's vector position array is refused")
    {
      auto &scene = session.server->appContext().vsr.scene;
      std::optional<SceneObjectRef> positions;
      for (size_t i = 0; i < scene.numberOfObjects(ANARI_ARRAY); ++i) {
        auto *obj = scene.getObject(ANARI_ARRAY, i);
        if (!obj)
          continue;
        if (static_cast<vsr::scene::Array *>(obj)->elementType()
            == ANARI_FLOAT32_VEC3) {
          positions = SceneObjectRef{ANARI_ARRAY, i};
          break;
        }
      }
      REQUIRE(positions);

      RequestArrayHistogram req;
      req.array = *positions;
      req.binCount = 8;
      const auto reply = session.request(req);
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("not scalar") != std::string::npos);
    }

    THEN("a proxy array is refused")
    {
      RequestArrayHistogram req;
      req.array = session.proxyArray;
      req.binCount = 8;
      const auto reply = session.request(req);
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("proxy") != std::string::npos);
    }

    THEN("a reference that is no array is refused")
    {
      RequestArrayHistogram req;
      req.array = session.cameraRef;
      req.binCount = 8;
      const auto reply = session.request(req);
      REQUIRE_FALSE(reply.ok);
      REQUIRE(reply.error.find("not an array") != std::string::npos);
    }
  }
}

SCENARIO("computeArrayHistogram bins host arrays", "[StudioServer]")
{
  vsr::scene::Scene scene;

  GIVEN("a uint8 array with a known distribution")
  {
    auto array = scene.createArray(ANARI_UINT8, 6);
    const uint8_t values[] = {0, 0, 10, 10, 10, 20};
    array->setData(values, 6);

    THEN("two bins split at the midpoint, the maximum closing the last")
    {
      ArrayHistogramResult result;
      std::string error;
      REQUIRE(computeArrayHistogram(*array, 2, result, &error));
      REQUIRE(result.minValue == 0.f);
      REQUIRE(result.maxValue == 20.f);
      REQUIRE(result.bins == std::vector<uint64_t>{2, 4});
    }
  }

  GIVEN("an array whose values are all equal")
  {
    auto array = scene.createArray(ANARI_FLOAT32, 5);
    const float values[] = {3.f, 3.f, 3.f, 3.f, 3.f};
    array->setData(values, 5);

    THEN("everything lands in bin 0 and min equals max")
    {
      ArrayHistogramResult result;
      REQUIRE(computeArrayHistogram(*array, 4, result));
      REQUIRE(result.minValue == 3.f);
      REQUIRE(result.maxValue == 3.f);
      REQUIRE(result.bins == std::vector<uint64_t>{5, 0, 0, 0});
    }
  }

  GIVEN("a float32 array with NaN and infinite elements")
  {
    auto array = scene.createArray(ANARI_FLOAT32, 6);
    const float inf = std::numeric_limits<float>::infinity();
    const float values[] = {
        0.f, 0.5f, 1.f, inf, -inf, std::numeric_limits<float>::quiet_NaN()};
    array->setData(values, 6);

    THEN("the finite elements set the range and fill the bins alone")
    {
      ArrayHistogramResult result;
      REQUIRE(computeArrayHistogram(*array, 2, result));
      REQUIRE(result.minValue == 0.f);
      REQUIRE(result.maxValue == 1.f);
      REQUIRE(result.bins == std::vector<uint64_t>{1, 2});
      REQUIRE(result.nonFinite == 3);
    }
  }

  GIVEN("a float64 array whose values exceed float range")
  {
    auto array = scene.createArray(ANARI_FLOAT64, 3);
    const double values[] = {1.0, 2.0, 1e300};
    array->setData(values, 3);

    THEN("the overflowing element is left out")
    {
      ArrayHistogramResult result;
      REQUIRE(computeArrayHistogram(*array, 2, result));
      REQUIRE(result.minValue == 1.f);
      REQUIRE(result.maxValue == 2.f);
      REQUIRE(result.bins == std::vector<uint64_t>{1, 1});
      REQUIRE(result.nonFinite == 1);
    }
  }

  GIVEN("an array with no finite element")
  {
    auto array = scene.createArray(ANARI_FLOAT32, 2);
    const float values[] = {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()};
    array->setData(values, 2);

    THEN("the range is empty and so are the bins")
    {
      ArrayHistogramResult result;
      REQUIRE(computeArrayHistogram(*array, 3, result));
      REQUIRE(result.minValue == 0.f);
      REQUIRE(result.maxValue == 0.f);
      REQUIRE(result.bins == std::vector<uint64_t>{0, 0, 0});
      REQUIRE(result.nonFinite == 2);
    }
  }

  GIVEN("a ufixed8 array")
  {
    auto array = scene.createArray(ANARI_UFIXED8, 2);
    const uint8_t values[] = {0, 255};
    array->setData(values, 2);

    THEN("values count in ANARI's normalized range")
    {
      ArrayHistogramResult result;
      REQUIRE(computeArrayHistogram(*array, 2, result));
      REQUIRE(result.minValue == 0.f);
      REQUIRE(result.maxValue == 1.f);
      REQUIRE(result.bins == std::vector<uint64_t>{1, 1});
    }
  }
}
