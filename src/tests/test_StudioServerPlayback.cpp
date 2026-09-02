// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "StudioServerTestHelpers.h"
#include "catch.hpp"
// vsr_scivis_studio_server_core
#include "ServerOptions.h"
#include "StudioServer.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PlaybackMessages.h"
#include "ProjectOpReply.h"
#include "ProjectSnapshot.h"
#include "SceneEditMessages.h"
#include "SessionMessages.h"
#include "ShotRigRequests.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// vsr_animation
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/animation/FileBinding.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Camera.hpp"
// std
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace vsr::scivis_studio;
using namespace vsr::scivis_studio::server;
using namespace vsr::scivis_studio::protocol;
using vsr::network::Message;
using namespace std::chrono_literals;

namespace {

// A file binding whose frames past `lastGoodFrame` refuse to load: the
// in-process stand-in for a file animation with a corrupt tail.
struct FailingFileBinding : public vsr::animation::FileBinding
{
  FailingFileBinding(vsr::scene::Scene *scene, int frames, int lastGoodFrame)
      : FileBinding(scene), frames(frames), lastGoodFrame(lastGoodFrame)
  {}

  std::string kind() const override
  {
    return "failing";
  }

  void toDataNode(vsr::core::DataNode &) const override {}

  void update(float t) override
  {
    const int frame = int(std::lround(t * float(frames - 1)));
    if (frame > lastGoodFrame)
      reportLoadFailure(frame, "frame " + std::to_string(frame) + " is bad");
  }

  int frames{0};
  int lastGoodFrame{0};

 private:
  void addCallbackToAnimation(vsr::animation::Animation &anim) override
  {
    anim.addCallbackBinding([this](float t) { update(t); });
  }
};

// A started server on a fresh project, one bootstrapped client, rendering
// small raw frames. `beforeLoop` runs between start() and the loop thread,
// while the server's state is still the caller's to touch.
struct PlaybackSession
{
  explicit PlaybackSession(
      const std::function<void(StudioServer &)> &beforeLoop = {});

  template <typename R>
  ProjectOpReply request(R req);
  // Replaces the active shot's clock; waits for the snapshot it earns.
  void setClock(int frameCount, float fps, bool loop);
  bool waitForSnapshots(size_t n);
  ProjectSnapshot latestSnapshot();
  // Frame numbers of every Frame received so far, in arrival order.
  std::vector<int> frameNumbers();
  std::optional<FrameHeader> latestFrameHeader();
  bool waitForFrame(int frame);
  // Waits until `n` more frames than now have arrived.
  bool waitForMoreFrames(size_t n);

