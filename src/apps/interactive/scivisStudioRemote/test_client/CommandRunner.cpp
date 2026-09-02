// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandRunner.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
#include "PayloadCommon.h"
#include "ShotRigRequests.h"
#include "TaskMessages.h"
// vsr_scivis_studio_model
#include "CameraRig.h"
#include "LightRig.h"
#include "Project.h"
#include "Shot.h"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Object.hpp"
#include "vsr/scene/Parameter.hpp"
// vsr_core
#include "vsr/core/ObjectPool.hpp"
// std
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <ostream>
#include <sstream>

namespace vsr::scivis_studio::test_client {

using namespace protocol;
using vsr::core::Any;

namespace {

// Text helpers ///////////////////////////////////////////////////////////////

std::string lower(std::string text)
{
  for (auto &c : text)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::string upper(std::string text)
{
  for (auto &c : text)
    c = char(std::toupper(static_cast<unsigned char>(c)));
  return text;
}

std::string quoted(const std::string &text)
{
  return "\"" + text + "\"";
}

std::string join(const std::vector<std::string> &items, const char *sep)
{
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i)
      out += sep;
    out += items[i];
  }
  return out;
}

bool parseDouble(const std::string &text, double &out)
{
  if (text.empty())
    return false;
  errno = 0;
  char *end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == text.c_str() || *end != '\0')
    return false;
  out = value;
  return true;
}

// The shortest text that reads back as the same number, so a float32 set from
// "0.9" prints "0.9" and one set from "0.123456789" prints "0.12345679", which
// `assert ... == 0.123456789` still equates at float32 precision.
template <typename T>
std::string numberText(T value)
{
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

// ANARI type names ///////////////////////////////////////////////////////////

// Script spelling ("camera", "float32_vec3") or the wire spelling
// ("ANARI_CAMERA"); case-insensitive.
std::optional<anari::DataType> parseAnariType(const std::string &text)
{
  std::string name = upper(text);
  if (name.rfind("ANARI_", 0) != 0)
    name = "ANARI_" + name;
  return anariTypeFromString(name);
}

// "ANARI_FLOAT32_VEC3" -> "float32_vec3"
std::string shortTypeName(anari::DataType type)
{
  std::string name = anari::toString(type);
  if (name.rfind("ANARI_", 0) == 0)
    name.erase(0, 6);
  return lower(name);
}

// The scalar an ANARI value type is made of, read off its name.
enum class Component
{
  None,
  Float32,
  Float64,
  Int8,
  Int16,
  Int32,
  Int64,
  UInt8,
  UInt16,
  UInt32,
  UInt64
};

struct ComponentInfo
{
  const char *prefix;
  Component component;
  size_t size;
};

// clang-format off
constexpr ComponentInfo COMPONENTS[] = {
    {"ANARI_FLOAT32", Component::Float32, 4},
    {"ANARI_FLOAT64", Component::Float64, 8},
    {"ANARI_INT8",    Component::Int8,    1},
    {"ANARI_INT16",   Component::Int16,   2},
    {"ANARI_INT32",   Component::Int32,   4},
    {"ANARI_INT64",   Component::Int64,   8},
    {"ANARI_UINT8",   Component::UInt8,   1},
    {"ANARI_UINT16",  Component::UInt16,  2},
    {"ANARI_UINT32",  Component::UInt32,  4},
    {"ANARI_UINT64",  Component::UInt64,  8},
};
// clang-format on

const ComponentInfo *componentOf(anari::DataType type)
{
  const std::string name = anari::toString(type);
  for (const auto &info : COMPONENTS) {
    const size_t n = std::strlen(info.prefix);
    if (name.compare(0, n, info.prefix) == 0
        && (name.size() == n || name[n] == '_'))
      return &info;
  }
  return nullptr;
}

// Rejects what T cannot hold rather than wrapping it: uint8 300 is a typo,
// not 44.
template <typename T>
bool storeInteger(const std::string &text, std::byte *dst)
{
  T typed{};
  if constexpr (std::is_signed_v<T>) {
    long long value = 0;
    if (!parseInteger(text, value) || value < std::numeric_limits<T>::min()
        || value > std::numeric_limits<T>::max())
      return false;
    typed = static_cast<T>(value);
  } else {
    unsigned long long value = 0;
    if (!parseNonNegative(text, value) || value > std::numeric_limits<T>::max())
      return false;
    typed = static_cast<T>(value);
  }
  std::memcpy(dst, &typed, sizeof(typed));
  return true;
}

template <typename T>
bool storeFloat(const std::string &text, std::byte *dst)
{
  double value = 0;
  if (!parseDouble(text, value))
    return false;
  const T typed = static_cast<T>(value);
  std::memcpy(dst, &typed, sizeof(typed));
  return true;
}

bool storeComponent(
    Component component, const std::string &text, std::byte *dst)
{
  switch (component) {
  case Component::Float32:
    return storeFloat<float>(text, dst);
  case Component::Float64:
    return storeFloat<double>(text, dst);
  case Component::Int8:
    return storeInteger<int8_t>(text, dst);
  case Component::Int16:
    return storeInteger<int16_t>(text, dst);
  case Component::Int32:
    return storeInteger<int32_t>(text, dst);
  case Component::Int64:
    return storeInteger<int64_t>(text, dst);
  case Component::UInt8:
    return storeInteger<uint8_t>(text, dst);
  case Component::UInt16:
    return storeInteger<uint16_t>(text, dst);
  case Component::UInt32:
    return storeInteger<uint32_t>(text, dst);
  case Component::UInt64:
    return storeInteger<uint64_t>(text, dst);
  case Component::None:
    break;
  }
  return false;
}

template <typename T>
std::string componentText(const std::byte *src)
{
  T value;
  std::memcpy(&value, src, sizeof(value));
  return numberText(value);
}

std::string componentText(Component component, const std::byte *src)
{
  switch (component) {
  case Component::Float32:
    return componentText<float>(src);
  case Component::Float64:
    return componentText<double>(src);
  case Component::Int8:
    return componentText<int8_t>(src);
  case Component::Int16:
    return componentText<int16_t>(src);
  case Component::Int32:
    return componentText<int32_t>(src);
  case Component::Int64:
    return componentText<int64_t>(src);
  case Component::UInt8:
    return componentText<uint8_t>(src);
  case Component::UInt16:
    return componentText<uint16_t>(src);
  case Component::UInt32:
    return componentText<uint32_t>(src);
  case Component::UInt64:
    return componentText<uint64_t>(src);
  case Component::None:
    break;
  }
  return "?";
}

// Builds the Any a `set-param` names: bool, string, or any scalar, vector,
// matrix or box whose components are one of the integer or float types above,
// with one token per component.
bool anyFromTokens(anari::DataType type,
    const std::vector<std::string> &tokens,
    Any &out,
    std::string &error)
{
  if (type == ANARI_STRING) {
    if (tokens.size() != 1) {
      error = "string takes exactly one value (quote it to include spaces)";
      return false;
    }
    out = Any(tokens.front());
    return true;
  }
  if (type == ANARI_BOOL) {
    if (tokens.size() != 1) {
      error = "bool takes exactly one value";
      return false;
    }
    const auto text = lower(tokens.front());
    if (text == "true" || text == "1")
      out = Any(true);
    else if (text == "false" || text == "0")
      out = Any(false);
    else {
      error = "bool value must be true, false, 1 or 0, got: " + tokens.front();
      return false;
    }
    return true;
  }

  const auto *component = componentOf(type);
  if (!component || anari::isObject(type) || anari::isArray(type)) {
    error = std::string("unsupported value type ") + anari::toString(type);
    return false;
  }
  const size_t count = anari::sizeOf(type) / component->size;
  if (tokens.size() != count) {
    error = shortTypeName(type) + " takes " + std::to_string(count)
        + " value(s), got " + std::to_string(tokens.size());
    return false;
  }
  std::vector<std::byte> bytes(anari::sizeOf(type));
  for (size_t i = 0; i < count; ++i) {
    if (!storeComponent(component->component,
            tokens[i],
            bytes.data() + i * component->size)) {
      error = "not a " + shortTypeName(type) + " component: " + tokens[i];
      return false;
    }
  }
  out = Any(type, bytes.data());
  return true;
}

// The string form of a mirror value: strings verbatim, bools as true/false,
// object references as type:index, numbers space-separated per component.
std::string anyText(const Any &value)
{
  if (!value.valid())
    return "<none>";
  const auto type = value.type();
  if (type == ANARI_STRING)
    return value.getString();
  if (type == ANARI_BOOL)
    return value.get<bool>() ? "true" : "false";
  if (anari::isObject(type)) {
    return shortTypeName(type) + ":" + std::to_string(value.getAsObjectIndex());
  }
  const auto *component = componentOf(type);
  if (!component)
    return std::string("<") + shortTypeName(type) + ">";
  const auto *bytes = static_cast<const std::byte *>(value.data());
  const size_t count = anari::sizeOf(type) / component->size;
  std::string out;
  for (size_t i = 0; i < count; ++i) {
    if (i)
      out += ' ';
    out += componentText(component->component, bytes + i * component->size);
  }
  return out;
}

// Comparison /////////////////////////////////////////////////////////////////

// Numbers compare numerically at float32 precision for equality (the scene's
// values are float32, so `== 0.9` must hold for a value set from "0.9");
// anything else compares as text.
bool compareValues(const std::string &lhs,
    const std::string &op,
    const std::string &rhs,
    bool &result,
    std::string &error)
{
  if (op == "contains") {
    result = lhs.find(rhs) != std::string::npos;
    return true;
  }

  double a = 0;
  double b = 0;
  int cmp = 0;
  if (parseDouble(lhs, a) && parseDouble(rhs, b)) {
    if (float(a) == float(b))
      cmp = 0;
    else
      cmp = a < b ? -1 : 1;
  } else {
    cmp = lhs.compare(rhs);
    cmp = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
  }

  if (op == "==")
    result = cmp == 0;
  else if (op == "!=")
    result = cmp != 0;
  else if (op == "<")
    result = cmp < 0;
  else if (op == "<=")
    result = cmp <= 0;
  else if (op == ">")
    result = cmp > 0;
  else if (op == ">=")
    result = cmp >= 0;
  else {
    error = "unknown operator '" + op + "'; valid: == != < <= > >= contains";
    return false;
  }
  return true;
}

// Scene refs /////////////////////////////////////////////////////////////////

bool parseObjectRef(const std::string &typeText,
    const std::string &indexText,
    SceneObjectRef &ref,
    std::string &error)
{
  const auto type = parseAnariType(typeText);
  if (!type || !anari::isObject(*type)) {
    error = "not a scene object type: " + typeText;
    return false;
  }
  unsigned long long index = 0;
  if (!parseNonNegative(indexText, index)) {
    error = "not an object index: " + indexText;
    return false;
  }
  ref.type = *type;
  ref.objectIndex = size_t(index);
  return true;
}

bool parseHexBytes(const std::vector<std::string> &tokens,
    size_t first,
    std::vector<std::byte> &out,
    std::string &error)
{
  out.clear();
  for (size_t i = first; i < tokens.size(); ++i) {
    const auto &token = tokens[i];
    if (token.empty() || token.size() % 2 != 0) {
      error = "hex bytes need an even number of digits: " + token;
      return false;
    }
    for (size_t j = 0; j < token.size(); j += 2) {
      // Two hex digits exactly: strtol's tolerance of "-1" or "+f" is not
      // wanted on a byte.
      const std::string pair = token.substr(j, 2);
      if (!std::isxdigit(static_cast<unsigned char>(pair[0]))
          || !std::isxdigit(static_cast<unsigned char>(pair[1]))) {
        error = "not a hex byte: " + pair;
        return false;
      }
      out.push_back(std::byte(std::stoul(pair, nullptr, 16)));
    }
  }
  return true;
}

bool writePPM(const std::string &path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t> &rgba,
    std::string &error)
{
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open " + path + " for writing";
    return false;
  }
  file << "P6\n" << width << ' ' << height << "\n255\n";
  std::vector<char> rgb;
  rgb.reserve(size_t(width) * height * 3);
  for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
    rgb.push_back(char(rgba[i]));
    rgb.push_back(char(rgba[i + 1]));
    rgb.push_back(char(rgba[i + 2]));
  }
  file.write(rgb.data(), std::streamsize(rgb.size()));
  if (!file) {
    error = "write to " + path + " failed";
    return false;
  }
  return true;
}

