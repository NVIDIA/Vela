// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ArrayHydration.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/windows/TransferFunctionEditor.h"

namespace vsr::scivis_studio::client {

/*
 * The Transfer Function editor's array seam, answered for a thin client: an
 * array is ready once ArrayHydration has filled it with the server's
 * samples, and asking is what starts the fetch. All this adds to
 * ArrayHydration is the widget's interface and the line it shows while the
 * samples are on their way.
 *
 * Example:
 *   RemoteArrayAccess access(&connection);
 *   editor->setArrayAccess(&access);
 */
struct RemoteArrayAccess : public vsr::ui::imgui::TransferFunctionArrayAccess
{
  explicit RemoteArrayAccess(ServerConnection *connection);
  ~RemoteArrayAccess() override;

  bool ready(vsr::scene::Array &array) override;
  const char *pendingReason() const override;

  void dropMirrorReferences();

 private:
  ArrayHydration m_hydration;
};

} // namespace vsr::scivis_studio::client
