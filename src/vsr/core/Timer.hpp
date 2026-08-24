// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

namespace vsr::core {

/*
 * High-resolution interval timer that measures elapsed time between start()
 * and end() calls, with exponential smoothing for stable per-frame metrics.
 *
 * Example:
 *   Timer t;
 *   t.start();
 *   doWork();
 *   t.end();
 *   double ms = t.millisecondsSmoothed();
 */
struct Timer
{
  void start();
  void end();

  double seconds() const;
  double milliseconds() const;
  double perSecond() const;

  double secondsSmoothed() const;
  double millisecondsSmoothed() const;
  double perSecondSmoothed() const;

 private:
  double m_smoothNom{0.0};
  double m_smoothDen{0.0};

  std::chrono::time_point<std::chrono::steady_clock> m_endTime;
  std::chrono::time_point<std::chrono::steady_clock> m_startTime;
};

// Inlined Timer definitions //////////////////////////////////////////////////

inline void Timer::start()
{
  m_startTime = std::chrono::steady_clock::now();
}

inline void Timer::end()
{
  m_endTime = std::chrono::steady_clock::now();

  m_smoothNom = m_smoothNom * 0.8f + seconds();
  m_smoothDen = m_smoothDen * 0.8f + 1.f;
}

inline double Timer::seconds() const
{
  auto diff = m_endTime - m_startTime;
  return std::chrono::duration<double>(diff).count();
}

inline double Timer::milliseconds() const
{
  auto diff = m_endTime - m_startTime;
  return std::chrono::duration<double, std::milli>(diff).count();
}

inline double Timer::perSecond() const
{
  return 1.0 / seconds();
}

inline double Timer::secondsSmoothed() const
{
  return 1.0 / perSecondSmoothed();
}

inline double Timer::millisecondsSmoothed() const
{
  return secondsSmoothed() * 1000.0;
}

inline double Timer::perSecondSmoothed() const
{
  return m_smoothDen / m_smoothNom;
}

} // namespace vsr