std::string usageError(const Command &command, const char *usage)
{
  return "usage: " + command.name + " " + usage;
}

const char *boolText(bool value)
{
  return value ? "true" : "false";
}

// true/false, on/off, 1/0; case-insensitive.
bool parseBool(const std::string &text, bool &out)
{
  const auto word = lower(text);
  if (word == "true" || word == "on" || word == "1")
    out = true;
  else if (word == "false" || word == "off" || word == "0")
    out = false;
  else
    return false;
  return true;
}

std::string nodeText(const SceneNodeRef &node)
{
  return node.layerName + ":" + std::to_string(node.nodeIndex);
}

std::string objectRefText(const SceneObjectRef &ref)
{
  return shortTypeName(ref.type) + ":" + std::to_string(ref.objectIndex);
}

// The wire names of vsr::io::ImporterType ("OBJ", "VOLUME_ANIMATION", ...),
// case-insensitive.
std::optional<vsr::io::ImporterType> parseImporter(const std::string &text)
{
  return importerTypeFromString(upper(text));
}

// A trailing `key=value` argument, split off when present.
std::optional<std::string> takeOption(
    std::vector<std::string> &args, const char *key)
{
  if (args.empty())
    return {};
  const std::string prefix = std::string(key) + "=";
  if (args.back().compare(0, prefix.size(), prefix) != 0)
    return {};
  auto value = args.back().substr(prefix.size());
  args.pop_back();
  return value;
}

// Applies one `field=value` edit of update-shot to a Shot; false with the
// reason on an unknown field or a value the field cannot hold.
bool applyShotField(Shot &shot,
    const std::string &field,
    const std::string &value,
    std::string &error)
{
  const auto badValue = [&] {
    error = "not a valid " + field + ": " + value;
    return false;
  };
  long long integer = 0;
  unsigned long long natural = 0;
  double number = 0;
  bool flag = false;
  auto &rs = shot.renderSettings;

  if (field == "name")
    shot.name = value;
  else if (field == "frameCount") {
    if (!parseInteger(value, integer) || integer < 1
        || integer > std::numeric_limits<int>::max())
      return badValue();
    shot.frameCount = int(integer);
  } else if (field == "fps") {
    if (!parseDouble(value, number) || number <= 0)
      return badValue();
    shot.fps = float(number);
  } else if (field == "currentFrame") {
    if (!parseInteger(value, integer) || integer < 0
        || integer > std::numeric_limits<int>::max())
      return badValue();
    shot.currentFrame = int(integer);
  } else if (field == "loop") {
    if (!parseBool(value, flag))
      return badValue();
    shot.loop = flag;
  } else if (field == "playing") {
    error = "playing is playback state (SetPlaying), not a Shot edit";
    return false;
  } else if (field == "lightRigId")
    shot.lightRigId = value;
  else if (field == "cameraRigId")
    shot.cameraRigId = value;
  else if (field == "renderSettings.width" || field == "renderSettings.height"
      || field == "renderSettings.samples") {
    if (!parseNonNegative(value, natural) || natural < 1
        || natural > std::numeric_limits<uint32_t>::max())
      return badValue();
    (field == "renderSettings.width"           ? rs.width
            : field == "renderSettings.height" ? rs.height
                                               : rs.samples) =
        uint32_t(natural);
  } else if (field == "renderSettings.rendererLibrary")
    rs.rendererLibrary = value;
  else if (field == "renderSettings.rendererSubtype")
    rs.rendererSubtype = value;
  else if (field == "renderSettings.outputFilePrefix")
    rs.outputFilePrefix = value;
  else if (field == "renderSettings.rendererObjectIndex") {
    if (value == "none")
      rs.rendererObjectIndex = VSR_INVALID_INDEX;
    else if (parseNonNegative(value, natural))
      rs.rendererObjectIndex = size_t(natural);
    else
      return badValue();
  } else if (field.rfind("binding.", 0) == 0 && field.size() > 8) {
    if (!parseBool(value, flag))
      return badValue();
    shot::setDatasetBinding(shot, field.substr(8), flag);
  } else {
    error = "unknown Shot field '" + field
        + "'; valid: name, frameCount, fps, currentFrame, loop, lightRigId,"
          " cameraRigId, renderSettings.{width,height,samples,rendererLibrary,"
          "rendererSubtype,rendererObjectIndex,outputFilePrefix},"
          " binding.<datasetId>";
    return false;
  }
  return true;
}

} // namespace

// Construction ///////////////////////////////////////////////////////////////

CommandRunner::CommandRunner(
    TestSession *session, std::ostream *out, RunnerOptions options)
    : m_session(session), m_out(out), m_options(std::move(options))
{}

// Running ////////////////////////////////////////////////////////////////////

bool CommandRunner::run(const std::vector<Command> &commands)
{
  bool ok = true;
  for (const auto &command : commands) {
    if (!runCommand(command)) {
      ok = false;
      if (!m_options.keepGoing)
        break;
    }
  }
  drainEvents(); // nothing is in flight any more for an Error to fail
  return ok;
}

bool CommandRunner::runCommand(const Command &command)
{
  const auto failure = execute(command);
  if (failure) {
    printRecord("FAIL " + command.text + ": " + *failure);
    return false;
  }
  printRecord("OK " + command.text);
  return true;
}

const std::vector<std::string> &CommandRunner::assertNames()
{
  static const std::vector<std::string> names = {"state",
      "scene.objects",
      "scene.layers",
      "scene.cameras",
      "scene.renderers",
      "project.name",
      "project.directory",
      "project.activeShot",
      "project.dirty",
      "project.shots",
      "project.datasets",
      "project.lightRigs",
      "project.cameraRigs",
      "project.colorMaps",
      "shot.<id>.<field>",
      "dataset.<id>.<field>",
      "lightRig.<id>.name",
      "cameraRig.<id>.name",
      "colorMap.<id>.name",
      "tasks.completed",
      "tasks.failed",
      "replies.failed",
      "replies.pending",
      "snapshots.received",
      "browse.entries",
      "var.<name>",
      "frame.width",
      "frame.height",
      "frame.encoding",
      "frame.shotId",
      "frame.frame",
      "frames.received",
      "frameConfig.width",
      "frameConfig.height",
      "param.<type>.<index>.<name>",
      "errors.received",
      "lastError"};
  return names;
}

