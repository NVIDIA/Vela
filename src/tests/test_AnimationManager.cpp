// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/io/serialization/serialization_internal.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/UpdateDelegate.hpp"
#include "vsr/scene/objects/Array.hpp"
// std
#include <cmath>
#include <string>
#include <vector>

using vsr::animation::AnimationManager;
using vsr::scene::Scene;

namespace {

// Records the batch bracket and the array unmaps inside it, which is the
// signal a render index coalesces its world rebuilds on.
struct BatchRecordingDelegate : public vsr::scene::EmptyUpdateDelegate
{
  void signalUpdateBatchBegin() override
  {
    depth++;
    begins++;
  }

  void signalUpdateBatchEnd() override
  {
    depth--;
    ends++;
  }

  void signalArrayUnmapped(const vsr::scene::Array *) override
  {
    unmapsInsideBatch += depth > 0 ? 1 : 0;
    unmapsOutsideBatch += depth > 0 ? 0 : 1;
  }

  int depth{0};
  int begins{0};
  int ends{0};
  int unmapsInsideBatch{0};
  int unmapsOutsideBatch{0};
};

// A file binding whose frames past `lastGoodFrame` refuse to load.
struct FailingFileBinding : public vsr::animation::FileBinding
{
  FailingFileBinding(Scene *scene, int frames, int lastGoodFrame)
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
    else
      loaded = frame;
  }

  int frames{0};
  int lastGoodFrame{0};
  int loaded{0};

 private:
  void addCallbackToAnimation(vsr::animation::Animation &anim) override
  {
    anim.addCallbackBinding([this](float t) { update(t); });
  }
};

} // namespace

SCENARIO("A file binding reports the frames it cannot load", "[AnimationManager]")
{
  GIVEN("A manager owning a binding whose last two frames are bad")
  {
    Scene scene;
    AnimationManager mgr(&scene);
    mgr.setAnimationTotalFrames(5);
    auto &anim = mgr.addAnimation("files");
    auto &binding = anim.emplaceFileBinding<FailingFileBinding>(&scene, 5, 2);

    WHEN("Time lands on a good frame")
    {
      mgr.setAnimationFrame(2);

      THEN("Nothing is reported and the frame loaded")
      {
        REQUIRE(binding.loaded == 2);
        REQUIRE(mgr.takeLoadFailures().empty());
      }
    }

    WHEN("Time lands on bad frames")
    {
      mgr.setAnimationFrame(3);
      mgr.setAnimationFrame(4);

      THEN("takeLoadFailures() returns each failure once, then nothing")
      {
        const auto failures = mgr.takeLoadFailures();
        REQUIRE(failures.size() == 2);
        REQUIRE(failures[0].frame == 3);
        REQUIRE(failures[0].message == "frame 3 is bad");
        REQUIRE(failures[1].frame == 4);
        REQUIRE(mgr.takeLoadFailures().empty());
      }
    }

    WHEN("Playback runs across a bad frame")
    {
      mgr.setAnimationFPS(1.f);
      mgr.setAnimationFrame(2);
      mgr.play();
      mgr.tick(1.f);

      THEN("The failure is collected and playback goes on")
      {
        REQUIRE(mgr.getAnimationFrame() == 3);
        REQUIRE(mgr.isPlaying());
        REQUIRE(mgr.takeLoadFailures().size() == 1);
      }
    }
  }
}

SCENARIO("A time change is one update batch", "[AnimationManager]")
{
  GIVEN("Several bindings that each rewrite an Array")
  {
    Scene scene;
    AnimationManager mgr(&scene);

    auto *recorder =
        scene.updateDelegate().emplace<BatchRecordingDelegate>();

    std::vector<vsr::scene::ArrayRef> arrays;
    for (int i = 0; i < 3; ++i) {
      auto &anim = mgr.addAnimation("rewriter" + std::to_string(i));
      auto array = scene.createArray(ANARI_FLOAT32_MAT4, 1);
      arrays.push_back(array);
      anim.addCallbackBinding([array](float t) mutable {
        const auto m = vsr::math::IDENTITY_MAT4;
        array->setData(&m, 1);
      });
    }

    WHEN("The animation time changes once")
    {
      const int unmapsBefore = recorder->unmapsOutsideBatch;
      mgr.setAnimationTime(0.5f);

      THEN("Every rewrite lands inside exactly one balanced batch")
      {
        REQUIRE(recorder->begins == 1);
        REQUIRE(recorder->ends == 1);
        REQUIRE(recorder->depth == 0);
        REQUIRE(recorder->unmapsInsideBatch == 3);
        REQUIRE(recorder->unmapsOutsideBatch == unmapsBefore);
      }
    }
  }
}