  ServerOptions options;
  std::unique_ptr<StudioServer> server;
  std::unique_ptr<ServerLoop> loop;
  TestClient client;
  uint64_t nextRequestId{1};
  ShotID shotId;
  SceneObjectRef cameraRef;
  Shot initialShot;
};

PlaybackSession::PlaybackSession(
    const std::function<void(StudioServer &)> &beforeLoop)
{
  options.port = 0;
  options.library = "helide";
  server = std::make_unique<StudioServer>(options);
  std::string error;
  REQUIRE(server->start(&error));
  if (beforeLoop)
    beforeLoop(*server);
  loop = std::make_unique<ServerLoop>(server.get());
  client.connect(server->port());
  REQUIRE(client.waitForCount(StudioMessageType::Hello, 1));
  client.send(Hello{});
  REQUIRE(client.waitForCount(StudioMessageType::BootstrapEnd, 1));
  const auto bootstrap = client.lastDecoded<ProjectSnapshot>();
  REQUIRE(bootstrap);
  REQUIRE(bootstrap->project.shots.size() == 1);
  initialShot = bootstrap->project.shots.front();
  shotId = bootstrap->project.activeShotId;
  cameraRef = initialShot.camera;
  REQUIRE(cameraRef.objectIndex != VSR_INVALID_INDEX);
  client.clear();

  SetEncodings encodings;
  encodings.supported = {FrameEncoding::Raw};
  client.send(encodings);
  SetFrameConfig frameConfig;
  frameConfig.width = 32;
  frameConfig.height = 24;
  client.send(frameConfig);
  client.send(StartRendering{});
  REQUIRE(client.waitForCount(StudioMessageType::Frame, 1));
  REQUIRE(waitFor(
      [&] { return server->sessionState() == SessionState::Rendering; }));
  client.clear();
}

template <typename R>
ProjectOpReply PlaybackSession::request(R req)
{
  req.requestId = nextRequestId++;
  client.send(req);
  const auto reply = client.waitForReply(req.requestId);
  REQUIRE(reply);
  return *reply;
}

void PlaybackSession::setClock(int frameCount, float fps, bool loop)
{
  UpdateShot update;
  update.shot = initialShot;
  update.shot.frameCount = frameCount;
  update.shot.fps = fps;
  update.shot.loop = loop;
  const size_t snapshots = client.count(StudioMessageType::ProjectSnapshot);
  REQUIRE(request(update).ok);
  REQUIRE(waitForSnapshots(snapshots + 1));
  initialShot = latestSnapshot().project.shots.front();
}

bool PlaybackSession::waitForSnapshots(size_t n)
{
  return client.waitForCount(StudioMessageType::ProjectSnapshot, n);
}

ProjectSnapshot PlaybackSession::latestSnapshot()
{
  const auto snapshot = client.lastDecoded<ProjectSnapshot>();
  REQUIRE(snapshot);
  return *snapshot;
}

std::vector<int> PlaybackSession::frameNumbers()
{
  std::vector<int> frames;
  for (const auto &msg : client.messages()) {
    if (msg.header.type != uint8_t(StudioMessageType::Frame))
      continue;
    if (auto view = decodeFrame(msg))
      frames.push_back(int(view->header.frame));
  }
  return frames;
}

std::optional<FrameHeader> PlaybackSession::latestFrameHeader()
{
  const auto msg = client.last(StudioMessageType::Frame);
  if (msg.header.type != uint8_t(StudioMessageType::Frame))
    return {};
  const auto view = decodeFrame(msg);
  if (!view)
    return {};
  return view->header;
}

bool PlaybackSession::waitForFrame(int frame)
{
  return waitFor([&] {
    const auto header = latestFrameHeader();
    return header && int(header->frame) == frame;
  });
}

bool PlaybackSession::waitForMoreFrames(size_t n)
{
  const size_t target = client.count(StudioMessageType::Frame) + n;
  return client.waitForCount(StudioMessageType::Frame, target);
}

// The Shot the snapshot carries for `id`.
const Shot *findShot(const ProjectSnapshot &snapshot, const ShotID &id)
{
  for (const auto &shot : snapshot.project.shots)
    if (shot.id == id)
      return &shot;
  return nullptr;
}

std::optional<vsr::math::float3> cameraParameter(
    PlaybackSession &session, const char *name)
{
  auto &scene = session.server->appContext().vsr.scene;
  auto *camera = scene.getObject(ANARI_CAMERA, session.cameraRef.objectIndex);
  if (!camera)
    return {};
  return camera->parameterValueAs<vsr::math::float3>(name);
}

bool near(const vsr::math::float3 &a, const vsr::math::float3 &b)
{
  return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f
      && std::fabs(a.z - b.z) < 1e-3f;
}

} // namespace

