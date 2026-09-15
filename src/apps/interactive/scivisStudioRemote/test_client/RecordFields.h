// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CommandText.h"
// vsr_scivis_studio_model
#include "Project.h"
// std
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::test_client {

/*
 * The fields of the records a script names, one table per record type:
 * `assert shot.<id>.<field>` reads a row's get, `update-shot <field>=<value>`
 * runs its set (into the ShotPatch the request carries), dump-project prints
 * every row that has a Dump form, and the assert values --help and the README
 * list are derived from the names. A field is written once, here. The tables
 * below are the Project Replica's records; NamedValues.cpp keeps the
 * session-side ones (a task, a frame header, a pick, a histogram) to itself.
 *
 * Example:
 *   std::string error;
 *   const auto fps = fieldText(SHOT_FIELDS, shot, "fps", "shot", error);
 *   printRecord("EVT Shot id=" + shot.id + dumpFields(SHOT_FIELDS, shot));
 */

// How dump-project prints a field: as it is, in quotes (free text), or not at
// all (a value only assert reads).
enum class Dump
{
  Plain,
  Quoted,
  Omit
};

// `Edit` is what a script's value is written into: the record itself, or
// the patch an edit request carries (a Shot's is a ShotPatch).
template <typename T, typename Edit = T>
struct Field
{
  const char *name;
  // The field's text, as assert compares it and dump-project prints it.
  std::string (*get)(const T &);
  // Applies a script's value. False when the field cannot hold it, with the
  // reason, or with the reason left empty for the plain "not a valid <field>:
  // <value>". Null: the field is read-only.
  bool (*set)(Edit &, const std::string &value, std::string &error){nullptr};
  Dump dump{Dump::Plain};
};

extern const std::vector<Field<Project>> PROJECT_FIELDS;
extern const std::vector<Field<Shot, ShotPatch>> SHOT_FIELDS;
extern const std::vector<Field<Dataset>> DATASET_FIELDS;
extern const std::vector<Field<LightRig>> LIGHT_RIG_FIELDS;
extern const std::vector<Field<CameraRig>> CAMERA_RIG_FIELDS;
extern const std::vector<Field<ColorMapRecord>> COLOR_MAP_FIELDS;

// The row so named; null when the table has none.
template <typename T, typename E>
const Field<T, E> *findField(
    const std::vector<Field<T, E>> &fields, const std::string &name);

// The names of every row, and of every row that has a set.
template <typename T, typename E>
std::vector<std::string> fieldNames(const std::vector<Field<T, E>> &fields);
template <typename T, typename E>
std::vector<std::string> settableFieldNames(
    const std::vector<Field<T, E>> &fields);

// The text of the field `name` of `record`; empty with the reason ("unknown
// <label> field 'x'; valid: ...") when the table has no such row.
template <typename T, typename E>
std::optional<std::string> fieldText(const std::vector<Field<T, E>> &fields,
    const T &record,
    const std::string &name,
    const char *label,
    std::string &error);

// dump-project's text of a record: one ` key=value` per row whose Dump is
// not Omit, in table order, quoted where the row says so.
template <typename T, typename E>
std::string dumpFields(const std::vector<Field<T, E>> &fields, const T &record);

// A Shot's field as `assert shot.<id>.<field>` reads it: a SHOT_FIELDS row,
// or the one field with a parameter, `binding.<datasetId>` (whether that
// dataset's binding is enabled). Empty with the reason.
std::optional<std::string> shotFieldText(
    const Shot &shot, const std::string &field, std::string &error);

// One `field=value` edit of update-shot, written into the patch: a
// SHOT_FIELDS row's set, or `binding.<datasetId>=on|off`. False with the
// reason: an unknown or read-only field, or a value the field cannot hold.
bool setShotField(ShotPatch &patch,
    const std::string &field,
    const std::string &value,
    std::string &error);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T, typename E>
inline const Field<T, E> *findField(
    const std::vector<Field<T, E>> &fields, const std::string &name)
{
  for (const auto &field : fields) {
    if (name == field.name)
      return &field;
  }
  return nullptr;
}

template <typename T, typename E>
inline std::vector<std::string> fieldNames(
    const std::vector<Field<T, E>> &fields)
{
  std::vector<std::string> names;
  for (const auto &field : fields)
    names.emplace_back(field.name);
  return names;
}

template <typename T, typename E>
inline std::vector<std::string> settableFieldNames(
    const std::vector<Field<T, E>> &fields)
{
  std::vector<std::string> names;
  for (const auto &field : fields) {
    if (field.set)
      names.emplace_back(field.name);
  }
  return names;
}

template <typename T, typename E>
inline std::optional<std::string> fieldText(
    const std::vector<Field<T, E>> &fields,
    const T &record,
    const std::string &name,
    const char *label,
    std::string &error)
{
  const auto *field = findField(fields, name);
  if (!field) {
    error = std::string("unknown ") + label + " field '" + name
        + "'; valid: " + join(fieldNames(fields), ", ");
    return {};
  }
  return field->get(record);
}

template <typename T, typename E>
inline std::string dumpFields(
    const std::vector<Field<T, E>> &fields, const T &record)
{
  std::string out;
  for (const auto &field : fields) {
    if (field.dump == Dump::Omit)
      continue;
    const auto value = field.get(record);
    out += ' ';
    out += field.name;
    out += '=';
    out += field.dump == Dump::Quoted ? quotedText(value) : value;
  }
  return out;
}

} // namespace vsr::scivis_studio::test_client