SCENARIO("vsr::animation::AnimationManager playback", "[AnimationManager]")
{
  Scene scene;
  AnimationManager mgr(&scene);

  GIVEN("A manager configured for deterministic frame playback")
  {
    mgr.setAnimationTotalFrames(5);
    mgr.setAnimationFPS(2.f);

    WHEN("Playing with one frame worth of elapsed wall-clock time")
    {
      mgr.play();
      mgr.tick(0.5f);

      THEN("Playback advances exactly one frame")
      {
        REQUIRE(mgr.getAnimationFrame() == 1);
      }
    }

    WHEN("A slow frame accumulates enough time for multiple animation steps")
    {
      mgr.play();
      const bool advanced = mgr.tick(1.25f);

      THEN("Playback advances one frame only: fps is a ceiling, never a skip")
      {
        REQUIRE(advanced);
        REQUIRE(mgr.getAnimationFrame() == 1);
      }

      THEN("The carried-over time is capped at one frame's worth")
      {
        // 0.75 s were left over; only 0.5 s (one frame) carry, so the next
        // tick advances once, and the one after that needs fresh time.
        REQUIRE(mgr.tick(0.001f));
        REQUIRE(mgr.getAnimationFrame() == 2);
        REQUIRE_FALSE(mgr.tick(0.001f));
        REQUIRE(mgr.getAnimationFrame() == 2);
      }
    }

    WHEN("Too little time has passed for a frame")
    {
      mgr.play();

      THEN("tick() reports that nothing advanced")
      {
        REQUIRE_FALSE(mgr.tick(0.1f));
        REQUIRE(mgr.getAnimationFrame() == 0);
      }
    }

    WHEN("Looping playback advances past the last frame")
    {
      mgr.setAnimationFrame(4);
      mgr.play();
      mgr.tick(0.5f);

      THEN("Playback wraps back to the first frame")
      {
        REQUIRE(mgr.getAnimationFrame() == 0);
      }
    }

    WHEN("Non-looping playback reaches the last frame")
    {
      int stopped = 0;
      bool playingWhenStopped = true;
      mgr.setPlaybackStoppedCallback([&] {
        stopped++;
        playingWhenStopped = mgr.isPlaying();
      });
      mgr.setLoop(false);
      mgr.setAnimationFrame(3);
      mgr.play();
      const bool advanced = mgr.tick(1.0f);

      THEN("Playback stops on the last frame and says so once")
      {
        REQUIRE(advanced);
        REQUIRE(mgr.getAnimationFrame() == 4);
        REQUIRE_FALSE(mgr.isPlaying());
        REQUIRE(stopped == 1);
        REQUIRE_FALSE(playingWhenStopped);
      }

      THEN("Further ticks and an explicit stop() do not fire the callback")
      {
        REQUIRE_FALSE(mgr.tick(1.0f));
        mgr.play();
        mgr.stop();
        REQUIRE(stopped == 1);
      }
    }

    WHEN("Non-looping playback starts on the last frame")
    {
      int stopped = 0;
      mgr.setPlaybackStoppedCallback([&] { stopped++; });
      mgr.setLoop(false);
      mgr.setAnimationFrame(4);
      mgr.play();
      const bool advanced = mgr.tick(1.0f);

      THEN("The first tick stops playback without advancing")
      {
        REQUIRE_FALSE(advanced);
        REQUIRE(mgr.getAnimationFrame() == 4);
        REQUIRE_FALSE(mgr.isPlaying());
        REQUIRE(stopped == 1);
      }
    }

    WHEN("An explicit seek happens after partial playback accumulation")
    {
      mgr.play();
      mgr.tick(0.25f);
      mgr.setAnimationFrame(2);
      mgr.tick(0.25f);

      THEN("The seek clears the accumulator so no extra frame is consumed")
      {
        REQUIRE(mgr.getAnimationFrame() == 2);
      }
    }
  }

  GIVEN("An animation manager with custom playback settings")
  {
    mgr.setAnimationTime(0.3f);
    mgr.setAnimationIncrement(0.2f);
    mgr.setAnimationTotalFrames(9);
    mgr.setAnimationFPS(12.f);

    WHEN("The manager is serialized and restored")
    {
      vsr::core::DataTree tree;
      vsr::io::animationManagerToNode(mgr, tree.root()["animations"]);

      Scene restoredScene;
      AnimationManager restored(&restoredScene);
      vsr::io::nodeToAnimationManager(
          tree.root()["animations"], restored, restoredScene);

      THEN("Playback FPS and existing timing state round-trip")
      {
        REQUIRE(restored.getAnimationTime() == Approx(0.3f));
        REQUIRE(restored.getAnimationIncrement() == Approx(0.2f));
        REQUIRE(restored.getAnimationTotalFrames() == 9);
        REQUIRE(restored.getAnimationFPS() == Approx(12.f));
      }
    }
  }
}
