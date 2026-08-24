// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/animation/Animation.hpp"
#include "vsr/animation/FileBinding.hpp"
#include "vsr/scene/ObjectUsePtr.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Geometry.hpp"
// std
#include <optional>
#include <string>
#include <vector>

namespace vsr::io {

struct EnSightFileBinding : public vsr::animation::FileBinding
{
  struct FieldMapping
  {
    core::Token attributeName; // "vertex.attribute0", etc.
    std::string ensightVarName; // "SOMEVAR"
    std::string type; // "scalar" or "vector"
    std::vector<std::string> files; // expanded per-frame file paths
  };

  struct PartBinding
  {
    int partId;
    scene::ObjectUsePtr<scene::Geometry, scene::Object::UseKind::ANIM> geometry;
  };

  struct SerializedData
  {
    std::vector<PartBinding> parts;
    std::vector<std::string> geoFiles;
    std::vector<FieldMapping> fieldMappings;
  };

  EnSightFileBinding(scene::Scene *scene,
      std::vector<PartBinding> parts,
      std::vector<std::string> geoFiles,
      std::vector<FieldMapping> fieldMappings);

  static std::optional<SerializedData> fromDataNode(
      scene::Scene &scene, vsr::core::DataNode &node);

  // FileBinding interface

  std::string kind() const override;
  void toDataNode(vsr::core::DataNode &node) const override;
  void onDefragment(const scene::IndexRemapper &cb) override;
  void update(float t) override;

  size_t frameCount() const;
  int currentFrame() const;

 private:
  void addCallbackToAnimation(vsr::animation::Animation &anim) override;
  void loadFrame(int idx);

  std::vector<PartBinding> m_parts;
  std::vector<std::string> m_geoFiles;
  std::vector<FieldMapping> m_fieldMappings;
  int m_currentFrame{-1};
};

} // namespace vsr::io
