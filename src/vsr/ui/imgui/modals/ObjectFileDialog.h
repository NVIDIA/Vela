// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"
// std
#include <cstddef>
#include <string>

namespace vsr::ui::imgui {

enum class VSRObjectFileType
{
  Surface,
  Volume
};

struct ObjectFileDialog : public Modal
{
  ObjectFileDialog(Application *app);
  ~ObjectFileDialog() override;

  void showLoadObject(vsr::scene::LayerNodeRef destination);
  void showSaveObject(VSRObjectFileType fileType,
      anari::DataType objectType,
      size_t objectIndex);

  // Layer subtree files (a node and its descendants + referenced objects) //
  void showLoadLayerSubtree(vsr::scene::LayerNodeRef destinationParent);
  void showSaveLayerSubtree(vsr::scene::LayerNodeRef sourceRoot);

  void buildUI() override;

 private:
  enum class Mode
  {
    Load,
    Save
  };

  enum class Kind
  {
    Object,
    LayerSubtree
  };

  const char *fileTypeLabel() const;
  const char *actionLabel() const;
  const char *taskLabel() const;
  anari::DataType anariObjectType() const;

  void loadObjectArchive();
  void saveObjectArchive();
  void loadLayerSubtreeArchive();
  void saveLayerSubtreeArchive();

  std::string m_filename;
  std::string m_dialogFilename;
  Mode m_mode{Mode::Load};
  Kind m_kind{Kind::Object};
  VSRObjectFileType m_fileType{VSRObjectFileType::Surface};
  vsr::scene::LayerNodeRef m_destination;
  vsr::scene::LayerNodeRef m_subtreeNode;
  anari::DataType m_objectType{ANARI_UNKNOWN};
  size_t m_objectIndex{vsr::core::INVALID_INDEX};
};

} // namespace vsr::ui::imgui