const std::vector<std::string> &CommandRunner::commandHelp()
{
  // clang-format off
  static const std::vector<std::string> lines = {
      "connect [HOST] [PORT]        connect, exchange Hellos, await the Bootstrap",
      "disconnect                   send Disconnect and close -> Disconnected",
      "shutdown                     send Shutdown, await the server closing the socket",
      "ping                         send Ping",
      "expect-pong                  the next server message (Frames aside) must be a Pong",
      "await-lost                   wait until the connection is Lost",
      "reconnect                    connect again to the last host and port, retrying until the deadline",
      "sleep MS                     keep polling for MS milliseconds",
      "expect-error [SUBSTRING]     the next server message (Frames aside) must be an Error",
      "send-raw TYPE [HEX ...]      send a message of type byte TYPE with raw payload bytes",
      "set-frame-config W H         request a frame size, await the FrameConfig ack",
      "set-encodings NAME[,NAME]    offer frame encodings, most preferred first (raw, turbojpeg)",
      "start-rendering              ask the server to stream frames",
      "stop-rendering               pause the stream",
      "await-frame [COUNT]          wait for COUNT (default 1) frames",
      "save-frame PATH.ppm          decode the last frame into a binary P6 PPM",
      "set-param TYPE INDEX NAME ANARITYPE VALUE...",
      "                             edit a parameter (e.g. camera 0 fovy float32 0.9)",
      "remove-param TYPE INDEX NAME remove a parameter",
      "set-node-transform LAYER NODE M0..M15",
      "                             set a transform node's matrix (column-major)",
      "dump-scene | dump-layers | dump-project | dump-frame",
      "                             print the mirror, replica or last frame header",
      "assert VALUE OP RHS          OP in == != < <= > >= contains; VALUE one of the names below",
      "",
      "Project Ops (each sends one request, awaits its ProjectOpReply and prints it):",
      "new-project | open-project DIR | save-project [DIR]",
      "import-static-dataset PATH [NAME] [IMPORTER|VSR_SUBTREE]",
      "import-file-animation-dataset NAME IMPORTER PATH... [set-frame-count=BOOL]",
      "declare-file-animation-dataset NAME IMPORTER SOURCE... [set-frame-count=BOOL]",
      "reimport-dataset ID | rename-dataset ID NAME | remove-dataset ID [keep-asset-file]",
      "load-dataset ID | unload-dataset ID | refresh-dataset-availability ID",
      "save-dataset-archive ID PATH | load-dataset-archive PATH",
      "discover-dataset-candidates | incorporate-dataset-candidate FILE [PROPOSED] [NAME]",
      "create-shot [NAME] | remove-shot ID | set-active-shot ID",
      "update-shot ID FIELD=VALUE...  (name, frameCount, fps, loop, currentFrame, lightRigId,",
      "                             cameraRigId, renderSettings.*, binding.<datasetId>=on|off)",
      "create-light-rig [NAME] | clone-light-rig ID | remove-light-rig ID | rename-light-rig ID NAME",
      "add-light RIG [SUBTYPE] | remove-light RIG LAYER NODE",
      "create-camera-rig [NAME] | remove-camera-rig ID | rename-camera-rig ID NAME",
      "save-light-rig-archive ID PATH | load-light-rig-archive PATH",
      "save-camera-rig-archive ID PATH | load-camera-rig-archive PATH",
      "create-color-map [NAME] | rename-color-map ID NAME | remove-color-map ID",
      "list-roots | list-directory PATH   (one EVT DataRoot / DirectoryEntry per item)",
      "cancel-task TASKID",
      "await-task [TASKID] [expect-fail]",
      "                             wait for the task (default $lastTaskId) to end",
      "await-snapshot               wait for a ProjectSnapshot newer than the last request",
      "await-reply [REQUESTID]      collect the reply of a no-wait request (default the oldest)",
      "",
      "Prefixes: `expect-fail <request>` makes ok=false the OK outcome;",
      "`no-wait <request>` sends without awaiting the reply. Replies fill",
      "$lastShotId $lastLightRigId $lastCameraRigId $lastColorMapId $lastObjectRef",
      "$lastObjectType $lastObjectIndex $lastLightLayer $lastLightNode $lastDatasetId",
      "$lastTaskId $lastRequestId $dataRoot; `$name` expands in any argument.",
      "",
      "Waiting commands take a trailing timeout=MS and FAIL as soon as the",
      "connection is Lost. An Error the server sends during any command but",
      "expect-error FAILs that command. assert values:"};
  // clang-format on
  return lines;
}

CommandRunner::Failure CommandRunner::execute(Command command)
{
  m_expectFail = false;
  m_noWait = false;
  while (command.name == "expect-fail" || command.name == "no-wait") {
    if (command.args.empty())
      return usageError(command, "<request command> [args...]");
    (command.name == "expect-fail" ? m_expectFail : m_noWait) = true;
    command.name = command.args.front();
    command.args.erase(command.args.begin());
  }

  std::string error;
  if (!expandVariables(
          command,
          [this](const std::string &n) { return variable(n); },
          &error))
    return error;
  const auto suffix = takeTimeoutSuffix(command, &error);
  if (!error.empty())
    return error;
  const Deadline deadline = suffix ? *suffix : m_options.timeout;

  bool handled = false;
  const auto requestFailure = executeRequest(command, deadline, handled);
  if (handled)
    return requestFailure;
  if (m_expectFail || m_noWait) {
    return std::string(m_expectFail ? "expect-fail" : "no-wait")
        + " applies to request commands, not to " + command.name;
  }

  const auto &name = command.name;
  if (name == "connect")
    return connect(command, deadline);
  if (name == "disconnect")
    return disconnect(command);
  if (name == "shutdown")
    return shutdown(command, deadline);
  if (name == "ping")
    return ping(command);
  if (name == "expect-pong")
    return expectPong(command, deadline);
  if (name == "await-lost")
    return awaitLost(command, deadline);
  if (name == "reconnect")
    return reconnect(command, deadline);
  if (name == "sleep")
    return sleep(command);
  if (name == "expect-error")
    return expectError(command, deadline);
  if (name == "send-raw")
    return sendRaw(command);
  if (name == "set-frame-config")
    return setFrameConfig(command, deadline);
  if (name == "set-encodings")
    return setEncodings(command);
  if (name == "start-rendering")
    return startRendering(command);
  if (name == "stop-rendering")
    return stopRendering(command);
  if (name == "await-frame")
    return awaitFrame(command, deadline);
  if (name == "save-frame")
    return saveFrame(command);
  if (name == "set-param")
    return setParam(command);
  if (name == "remove-param")
    return removeParam(command);
  if (name == "set-node-transform")
    return setNodeTransform(command);
  if (name == "dump-scene")
    return dumpScene(command);
  if (name == "dump-layers")
    return dumpLayers(command);
  if (name == "dump-project")
    return dumpProject(command);
  if (name == "dump-frame")
    return dumpFrame(command);
  if (name == "assert")
    return assertValue(command);
  return "unknown command '" + name + "'";
}

// Session commands ///////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::connect(
    const Command &command, Deadline deadline)
{
  if (command.args.size() > 2)
    return usageError(command, "[host] [port]");
  if (m_session->state() == SessionState::Connected)
    return "already connected to " + m_session->host() + ":"
        + std::to_string(m_session->port());
  std::string host = m_options.host;
  int port = m_options.port;
  if (!command.args.empty())
    host = command.args[0];
  if (command.args.size() > 1 && !parsePort(command.args[1], port))
    return "port must be an integer in 1..65535, got: " + command.args[1];

  std::string error;
  const bool ok = m_session->connect(host, port, deadline, &error);
  const auto pending = drainEvents();
  if (!ok)
    return error;
  // The Bootstrap's snapshot is not the one await-snapshot waits for.
  m_snapshotMark = m_session->snapshotsReceived();
  m_pendingReplies.clear();
  return pending;
}

CommandRunner::Failure CommandRunner::disconnect(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  if (m_session->state() != SessionState::Connected
      && m_session->state() != SessionState::Lost)
    return std::string("not connected (") + toString(m_session->state()) + ")";
  // Before the close: whatever the server said last still counts.
  const auto pending = drainEvents();
  m_session->disconnect();
  return pending;
}

CommandRunner::Failure CommandRunner::shutdown(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return usageError(command, "");
  std::string error;
  // Events keep flowing while the socket is still open; print them after.
  const bool ok = m_session->shutdown(deadline, &error);
  const auto pending = drainEvents();
  if (!ok)
    return error;
  return pending;
}

CommandRunner::Failure CommandRunner::ping(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  std::string error;
  if (!m_session->ping(&error))
    return error;
  // No drain: the Pong belongs to the expect-pong that usually follows.
  return {};
}

CommandRunner::Failure CommandRunner::expectPong(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return usageError(command, "");
  // Frames are stream data, not replies; any other message is the answer.
  Event next;
  const auto wait = pumpUntilEvent(
      [](const Event &e) { return e.name != "Frame"; }, deadline, &next);
  if (wait != Wait::Done)
    return waitFailure(wait, "Pong", deadline);
  if (next.name != "Pong")
    return "expected Pong, got " + next.text();
  return {};
}

CommandRunner::Failure CommandRunner::awaitLost(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return usageError(command, "");
  const auto wait = pumpUntil(
      [&] { return m_session->state() == SessionState::Lost; }, deadline);
  if (wait != Wait::Done) {
    return wait == Wait::Error
        ? waitFailure(wait, "the loss", deadline)
        : std::string("still ") + toString(m_session->state()) + " after "
            + std::to_string(deadline.count()) + " ms";
  }
  return {};
}

CommandRunner::Failure CommandRunner::reconnect(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return usageError(command, "");
  if (m_session->state() == SessionState::Connected)
    return "already connected";
  std::string error;
  const bool ok = m_session->reconnect(deadline, &error);
  const auto pending = drainEvents();
  if (!ok)
    return error;
  m_snapshotMark = m_session->snapshotsReceived();
  m_pendingReplies.clear();
  return pending;
}