SCENARIO("The server free-runs playback of the active shot", "[StudioServer]")
{
  GIVEN("A rendering session on a looping 1000-frame shot at 1000 fps")
  {
    PlaybackSession session;
    session.setClock(1000, 1000.f, true);
    session.client.clear();

    WHEN("the client sends SetPlaying true")
    {
      const auto reply = session.request(SetPlaying{0, session.shotId, true});

      THEN("the reply is ok and one snapshot says the shot plays")
      {
        REQUIRE(reply.ok);
        REQUIRE(session.waitForSnapshots(1));
        const auto *shot = findShot(session.latestSnapshot(), session.shotId);
        REQUIRE(shot);
        REQUIRE(shot->playing);

        AND_THEN("frames advance one at a time, never skipping")
        {
          REQUIRE(session.waitForMoreFrames(40));
          const auto frames = session.frameNumbers();
          REQUIRE(frames.size() >= 40);
          REQUIRE(frames.back() > frames.front());
          for (size_t i = 1; i < frames.size(); ++i) {
            const int step = frames[i] - frames[i - 1];
            const bool wrapped = frames[i - 1] == 999 && frames[i] == 0;
            CAPTURE(i, frames[i - 1], frames[i]);
            REQUIRE((step == 0 || step == 1 || wrapped));
          }
          // Time is in motion: no snapshot carries it.
          REQUIRE(
              session.client.count(StudioMessageType::ProjectSnapshot) == 1);

          AND_THEN("SetPlaying false commits the frame time rests on")
          {
            const auto paused =
                session.request(SetPlaying{0, session.shotId, false});
            REQUIRE(paused.ok);
            REQUIRE(session.waitForSnapshots(2));
            const auto *rested =
                findShot(session.latestSnapshot(), session.shotId);
            REQUIRE(rested);
            REQUIRE_FALSE(rested->playing);
            // Frames rendered after the pause show the committed frame.
            REQUIRE(session.waitForMoreFrames(2));
            const auto header = session.latestFrameHeader();
            REQUIRE(header);
            REQUIRE(int(header->frame) == rested->currentFrame);
          }
        }
      }
    }

    WHEN("SetPlaying names an unknown or a non-active shot")
    {
      const auto unknown = session.request(SetPlaying{0, "no-such-shot", true});
      REQUIRE(session.request(CreateShot{0, "Two"}).ok); // becomes active
      REQUIRE(session.waitForSnapshots(1));
      const auto inactive =
          session.request(SetPlaying{0, session.shotId, true});

      THEN("both are refused with an error reply and nothing plays")
      {
        REQUIRE_FALSE(unknown.ok);
        REQUIRE_FALSE(unknown.error.empty());
        REQUIRE_FALSE(inactive.ok);
        REQUIRE_FALSE(inactive.error.empty());
        REQUIRE(session.waitForMoreFrames(3));
        REQUIRE(session.client.count(StudioMessageType::ProjectSnapshot) == 1);
        const auto header = session.latestFrameHeader();
        REQUIRE(header);
        REQUIRE(header->frame == 0);
      }
    }
  }

  GIVEN("A rendering session on a non-looping 5-frame shot")
  {
    PlaybackSession session;
    session.setClock(5, 200.f, false);
    session.client.clear();

    WHEN("the shot plays off its end")
    {
      REQUIRE(session.request(SetPlaying{0, session.shotId, true}).ok);
      REQUIRE(session.waitForSnapshots(1));

      THEN("exactly one more snapshot commits the auto-stop on the last frame")
      {
        REQUIRE(session.waitForSnapshots(2));
        const auto *shot = findShot(session.latestSnapshot(), session.shotId);
        REQUIRE(shot);
        REQUIRE_FALSE(shot->playing);
        REQUIRE(shot->currentFrame == 4);

        REQUIRE(session.waitForMoreFrames(5));
        REQUIRE(session.client.count(StudioMessageType::ProjectSnapshot) == 2);
        const auto header = session.latestFrameHeader();
        REQUIRE(header);
        REQUIRE(header->frame == 4);
        const auto frames = session.frameNumbers();
        for (size_t i = 1; i < frames.size(); ++i)
          REQUIRE(frames[i] - frames[i - 1] <= 1);
      }
    }
  }
}

SCENARIO("A paused scrub shows at once and commits after a quiet spell",
    "[StudioServer]")
{
  GIVEN("A rendering session on the default 120-frame shot, paused")
  {
    PlaybackSession session;

    WHEN("one SetTime arrives")
    {
      session.client.send(SetTime{session.shotId, 7});

      THEN("the next frames carry the frame and one snapshot follows")
      {
        REQUIRE(session.waitForFrame(7));
        REQUIRE(session.waitForSnapshots(1));
        const auto *shot = findShot(session.latestSnapshot(), session.shotId);
        REQUIRE(shot);
        REQUIRE(shot->currentFrame == 7);
        REQUIRE_FALSE(shot->playing);
        std::this_thread::sleep_for(400ms);
        REQUIRE(session.client.count(StudioMessageType::ProjectSnapshot) == 1);
      }
    }

    WHEN("a burst of 20 SetTimes arrives")
    {
      for (int frame = 10; frame < 30; ++frame)
        session.client.send(SetTime{session.shotId, frame});

      THEN("one snapshot commits the last frame of the burst")
      {
        REQUIRE(session.waitForFrame(29));
        REQUIRE(session.waitForSnapshots(1));
        const auto *shot = findShot(session.latestSnapshot(), session.shotId);
        REQUIRE(shot);
        REQUIRE(shot->currentFrame == 29);
        std::this_thread::sleep_for(400ms);
        REQUIRE(session.client.count(StudioMessageType::ProjectSnapshot) == 1);

        AND_THEN("a scrub that lands where time already rests commits nothing")
        {
          session.client.clear();
          session.client.send(SetTime{session.shotId, 29});
          REQUIRE(session.waitForMoreFrames(2));
          std::this_thread::sleep_for(400ms);
          REQUIRE(
              session.client.count(StudioMessageType::ProjectSnapshot) == 0);
        }
      }
    }

    WHEN("SetTime names a shot that is not active")
    {
      const auto created = session.request(CreateShot{0, "Two"});
      REQUIRE(created.ok);
      REQUIRE(session.waitForSnapshots(1));
      const auto active = session.latestSnapshot().project.activeShotId;
      REQUIRE(active != session.shotId);
      session.client.clear();
      session.client.send(SetTime{session.shotId, 3});

      THEN("it is ignored: frames stay on frame 0 and no snapshot is sent")
      {
        REQUIRE(session.waitForMoreFrames(3));
        std::this_thread::sleep_for(400ms);
        for (const int frame : session.frameNumbers())
          REQUIRE(frame == 0);
        const auto header = session.latestFrameHeader();
        REQUIRE(header);
        REQUIRE(header->shotId == active);
        REQUIRE(session.client.count(StudioMessageType::ProjectSnapshot) == 0);
      }
    }
  }
}

