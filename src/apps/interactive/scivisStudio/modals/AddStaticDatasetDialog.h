// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_modals
#include "modals/BrowseProvider.h"
#include "modals/ModalAction.h"
// vsr_io
#include "vsr/io/UsdImport.hpp"
#include "vsr/io/importers.hpp"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace vsr::scivis_studio::modals {

/*
 * Add Static Dataset: name, source path (typed or browsed) and the explicit
 * source choice -- one of the importers, a VSR Dataset Archive or a VSR
 * Layer Subtree Archive; nothing is inferred from the extension. A USD
 * importer also takes Variant Selections, typed as "set=variant,..." or
 * picked from the Variant Sets a scan of the source finds. Both
 * Studios show this dialog; the host supplies where paths come from
 * (BrowseProvider) and what an accepted dialog does (Action). The dialog
 * stays open, greyed, until the action answers: an accepted request closes
 * it, a refused one shows the host's error.
 *
 * Example:
 *   auto dialog = std::make_unique<AddStaticDatasetDialog>(app,
 *       std::make_unique<NativeBrowseProvider>(app),
 *       std::make_unique<LocalStaticDatasetAction>(app, &projectContext));
 *   dialog->show();
 */
struct AddStaticDatasetDialog : public vsr::ui::imgui::Modal
{
  // What the user chose. No importer means the source is a VSR archive:
  // a Layer Subtree Archive when `subtree`, else a Dataset Archive.
  struct Request
  {
    std::string name;
    std::filesystem::path sourcePath;
    std::optional<vsr::io::ImporterType> importer;
    bool subtree{false};
    // Only sent with a USD importer (vsr::io::isUsdImporter).
    vsr::io::UsdVariantSelections variantSelections;
  };

  // How a host answers a scan, exactly once: the Variant Sets the source
  // offers, or why it could not say.
  using VariantSetsResult = std::function<void(bool ok,
      const vsr::io::UsdVariantSetCatalog &sets,
      const std::string &error)>;

  struct Action : public ModalAction
  {
    virtual void submit(const Request &request, ActionResult done) = 0;
    // The Variant Sets of the USD Stage at `sourcePath`, read where the
    // source lives (in process, or on the server).
    virtual void scanVariantSets(
        const std::filesystem::path &sourcePath, VariantSetsResult done) = 0;
  };

  AddStaticDatasetDialog(vsr::ui::imgui::Application *app,
      std::unique_ptr<BrowseProvider> browse,
      std::unique_ptr<Action> action);
  ~AddStaticDatasetDialog() override;

 private:
  void buildUI() override;
  void buildUsdVariantUI();
  void scanVariantSets();
  void submit();
  void reset();

  std::unique_ptr<BrowseProvider> m_browse;
  std::unique_ptr<Action> m_action;
  std::string m_name;
  std::string m_sourcePath;
  int m_selectedSource{0};
  std::string m_error;

  // USD only. The text, "set=variant,...", is what the import uses; the
  // per-set combos a scan produces edit it, so a set the scan cannot see (one
  // only a payload authors) can still be typed in.
  std::string m_variantSelections;
  vsr::io::UsdVariantSetCatalog m_scannedVariantSets;
  std::string m_scannedPath;
  std::string m_scanError;
  bool m_scanning{false};
  // Bumped by reset(), so a scan answered after it is dropped.
  uint64_t m_scanGeneration{0};
};

} // namespace vsr::scivis_studio::modals
