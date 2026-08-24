// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/network/Message.hpp"
// vsr_core
#include "vsr/scene/Scene.hpp"

namespace vsr::network::messages {

struct TransferLayer : public StructuredMessage
{
  // Sender -- will serialize the scene into the message on construction
  TransferLayer(vsr::scene::Scene *scene, const vsr::scene::Layer *layer);

  // Receiver -- will setup deserialization on execute()
  TransferLayer(const Message &msg, vsr::scene::Scene *scene);

  // Receiver behavior
  void execute() override;

 private:
  vsr::scene::Scene *m_scene{nullptr};
};

} // namespace vsr::network::messages
