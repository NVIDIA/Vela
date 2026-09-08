// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vsr::ui::imgui {
class Application;
}

namespace vsr::scivis_studio::modals {

// The three native dialog modes plus the multi-file pick the file-animation
// dialog needs.
enum class BrowseMode
{
  OpenFile,
  OpenFiles,
  SaveFile,
  OpenDirectory
};

// One browse; the provider copies it and calls onAccept at most once with
// absolute paths.
struct BrowseRequest
{
  BrowseMode mode{BrowseMode::OpenFile};
  std::string title;
  // Lower-case, with the dot (".vsr"). Advisory: a provider may grey files
  // not matching, never hide them; empty matches everything.
  std::vector<std::string> extensions;
  // Empty: wherever the provider browsed last.
  std::filesystem::path startDirectory;
  // SaveFile: the proposed file name.
  std::string defaultName;
  std::function<void(const std::vector<std::filesystem::path> &)> onAccept;
};

/*
 * Where a shared modal gets a path from: the one seam between the two
 * Studios. The monolith browses its own filesystem through the native SDL
 * dialogs (NativeBrowseProvider below); the remote client browses the
 * server's through Remote Browse. Owners hold one provider per modal and
 * call renderUI() every frame from inside their popup, so a provider that
 * draws its own ImGui window nests in the owner's.
 *
 * Example:
 *   BrowseRequest request;
 *   request.mode = BrowseMode::OpenFile;
 *   request.title = "Choose the dataset source file";
 *   request.onAccept = [this](const auto &paths) { take(paths.front()); };
 *   m_browse->browse(std::move(request));
 *   ...
 *   m_browse->renderUI(); // every frame
 */
struct BrowseProvider
{
  virtual ~BrowseProvider();

  virtual void browse(BrowseRequest request) = 0;
  // Drawn by the owning modal every frame: where a provider with its own UI
  // draws, and where one without polls whatever answered it.
  virtual void renderUI() = 0;
  // A provider window is up inside the owner's popup, so the owner must not
  // read Escape this frame: the provider's own Escape hides it.
  virtual bool visible() const = 0;
};

/*
 * The monolith's provider: the native SDL file dialogs. They are their own
 * OS windows, so visible() is always false and renderUI() only picks up what
 * the dialog's callback left behind, one or more frames later.
 */
struct NativeBrowseProvider : public BrowseProvider
{
  explicit NativeBrowseProvider(vsr::ui::imgui::Application *app);
  ~NativeBrowseProvider() override;

  void browse(BrowseRequest request) override;
  void renderUI() override;
  bool visible() const override;

 private:
  vsr::ui::imgui::Application *m_app{nullptr};
  BrowseRequest m_request;
  // Written by the SDL dialog callback on a later frame; this provider must
  // outlive the dialog it opened.
  std::string m_path;
  std::vector<std::string> m_paths;
  bool m_browsing{false};
};

} // namespace vsr::scivis_studio::modals
