// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RecordFields.h"
#include "AnyText.h"
#include "Script.h"
// vsr_scivis_studio_model
#include "CameraRig.h"
#include "LightRig.h"
#include "Shot.h"
// std
#include <limits>

namespace vsr::scivis_studio::test_client {

namespace {

// The one Shot field with a parameter.
constexpr const char *SHOT_BINDING_PREFIX = "binding.";

std::string text(bool value)
{
  return boolText(value);
}

std::string text(size_t value)
{
  return std::to_string(value);
}

// The setters the Shot rows share: each false, reason left empty, when the
// text is not a value the field holds.
bool setBool(bool &field, const std::string &value)
{
  return parseBool(value, field);
}

bool setPositiveInt(int &field, const std::string &value)
{
  long long integer = 0;
  if (!parseInteger(value, integer) || integer < 1
      || integer > std::numeric_limits<int>::max())
    return false;
  field = int(integer);
  return true;
}

bool setNonNegativeInt(int &field, const std::string &value)
{
  long long integer = 0;
  if (!parseInteger(value, integer) || integer < 0
      || integer > std::numeric_limits<int>::max())
    return false;
  field = int(integer);
  return true;
}

bool setPositiveFloat(float &field, const std::string &value)
{
  double number = 0;
  if (!parseDouble(value, number) || number <= 0)
    return false;
  field = float(number);
  return true;
}

bool setPositiveU32(uint32_t &field, const std::string &value)
{
  unsigned long long natural = 0;
  if (!parseNonNegative(value, natural) || natural < 1
      || natural > std::numeric_limits<uint32_t>::max())
    return false;
  field = uint32_t(natural);
  return true;
}

// The dataset id a `binding.<datasetId>` field names; empty when the field
// is not one.
std::string shotBindingId(const std::string &field)
{
  const std::string prefix = SHOT_BINDING_PREFIX;
  if (field.size() <= prefix.size() || field.compare(0, prefix.size(), prefix))
    return {};
  return field.substr(prefix.size());
}

} // namespace

// The Project Replica's records ///////////////////////////////////////////////

// Rows in the order dump-project prints them; Dump::Omit rows are assert's
// alone.

const std::vector<Field<Project>> PROJECT_FIELDS = {
    {"name", [](const Project &p) { return p.name; }, nullptr, Dump::Quoted},
    {"activeShot", [](const Project &p) { return p.activeShotId; }},
    {"shots", [](const Project &p) { return text(p.shots.size()); }},
    {"datasets", [](const Project &p) { return text(p.datasets.size()); }},
    {"lightRigs", [](const Project &p) { return text(p.lightRigs.size()); }},
    {"cameraRigs", [](const Project &p) { return text(p.cameraRigs.size()); }},
    {"colorMaps", [](const Project &p) { return text(p.colorMaps.size()); }},
    {"dirty", [](const Project &p) { return text(p.dirty); }},
    {"directory",
        [](const Project &p) { return p.projectDirectory.generic_string(); },
        nullptr,
        Dump::Quoted},
};

const std::vector<Field<Shot>> SHOT_FIELDS = {
    {"name",
        [](const Shot &s) { return s.name; },
        [](Shot &s, const std::string &v, std::string &) {
          s.name = v;
          return true;
        },
        Dump::Quoted},
    {"frameCount",
        [](const Shot &s) { return std::to_string(s.frameCount); },
        [](Shot &s, const std::string &v, std::string &) {
          return setPositiveInt(s.frameCount, v);
        }},
    {"fps",
        [](const Shot &s) { return numberText(s.fps); },
        [](Shot &s, const std::string &v, std::string &) {
          return setPositiveFloat(s.fps, v);
        }},
    {"currentFrame",
        [](const Shot &s) { return std::to_string(s.currentFrame); },
        [](Shot &s, const std::string &v, std::string &) {
          return setNonNegativeInt(s.currentFrame, v);
        }},
    {"loop",
        [](const Shot &s) { return text(s.loop); },
        [](Shot &s, const std::string &v, std::string &) {
          return setBool(s.loop, v);
        }},
    // Playback state, which SetPlaying changes: readable, and refused as an
    // edit by name rather than as read-only.
    {"playing",
        [](const Shot &s) { return text(s.playing); },
        [](Shot &, const std::string &, std::string &error) {
          error = "playing is playback state (SetPlaying), not a Shot edit";
          return false;
        },
        Dump::Omit},
    {"lightRigId",
        [](const Shot &s) { return s.lightRigId; },
        [](Shot &s, const std::string &v, std::string &) {
          s.lightRigId = v;
          return true;
        }},
    {"cameraRigId",
        [](const Shot &s) { return s.cameraRigId; },
        [](Shot &s, const std::string &v, std::string &) {
          s.cameraRigId = v;
          return true;
        }},
    {"bindings", [](const Shot &s) { return text(s.datasetBindings.size()); }},
    {"camera", [](const Shot &s) { return objectRefText(s.camera); }},
    {"renderSettings.width",
        [](const Shot &s) { return std::to_string(s.renderSettings.width); },
        [](Shot &s, const std::string &v, std::string &) {
          return setPositiveU32(s.renderSettings.width, v);
        },
        Dump::Omit},
    {"renderSettings.height",
        [](const Shot &s) { return std::to_string(s.renderSettings.height); },
        [](Shot &s, const std::string &v, std::string &) {
          return setPositiveU32(s.renderSettings.height, v);
        },
        Dump::Omit},
    {"renderSettings.samples",
        [](const Shot &s) { return std::to_string(s.renderSettings.samples); },
        [](Shot &s, const std::string &v, std::string &) {
          return setPositiveU32(s.renderSettings.samples, v);
        },
        Dump::Omit},
    {"renderSettings.rendererLibrary",
        [](const Shot &s) { return s.renderSettings.rendererLibrary; },
        [](Shot &s, const std::string &v, std::string &) {
          s.renderSettings.rendererLibrary = v;
          return true;
        },
        Dump::Omit},
    {"renderSettings.rendererSubtype",
        [](const Shot &s) { return s.renderSettings.rendererSubtype; },
        [](Shot &s, const std::string &v, std::string &) {
          s.renderSettings.rendererSubtype = v;
          return true;
        },
        Dump::Omit},
    // `none` for no renderer object, else the object's index.
    {"renderSettings.rendererObjectIndex",
        [](const Shot &s) {
          const auto index = s.renderSettings.rendererObjectIndex;
          return index == VSR_INVALID_INDEX ? std::string("none")
                                            : std::to_string(index);
        },
        [](Shot &s, const std::string &v, std::string &) {
          unsigned long long natural = 0;
          if (v == "none")
            s.renderSettings.rendererObjectIndex = VSR_INVALID_INDEX;
          else if (parseNonNegative(v, natural))
            s.renderSettings.rendererObjectIndex = size_t(natural);
          else
            return false;
          return true;
        },
        Dump::Omit},
    {"renderSettings.outputFilePrefix",
        [](const Shot &s) { return s.renderSettings.outputFilePrefix; },
        [](Shot &s, const std::string &v, std::string &) {
          s.renderSettings.outputFilePrefix = v;
          return true;
        },
        Dump::Omit},
};

const std::vector<Field<Dataset>> DATASET_FIELDS = {
    {"name", [](const Dataset &d) { return d.name; }, nullptr, Dump::Quoted},
    {"status",
        [](const Dataset &d) {
          return std::string(dataset::toString(d.status));
        }},
    {"residency",
        [](const Dataset &d) {
          return std::string(dataset::toString(d.residency));
        }},
    {"sourceKind",
        [](const Dataset &d) {
          return std::string(dataset::toString(d.sourceKind));
        }},
    {"importerType", [](const Dataset &d) { return d.importerType; }},
    {"sourcePath",
        [](const Dataset &d) { return d.source.sourcePath; },
        nullptr,
        Dump::Omit},
    {"rootNode", [](const Dataset &d) { return nodeText(d.rootNode); }},
    {"dirty", [](const Dataset &d) { return text(d.dirty); }},
    {"declared",
        [](const Dataset &d) { return text(d.declared); },
        nullptr,
        Dump::Omit},
};

const std::vector<Field<LightRig>> LIGHT_RIG_FIELDS = {
    {"name", [](const LightRig &r) { return r.name; }, nullptr, Dump::Quoted},
    {"rootNode", [](const LightRig &r) { return nodeText(r.rootNode); }},
};

const std::vector<Field<CameraRig>> CAMERA_RIG_FIELDS = {
    {"name", [](const CameraRig &r) { return r.name; }, nullptr, Dump::Quoted},
    {"keyframes", [](const CameraRig &r) { return text(r.keyframes.size()); }},
};

const std::vector<Field<ColorMapRecord>> COLOR_MAP_FIELDS = {
    {"name",
        [](const ColorMapRecord &m) { return m.name; },
        nullptr,
        Dump::Quoted},
};

// Shot fields with the binding folded in //////////////////////////////////////

std::optional<std::string> shotFieldText(
    const Shot &shot, const std::string &field, std::string &error)
{
  const auto datasetId = shotBindingId(field);
  if (datasetId.empty())
    return fieldText(SHOT_FIELDS, shot, field, "shot", error);
  const auto *binding = shot::findDatasetBinding(shot, datasetId);
  if (!binding) {
    error = "shot '" + shot.id + "' has no binding for '" + datasetId + "'";
    return {};
  }
  return text(binding->enabled);
}

bool setShotField(Shot &shot,
    const std::string &field,
    const std::string &value,
    std::string &error)
{
  const auto badValue = [&] {
    error = "not a valid " + field + ": " + value;
    return false;
  };
  const auto datasetId = shotBindingId(field);
  if (!datasetId.empty()) {
    bool enabled = false;
    if (!parseBool(value, enabled))
      return badValue();
    shot::setDatasetBinding(shot, datasetId, enabled);
    return true;
  }
  const auto *row = findField(SHOT_FIELDS, field);
  if (!row || !row->set) {
    error = std::string(row ? "read-only" : "unknown") + " Shot field '" + field
        + "'; valid: " + join(settableFieldNames(SHOT_FIELDS), ", ") + ", "
        + SHOT_BINDING_PREFIX + "<datasetId>";
    return false;
  }
  error.clear();
  if (row->set(shot, value, error))
    return true;
  return error.empty() ? badValue() : false;
}

} // namespace vsr::scivis_studio::test_client
