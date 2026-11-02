// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include "vsr/rendering/index/RenderIndex.hpp"
// std
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vsr::app {

enum class RenderIndexKind : int
{
  ALL_LAYERS = 0,
  FLAT
};

using DeviceInitParam = std::pair<std::string, vsr::core::Any>;

/*
 * A parsed Device Identifier: `[subtype@]library`. An omitted or empty
 * subtype means "default"; `str()` yields the canonical form, which drops a
 * "default" subtype so that `default@helide` and `helide` compare equal.
 *
 * Example:
 *   auto id = DeviceIdentifier::parse("gpu@visrtx");
 *   // id->subtype == "gpu", id->library == "visrtx"
 */
struct DeviceIdentifier
{
  std::string library;
  std::string subtype{"default"};

  // Returns std::nullopt when `identifier` names no loadable device: an empty
  // library, the "{none}" sentinel, or more than one '@'.
  static std::optional<DeviceIdentifier> parse(std::string_view identifier);
  std::string str() const;
};

/*
 * Manages the lifecycle of ANARI devices and their associated RenderIndex
 * instances; loads libraries on demand and reference-counts one scene-owned
 * RenderIndex per ANARI device. Devices are named by Device Identifiers
 * (`[subtype@]library`, see DeviceIdentifier); subtypes of the same library
 * share one loaded anari::Library.
 *
 * ANARI status messages go to anariStatusFunc unless the caller installs its
 * own callback with setStatusCallback(); embedders do that to handle a fatal
 * device error themselves instead of having the process exit.
 *
 * Example:
 *   ANARIDeviceManager mgr;
 *   mgr.setStatusCallback(myStatusFunc, myAppState); // optional
 *   auto device = mgr.loadDevice("visrtx");
 *   auto other = mgr.loadDevice("gpu@visrtx");
 *   auto *idx = mgr.acquireRenderIndex(scene, deviceToken, device);
 *   mgr.releaseRenderIndex(scene, device);
 */
struct ANARIDeviceManager
{
  ANARIDeviceManager(const bool *verboseFlag = nullptr);
  ~ANARIDeviceManager();

  // Device Identifiers offered for selection, from VSR_ANARI_LIBRARIES.
  const std::vector<std::string> &deviceList() const;
  void setDeviceList(const std::vector<std::string> &deviceIds);
  bool isLoadableDevice(const std::string &deviceId) const;

  // Status callback (and its user data) every library loaded from now on
  // reports to. ANARI binds it when a library loads, so libraries that are
  // already loaded keep the one they were loaded with: set it before the
  // first loadDevice(). A null callback restores the default, anariStatusFunc
  // with this manager's verbose flag, which exits the process on a fatal
  // error.
  void setStatusCallback(
      ANARIStatusCallback callback, const void *userData = nullptr);
  // The callback and user data in effect; the default when none was set.
  // Code that loads ANARI libraries on its own passes these to
  // anari::loadLibrary() so it reports the same way.
  ANARIStatusCallback statusCallback() const;
  const void *statusCallbackUserData() const;

  anari::Device loadDevice(const std::string &deviceId,
      const std::vector<DeviceInitParam> &initialDeviceParams = {});
  // loadDevice(deviceId), falling back through deviceList() when that fails
  // (or when `deviceId` names nothing loadable). On return `deviceId` names
  // the device that loaded, or is empty when none did.
  anari::Device loadFirstAvailableDevice(std::string &deviceId);

  const anari::Extensions *loadDeviceExtensions(const std::string &deviceId);
  vsr::rendering::RenderIndex *acquireRenderIndex(
      vsr::scene::Scene &c, vsr::core::Token deviceName, anari::Device device);
  void releaseRenderIndex(vsr::scene::Scene &c, anari::Device device);
  void releaseAllDevices();

  void setRenderIndexKind(RenderIndexKind k);
  RenderIndexKind renderIndexKind() const;

  void saveSettings(vsr::core::DataNode &root) const;
  void loadSettings(vsr::core::DataNode &root);

 private:
  void unloadAllLibraries();

  const bool *m_verboseFlag{nullptr};
  ANARIStatusCallback m_statusCallback{nullptr};
  const void *m_statusCallbackUserData{nullptr};
  struct LiveAnariIndex
  {
    vsr::scene::Scene *scene{nullptr};
    int refCount{0};
    vsr::rendering::RenderIndex *idx{nullptr};
  };
  std::map<anari::Device, LiveAnariIndex> m_rIdxs;
  // keyed by library name
  std::map<std::string, anari::Library> m_loadedLibraries;
  // keyed by canonical Device Identifier
  std::map<std::string, anari::Device> m_loadedDevices;
  std::map<std::string, anari::Extensions> m_loadedDeviceExtensions;
  std::vector<std::string> m_deviceList;

  // Settings //

  struct Settings
  {
    RenderIndexKind renderIndexKind{RenderIndexKind::ALL_LAYERS};
  } m_settings;
};

// The default status callback. `userData` is a `const bool *` verbose flag
// (or null); a fatal error is printed and exits the process.
void anariStatusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    anari::DataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message);

} // namespace vsr::app