CommandRunner::Failure CommandRunner::sleep(const Command &command)
{
  std::chrono::milliseconds ms;
  if (command.args.size() != 1 || !parseMilliseconds(command.args[0], ms))
    return usageError(command, "<ms>");
  // Passing time is the point, so a loss meanwhile is the next command's to
  // notice; an Error is still an answer nobody asked for.
  const auto wait = pumpUntil([] { return false; }, ms, LossEnds::Nothing);
  if (wait == Wait::Error)
    return waitFailure(wait, "the sleep to end", ms);
  return {};
}

CommandRunner::Failure CommandRunner::expectError(
    const Command &command, Deadline deadline)
{
  if (command.args.size() > 1)
    return usageError(command, "[substring]");
  // Frames are stream data, not replies: one still in flight after
  // stop-rendering must not stand in for the answer. Nor is a Pong: the
  // session pings on its own after a quiet spell.
  Event next;
  const auto wait = pumpUntilEvent(
      [](const Event &e) { return e.name != "Frame" && e.name != "Pong"; },
      deadline,
      &next);
  if (wait != Wait::Done)
    return waitFailure(wait, "server message", deadline);
  if (next.name != "Error")
    return "expected Error, got " + next.text();
  if (!command.args.empty()
      && m_session->lastError().find(command.args[0]) == std::string::npos) {
    return "Error " + quoted(m_session->lastError()) + " does not contain "
        + quoted(command.args[0]);
  }
  return {};
}

CommandRunner::Failure CommandRunner::sendRaw(const Command &command)
{
  unsigned long long type = 0;
  if (command.args.empty() || !parseNonNegative(command.args[0], type)
      || type > 0xff)
    return usageError(command, "<typeByte 0..255> [hex bytes...]");
  std::vector<std::byte> payload;
  std::string error;
  if (!parseHexBytes(command.args, 1, payload, error))
    return error;
  if (!m_session->sendRaw(uint8_t(type), std::move(payload), &error))
    return error;
  // No drain: the reply belongs to the expect-error that usually follows.
  return {};
}

// Rendering commands /////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::setFrameConfig(
    const Command &command, Deadline deadline)
{
  unsigned long long width = 0;
  unsigned long long height = 0;
  if (command.args.size() != 2 || !parseNonNegative(command.args[0], width)
      || !parseNonNegative(command.args[1], height))
    return usageError(command, "<width> <height>");
  std::string error;
  if (!m_session->setFrameConfig(uint32_t(width), uint32_t(height), &error))
    return error;
  const auto wait = pumpUntilEvent(
      [](const Event &e) { return e.name == "FrameConfig"; }, deadline);
  if (wait != Wait::Done)
    return waitFailure(wait, "FrameConfig ack", deadline);
  return {};
}

