// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/network/Message.hpp"
// vsr_core
#include "vsr/scene/Scene.hpp"

namespace vsr::network::messages {

struct ParameterChange : public StructuredMessage
{
  // Sender -- will serialize the data on construction
  ParameterChange(
      const vsr::scene::Object *obj, const vsr::scene::Parameter *param);

  // Sender -- will serialize the data on construction
  ParameterChange(const vsr::scene::Object *obj,
      const vsr::scene::Parameter *const *params,
      size_t np);

  // Receiver -- will setup deserialization on execute()
  ParameterChange(const Message &msg, vsr::scene::Scene *scene);

  // Receiver behavior
  void execute() override;

 private:
  vsr::scene::Scene *m_scene{nullptr};
};

} // namespace vsr::network::messages
