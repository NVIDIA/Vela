// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ExtensionManager.h"
#include "vsr_ui_imgui.h"

#include "modals/AppSettingsDialog.h"
#include "modals/BlockingTaskModal.h"
#include "modals/CuttingPlaneDialog.h"
#include "modals/ExportNanoVDBFileDialog.h"
#include "modals/ImportFileDialog.h"
#include "modals/ObjectFileDialog.h"
#include "modals/OfflineRenderModal.h"
#include "modals/VorticityDialog.h"
// vsr_app
#include "vsr/app/Context.h"
// vsr_core
#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/core/Logging.hpp"
#include "vsr/core/TaskQueue.hpp"
// SDL
#include <SDL3/SDL.h>
// std
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vsr::ui::imgui {

struct Window;
using WindowArray = std::vector<std::unique_ptr<Window>>;

struct UIConfig
{
  float fontScale{1.f};
  float rounding{4.f};
};

struct CommandLineOptions
{
  bool useDefaultLayout{true};
  bool useDefaultRenderer{true};
  std::string secondaryViewportLibrary;
};

enum class FileDialogMode
{
  OpenFile,
  SaveFile,
  OpenDirectory
};

class Application
{
 public:
  Application(int argc = 0, const char **argv = nullptr);
  virtual ~Application();

  SDL_Renderer *sdlRenderer();
  SDL_Window *sdlWindow();

  // Start the application run loop
  void run(int width, int height, const char *name);

  vsr::app::Context *appContext();
  UIConfig *uiConfig();
  CommandLineOptions *commandLineOptions();

  // Which parameter edits this application's object editors may offer.
  // Permissive by default; an application whose scene is not its own to
  // change (the SciVis Studio client, whose edits must reach a server)
  // narrows it once at startup and every window sees the same answer.
  const vsr::ui::ObjectEditPolicy &objectEditPolicy() const;
  void setObjectEditPolicy(const vsr::ui::ObjectEditPolicy &policy);

  // NOTE: These wrap SDL3's file/folder dialogs, which are ASYNCHRONOUS. The
  // call returns immediately and the chosen path is written into the out-param
  // from a later event-loop iteration. Therefore the out-param MUST outlive the
  // call (e.g. a member, not a local) and the result MUST be polled on
  // subsequent frames -- reading it synchronously right after the call sees an
  // empty string, and passing a local/stack buffer crashes when the callback
  // writes into freed memory. See m_filenameToLoadNextFrame /
  // m_filenameToSaveNextFrame for the canonical populate-then-poll pattern.
  void getFilenameFromDialog(
      std::string &filenameOut, FileDialogMode mode = FileDialogMode::OpenFile);
  void getFilenameFromDialog(std::string &filenameOut, bool isSaveDialog);
  void getFilenamesFromDialog(std::vector<std::string> &filenamesOut);

  // Enqueue a task to be executed on a background thread
  template <class FUNCTION>
  vsr::core::Future enqueueTask(FUNCTION &&task);

  // Enqueue a task, then show a modal until task is complete
  template <class FUNCTION>
  void showTaskModal(FUNCTION &&f, const char *text = "Please Wait");

  // Enqueue a cancellable task, then show a modal until task is complete
  template <class FUNCTION>
  void showTaskModalWithCancel(FUNCTION &&f, const char *text = "Please Wait");
  void showImportFileDialog();
  void showExportNanoVDBFileDialog();
  void showLoadObjectArchiveDialog(vsr::scene::LayerNodeRef destination);
  void showSaveObjectArchiveDialog(VSRObjectFileType fileType,
      anari::DataType objectType,
      size_t objectIndex);
  void showLoadLayerSubtreeArchiveDialog(
      vsr::scene::LayerNodeRef destinationParent);
  void showSaveLayerSubtreeArchiveDialog(vsr::scene::LayerNodeRef sourceRoot);
  // Writes the Application Preferences file (Settings -> Save as Defaults).
  void saveApplicationPreferences();

  ExtensionManager *extensionManager() const;

  VSR_NOT_COPYABLE(Application)
  VSR_NOT_MOVEABLE(Application)

 protected:
  void parseCommandLine(std::vector<std::string> &args);
  bool getWindowSize(int &width, int &height) const;
  float getLastFrameLatency() const;

  // Internal API //

  virtual void setupImGuiStyle();

  virtual Uint32 sdlWindowFlags() const;
  virtual WindowArray setupWindows();

  virtual void mainLoopStart();
  virtual void mainLoopEnd();
  virtual void teardown();

  virtual void uiFrameStart();
  virtual void uiRenderStart();
  virtual void uiRenderEnd();
  virtual void uiFrameEnd();

  virtual void uiMainMenuBar();
  void uiMainMenuBar_File();
  void uiMainMenuBar_Edit();
  void uiMainMenuBar_Tools();
  void uiMainMenuBar_Lua();
  void uiMainMenuBar_View();

  void uiActionMenu(const std::vector<ActionMenuNode> &entries);

  void doSave(const std::string &name = "");

  void saveApplicationState(const char *filename = "state.vsr");
  void loadApplicationState(const char *filename = "state.vsr");
  virtual vsr::core::DataTreeMetadata applicationStateMetadata() const;
  virtual bool validateApplicationStateMetadata(
      const vsr::core::DataTreeMetadataReadResult &metadata,
      const vsr::core::DataNode &root,
      const char *filename) const;

  // Application Preferences: settings every Vela application shares, kept in
  // <User Config Directory>/preferences.vsr and written only on request.
  void savePreferences(vsr::core::DataNode &root);
  void loadPreferences(vsr::core::DataNode &root);
  void loadApplicationPreferences();
  std::filesystem::path applicationPreferencesFile() const;

  // UI State (docs/adr/0040): the dock layout plus each window's presentation
  // settings, in the shape of vsr/app/UIStateTree.h. It belongs to this
  // application, never to a document: run() restores it from uiStateFile()
  // before the first frame (unless --noDefaultLayout) and saves it at exit.
  // Both work with or without an open frame.
  void saveUIState(vsr::core::DataNode &root);
  void applyUIState(vsr::core::DataNode &root);
  std::filesystem::path uiStateFile() const;
  void saveUIStateFile();
  void restoreUIStateFile();
  void restoreDefaultLayout();

  // The Application Identifier: the stable name that keeps this
  // application's UI State apart from every other's. Changing it abandons
  // the users' saved UI State.
  virtual const char *applicationIdentifier() const = 0;

  void loadStateForNextFrame();

  void setupUsdDevice();
  bool usdDeviceIsSetup() const;
  void syncUsdScene();
  void teardownUsdDevice();

  void setupVsrDevice();
  bool vsrDeviceIsSetup() const;
  void syncVsrScene();
  void teardownVsrDevice();

  void setWindowArray(const WindowArray &wa);
  virtual const char *getDefaultLayout() const = 0;

  // Data //

  std::vector<Window *> m_windows;
  std::unique_ptr<AppSettingsDialog> m_appSettingsDialog;
  std::unique_ptr<BlockingTaskModal> m_taskModal;
  std::unique_ptr<OfflineRenderModal> m_offlineRenderModal;
  std::unique_ptr<ImportFileDialog> m_fileDialog;
  std::unique_ptr<ExportNanoVDBFileDialog> m_exportNanoVDBFileDialog;
  std::unique_ptr<ObjectFileDialog> m_objectFileDialog;
  std::unique_ptr<VorticityDialog> m_vorticityDialog;
  std::unique_ptr<CuttingPlaneDialog> m_cuttingPlaneDialog;

  vsr::core::DataTree m_settings;

  UIConfig m_uiConfig;
  CommandLineOptions m_commandLine;
  vsr::ui::ObjectEditPolicy m_objectEditPolicy;

 private:
  void mainLoop();
  void updateWindowTitle();

  // Data //

  vsr::app::Context m_ctx;

  struct AppImpl;
  std::unique_ptr<AppImpl> m_impl;

  vsr::core::TaskQueue m_jobs{10};

  std::string m_applicationName = "VSR";

  std::string m_currentSessionFilename;
  std::string m_filenameToSaveNextFrame;
  std::string m_filenameToLoadNextFrame;

  std::unique_ptr<ExtensionManager> m_extensionManager;

  struct UsdDeviceState
  {
    anari::Device device{nullptr};
    anari::Frame frame{nullptr};
    vsr::rendering::RenderIndex *renderIndex{nullptr};
  } m_usdDevice;

  struct VsrDeviceState
  {
    anari::Device device{nullptr};
    anari::Frame frame{nullptr};
    vsr::rendering::RenderIndex *renderIndex{nullptr};
  } m_vsrDevice;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <class FUNCTION>
inline vsr::core::Future Application::enqueueTask(FUNCTION &&task)
{
  return m_jobs.enqueue(std::forward<FUNCTION>(task));
}

template <class F>
inline void Application::showTaskModal(F &&f, const char *text)
{
  auto future = enqueueTask(std::forward<F>(f));

  if (!m_taskModal) {
    vsr::core::logWarning(
        "[Application] No task modal available to show, "
        "executing task without showing modal.");
    future.wait();
  } else {
    m_taskModal->activate(std::move(future), text);
  }
}

template <class F>
inline void Application::showTaskModalWithCancel(F &&f, const char *text)
{
  auto cancelRequested = std::make_shared<std::atomic_bool>(false);
  auto future =
      enqueueTask([task = std::forward<F>(f), cancelRequested]() mutable {
        task(*cancelRequested);
      });

  if (!m_taskModal) {
    vsr::core::logWarning(
        "[Application] No task modal available to show, "
        "executing task without showing modal.");
    future.wait();
  } else {
    m_taskModal->activate(std::move(future), text, cancelRequested);
  }
}

} // namespace vsr::ui::imgui