CommandRunner::Failure CommandRunner::setEncodings(const Command &command)
{
  if (command.args.size() != 1)
    return usageError(command, "<name>[,<name>...]");
  std::vector<FrameEncoding> preferred;
  std::stringstream list(command.args[0]);
  std::string item;
  while (std::getline(list, item, ',')) {
    const auto wanted = lower(item);
    std::optional<FrameEncoding> match;
    for (auto encoding : {FrameEncoding::Raw, FrameEncoding::TurboJpeg}) {
      if (lower(toString(encoding)) == wanted)
        match = encoding;
    }
    if (!match)
      return "unknown frame encoding '" + item + "'; valid: raw, turbojpeg";
    preferred.push_back(*match);
  }
  std::string error;
  if (!m_session->setEncodings(preferred, &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::startRendering(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  std::string error;
  if (!m_session->startRendering(&error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::stopRendering(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  std::string error;
  if (!m_session->stopRendering(&error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::awaitFrame(
    const Command &command, Deadline deadline)
{
  unsigned long long count = 1;
  if (command.args.size() > 1
      || (!command.args.empty() && !parseNonNegative(command.args[0], count)))
    return usageError(command, "[count]");
  if (m_session->state() != SessionState::Connected)
    return std::string("not connected (") + toString(m_session->state()) + ")";
  size_t seen = 0;
  const auto wait = pumpUntilEvent(
      [&](const Event &e) {
        if (e.name == "Frame")
          ++seen;
        return seen >= count;
      },
      deadline);
  if (wait != Wait::Done) {
    return waitFailure(wait,
        "frame " + std::to_string(seen + 1) + " of " + std::to_string(count),
        deadline);
  }
  return {};
}

CommandRunner::Failure CommandRunner::saveFrame(const Command &command)
{
  if (command.args.size() != 1)
    return usageError(command, "<path.ppm>");
  if (!m_session->lastFrameHeader())
    return "no frame received yet";
  const auto view = decodeFrame(m_session->lastFrame());
  if (!view)
    return "the last frame does not decode";
  std::vector<uint8_t> pixels;
  if (!decodeFramePixels(*view, pixels)) {
    return std::string("cannot decode ") + toString(view->header.encoding)
        + " pixels in this build";
  }
  std::string error;
  if (!writePPM(command.args[0],
          view->header.width,
          view->header.height,
          pixels,
          error))
    return error;
  return {};
}

// Scene edits ////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::setParam(const Command &command)
{
  if (command.args.size() < 5)
    return usageError(command, "<type> <index> <name> <anariType> <value...>");
  SceneObjectRef ref;
  std::string error;
  if (!parseObjectRef(command.args[0], command.args[1], ref, error))
    return error;
  const auto valueType = parseAnariType(command.args[3]);
  if (!valueType)
    return "unknown ANARI type: " + command.args[3];
  Any value;
  const std::vector<std::string> tokens(
      command.args.begin() + 4, command.args.end());
  if (!anyFromTokens(*valueType, tokens, value, error))
    return error;
  if (!m_session->setParameter(ref, command.args[2], value, &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::removeParam(const Command &command)
{
  if (command.args.size() != 3)
    return usageError(command, "<type> <index> <name>");
  SceneObjectRef ref;
  std::string error;
  if (!parseObjectRef(command.args[0], command.args[1], ref, error))
    return error;
  if (!m_session->removeParameter(ref, command.args[2], &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::setNodeTransform(const Command &command)
{
  if (command.args.size() != 18)
    return usageError(command, "<layer> <node> <16 floats>");
  SceneNodeRef node;
  node.layerName = command.args[0];
  unsigned long long index = 0;
  if (!parseNonNegative(command.args[1], index))
    return "not a node index: " + command.args[1];
  node.nodeIndex = size_t(index);
  float values[16];
  for (size_t i = 0; i < 16; ++i) {
    double v = 0;
    if (!parseDouble(command.args[2 + i], v))
      return "not a number: " + command.args[2 + i];
    values[i] = float(v);
  }
  vsr::math::mat4 transform;
  std::memcpy(&transform, values, sizeof(values));
  std::string error;
  if (!m_session->setNodeTransform(node, transform, &error))
    return error;
  return drainEvents();
}

// Inspection /////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::dumpScene(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  if (const auto pending = drainEvents())
    return pending;
  forEachObjectPool(m_session->mirror().objectDB(),
      [&](anari::DataType type, const auto &pool) {
        vsr::core::foreach_item_const(pool, [&](const vsr::scene::Object *obj) {
          if (!obj)
            return;
          printRecord("EVT Object type=" + shortTypeName(type)
              + " index=" + std::to_string(obj->index()) + " subtype="
              + obj->subtype().str() + " name=" + quoted(obj->name())
              + " params=" + std::to_string(obj->numParameters()));
        });
      });
  return {};
}

CommandRunner::Failure CommandRunner::dumpLayers(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  if (const auto pending = drainEvents())
    return pending;
  const auto &scene = m_session->mirror();
  for (size_t i = 0; i < scene.numberOfLayers(); ++i) {
    const auto *layer = scene.layer(i);
    if (!layer)
      continue;
    printRecord("EVT Layer index=" + std::to_string(i) + " name="
        + quoted(layer->name()) + " nodes=" + std::to_string(layer->size())
        + " active=" + (scene.layerIsActive(layer->name()) ? "true" : "false"));
  }
  return {};
}

CommandRunner::Failure CommandRunner::dumpProject(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  if (const auto pending = drainEvents())
    return pending;
  const auto *project = m_session->project();
  if (!project)
    return "no Project Replica";
  printRecord("EVT Project name=" + quoted(project->name)
      + " activeShot=" + project->activeShotId
      + " shots=" + std::to_string(project->shots.size())
      + " datasets=" + std::to_string(project->datasets.size())
      + " lightRigs=" + std::to_string(project->lightRigs.size())
      + " cameraRigs=" + std::to_string(project->cameraRigs.size())
      + " colorMaps=" + std::to_string(project->colorMaps.size())
      + " dirty=" + boolText(project->dirty)
      + " directory=" + quoted(project->projectDirectory.generic_string()));
  for (const auto &shot : project->shots) {
    printRecord("EVT Shot id=" + shot.id + " name=" + quoted(shot.name)
        + " frameCount=" + std::to_string(shot.frameCount)
        + " fps=" + numberText(shot.fps) + " currentFrame="
        + std::to_string(shot.currentFrame) + " loop=" + boolText(shot.loop)
        + " lightRigId=" + shot.lightRigId + " cameraRigId=" + shot.cameraRigId
        + " bindings=" + std::to_string(shot.datasetBindings.size())
        + " camera=" + objectRefText(shot.camera)
        + " active=" + boolText(shot.id == project->activeShotId));
  }
  for (const auto &dataset : project->datasets) {
    printRecord("EVT Dataset id=" + dataset.id + " name=" + quoted(dataset.name)
        + " status=" + dataset::toString(dataset.status)
        + " residency=" + dataset::toString(dataset.residency)
        + " sourceKind=" + dataset::toString(dataset.sourceKind)
        + " importerType=" + dataset.importerType + " rootNode="
        + nodeText(dataset.rootNode) + " dirty=" + boolText(dataset.dirty));
  }
  for (const auto &rig : project->lightRigs) {
    printRecord("EVT LightRig id=" + rig.id + " name=" + quoted(rig.name)
        + " rootNode=" + nodeText(rig.rootNode));
  }
  for (const auto &rig : project->cameraRigs) {
    printRecord("EVT CameraRig id=" + rig.id + " name=" + quoted(rig.name)
        + " keyframes=" + std::to_string(rig.keyframes.size()));
  }
  for (const auto &map : project->colorMaps)
    printRecord("EVT ColorMap id=" + map.id + " name=" + quoted(map.name));
  return {};
}

CommandRunner::Failure CommandRunner::dumpFrame(const Command &command)
{
  if (!command.args.empty())
    return usageError(command, "");
  if (const auto pending = drainEvents())
    return pending;
  const auto &header = m_session->lastFrameHeader();
  if (!header)
    return "no frame received yet";
  const auto view = decodeFrame(m_session->lastFrame());
  printRecord("EVT " + frameEvent(*header, view ? view->size : 0).text());
  return {};
}

// Assertions /////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::assertValue(const Command &command)
{
  if (command.args.size() != 3)
    return usageError(command, "<value> <op> <rhs>");
  if (const auto pending = drainEvents())
    return pending;
  std::string error;
  const auto lhs = namedValue(command.args[0], error);
  if (!lhs)
    return error;
  bool holds = false;
  if (!compareValues(*lhs, command.args[1], command.args[2], holds, error))
    return error;
  if (!holds) {
    return command.args[0] + " is " + quoted(*lhs) + ", not " + command.args[1]
        + " " + quoted(command.args[2]);
  }
  return {};
}

std::optional<std::string> CommandRunner::namedValue(
    const std::string &name, std::string &error)
{
  const auto &scene = m_session->mirror();
  const auto *project = m_session->project();
  const auto &frame = m_session->lastFrameHeader();

  const auto needProject = [&]() -> std::optional<std::string> {
    error = name + ": no Project Replica";
    return {};
  };
  const auto needFrame = [&]() -> std::optional<std::string> {
    error = name + ": no frame received yet";
    return {};
  };

  if (name == "state")
    return std::string(toString(m_session->state()));
  if (name == "scene.objects")
    return std::to_string(totalObjects(scene));
  if (name == "scene.layers")
    return std::to_string(scene.numberOfLayers());
  if (name == "scene.cameras")
    return std::to_string(scene.numberOfObjects(ANARI_CAMERA));
  if (name == "scene.renderers")
    return std::to_string(scene.numberOfObjects(ANARI_RENDERER));
  if (name.rfind("project.", 0) == 0) {
    if (!project)
      return needProject();
    const auto field = name.substr(8);
    if (field == "activeShot")
      return project->activeShotId;
    if (field == "name")
      return project->name;
    if (field == "directory")
      return project->projectDirectory.generic_string();
    if (field == "dirty")
      return std::string(boolText(project->dirty));
    if (field == "shots")
      return std::to_string(project->shots.size());
    if (field == "datasets")
      return std::to_string(project->datasets.size());
    if (field == "lightRigs")
      return std::to_string(project->lightRigs.size());
    if (field == "cameraRigs")
      return std::to_string(project->cameraRigs.size());
    if (field == "colorMaps")
      return std::to_string(project->colorMaps.size());
  }
  if (name == "tasks.completed")
    return std::to_string(m_session->tasksCompleted());
  if (name == "tasks.failed")
    return std::to_string(m_session->tasksFailed());
  if (name == "replies.failed")
    return std::to_string(m_session->repliesFailed());
  if (name == "replies.pending")
    return std::to_string(m_pendingReplies.size());
  if (name == "snapshots.received")
    return std::to_string(m_session->snapshotsReceived());
  if (name == "browse.entries")
    return std::to_string(m_browseEntries.size());
  if (name.rfind("var.", 0) == 0) {
    // The variable itself, since `$name` in this position would expand to a
    // value name.
    const auto value = variable(name.substr(4));
    if (!value)
      error = name + ": unknown variable $" + name.substr(4);
    return value;
  }

  // <collection>.<id>.<field>: ids carry no dots, fields may.
  for (const char *collection :
      {"shot", "dataset", "lightRig", "cameraRig", "colorMap"}) {
    const std::string prefix = std::string(collection) + ".";
    if (name.rfind(prefix, 0) != 0)
      continue;
    const auto idEnd = name.find('.', prefix.size());
    if (idEnd == std::string::npos || idEnd + 1 >= name.size()) {
      error =
          "malformed value '" + name + "'; use " + collection + ".<id>.<field>";
      return {};
    }
    if (!project)
      return needProject();
    const auto id = name.substr(prefix.size(), idEnd - prefix.size());
    const auto field = name.substr(idEnd + 1);
    const auto missing = [&](const char *what) -> std::optional<std::string> {
      error = name + ": the replica has no " + what + " '" + id + "'";
      return {};
    };
    const auto noField = [&]() -> std::optional<std::string> {
      error = name + ": unknown " + collection + " field '" + field + "'";
      return {};
    };
    if (prefix == "shot.") {
      const auto *shot = project::findShot(*project, id);
      if (!shot)
        return missing("shot");
      if (field == "name")
        return shot->name;
      if (field == "frameCount")
        return std::to_string(shot->frameCount);
      if (field == "fps")
        return numberText(shot->fps);
      if (field == "currentFrame")
        return std::to_string(shot->currentFrame);
      if (field == "loop")
        return std::string(boolText(shot->loop));
      if (field == "playing")
        return std::string(boolText(shot->playing));
      if (field == "lightRigId")
        return shot->lightRigId;
      if (field == "cameraRigId")
        return shot->cameraRigId;
      if (field == "camera")
        return objectRefText(shot->camera);
      if (field == "bindings")
        return std::to_string(shot->datasetBindings.size());
      if (field == "renderSettings.width")
        return std::to_string(shot->renderSettings.width);
      if (field == "renderSettings.height")
        return std::to_string(shot->renderSettings.height);
      if (field == "renderSettings.samples")
        return std::to_string(shot->renderSettings.samples);
      if (field == "renderSettings.rendererLibrary")
        return shot->renderSettings.rendererLibrary;
      if (field == "renderSettings.rendererSubtype")
        return shot->renderSettings.rendererSubtype;
      if (field == "renderSettings.outputFilePrefix")
        return shot->renderSettings.outputFilePrefix;
      if (field.rfind("binding.", 0) == 0 && field.size() > 8) {
        const auto *binding = shot::findDatasetBinding(*shot, field.substr(8));
        if (!binding) {
          error = name + ": shot '" + id + "' has no binding for '"
              + field.substr(8) + "'";
          return {};
        }
        return std::string(boolText(binding->enabled));
      }
      return noField();
    }
    if (prefix == "dataset.") {
      const auto *dataset = project::findDataset(*project, id);
      if (!dataset)
        return missing("dataset");
      if (field == "name")
        return dataset->name;
      if (field == "status")
        return std::string(dataset::toString(dataset->status));
      if (field == "residency")
        return std::string(dataset::toString(dataset->residency));
      if (field == "sourceKind")
        return std::string(dataset::toString(dataset->sourceKind));
      if (field == "importerType")
        return dataset->importerType;
      if (field == "sourcePath")
        return dataset->source.sourcePath;
      if (field == "dirty")
        return std::string(boolText(dataset->dirty));
      if (field == "declared")
        return std::string(boolText(dataset->declared));
      if (field == "rootNode")
        return nodeText(dataset->rootNode);
      return noField();
    }
    if (field != "name")
      return noField();
    if (prefix == "lightRig.") {
      const auto *rig = light_rig::findLightRig(*project, id);
      return rig ? std::optional(rig->name) : missing("light rig");
    }
    if (prefix == "cameraRig.") {
      const auto *rig = camera_rig::findCameraRig(*project, id);
      return rig ? std::optional(rig->name) : missing("camera rig");
    }
    const auto *map = project::findColorMap(*project, id);
    return map ? std::optional(map->name) : missing("color map");
  }
  if (name == "frame.width")
    return frame ? std::optional(std::to_string(frame->width)) : needFrame();
  if (name == "frame.height")
    return frame ? std::optional(std::to_string(frame->height)) : needFrame();
  if (name == "frame.encoding") {
    return frame ? std::optional(std::string(toString(frame->encoding)))
                 : needFrame();
  }
  if (name == "frame.shotId")
    return frame ? std::optional(frame->shotId) : needFrame();
  if (name == "frame.frame")
    return frame ? std::optional(std::to_string(frame->frame)) : needFrame();
  if (name == "frames.received")
    return std::to_string(m_session->framesReceived());
  if (name == "frameConfig.width")
    return std::to_string(m_session->frameConfig().width);
  if (name == "frameConfig.height")
    return std::to_string(m_session->frameConfig().height);
  if (name == "errors.received")
    return std::to_string(m_session->errorsReceived());
  if (name == "lastError")
    return m_session->lastError();

  if (name.rfind("param.", 0) == 0) {
    // param.<type>.<index>.<name>; the parameter name may itself hold dots.
    const auto typeEnd = name.find('.', 6);
    const auto indexEnd =
        typeEnd == std::string::npos ? typeEnd : name.find('.', typeEnd + 1);
    if (typeEnd == std::string::npos || indexEnd == std::string::npos
        || indexEnd + 1 >= name.size()) {
      error = "malformed value '" + name + "'; use param.<type>.<index>.<name>";
      return {};
    }
    SceneObjectRef ref;
    if (!parseObjectRef(name.substr(6, typeEnd - 6),
            name.substr(typeEnd + 1, indexEnd - typeEnd - 1),
            ref,
            error))
      return {};
    const auto *obj = scene.getObject(ref.type, ref.objectIndex);
    if (!obj) {
      error = name + ": no " + shortTypeName(ref.type) + " "
          + std::to_string(ref.objectIndex) + " in the mirror";
      return {};
    }
    const auto paramName = name.substr(indexEnd + 1);
    const auto *param = obj->parameter(paramName.c_str());
    if (!param) {
      error = name + ": " + shortTypeName(ref.type) + " "
          + std::to_string(ref.objectIndex) + " has no parameter '" + paramName
          + "'";
      return {};
    }
    return anyText(param->value());
  }

  error = "unknown value '" + name + "'; valid: " + join(assertNames(), ", ");
  return {};
}

// Project Ops, Remote Browse and tasks /////////////////////////////////////

CommandRunner::Failure CommandRunner::executeRequest(
    const Command &command, Deadline deadline, bool &handled)
{
  handled = true;
  const auto &name = command.name;

  // Project
  if (name == "new-project")
    return bareRequest<NewProject>(command, deadline);
  if (name == "open-project")
    return openProject(command, deadline);
  if (name == "save-project")
    return saveProject(command, deadline);

  // Datasets
  if (name == "import-static-dataset")
    return importStaticDataset(command, deadline);
  if (name == "import-file-animation-dataset")
    return importFileAnimationDataset(command, deadline);
  if (name == "declare-file-animation-dataset")
    return declareFileAnimationDataset(command, deadline);
  if (name == "reimport-dataset") {
    return idRequest<ReimportDataset>(
        command, deadline, &ReimportDataset::datasetId, "id", taskStarted());
  }
  if (name == "rename-dataset") {
    return renameRequest<RenameDataset>(
        command, deadline, &RenameDataset::datasetId, "id");
  }
  if (name == "remove-dataset")
    return removeDataset(command, deadline);
  if (name == "load-dataset") {
    return idRequest<LoadDataset>(
        command, deadline, &LoadDataset::datasetId, "id", taskStarted());
  }
  if (name == "unload-dataset") {
    return idRequest<UnloadDataset>(
        command, deadline, &UnloadDataset::datasetId, "id");
  }
  if (name == "refresh-dataset-availability") {
    return idRequest<RefreshDatasetAvailability>(
        command, deadline, &RefreshDatasetAvailability::datasetId, "id");
  }
  if (name == "save-dataset-archive") {
    return saveArchiveRequest<SaveDatasetArchive>(
        command, deadline, &SaveDatasetArchive::datasetId, "id", taskStarted());
  }
  if (name == "load-dataset-archive") {
    return loadArchiveRequest<LoadDatasetArchive>(
        command, deadline, taskStarted());
  }
  if (name == "discover-dataset-candidates")
    return discoverDatasetCandidates(command, deadline);
  if (name == "incorporate-dataset-candidate")
    return incorporateDatasetCandidate(command, deadline);

  // Shots
  if (name == "create-shot") {
    return nameRequest<CreateShot>(command,
        deadline,
        &CreateShot::name,
        createdResult<ShotCreatedResult>(
            &ShotCreatedResult::shotId, "shotId", "lastShotId"));
  }
  if (name == "remove-shot")
    return idRequest<RemoveShot>(command, deadline, &RemoveShot::shotId, "id");
  if (name == "set-active-shot") {
    return idRequest<SetActiveShot>(
        command, deadline, &SetActiveShot::shotId, "id");
  }
  if (name == "update-shot")
    return updateShot(command, deadline);

  // Light rigs
  const auto lightRigCreated = [&] {
    return createdResult<LightRigCreatedResult>(
        &LightRigCreatedResult::lightRigId, "lightRigId", "lastLightRigId");
  };
  if (name == "create-light-rig") {
    return nameRequest<CreateLightRig>(
        command, deadline, &CreateLightRig::name, lightRigCreated());
  }
  if (name == "clone-light-rig") {
    return idRequest<CloneLightRig>(
        command, deadline, &CloneLightRig::lightRigId, "id", lightRigCreated());
  }
  if (name == "remove-light-rig") {
    return idRequest<RemoveLightRig>(
        command, deadline, &RemoveLightRig::lightRigId, "id");
  }
  if (name == "rename-light-rig") {
    return renameRequest<RenameLightRig>(
        command, deadline, &RenameLightRig::lightRigId, "id");
  }
  if (name == "add-light")
    return addLight(command, deadline);
  if (name == "remove-light")
    return removeLight(command, deadline);
  if (name == "save-light-rig-archive") {
    return saveArchiveRequest<SaveLightRigArchive>(
        command, deadline, &SaveLightRigArchive::lightRigId, "id");
  }
  if (name == "load-light-rig-archive") {
    return loadArchiveRequest<LoadLightRigArchive>(
        command, deadline, lightRigCreated());
  }

  // Camera rigs
  const auto cameraRigCreated = [&] {
    return createdResult<CameraRigCreatedResult>(
        &CameraRigCreatedResult::cameraRigId, "cameraRigId", "lastCameraRigId");
  };
  if (name == "create-camera-rig") {
    return nameRequest<CreateCameraRig>(
        command, deadline, &CreateCameraRig::name, cameraRigCreated());
  }
  if (name == "remove-camera-rig") {
    return idRequest<RemoveCameraRig>(
        command, deadline, &RemoveCameraRig::cameraRigId, "id");
  }
  if (name == "rename-camera-rig") {
    return renameRequest<RenameCameraRig>(
        command, deadline, &RenameCameraRig::cameraRigId, "id");
  }
  if (name == "save-camera-rig-archive") {
    return saveArchiveRequest<SaveCameraRigArchive>(
        command, deadline, &SaveCameraRigArchive::cameraRigId, "id");
  }
  if (name == "load-camera-rig-archive") {
    return loadArchiveRequest<LoadCameraRigArchive>(
        command, deadline, cameraRigCreated());
  }

  // Color maps
  if (name == "create-color-map") {
    return nameRequest<CreateColorMap>(command,
        deadline,
        &CreateColorMap::name,
        [this](const ProjectOpReply &reply,
            Event &event,
            std::vector<Event> &) -> Failure {
          const auto result = results<ColorMapCreatedResult>(reply);
          if (!result)
            return "the reply carries no ColorMapCreatedResult";
          event.fields.emplace_back("colorMapId", result->colorMapId);
          event.fields.emplace_back("object", objectRefText(result->object));
          m_variables["lastColorMapId"] = result->colorMapId;
          m_variables["lastObjectRef"] = objectRefText(result->object);
          m_variables["lastObjectType"] = shortTypeName(result->object.type);
          m_variables["lastObjectIndex"] =
              std::to_string(result->object.objectIndex);
          return {};
        });
  }
  if (name == "rename-color-map") {
    return renameRequest<RenameColorMap>(
        command, deadline, &RenameColorMap::colorMapId, "id");
  }
  if (name == "remove-color-map") {
    return idRequest<RemoveColorMap>(
        command, deadline, &RemoveColorMap::colorMapId, "id");
  }

  // Remote Browse and tasks
  if (name == "list-roots")
    return listRoots(command, deadline);
  if (name == "list-directory")
    return listDirectory(command, deadline);
  if (name == "cancel-task")
    return cancelTask(command, deadline);
  if (name == "await-task")
    return awaitTask(command, deadline);
  if (name == "await-snapshot")
    return awaitSnapshot(command, deadline);
  if (name == "await-reply")
    return awaitReply(command, deadline);

  handled = false;
  return {};
}

// Request shapes //

template <typename R>
CommandRunner::Failure CommandRunner::bareRequest(
    const Command &command, Deadline deadline, const Describe &describe)
{
  if (!command.args.empty())
    return usageError(command, "");
  return sendRequest(R{}, deadline, describe);
}

template <typename R>
CommandRunner::Failure CommandRunner::nameRequest(const Command &command,
    Deadline deadline,
    std::string R::*name,
    const Describe &describe)
{
  if (command.args.size() > 1)
    return usageError(command, "[name]");
  R request;
  if (!command.args.empty())
    request.*name = command.args[0];
  return sendRequest(std::move(request), deadline, describe);
}

template <typename R>
CommandRunner::Failure CommandRunner::idRequest(const Command &command,
    Deadline deadline,
    std::string R::*id,
    const char *idName,
    const Describe &describe)
{
  if (command.args.size() != 1)
    return usageError(command, ("<" + std::string(idName) + ">").c_str());
  R request;
  request.*id = command.args[0];
  return sendRequest(std::move(request), deadline, describe);
}

template <typename R>
CommandRunner::Failure CommandRunner::renameRequest(const Command &command,
    Deadline deadline,
    std::string R::*id,
    const char *idName)
{
  if (command.args.size() != 2) {
    return usageError(
        command, ("<" + std::string(idName) + "> <newName>").c_str());
  }
  R request;
  request.*id = command.args[0];
  request.newName = command.args[1];
  return sendRequest(std::move(request), deadline);
}

template <typename R>
CommandRunner::Failure CommandRunner::saveArchiveRequest(const Command &command,
    Deadline deadline,
    std::string R::*id,
    const char *idName,
    const Describe &describe)
{
  if (command.args.size() != 2)
    return usageError(
        command, ("<" + std::string(idName) + "> <file>").c_str());
  R request;
  request.*id = command.args[0];
  request.file = command.args[1];
  return sendRequest(std::move(request), deadline, describe);
}

template <typename R>
CommandRunner::Failure CommandRunner::loadArchiveRequest(
    const Command &command, Deadline deadline, const Describe &describe)
{
  if (command.args.size() != 1)
    return usageError(command, "<file>");
  R request;
  request.file = command.args[0];
  return sendRequest(std::move(request), deadline, describe);
}

// Project //

CommandRunner::Failure CommandRunner::openProject(
    const Command &command, Deadline deadline)
{
  if (command.args.size() != 1)
    return usageError(command, "<directory>");
  OpenProject request;
  request.directory = command.args[0];
  return sendRequest(std::move(request), deadline, taskStarted());
}

CommandRunner::Failure CommandRunner::saveProject(
    const Command &command, Deadline deadline)
{
  if (command.args.size() > 1)
    return usageError(command, "[directory]");
  SaveProject request;
  if (!command.args.empty())
    request.directory = std::filesystem::path(command.args[0]);
  return sendRequest(std::move(request), deadline, taskStarted());
}

// Datasets //

CommandRunner::Failure CommandRunner::importStaticDataset(
    const Command &command, Deadline deadline)
{
  if (command.args.empty() || command.args.size() > 3)
    return usageError(command, "<path> [name] [importer|VSR_SUBTREE]");
  ImportStaticDataset request;
  request.sourcePath = command.args[0];
  if (command.args.size() > 1)
    request.name = command.args[1];
  if (command.args.size() > 2) {
    if (upper(command.args[2]) == "VSR_SUBTREE") {
      request.fromSubtreeArchive = true;
    } else {
      const auto importer = parseImporter(command.args[2]);
      if (!importer)
        return "unknown importer '" + command.args[2] + "'";
      request.importerType = *importer;
    }
  }
  return sendRequest(std::move(request), deadline, taskStarted());
}

CommandRunner::Failure CommandRunner::importFileAnimationDataset(
    const Command &command, Deadline deadline)
{
  auto args = command.args;
  const auto frameCount = takeOption(args, "set-frame-count");
  if (args.size() < 3)
    return usageError(
        command, "<name> <importer> <path>... [set-frame-count=BOOL]");
  ImportFileAnimationDataset request;
  request.name = args[0];
  const auto importer = parseImporter(args[1]);
  if (!importer)
    return "unknown importer '" + args[1] + "'";
  request.importerType = *importer;
  for (size_t i = 2; i < args.size(); ++i)
    request.sourcePaths.emplace_back(args[i]);
  if (frameCount && !parseBool(*frameCount, request.setActiveShotFrameCount))
    return "set-frame-count must be true or false, got: " + *frameCount;
  return sendRequest(std::move(request), deadline, taskStarted());
}

CommandRunner::Failure CommandRunner::declareFileAnimationDataset(
    const Command &command, Deadline deadline)
{
  auto args = command.args;
  const auto frameCount = takeOption(args, "set-frame-count");
  if (args.size() < 3) {
    return usageError(
        command, "<name> <importer> <source>... [set-frame-count=BOOL]");
  }
  DeclareFileAnimationDataset request;
  request.name = args[0];
  const auto importer = parseImporter(args[1]);
  if (!importer)
    return "unknown importer '" + args[1] + "'";
  request.importerType = *importer;
  request.sourceList.assign(args.begin() + 2, args.end());
  if (frameCount && !parseBool(*frameCount, request.setActiveShotFrameCount))
    return "set-frame-count must be true or false, got: " + *frameCount;
  return sendRequest(std::move(request),
      deadline,
      createdResult<DatasetCreatedResult>(
          &DatasetCreatedResult::datasetId, "datasetId", "lastDatasetId"));
}

CommandRunner::Failure CommandRunner::removeDataset(
    const Command &command, Deadline deadline)
{
  if (command.args.empty() || command.args.size() > 2
      || (command.args.size() == 2 && command.args[1] != "keep-asset-file"))
    return usageError(command, "<id> [keep-asset-file]");
  RemoveDataset request;
  request.datasetId = command.args[0];
  request.keepAssetFile = command.args.size() == 2;
  return sendRequest(std::move(request), deadline);
}

CommandRunner::Failure CommandRunner::discoverDatasetCandidates(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return usageError(command, "");
  return sendRequest(DiscoverDatasetCandidates{},
      deadline,
      [](const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &following) -> Failure {
        const auto result = results<DiscoverDatasetCandidatesResult>(reply);
        if (!result)
          return "the reply carries no DiscoverDatasetCandidatesResult";
        event.fields.emplace_back(
            "candidates", std::to_string(result->candidates.size()));
        for (const auto &candidate : result->candidates) {
          Event entry{"DatasetCandidate", {}};
          entry.fields.emplace_back(
              "file", quoted(candidate.file.generic_string()));
          entry.fields.emplace_back(
              "proposedName", quoted(candidate.proposedName));
          following.push_back(std::move(entry));
        }
        return {};
      });
}

CommandRunner::Failure CommandRunner::incorporateDatasetCandidate(
    const Command &command, Deadline deadline)
{
  if (command.args.empty() || command.args.size() > 3)
    return usageError(command, "<file> [proposedName] [name]");
  IncorporateDatasetCandidate request;
  request.file = command.args[0];
  if (command.args.size() > 1)
    request.proposedName = command.args[1];
  if (command.args.size() > 2)
    request.name = command.args[2];
  return sendRequest(std::move(request), deadline, taskStarted());
}

// Shots and rigs //

CommandRunner::Failure CommandRunner::updateShot(
    const Command &command, Deadline deadline)
{
  if (command.args.size() < 2)
    return usageError(command, "<id> <field>=<value> [...]");
  std::string error;
  const auto *current = replicaShot(command.args[0], error);
  if (!current)
    return error;
  UpdateShot request;
  request.shot = *current;
  for (size_t i = 1; i < command.args.size(); ++i) {
    const auto &edit = command.args[i];
    const auto eq = edit.find('=');
    if (eq == std::string::npos || eq == 0)
      return "not a <field>=<value> edit: " + edit;
    if (!applyShotField(
            request.shot, edit.substr(0, eq), edit.substr(eq + 1), error))
      return error;
  }
  return sendRequest(std::move(request), deadline);
}

CommandRunner::Failure CommandRunner::addLight(
    const Command &command, Deadline deadline)
{
  if (command.args.empty() || command.args.size() > 2)
    return usageError(command, "<lightRigId> [subtype]");
  AddLightToRig request;
  request.lightRigId = command.args[0];
  if (command.args.size() > 1)
    request.subtype = command.args[1];
  return sendRequest(std::move(request),
      deadline,
      [this](const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &) -> Failure {
        const auto result = results<LightAddedResult>(reply);
        if (!result)
          return "the reply carries no LightAddedResult";
        event.fields.emplace_back("lightNode", nodeText(result->lightNode));
        m_variables["lastLightLayer"] = result->lightNode.layerName;
        m_variables["lastLightNode"] =
            std::to_string(result->lightNode.nodeIndex);
        return {};
      });
}

CommandRunner::Failure CommandRunner::removeLight(
    const Command &command, Deadline deadline)
{
  unsigned long long index = 0;
  if (command.args.size() != 3 || !parseNonNegative(command.args[2], index))
    return usageError(command, "<lightRigId> <layer> <nodeIndex>");
  RemoveLightFromRig request;
  request.lightRigId = command.args[0];
  request.lightNode.layerName = command.args[1];
  request.lightNode.nodeIndex = size_t(index);
  return sendRequest(std::move(request), deadline);
}

// Remote Browse //

CommandRunner::Failure CommandRunner::listRoots(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return usageError(command, "");
  return sendRequest(ListRoots{},
      deadline,
      [this](const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &following) -> Failure {
        const auto result = results<ListRootsResult>(reply);
        if (!result)
          return "the reply carries no ListRootsResult";
        event.fields.emplace_back(
            "roots", std::to_string(result->roots.size()));
        for (const auto &root : result->roots) {
          Event entry{"DataRoot", {}};
          entry.fields.emplace_back("path", quoted(root.generic_string()));
          following.push_back(std::move(entry));
        }
        if (!result->roots.empty())
          m_variables["dataRoot"] = result->roots.front().generic_string();
        return {};
      });
}

CommandRunner::Failure CommandRunner::listDirectory(
    const Command &command, Deadline deadline)
{
  if (command.args.size() != 1)
    return usageError(command, "<directory>");
  ListDirectory request;
  request.directory = command.args[0];
  m_browseEntries.clear();
  return sendRequest(std::move(request),
      deadline,
      [this](const ProjectOpReply &reply,
          Event &event,
          std::vector<Event> &following) -> Failure {
        const auto result = results<ListDirectoryResult>(reply);
        if (!result)
          return "the reply carries no ListDirectoryResult";
        m_browseEntries = result->entries;
        event.fields.emplace_back(
            "entries", std::to_string(result->entries.size()));
        for (const auto &e : result->entries) {
          Event entry{"DirectoryEntry", {}};
          entry.fields.emplace_back("name", quoted(e.name));
          entry.fields.emplace_back("kind", toString(e.kind));
          entry.fields.emplace_back("size", std::to_string(e.size));
          entry.fields.emplace_back("mtime", std::to_string(e.mtimeSeconds));
          following.push_back(std::move(entry));
        }
        return {};
      });
}

// Tasks //

CommandRunner::Failure CommandRunner::cancelTask(
    const Command &command, Deadline deadline)
{
  unsigned long long taskId = 0;
  if (command.args.size() != 1 || !parseNonNegative(command.args[0], taskId))
    return usageError(command, "<taskId>");
  CancelTask request;
  request.taskId = taskId;
  return sendRequest(std::move(request), deadline);
}

CommandRunner::Failure CommandRunner::awaitTask(
    const Command &command, Deadline deadline)
{
  if (m_noWait)
    return "no-wait applies to request commands, not to await-task";
  auto args = command.args;
  if (!args.empty() && args.back() == "expect-fail") {
    m_expectFail = true;
    args.pop_back();
  }
  unsigned long long taskId = 0;
  if (args.size() > 1 || (!args.empty() && !parseNonNegative(args[0], taskId)))
    return usageError(command, "[taskId] [expect-fail]");
  if (args.empty()) {
    const auto *last = m_variables.at("lastTaskId");
    if (!last)
      return "no task has been started yet ($lastTaskId is unset)";
    parseNonNegative(*last, taskId);
  }
  if (m_session->state() != SessionState::Connected)
    return std::string("not connected (") + toString(m_session->state()) + ")";

  const auto ended = [&] {
    const auto *task = m_session->task(taskId);
    return task && task->status != TaskRecord::Status::Running;
  };
  const auto wait = pumpUntil(ended, deadline);
  if (wait != Wait::Done) {
    return waitFailure(
        wait, "the end of task " + std::to_string(taskId), deadline);
  }
  const auto &task = *m_session->task(taskId);
  const bool failed = task.status == TaskRecord::Status::Failed;
  if (failed && !m_expectFail)
    return "task " + std::to_string(taskId) + " failed: " + task.message;
  if (!failed && m_expectFail) {
    return "task " + std::to_string(taskId)
        + " completed, but was expected to fail";
  }
  // Imports report the new dataset's id as their completion message.
  if (!failed && !task.message.empty())
    m_variables["lastDatasetId"] = task.message;
  return {};
}

CommandRunner::Failure CommandRunner::awaitSnapshot(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return usageError(command, "");
  if (m_session->state() != SessionState::Connected)
    return std::string("not connected (") + toString(m_session->state()) + ")";
  const auto wait =
      pumpUntil([&] { return m_session->snapshotsReceived() > m_snapshotMark; },
          deadline);
  if (wait != Wait::Done)
    return waitFailure(wait, "ProjectSnapshot", deadline);
  // The next await-snapshot waits for the one after this.
  m_snapshotMark = m_session->snapshotsReceived();
  return {};
}

CommandRunner::Failure CommandRunner::awaitReply(
    const Command &command, Deadline deadline)
{
  if (m_noWait)
    return "no-wait applies to request commands, not to await-reply";
  unsigned long long requestId = 0;
  if (command.args.size() > 1
      || (!command.args.empty()
          && !parseNonNegative(command.args[0], requestId)))
    return usageError(command, "[requestId]");
  if (m_pendingReplies.empty())
    return "no no-wait request is awaiting its reply";
  auto pending = m_pendingReplies.begin();
  if (!command.args.empty()) {
    pending = std::find_if(m_pendingReplies.begin(),
        m_pendingReplies.end(),
        [&](const auto &p) { return p.first == requestId; });
    if (pending == m_pendingReplies.end()) {
      return "request " + std::to_string(requestId)
          + " was not sent with no-wait, or its reply was collected already";
    }
  }
  const auto id = pending->first;
  const auto describe = std::move(pending->second);
  m_pendingReplies.erase(pending);
  return awaitReply(id, deadline, describe);
}

// The plumbing every request shares //

template <typename R>
CommandRunner::Failure CommandRunner::sendRequest(
    R request, Deadline deadline, const Describe &describe)
{
  request.requestId = m_session->nextRequestId();
  m_variables["lastRequestId"] = std::to_string(request.requestId);
  // Until the reply is collected (at once, or by await-reply under no-wait)
  // the best mark is the count as the request goes out.
  m_snapshotMark = m_session->snapshotsReceived();
  std::string error;
  if (!m_session->send(request, &error))
    return error;
  if (m_noWait) {
    m_pendingReplies.emplace_back(request.requestId, describe);
    return drainEvents();
  }
  return awaitReply(request.requestId, deadline, describe);
}

CommandRunner::Failure CommandRunner::awaitReply(
    uint64_t requestId, Deadline deadline, const Describe &describe)
{
  const auto idText = std::to_string(requestId);
  std::vector<Event> following;
  Failure described;

  // Runs `describe` on an ok reply, so the reply's record shows the results.
  const auto decorate = [&](const ProjectOpReply &reply, Event &event) {
    if (reply.ok && describe)
      described = describe(reply, event, following);
  };

  const auto *reply = m_session->reply(requestId);
  if (reply) {
    // Already consumed and printed (a no-wait reply an earlier command
    // drained): the results are decoded, the record is not repeated.
    Event event{"ProjectOpReply", {}};
    decorate(*reply, event);
  } else {
    const auto wait = pumpUntilEvent(
        [&](Event &event) {
          if (event.name != "ProjectOpReply")
            return false;
          for (const auto &[key, value] : event.fields) {
            if (key == "requestId" && value == idText) {
              reply = m_session->reply(requestId);
              if (reply)
                decorate(*reply, event);
              return true;
            }
          }
          return false;
        },
        deadline);
    if (wait != Wait::Done)
      return waitFailure(wait, "the reply to request " + idText, deadline);
    if (!reply)
      return "the reply to request " + idText + " did not decode";
  }
  // A snapshot this request caused follows its reply; whatever arrived up to
  // the reply belongs to earlier requests.
  m_snapshotMark =
      m_session->snapshotsAtReply(requestId).value_or(m_snapshotMark);
  for (const auto &event : following)
    printEvent(event);

  if (!reply->ok && !m_expectFail)
    return "server refused: " + reply->error;
  if (reply->ok && m_expectFail)
    return "expected the request to fail, but the server accepted it";
  return described;
}

template <typename Result>
CommandRunner::Describe CommandRunner::createdResult(
    std::string Result::*id, const char *key, const char *variable)
{
  return [this, id, key, variable](const ProjectOpReply &reply,
             Event &event,
             std::vector<Event> &) -> Failure {
    const auto result = results<Result>(reply);
    if (!result)
      return std::string("the reply carries no ") + key + " result";
    event.fields.emplace_back(key, (*result).*id);
    m_variables[variable] = (*result).*id;
    return {};
  };
}

CommandRunner::Describe CommandRunner::taskStarted()
{
  return [this](const ProjectOpReply &reply,
             Event &event,
             std::vector<Event> &) -> Failure {
    const auto started = results<TaskStartedResult>(reply);
    if (!started)
      return "the reply carries no TaskStartedResult";
    event.fields.emplace_back("taskId", std::to_string(started->taskId));
    m_variables["lastTaskId"] = std::to_string(started->taskId);
    return {};
  };
}

std::optional<std::string> CommandRunner::variable(
    const std::string &name) const
{
  const auto *value = m_variables.at(name);
  if (!value)
    return {};
  return *value;
}

const Shot *CommandRunner::replicaShot(
    const std::string &id, std::string &error) const
{
  const auto *project = m_session->project();
  if (!project) {
    error = "no Project Replica";
    return nullptr;
  }
  const auto *shot = project::findShot(*project, id);
  if (!shot)
    error = "the replica has no shot '" + id + "'";
  return shot;
}

// Pumping and output /////////////////////////////////////////////////////////

CommandRunner::Wait CommandRunner::pumpUntil(
    const std::function<bool()> &done, Deadline deadline, LossEnds lossEnds)
{
  // Every event is taken as one, so the `done` test and the Error and Lost
  // checks see the same picture.
  return pumpUntilEvent(
      [&](const Event &) { return false; }, deadline, nullptr, lossEnds, done);
}

CommandRunner::Wait CommandRunner::pumpUntilEvent(
    const std::function<bool(Event &)> &accept,
    Deadline deadline,
    Event *matched,
    LossEnds lossEnds,
    const std::function<bool()> &done)
{
  // A wait that starts in Lost is not ended by it (await-lost, sleep after a
  // loss); one that watches the link go is.
  const bool wasLost = m_session->state() == SessionState::Lost;
  Wait wait = Wait::TimedOut;
  m_session->pollUntil(
      [&] {
        Event event;
        while (m_session->takeEvent(event)) {
          // accept() first: it may add the fields the record then shows.
          const bool accepted = accept(event);
          printEvent(event);
          if (accepted) {
            if (matched)
              *matched = std::move(event);
            wait = Wait::Done;
            return true;
          }
          if (event.name == "Error") {
            // Not what this command waited for: the server is objecting to
            // something, and the rest of the queue is the next command's.
            wait = Wait::Error;
            return true;
          }
        }
        if (done && done()) {
          wait = Wait::Done;
          return true;
        }
        if (lossEnds == LossEnds::Wait && !wasLost
            && m_session->state() == SessionState::Lost) {
          wait = Wait::Lost;
          return true;
        }
        return false;
      },
      deadline);
  return wait;
}

std::string CommandRunner::waitFailure(
    Wait wait, const std::string &awaited, Deadline deadline) const
{
  switch (wait) {
  case Wait::Lost:
    return "connection lost while waiting for " + awaited + ": "
        + m_session->failure();
  case Wait::Error:
    return "server answered Error " + quoted(m_session->lastError())
        + " while waiting for " + awaited;
  case Wait::TimedOut:
  case Wait::Done:
    break;
  }
  return "no " + awaited + " within " + std::to_string(deadline.count())
      + " ms";
}

CommandRunner::Failure CommandRunner::drainEvents()
{
  m_session->poll();
  Event event;
  Failure failure;
  while (m_session->takeEvent(event)) {
    printEvent(event);
    if (event.name == "Error" && !failure && !event.fields.empty())
      failure = "server answered Error " + event.fields.front().second;
  }
  return failure;
}

void CommandRunner::printEvent(const Event &event)
{
  if (m_options.quietEvents)
    return;
  printRecord("EVT " + event.text());
}

void CommandRunner::printRecord(const std::string &line)
{
  if (m_out)
    *m_out << line << '\n' << std::flush;
}

} // namespace vsr::scivis_studio::test_client
