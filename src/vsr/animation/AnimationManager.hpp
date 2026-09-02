// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Animation.hpp"

// std
#include <functional>
#include <string>
#include <vector>

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::animation {

/*
 * Central owner of all animations in a scene; provides time and frame control
 * and dispatches animation evaluation to each owned Animation instance.
 *
 * Example:
 *   AnimationManager mgr(&scene);
 *   Animation &a = mgr.addAnimation("fade");
 *   mgr.setAnimationTime(1.0f);
 *   mgr.incrementAnimationFrame();
 */
struct AnimationManager
{
  VSR_NOT_COPYABLE(AnimationManager)
  VSR_NOT_MOVEABLE(AnimationManager)

  AnimationManager(vsr::scene::Scene *scene);
  ~AnimationManager();

  using TimeChangedCallback = std::function<void(float)>;
  void setTimeChangedCallback(TimeChangedCallback cb);

  // Fired by tick() right after playback stops on its own at the end of a
  // non-looping clock (m_playing is already false); stop() does not fire it.
  // One slot, last setter wins, like the time-changed callback.
  using PlaybackStoppedCallback = std::function<void()>;
  void setPlaybackStoppedCallback(PlaybackStoppedCallback cb);

  // A frame a FileBinding could not load: `frame` is the binding's own file
  // index for the requested time, `message` the reason. Bindings report them
  // through FileBinding::reportLoadFailure(); whoever drives time collects
  // them with takeLoadFailures(), which returns and clears the list.
  struct LoadFailure
  {
    int frame{0};
    std::string message;
  };
  void reportLoadFailure(int frame, std::string message);
  std::vector<LoadFailure> takeLoadFailures();

  scene::Scene *scene() const;

  // Animation collection
  Animation &addAnimation(const std::string &name = "<unnamed_animation>");
  std::vector<Animation> &animations();
  const std::vector<Animation> &animations() const;
  void removeAnimation(size_t index);
  void removeAllAnimations();

  // Time control
  void setAnimationTime(float time);
  float getAnimationTime() const;
  bool isApplyingAnimations() const;
  void setAnimationIncrement(float increment);
  float getAnimationIncrement() const;
  void incrementAnimationTime();

  // Frame control
  int getAnimationTotalFrames() const;
  void setAnimationTotalFrames(int frames);
  void setAnimationFPS(float fps);
  float getAnimationFPS() const;
  int getAnimationFrame() const;
  void setAnimationFrame(int frame);
  void incrementAnimationFrame();

  // Widen the clock to hold content that needs `frames` at `fps`, keeping
  // whatever is already longer or faster. Every Animation shares this one
  // clock, so an import must not clobber it: content needing more frames than
  // the clock has would be under-sampled, while content needing fewer still
  // plays correctly on a longer clock. Returns false when the request lost,
  // so the caller can say so.
  bool widenClock(int frames, float fps);

  // Playing state — call tick(elapsedSeconds) once per UI frame. A tick
  // advances at most one frame, however much wall-clock time has passed: fps
  // is a ceiling, not a promise, so a slow iteration (a file binding loading
  // an 800 ms frame) is never made up for by skipping frames -- every frame is
  // the point. The elapsed time carried over to the next call is capped at
  // one frame's worth. Returns true when the frame changed.
  bool tick(float elapsedSeconds);
  void play();
  void stop();
  void togglePlay();
  bool isPlaying() const;

  // Loop state
  void setLoop(bool loop);
  bool isLoop() const;

 private:
  void setAnimationTimeInternal(float time, bool resetPlaybackAccumulator);
  void setAnimationFrameInternal(int frame, bool resetPlaybackAccumulator);
  // Playback ran off the end of a non-looping clock.
  void stopAtEnd();

  scene::Scene *m_scene{nullptr};
  TimeChangedCallback m_timeChangedCallback;
  PlaybackStoppedCallback m_playbackStoppedCallback;
  std::vector<LoadFailure> m_loadFailures;
  float m_incrementSize{0.01f};
  float m_animationFPS{30.f};
  float m_time{0.f};
  float m_playbackAccumulator{0.f};
  int m_totalFrames{100};
  bool m_playing{false};
  bool m_loop{true};
  bool m_applyingAnimations{false};
  std::vector<Animation> m_animations;
  size_t m_defragToken{0};
};

} // namespace vsr::animation