SCENARIO("A client camera edit survives a time change", "[StudioServer]")
{
  GIVEN("A rendering session whose client moved the shot camera")
  {
    PlaybackSession session;
    const vsr::math::float3 position{1.f, 2.f, 3.f};
    const vsr::math::float3 direction{0.f, 0.f, -1.f};
    const vsr::math::float3 up{0.f, 1.f, 0.f};
    const auto edit = [&](const char *name, const vsr::math::float3 &value) {
      SetObjectParameter e;
      e.object = session.cameraRef;
      e.name = name;
      e.value = vsr::core::Any(value);
      session.client.send(e);
    };
    edit("position", position);
    edit("direction", direction);
    edit("up", up);
    REQUIRE(waitFor([&] {
      const auto p = cameraParameter(session, "position");
      return p && near(*p, position);
    }));

    WHEN("a SetTime lands")
    {
      session.client.send(SetTime{session.shotId, 5});
      REQUIRE(session.waitForFrame(5));

      THEN("the camera keeps the client's pose")
      {
        const auto p = cameraParameter(session, "position");
        const auto d = cameraParameter(session, "direction");
        REQUIRE(p);
        REQUIRE(d);
        REQUIRE(near(*p, position));
        REQUIRE(near(*d, direction));
      }
    }
  }
}

SCENARIO("Frames that fail to load raise TimeAdvanceWarning", "[StudioServer]")
{
  GIVEN("A rendering session with a file binding whose frames past 50 are bad")
  {
    PlaybackSession session([](StudioServer &server) {
      auto &ctx = server.appContext();
      auto &anim = ctx.vsr.animationMgr.addAnimation("failing");
      anim.emplaceFileBinding<FailingFileBinding>(&ctx.vsr.scene, 120, 50);
    });

    WHEN("a scrub lands on a bad frame")
    {
      session.client.send(SetTime{session.shotId, 60});

      THEN("one warning names the shot, the frame and the reason")
      {
        REQUIRE(session.client.waitForCount(
            StudioMessageType::TimeAdvanceWarning, 1));
        const auto warning = session.client.lastDecoded<TimeAdvanceWarning>();
        REQUIRE(warning);
        REQUIRE(warning->shotId == session.shotId);
        REQUIRE(warning->frame == 60);
        REQUIRE(warning->message == "frame 60 is bad");
        REQUIRE(session.waitForFrame(60));
        REQUIRE(
            session.client.count(StudioMessageType::TimeAdvanceWarning) == 1);

        AND_THEN("playback across bad frames warns per frame and goes on")
        {
          // The new clock rests on frame 0; playing from there crosses into
          // the bad frames at 51.
          session.setClock(120, 500.f, true);
          session.client.clear();
          REQUIRE(session.request(SetPlaying{0, session.shotId, true}).ok);
          REQUIRE(session.client.waitForCount(
              StudioMessageType::TimeAdvanceWarning, 5));
          REQUIRE(session.waitForMoreFrames(3));
          const auto frames = session.frameNumbers();
          REQUIRE(frames.back() > 50);
          const auto warning = session.client.lastDecoded<TimeAdvanceWarning>();
          REQUIRE(warning);
          REQUIRE(warning->frame > 50);
          REQUIRE(warning->message
              == "frame " + std::to_string(warning->frame) + " is bad");
        }
      }
    }
  }
}
