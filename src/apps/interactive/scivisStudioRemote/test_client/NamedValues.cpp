// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/*
 * What `assert` can name: namedValues() is the table -- one row per name or
 * pattern with its summary (the --help and README text) and its resolver --
 * and namedValue() looks one name up in it. The replica's records resolve
 * through the field tables in RecordFields.h; the session-side records (a
 * task, the newest frame's header, the last pick and histogram) have theirs
 * here.
 */

#include "AnyText.h"
#include "CommandRunner.h"
#include "CommandText.h"
#include "RecordFields.h"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
// vsr_scivis_studio_model
#include "Project.h"
#include "Shot.h"
// vsr_scene
#include "vsr/scene/Object.hpp"
#include "vsr/scene/Parameter.hpp"
// std
#include <algorithm>

namespace vsr::scivis_studio::test_client {

using namespace protocol;

namespace {

using Value = std::optional<std::string>;

// The session-side records ///////////////////////////////////////////////////

const std::vector<Field<TaskRecord>> TASK_FIELDS = {
    {"state",
        [](const TaskRecord &t) { return std::string(toString(t.status)); }},
    {"message", [](const TaskRecord &t) { return t.message; }},
    {"framesCompleted",
        [](const TaskRecord &t) { return std::to_string(t.framesCompleted); }},
    {"current", [](const TaskRecord &t) { return std::to_string(t.current); }},
    {"total", [](const TaskRecord &t) { return std::to_string(t.total); }},
};

const std::vector<Field<FrameHeader>> FRAME_FIELDS = {
    {"width", [](const FrameHeader &h) { return std::to_string(h.width); }},
    {"height", [](const FrameHeader &h) { return std::to_string(h.height); }},
    {"encoding",
        [](const FrameHeader &h) { return std::string(toString(h.encoding)); }},
    {"shotId", [](const FrameHeader &h) { return h.shotId; }},
    {"frame", [](const FrameHeader &h) { return std::to_string(h.frame); }},
};

const std::vector<Field<PickReply>> PICK_FIELDS = {
    {"hit", [](const PickReply &p) { return std::string(boolText(p.hit)); }},
    {"worldPosition",
        [](const PickReply &p) {
          const auto &w = p.worldPosition;
          return numberText(w.x) + " " + numberText(w.y) + " "
              + numberText(w.z);
        }},
    {"objectType",
        [](const PickReply &p) {
          return p.objectIdentity ? shortTypeName(p.objectIdentity->type)
                                  : std::string("none");
        }},
    {"objectIndex",
        [](const PickReply &p) {
          return p.objectIdentity
              ? std::to_string(p.objectIdentity->objectIndex)
              : std::string("none");
        }},
};

const std::vector<Field<ArrayHistogramResult>> HISTOGRAM_FIELDS = {
    {"bins",
        [](const ArrayHistogramResult &h) {
          return std::to_string(h.bins.size());
        }},
    {"min",
        [](const ArrayHistogramResult &h) { return numberText(h.minValue); }},
    {"max",
        [](const ArrayHistogramResult &h) { return numberText(h.maxValue); }},
    {"total",
        [](const ArrayHistogramResult &h) {
          uint64_t total = 0;
          for (const auto count : h.bins)
            total += count;
          return std::to_string(total);
        }},
    {"nonFinite",
        [](const ArrayHistogramResult &h) {
          return std::to_string(h.nonFinite);
        }},
};

// Resolving //////////////////////////////////////////////////////////////////

// `name`, `frameCount`, ... for a summary.
std::string codeList(const std::vector<std::string> &names)
{
  std::string out;
  for (const auto &name : names)
    out += (out.empty() ? "`" : ", `") + name + "`";
  return out;
}

// A record's field, the error prefixed with the value's name.
template <typename T>
Value recordField(const std::vector<Field<T>> &fields,
    const T &record,
    const char *label,
    const std::string &name,
    const std::string &field,
    std::string &error)
{
  std::string reason;
  const auto value = fieldText(fields, record, field, label, reason);
  if (!value)
    error = name + ": " + reason;
  return value;
}

// The `<id>` and `<field>` of a `<collection>.<id>.<field>` value, `rest`
// being what follows the collection: ids carry no dots, fields may. False
// with the reason.
bool splitIdField(const std::string &name,
    const std::string &rest,
    std::string &id,
    std::string &field,
    std::string &error)
{
  const auto dot = rest.find('.');
  if (dot == std::string::npos || dot + 1 >= rest.size()) {
    error = "malformed value '" + name + "'; use "
        + name.substr(0, name.size() - rest.size()) + "<id>.<field>";
    return false;
  }
  id = rest.substr(0, dot);
  field = rest.substr(dot + 1);
  return true;
}

// The Project Replica, or null with the reason.
const Project *replicaFor(
    const TestSession &session, const std::string &name, std::string &error)
{
  const auto *project = session.project();
  if (!project)
    error = name + ": no Project Replica";
  return project;
}

// The record of `records` with that id, or null with the reason.
template <typename T>
const T *findRecord(const std::vector<T> &records,
    const char *what,
    const std::string &name,
    const std::string &id,
    std::string &error)
{
  const auto it = std::find_if(records.begin(),
      records.end(),
      [&](const T &record) { return record.id == id; });
  if (it == records.end()) {
    error = name + ": the replica has no " + what + " '" + id + "'";
    return nullptr;
  }
  return &*it;
}

// `<collection>.<id>.<field>` over one collection of the replica, read
// through its field table.
template <typename T>
Value collectionValue(const TestSession &session,
    const std::vector<T> Project::*records,
    const std::vector<Field<T>> &fields,
    const char *label,
    const char *what,
    const std::string &name,
    const std::string &rest,
    std::string &error)
{
  std::string id;
  std::string field;
  if (!splitIdField(name, rest, id, field, error))
    return {};
  const auto *project = replicaFor(session, name, error);
  if (!project)
    return {};
  const auto *record = findRecord(project->*records, what, name, id, error);
  if (!record)
    return {};
  return recordField(fields, *record, label, name, field, error);
}

} // namespace

// The value table ////////////////////////////////////////////////////////////

const std::vector<CommandRunner::ValueSpec> &CommandRunner::namedValues()
{
  using R = CommandRunner;
  using S = const std::string &;
  // A row's summary is the README's; the field lists come from the tables so
  // a field added there is documented here.
  static const std::vector<ValueSpec> table = {
      {"state",
          "`NeverConnected`, `Connected`, `Lost` or `Disconnected`",
          [](R &self, S, S, std::string &) -> Value {
            return std::string(toString(self.m_session->state()));
          }},
      {"scene.objects",
          "objects of every type in the Structural Mirror",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(totalObjects(self.m_session->mirror()));
          }},
      {"scene.layers",
          "layers in the mirror",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->mirror().numberOfLayers());
          }},
      {"scene.cameras",
          "cameras in the mirror",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(
                self.m_session->mirror().numberOfObjects(ANARI_CAMERA));
          }},
      {"scene.renderers",
          "renderers in the mirror",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(
                self.m_session->mirror().numberOfObjects(ANARI_RENDERER));
          }},
      {"project.<field>",
          "a Project Replica field (FAIL before the first snapshot): "
              + codeList(fieldNames(PROJECT_FIELDS))
              + "; the collections are their sizes",
          [](R &self, S name, S rest, std::string &error) -> Value {
            const auto *project = replicaFor(*self.m_session, name, error);
            if (!project)
              return {};
            return recordField(
                PROJECT_FIELDS, *project, "project", name, rest, error);
          }},
      {"shot.<id>.<field>",
          "a Shot in the replica (`active` names the active shot; an unknown"
          " id is a FAIL): "
              + codeList(fieldNames(SHOT_FIELDS))
              + ", `binding.<datasetId>` (`true`/`false`; FAIL when unbound);"
                " `camera` reads `type:index`, `bindings` is a count",
          [](R &self, S name, S rest, std::string &error) -> Value {
            std::string id;
            std::string field;
            if (!splitIdField(name, rest, id, field, error))
              return {};
            const auto *project = replicaFor(*self.m_session, name, error);
            if (!project)
              return {};
            if (id == "active")
              id = project->activeShotId;
            const auto *shot =
                findRecord(project->shots, "shot", name, id, error);
            if (!shot)
              return {};
            std::string reason;
            const auto value = shotFieldText(*shot, field, reason);
            if (!value)
              error = name + ": " + reason;
            return value;
          }},
      {"dataset.<id>.<field>",
          "a Dataset in the replica (an unknown id is a FAIL): "
              + codeList(fieldNames(DATASET_FIELDS))
              + "; `status` reads `Available`, `Unavailable`, `Importing` or"
                " `ImportFailed`, `residency` `Loaded` or `Unloaded`,"
                " `rootNode` `layer:node`",
          [](R &self, S name, S rest, std::string &error) -> Value {
            return collectionValue(*self.m_session,
                &Project::datasets,
                DATASET_FIELDS,
                "dataset",
                "dataset",
                name,
                rest,
                error);
          }},
      {"lightRig.<id>.<field>",
          "a LightRig in the replica: "
              + codeList(fieldNames(LIGHT_RIG_FIELDS)),
          [](R &self, S name, S rest, std::string &error) -> Value {
            return collectionValue(*self.m_session,
                &Project::lightRigs,
                LIGHT_RIG_FIELDS,
                "lightRig",
                "light rig",
                name,
                rest,
                error);
          }},
      {"cameraRig.<id>.<field>",
          "a CameraRig in the replica: "
              + codeList(fieldNames(CAMERA_RIG_FIELDS))
              + "; `keyframes` is a count",
          [](R &self, S name, S rest, std::string &error) -> Value {
            return collectionValue(*self.m_session,
                &Project::cameraRigs,
                CAMERA_RIG_FIELDS,
                "cameraRig",
                "camera rig",
                name,
                rest,
                error);
          }},
      {"colorMap.<id>.<field>",
          "a ColorMap record in the replica: "
              + codeList(fieldNames(COLOR_MAP_FIELDS)),
          [](R &self, S name, S rest, std::string &error) -> Value {
            return collectionValue(*self.m_session,
                &Project::colorMaps,
                COLOR_MAP_FIELDS,
                "colorMap",
                "color map",
                name,
                rest,
                error);
          }},
      {"tasks.completed",
          "`TaskCompleted` messages received since the session object was"
          " made, across reconnects, the ones a Bootstrap replays included (so"
          " an end heard live and then replayed counts twice). The"
          " \"connection lost\" failures a `BootstrapBegin` declares are not"
          " messages and do not count",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->tasksCompleted());
          }},
      {"tasks.failed",
          "`TaskFailed` messages received, counted like `tasks.completed`",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->tasksFailed());
          }},
      {"tasks.replayed",
          "task messages (progress or end) the newest Bootstrap carried"
          " between its Begin and End: the server's task-status replay of what"
          " ended since the previous Bootstrap, and the running task's status."
          " 0 again at every `BootstrapBegin`",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->tasksReplayed());
          }},
      {"task.<id>.<field>",
          "a task record (`last` is `$lastTaskId`; FAIL when nothing has been"
          " heard of the id): "
              + codeList(fieldNames(TASK_FIELDS))
              + "; `state` is `Queued` from the launching reply, `Running`"
                " from the first `TaskProgress`, then `Completed` or `Failed`;"
                " `message` the completion message or the failure's error;"
                " `current` and `total` the newest progress (0 when"
                " indeterminate)",
          [](R &self, S name, S rest, std::string &error) -> Value {
            std::string idText;
            std::string field;
            if (!splitIdField(name, rest, idText, field, error))
              return {};
            unsigned long long taskId = 0;
            if (idText == "last") {
              const auto last = self.variable("lastTaskId");
              if (!last) {
                error = name
                    + ": no task has been started yet ($lastTaskId is unset)";
                return {};
              }
              parseNonNegative(*last, taskId);
            } else if (!parseNonNegative(idText, taskId)) {
              error = name + ": not a task id: " + idText;
              return {};
            }
            const auto *task = self.m_session->task(taskId);
            if (!task) {
              error = name + ": nothing has been heard of task "
                  + std::to_string(taskId);
              return {};
            }
            return recordField(TASK_FIELDS, *task, "task", name, field, error);
          }},
      {"replies.failed",
          "replies with `ok=false`",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->repliesFailed());
          }},
      {"replies.pending",
          "`no-wait` requests awaiting collection",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_pendingReplies.size());
          }},
      {"snapshots.received",
          "Project Snapshots applied, the Bootstrap's included",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->snapshotsReceived());
          }},
      {"browse.entries",
          "entries of the last `list-directory` (0 after a refused one)",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_browseEntries.size());
          }},
      {"var.<name>",
          "a variable's value (see [Variables](#variables)); `$name` in this"
          " position would expand to a value name",
          [](R &self, S name, S variable, std::string &error) -> Value {
            const auto value = self.variable(variable);
            if (!value)
              error = name + ": unknown variable $" + variable;
            return value;
          }},
      {"frame.<field>",
          "the header of the newest frame (FAIL before the first frame): "
              + codeList(fieldNames(FRAME_FIELDS))
              + "; encodings read `Raw`, `TurboJpeg`",
          [](R &self, S name, S field, std::string &error) -> Value {
            const auto &frame = self.m_session->lastFrameHeader();
            if (!frame) {
              error = name + ": no frame received yet";
              return {};
            }
            return recordField(
                FRAME_FIELDS, *frame, "frame", name, field, error);
          }},
      {"frames.received",
          "frames consumed so far (frames superseded before they were read are"
          " not counted)",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->framesReceived());
          }},
      {"frames.advanced",
          "consumed frames whose header `frame` differed from the previous"
          " one's. A step backwards (a loop wrap to 0, a scrub back) is time"
          " moving on purpose, not a skip, and does not count; a forward scrub"
          " does",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->framesAdvanced());
          }},
      {"frames.maxStep",
          "the largest forward step between two consecutive headers. Frames"
          " are latest-wins, so a client slower than the stream can see steps"
          " the server never took",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->frameMaxStep());
          }},
      {"frameConfig.width",
          "the width of the last FrameConfig the server acknowledged",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->frameConfig().width);
          }},
      {"frameConfig.height",
          "the height of the last FrameConfig the server acknowledged",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->frameConfig().height);
          }},
      {"param.<type>.<index>.<name>",
          "a mirror parameter's value: strings verbatim, bools `true`/`false`,"
          " numbers space-separated per component (`\"1 2 3\"`), object"
          " references `type:index`; a missing parameter is a FAIL",
          [](R &self, S name, S rest, std::string &error) -> Value {
            // The parameter name may itself hold dots.
            const auto typeEnd = rest.find('.');
            const auto indexEnd = typeEnd == std::string::npos
                ? typeEnd
                : rest.find('.', typeEnd + 1);
            if (typeEnd == std::string::npos || indexEnd == std::string::npos
                || indexEnd + 1 >= rest.size()) {
              error = "malformed value '" + name
                  + "'; use param.<type>.<index>.<name>";
              return {};
            }
            SceneObjectRef ref;
            if (!parseObjectRef(rest.substr(0, typeEnd),
                    rest.substr(typeEnd + 1, indexEnd - typeEnd - 1),
                    ref,
                    error))
              return {};
            const auto where =
                shortTypeName(ref.type) + " " + std::to_string(ref.objectIndex);
            const auto *obj =
                self.m_session->mirror().getObject(ref.type, ref.objectIndex);
            if (!obj) {
              error = name + ": no " + where + " in the mirror";
              return {};
            }
            const auto paramName = rest.substr(indexEnd + 1);
            const auto *param = obj->parameter(paramName.c_str());
            if (!param) {
              error =
                  name + ": " + where + " has no parameter '" + paramName + "'";
              return {};
            }
            return anyText(param->value());
          }},
      {"errors.received",
          "Error messages received",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->errorsReceived());
          }},
      {"lastError",
          "the text of the newest Error",
          [](R &self, S, S, std::string &) -> Value {
            return self.m_session->lastError();
          }},
      {"lastReplyError",
          "the error text of the newest failed reply",
          [](R &self, S, S, std::string &) -> Value {
            return self.m_session->lastReplyError();
          }},
      {"warnings.received",
          "`TimeAdvanceWarning`s received",
          [](R &self, S, S, std::string &) -> Value {
            return std::to_string(self.m_session->warningsReceived());
          }},
      {"lastWarning",
          "the message of the newest `TimeAdvanceWarning` (empty before one)",
          [](R &self, S, S, std::string &) -> Value {
            const auto &warning = self.m_session->lastWarning();
            return warning ? warning->message : std::string();
          }},
      {"pick.<field>",
          "the last `PickReply` (FAIL before one): "
              + codeList(fieldNames(PICK_FIELDS))
              + "; `hit` `true`/`false`, `worldPosition` `\"x y z\"`, and the"
                " identity (`surface`/`volume` and the pool index, or `none`)",
          [](R &self, S name, S field, std::string &error) -> Value {
            if (!self.m_lastPick) {
              error = name + ": no pick has been answered yet";
              return {};
            }
            return recordField(
                PICK_FIELDS, *self.m_lastPick, "pick", name, field, error);
          }},
      {"histogram.<field>",
          "the last ok `request-array-histogram` (FAIL when the last request"
          " was refused or none was made): "
              + codeList(fieldNames(HISTOGRAM_FIELDS))
              + "; `bins` the bin count, `min` and `max` the value range,"
                " `total` the sum of all bins, `nonFinite` the NaN/inf"
                " elements left out of them",
          [](R &self, S name, S field, std::string &error) -> Value {
            if (!self.m_histogram) {
              error = name + ": no histogram has been answered yet";
              return {};
            }
            return recordField(HISTOGRAM_FIELDS,
                *self.m_histogram,
                "histogram",
                name,
                field,
                error);
          }},
  };
  return table;
}

// Looking a name up //////////////////////////////////////////////////////////

std::optional<std::string> CommandRunner::namedValue(
    const std::string &name, std::string &error)
{
  // An exact row wins; otherwise the pattern row with the longest prefix
  // (the text before its first `<`) that opens the name.
  const ValueSpec *match = nullptr;
  size_t matchedPrefix = 0;
  for (const auto &spec : namedValues()) {
    const std::string pattern = spec.name;
    const auto angle = pattern.find('<');
    if (angle == std::string::npos) {
      if (name == pattern) {
        match = &spec;
        matchedPrefix = pattern.size();
        break;
      }
      continue;
    }
    if (angle > matchedPrefix
        && name.compare(0, angle, pattern, 0, angle) == 0) {
      match = &spec;
      matchedPrefix = angle;
    }
  }
  if (!match) {
    std::vector<std::string> names;
    for (const auto &spec : namedValues())
      names.emplace_back(spec.name);
    error = "unknown value '" + name + "'; valid: " + join(names, ", ");
    return {};
  }
  return match->resolve(*this, name, name.substr(matchedPrefix), error);
}

} // namespace vsr::scivis_studio::test_client
