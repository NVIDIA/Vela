// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorContext.h"
// vsr_scivis_studio_modals
#include "modals/AddFileAnimationDatasetDialog.h"
#include "modals/AddStaticDatasetDialog.h"
#include "modals/ProjectLocationDialog.h"
// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
// std
#include <functional>
#include <utility>

namespace vsr::scivis_studio::client {

/*
 * The client's half of the shared modals: every request goes out as a
 * Project Op and is answered when its reply lands, so the dialog stays open
 * and greyed meanwhile and shows the server's error when the reply refuses.
 * Nothing here reads the local filesystem -- the paths are the server's.
 * The monolith's half is scivisStudio's LocalProjectActions.
 *
 * Example:
 *   auto dialog = std::make_unique<modals::AddStaticDatasetDialog>(this,
 *       std::make_unique<RemoteBrowseProvider>(this, &m_editorContext),
 *       std::make_unique<RemoteStaticDatasetAction>(&m_editorContext));
 */

/*
 * What the three actions share, mixed into each modal's own Action: the
 * context, the one request in flight, and the reply that answers the dialog.
 */
template <typename Action>
struct RemoteAction : public Action
{
  explicit RemoteAction(EditorContext *context);

  bool busy() const override;
  bool canSubmit() const override;
  const char *busyMessage() const override;
  void reset() override;

 protected:
  // The callback for the request about to be sent: it answers `done` with
  // the server's reply, and ignores anything that is not that reply.
  ResultCallback<protocol::TaskStartedResult> replyTo(
      modals::ActionResult done);

  EditorContext *m_context{nullptr};
  InFlight m_pending;
};

struct RemoteStaticDatasetAction
    : public RemoteAction<modals::AddStaticDatasetDialog::Action>
{
  using RemoteAction::RemoteAction;
  ~RemoteStaticDatasetAction() override;

  void submit(const modals::AddStaticDatasetDialog::Request &request,
      modals::ActionResult done) override;
};

struct RemoteFileAnimationAction
    : public RemoteAction<modals::AddFileAnimationDatasetDialog::Action>
{
  using RemoteAction::RemoteAction;
  ~RemoteFileAnimationAction() override;

  void submit(const modals::AddFileAnimationDatasetDialog::Request &request,
      modals::ActionResult done) override;
};

struct RemoteProjectLocationAction
    : public RemoteAction<modals::ProjectLocationDialog::Action>
{
  using RemoteAction::RemoteAction;
  ~RemoteProjectLocationAction() override;

  void submit(const modals::ProjectLocationDialog::Request &request,
      modals::ActionResult done) override;
  std::string initialDirectory(modals::ProjectLocationMode mode) const override;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename Action>
inline RemoteAction<Action>::RemoteAction(EditorContext *context)
    : m_context(context)
{}

template <typename Action>
inline bool RemoteAction<Action>::busy() const
{
  return m_pending.busy(m_context->ops());
}

template <typename Action>
inline bool RemoteAction<Action>::canSubmit() const
{
  return m_context->canSend();
}

template <typename Action>
inline const char *RemoteAction<Action>::busyMessage() const
{
  return "waiting for the server...";
}

template <typename Action>
inline void RemoteAction<Action>::reset()
{
  m_pending.clear();
}

template <typename Action>
inline ResultCallback<protocol::TaskStartedResult>
RemoteAction<Action>::replyTo(modals::ActionResult done)
{
  return [this, done = std::move(done)](const protocol::ProjectOpReply &reply,
             const std::optional<protocol::TaskStartedResult> &) {
    if (reply.requestId != m_pending.handle.requestId)
      return;
    m_pending.clear();
    done(reply.ok, reply.error);
  };
}

} // namespace vsr::scivis_studio::client
