// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ObjectFileDialog.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/archives/LayerSubtreeArchive.hpp"
#include "vsr/io/archives/ObjectArchive.hpp"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
// imgui
#include <misc/cpp/imgui_stdlib.h>

namespace vsr::ui::imgui {

ObjectFileDialog::ObjectFileDialog(Application *app) : Modal(app, "VSR Archive")
{}

ObjectFileDialog::~ObjectFileDialog() = default;

void ObjectFileDialog::showLoadObject(vsr::scene::LayerNodeRef destination)
{
  m_mode = Mode::Load;
  m_kind = Kind::Object;
  m_destination = destination;
  m_subtreeNode = {};
  m_objectType = ANARI_UNKNOWN;
  m_objectIndex = vsr::core::INVALID_INDEX;
  m_filename.clear();
  m_dialogFilename.clear();
  show();
}

void ObjectFileDialog::showSaveObject(
    VSRObjectFileType fileType, anari::DataType objectType, size_t objectIndex)
{
  m_mode = Mode::Save;
  m_kind = Kind::Object;
  m_fileType = fileType;
  m_destination = {};
  m_subtreeNode = {};
  m_objectType = objectType;
  m_objectIndex = objectIndex;
  m_filename.clear();
  m_dialogFilename.clear();
  show();
}

void ObjectFileDialog::showLoadLayerSubtree(
    vsr::scene::LayerNodeRef destinationParent)
{
  m_mode = Mode::Load;
  m_kind = Kind::LayerSubtree;
  m_destination = {};
  m_subtreeNode = destinationParent;
  m_objectType = ANARI_UNKNOWN;
  m_objectIndex = vsr::core::INVALID_INDEX;
  m_filename.clear();
  m_dialogFilename.clear();
  show();
}

void ObjectFileDialog::showSaveLayerSubtree(vsr::scene::LayerNodeRef sourceRoot)
{
  m_mode = Mode::Save;
  m_kind = Kind::LayerSubtree;
  m_destination = {};
  m_subtreeNode = sourceRoot;
  m_objectType = ANARI_UNKNOWN;
  m_objectIndex = vsr::core::INVALID_INDEX;
  m_filename.clear();
  m_dialogFilename.clear();
  show();
}

void ObjectFileDialog::buildUI()
{
  ImGui::Text("%s %s", actionLabel(), fileTypeLabel());

  if (ImGui::Button("...")) {
    m_dialogFilename.clear();
    m_app->getFilenameFromDialog(m_dialogFilename, m_mode == Mode::Save);
  }

  if (!m_dialogFilename.empty()) {
    m_filename = m_dialogFilename;
    m_dialogFilename.clear();
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(800.f);
  ImGui::InputText("##filename", &m_filename);

  ImGui::NewLine();

  if (ImGui::Button("cancel") || ImGui::IsKeyDown(ImGuiKey_Escape)) {
    hide();
    return;
  }

  ImGui::SameLine();

  ImGui::BeginDisabled(m_filename.empty());
  if (ImGui::Button(m_mode == Mode::Load ? "load" : "save")) {
    hide();
    if (m_kind == Kind::LayerSubtree) {
      if (m_mode == Mode::Load)
        loadLayerSubtreeArchive();
      else
        saveLayerSubtreeArchive();
    } else if (m_mode == Mode::Load)
      loadObjectArchive();
    else
      saveObjectArchive();
  }
  ImGui::EndDisabled();
}

const char *ObjectFileDialog::fileTypeLabel() const
{
  if (m_kind == Kind::LayerSubtree)
    return "VSR Layer Subtree Archive";

  switch (m_fileType) {
  case VSRObjectFileType::Surface:
    return "VSR Surface Object Archive";
  case VSRObjectFileType::Volume:
    return "VSR Volume Object Archive";
  }

  return "VSR Object Archive";
}

const char *ObjectFileDialog::actionLabel() const
{
  return m_mode == Mode::Load ? "Load" : "Save";
}

const char *ObjectFileDialog::taskLabel() const
{
  if (m_kind == Kind::LayerSubtree) {
    return m_mode == Mode::Load
        ? "Please Wait: Loading VSR Layer Subtree Archive..."
        : "Please Wait: Saving VSR Layer Subtree Archive...";
  }
  return m_mode == Mode::Load ? "Please Wait: Loading VSR Object Archive..."
                              : "Please Wait: Saving VSR Object Archive...";
}

anari::DataType ObjectFileDialog::anariObjectType() const
{
  switch (m_fileType) {
  case VSRObjectFileType::Surface:
    return ANARI_SURFACE;
  case VSRObjectFileType::Volume:
    return ANARI_VOLUME;
  }

  return ANARI_UNKNOWN;
}

void ObjectFileDialog::loadObjectArchive()
{
  auto filename = m_filename;
  auto destination = m_destination;
  auto *app = m_app;

  auto doLoad = [filename, destination, app]() mutable {
    auto *ctx = app->appContext();
    auto &scene = ctx->vsr.scene;

    if (!destination.valid())
      destination = scene.defaultLayer()->root();

    auto *object = vsr::io::load_ObjectArchive(scene, filename.c_str());
    if (!object)
      return;

    const auto nodeName = object->name().empty()
        ? std::string(object->type() == ANARI_SURFACE ? "surface" : "volume")
        : object->name();
    scene.insertChildObjectNode(
        destination, object->type(), object->index(), nodeName.c_str());
  };

  m_app->showTaskModal(doLoad, taskLabel());
}

void ObjectFileDialog::saveObjectArchive()
{
  auto filename = m_filename;
  auto expectedType = anariObjectType();
  auto objectType = m_objectType;
  auto objectIndex = m_objectIndex;
  auto *app = m_app;

  auto doSave = [filename, expectedType, objectType, objectIndex, app]() {
    auto *ctx = app->appContext();
    auto &scene = ctx->vsr.scene;
    auto *object = scene.getObject(objectType, objectIndex);

    if (!object) {
      vsr::core::logError("[ObjectFileDialog] No object selected to save.");
      return;
    }

    if (object->type() != expectedType) {
      vsr::core::logError("[ObjectFileDialog] Selected object is not a %s.",
          anari::toString(expectedType));
      return;
    }

    vsr::io::save_ObjectArchive(*object, filename.c_str());
  };

  m_app->showTaskModal(doSave, taskLabel());
}

void ObjectFileDialog::loadLayerSubtreeArchive()
{
  auto filename = m_filename;
  auto destinationParent = m_subtreeNode;
  auto *app = m_app;

  auto doLoad = [filename, destinationParent, app]() mutable {
    auto *ctx = app->appContext();
    auto &scene = ctx->vsr.scene;

    if (!destinationParent.valid())
      destinationParent = scene.defaultLayer()->root();

    vsr::io::load_LayerSubtreeArchive(destinationParent, filename.c_str());
  };

  m_app->showTaskModal(doLoad, taskLabel());
}

void ObjectFileDialog::saveLayerSubtreeArchive()
{
  auto filename = m_filename;
  auto sourceRoot = m_subtreeNode;
  auto *app = m_app;

  auto doSave = [filename, sourceRoot, app]() {
    if (!sourceRoot.valid()) {
      vsr::core::logError("[ObjectFileDialog] No layer node selected to save.");
      return;
    }

    vsr::io::save_LayerSubtreeArchive(sourceRoot, filename.c_str());
  };

  m_app->showTaskModal(doSave, taskLabel());
}

} // namespace vsr::ui::imgui
