// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <deque>
#include <string>

namespace vsr::scivis_studio::client {

// What the user pressed on the connection-lost banner this frame.
enum class LostBannerChoice
{
  None,
  Retry,
  Disconnect
};

/*
 * The transient layer drawn over the client's windows each frame: the
 * connection-lost banner across the top of the work area and the toasts in
 * its bottom-right corner. The overlay owns the toast queue (newest last;
 * each expires on its own and none takes input) and knows nothing of the
 * connection: the banner is drawn from the two facts it shows and reports
 * the button pressed, so the Application decides what a Retry or Disconnect
 * does.
 *
 * Example:
 *   m_overlay.pushToast("Import completed: dataset_0001", false);
 *   if (lost) {
 *     switch (m_overlay.drawLostBanner(autoRetrying, statusText)) {
 *     ...
 *     }
 *   }
 *   m_overlay.drawToasts();
 */
struct StatusOverlay
{
  // Queues a toast; an error stays longer. The oldest goes when the queue is
  // full.
  void pushToast(const std::string &text, bool isError);

  // "Server connection lost -- reconnecting..." while `autoRetrying`,
  // otherwise the text with Retry and Disconnect buttons; `statusText` is
  // the connection's reason, shown greyed.
  LostBannerChoice drawLostBanner(
      bool autoRetrying, const std::string &statusText);

  // Drops the expired toasts and draws the rest.
  void drawToasts();

 private:
  struct Toast
  {
    std::string text;
    bool isError{false};
    double expiresAt{0.0};
  };
  std::deque<Toast> m_toasts;
};

} // namespace vsr::scivis_studio::client
