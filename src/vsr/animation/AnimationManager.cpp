// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AnimationManager.hpp"
#include "vsr/core/Logging.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vsr::animation {

AnimationManager::AnimationManager(vsr::scene::Scene *scene) : m_scene(scene)
{
  if (!m_scene)
    throw std::runtime_error("AnimationManager requires a valid Scene.");

  m_defragToken = m_scene->addDefragCallback([this](const auto &remap) {
    for (auto &anim : m_animations)
      anim.updateObjectDefragmentedIndices(remap);
  });
}

AnimationManager::~AnimationManager()
{
  m_scene->removeDefragCallback(m_defragToken);
}

void AnimationManager::setTimeChangedCallback(TimeChangedCallback cb)
{
  m_timeChangedCallback = std::move(cb);
}

void AnimationManager::setPlaybackStoppedCallback(PlaybackStoppedCallback cb)
{
  m_playbackStoppedCallback = std::move(cb);
}

void AnimationManager::reportLoadFailure(int frame, std::string message)
{
  m_loadFailures.push_back({frame, std::move(message)});
}

std::vector<AnimationManager::LoadFailure> AnimationManager::takeLoadFailures()
{
  return std::exchange(m_loadFailures, {});
}

scene::Scene *AnimationManager::scene() const
{
  return m_scene;
}

Animation &AnimationManager::addAnimation(const std::string &name)
{
  return m_animations.emplace_back(this, name);
}

std::vector<Animation> &AnimationManager::animations()
{
  return m_animations;
}

const std::vector<Animation> &AnimationManager::animations() const
{
  return m_animations;
}

void AnimationManager::removeAnimation(size_t index)
{
  if (index < m_animations.size())
    m_animations.erase(m_animations.begin() + index);
}

void AnimationManager::removeAllAnimations()
{
  m_animations.clear();
}

bool AnimationManager::widenClock(int frames, float fps)
{
  const bool framesWon = frames > m_totalFrames;
  const bool fpsWon = fps > m_animationFPS;

  if (framesWon)
    setAnimationTotalFrames(frames);
  if (fpsWon)
    setAnimationFPS(fps);

  return !(frames > 0 && frames < m_totalFrames)
      && !(fps > 0.f && fps < m_animationFPS);
}

void AnimationManager::setAnimationTime(float time)
{
  setAnimationTimeInternal(time, true);
}

void AnimationManager::setAnimationTimeInternal(
    float time, bool resetPlaybackAccumulator)
{
  m_time = time;

  if (resetPlaybackAccumulator)
    m_playbackAccumulator = 0.f;

  // One time change is one update: a Stage with several animated instancers
  // rewrites one transform Array per instancer, and without this each rewrite
  // would cost a full world rebuild.
  m_scene->beginUpdateBatch();
  m_applyingAnimations = true;
  try {
    for (auto &anim : m_animations)
      anim.setAnimationTime(time);
  } catch (...) {
    m_applyingAnimations = false;
    m_scene->endUpdateBatch();
    throw;
  }
  m_applyingAnimations = false;
  m_scene->endUpdateBatch();

  if (m_timeChangedCallback)
    m_timeChangedCallback(time);
}

float AnimationManager::getAnimationTime() const
{
  return m_time;
}

bool AnimationManager::isApplyingAnimations() const
{
  return m_applyingAnimations;
}

void AnimationManager::setAnimationIncrement(float increment)
{
  m_incrementSize = increment;
  if (increment > 0.5f) {
    vsr::core::logWarning(
        "[scene] setting animation increment > 0.5 will cause odd"
        " animation behavior.");
  }
}

float AnimationManager::getAnimationIncrement() const
{
  return m_incrementSize;
}

void AnimationManager::incrementAnimationTime()
{
  auto newTime = m_time + m_incrementSize;
  if (newTime > 1.f)
    newTime = 0.f;
  setAnimationTime(newTime);
}

int AnimationManager::getAnimationTotalFrames() const
{
  return m_totalFrames;
}

void AnimationManager::setAnimationTotalFrames(int frames)
{
  m_totalFrames = std::max(2, frames);
}

void AnimationManager::setAnimationFPS(float fps)
{
  if (fps <= 0.f) {
    vsr::core::logWarning("[scene] animation fps must be > 0; clamping to 1.");
    fps = 1.f;
  }

  m_animationFPS = fps;
}

float AnimationManager::getAnimationFPS() const
{
  return m_animationFPS;
}

int AnimationManager::getAnimationFrame() const
{
  return static_cast<int>(std::round(m_time * (m_totalFrames - 1)));
}

void AnimationManager::setAnimationFrame(int frame)
{
  setAnimationFrameInternal(frame, true);
}

void AnimationManager::setAnimationFrameInternal(
    int frame, bool resetPlaybackAccumulator)
{
  int clamped = std::clamp(frame, 0, m_totalFrames - 1);
  setAnimationTimeInternal(static_cast<float>(clamped) / (m_totalFrames - 1),
      resetPlaybackAccumulator);
}

void AnimationManager::incrementAnimationFrame()
{
  int frame = getAnimationFrame() + 1;
  if (frame >= m_totalFrames)
    frame = 0;
  setAnimationFrame(frame);
}

bool AnimationManager::tick(float elapsedSeconds)
{
  if (!m_playing)
    return false;

  if (elapsedSeconds <= 0.f)
    return false;

  const float frameDuration = 1.f / m_animationFPS;
  m_playbackAccumulator += elapsedSeconds;

  if (m_playbackAccumulator < frameDuration)
    return false;

  // One frame per tick; the leftover never exceeds one more frame's worth, so
  // a slow iteration cannot be "caught up" later by skipping frames.
  m_playbackAccumulator =
      std::min(m_playbackAccumulator - frameDuration, frameDuration);

  if (m_loop) {
    int frame = getAnimationFrame() + 1;
    if (frame >= m_totalFrames)
      frame = 0;
    setAnimationFrameInternal(frame, false);
    return true;
  }

  const int frame = getAnimationFrame();
  if (frame >= m_totalFrames - 1) {
    stopAtEnd();
    return false;
  }

  setAnimationFrameInternal(frame + 1, false);
  if (frame + 1 >= m_totalFrames - 1)
    stopAtEnd();
  return true;
}

void AnimationManager::stopAtEnd()
{
  m_playing = false;
  m_playbackAccumulator = 0.f;
  if (m_playbackStoppedCallback)
    m_playbackStoppedCallback();
}

void AnimationManager::play()
{
  m_playbackAccumulator = 0.f;
  m_playing = true;
}

void AnimationManager::stop()
{
  m_playbackAccumulator = 0.f;
  m_playing = false;
}

void AnimationManager::togglePlay()
{
  m_playbackAccumulator = 0.f;
  m_playing = !m_playing;
}

bool AnimationManager::isPlaying() const
{
  return m_playing;
}

void AnimationManager::setLoop(bool loop)
{
  m_loop = loop;
}

bool AnimationManager::isLoop() const
{
  return m_loop;
}

} // namespace vsr::animation
