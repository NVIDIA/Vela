// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ImportFileDialog.h"
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/modals/BlockingTaskModal.h"
// SDL
#include <SDL3/SDL_dialog.h>
// vsr_io
#include "vsr/io/importers.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"

namespace vsr::ui::imgui {

namespace {

// The label shown in the combo, paired with the type it selects. Pairing them
// is what keeps the two in step: the combo index used to be cast straight to
// ImporterType, so every entry after the first gap named one importer and ran
// another.
struct ImporterChoice
{
  const char *name;
  vsr::io::ImporterType type;
};

constexpr ImporterChoice IMPORTERS[] = {
    {"AGX", vsr::io::ImporterType::AGX},
    {"ASSIMP", vsr::io::ImporterType::ASSIMP},
    {"ASSIMP_FLAT", vsr::io::ImporterType::ASSIMP_FLAT},
    {"AXYZ", vsr::io::ImporterType::AXYZ},
    {"DLAF", vsr::io::ImporterType::DLAF},
    {"E57XYZ", vsr::io::ImporterType::E57XYZ},
    {"ENSIGHT", vsr::io::ImporterType::ENSIGHT},
    {"GLTF", vsr::io::ImporterType::GLTF},
    {"HDRI", vsr::io::ImporterType::HDRI},
    {"HSMESH", vsr::io::ImporterType::HSMESH},
    {"NBODY", vsr::io::ImporterType::NBODY},
    {"OBJ", vsr::io::ImporterType::OBJ},
    {"PDB", vsr::io::ImporterType::PDB},
    {"PBRT", vsr::io::ImporterType::PBRT},
    {"PLY", vsr::io::ImporterType::PLY},
    {"POINTSBIN_MULTIFILE", vsr::io::ImporterType::POINTSBIN_MULTIFILE},
    {"PT (neural)", vsr::io::ImporterType::PT},
    {"SILO", vsr::io::ImporterType::SILO},
    {"SMESH", vsr::io::ImporterType::SMESH},
    {"SMESH_ANIMATION", vsr::io::ImporterType::SMESH_ANIMATION},
    {"SWC", vsr::io::ImporterType::SWC},
    {"SWC_SDF", vsr::io::ImporterType::SWC_SDF},
    {"TRK", vsr::io::ImporterType::TRK},
    {"USD", vsr::io::ImporterType::USD},
    {"USD_MTLX", vsr::io::ImporterType::USD_MTLX},
    {"VTP", vsr::io::ImporterType::VTP},
    {"VTU", vsr::io::ImporterType::VTU},
    {"XYZDP", vsr::io::ImporterType::XYZDP},
    {"VOLUME", vsr::io::ImporterType::VOLUME},
    {"VOLUME_ANIMATION", vsr::io::ImporterType::VOLUME_ANIMATION},
};

} // namespace

ImportFileDialog::ImportFileDialog(Application *app)
    : Modal(app, "ImportFileDialog")
{}

ImportFileDialog::~ImportFileDialog() = default;

void ImportFileDialog::buildUI()
{
  constexpr int MAX_LENGTH = 2000;
  m_filename.reserve(MAX_LENGTH);

  const char *importerNames[std::size(IMPORTERS)] = {};
  for (size_t i = 0; i < std::size(IMPORTERS); i++)
    importerNames[i] = IMPORTERS[i].name;

  ImGui::Combo("importer type",
      &m_selectedFileType,
      importerNames,
      std::size(importerNames));

  static std::string outPath;
  if (ImGui::Button("...")) {
    outPath.clear();
    m_app->getFilenameFromDialog(outPath);
  }

  if (!outPath.empty()) {
    m_filename = outPath;
    outPath.clear();
  }

  ImGui::SameLine();

  auto text_cb = [](ImGuiInputTextCallbackData *cbd) {
    auto &fname = *(std::string *)cbd->UserData;
    fname.resize(cbd->BufTextLen);
    return 0;
  };

  ImGui::InputText("##filename",
      m_filename.data(),
      MAX_LENGTH,
      ImGuiInputTextFlags_CallbackEdit,
      text_cb,
      &m_filename);

  //////////

  ImGui::NewLine();

  ImGuiIO &io = ImGui::GetIO();
  if (ImGui::Button("cancel") || ImGui::IsKeyDown(ImGuiKey_Escape))
    this->hide();

  ImGui::SameLine();

  if (ImGui::Button("import")) {
    this->hide();

    auto doLoad = [&]() {
      auto *ctx = appContext();
      auto &scene = ctx->vsr.scene;
      auto *layer = ctx->vsr.scene.defaultLayer();
      auto importRoot = ctx->getFirstSelected();
      if (!importRoot.valid())
        importRoot = layer->root();
      vsr::io::ImportFile file{IMPORTERS[m_selectedFileType].type, m_filename};
      vsr::io::import_file(scene, ctx->vsr.animationMgr, file, importRoot);
      scene.signalLayerStructureChanged(layer);
    };

    m_app->showTaskModal(doLoad, "Please Wait: Importing Data...");
  }
}

} // namespace vsr::ui::imgui
