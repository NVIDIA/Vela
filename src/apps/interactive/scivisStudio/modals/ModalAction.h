// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_modals
#include "modals/BrowseProvider.h"
// std
#include <functional>
#include <string>

namespace vsr::scivis_studio::modals {

// How a host answers a submitted request: exactly once, on the submitting
// frame (the monolith, which acts in process) or a later one (the client,
// when the server's reply lands). An unaccepted request carries the host's
// error, which the modal shows without closing.
using ActionResult = std::function<void(bool ok, const std::string &error)>;

/*
 * The other seam a shared modal has, beside its BrowseProvider: what the
 * host does with an accepted dialog. Each modal declares its own Action with
 * the request it submits; this is what they all share. The defaults are the
 * in-place host's answers -- nothing outstanding, always ready -- so only a
 * host that waits for a reply overrides them.
 */
struct ModalAction
{
  virtual ~ModalAction();

  // A submitted request is unanswered: the modal greys its fields and says
  // busyMessage().
  virtual bool busy() const;
  // The host can accept a submit at all (the client's connection is Ready).
  virtual bool canSubmit() const;
  // Shown while busy().
  virtual const char *busyMessage() const;
  // The modal was cancelled, cleared or reconfigured: forget any request
  // still outstanding, whose reply is no longer anyone's business.
  virtual void reset();
};

// What the user did with the footer this frame.
enum class ModalChoice
{
  None,
  Cancelled,
  Submitted
};

/*
 * The footer every shared modal has, which is the action's own UI: the busy
 * line, the host's error, the browse (drawn last, so a provider that takes
 * Escape in this frame does not have it counted as the modal's Escape too),
 * then Cancel beside the action button, greyed while the host is busy or
 * cannot take a request. `submitNow` is the owner's other way of accepting,
 * such as Enter in a field.
 *
 * Example:
 *   switch (modalFooter(*m_browse, *m_action, m_error, "Import")) {
 *   case ModalChoice::Cancelled: reset(); hide(); return;
 *   case ModalChoice::Submitted: submit(); return;
 *   case ModalChoice::None: return;
 *   }
 */
ModalChoice modalFooter(BrowseProvider &browse,
    const ModalAction &action,
    const std::string &error,
    const char *actionLabel,
    bool submitNow = false);

} // namespace vsr::scivis_studio::modals
