// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CommandRunner.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
#include "PayloadCommon.h"
// vsr_scivis_studio_model
#include "Project.h"
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
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ostream>
#include <sstream>
#include <thread>

namespace vsr::scivis_studio::test_client {

using namespace protocol;
using vsr::core::Any;

namespace {

constexpr std::chrono::milliseconds POLL_INTERVAL{1};

// Every object pool of a Scene, the order dump-scene lists them in.
constexpr anari::DataType OBJECT_TYPES[] = {ANARI_ARRAY,
    ANARI_SURFACE,
    ANARI_GEOMETRY,
    ANARI_MATERIAL,
    ANARI_SAMPLER,
    ANARI_VOLUME,
    ANARI_SPATIAL_FIELD,
    ANARI_LIGHT,
    ANARI_CAMERA,
    ANARI_RENDERER};

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

bool parseInteger(const std::string &text, long long &out)
{
  if (text.empty())
    return false;
  errno = 0;
  char *end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0')
    return false;
  out = value;
  return true;
}

bool parseUnsigned(const std::string &text, unsigned long long &out)
{
  long long value = 0;
  if (!parseInteger(text, value) || value < 0)
    return false;
  out = static_cast<unsigned long long>(value);
  return true;
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

// Scalars print the way std::ostream prints them (6 significant digits), so a
// float32 0.9 reads back as "0.9".
template <typename T>
std::string numberText(T value)
{
  std::ostringstream out;
  if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>)
    out << int(value);
  else
    out << value;
  return out.str();
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

template <typename T>
bool storeInteger(const std::string &text, std::byte *dst)
{
  long long value = 0;
  if (!parseInteger(text, value))
    return false;
  const T typed = static_cast<T>(value);
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

bool storeComponent(Component component, const std::string &text, std::byte *dst)
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
    if (!storeComponent(
            component->component, tokens[i], bytes.data() + i * component->size)) {
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
    return shortTypeName(type) + ":"
        + std::to_string(value.getAsObjectIndex());
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
    error = "unknown operator '" + op
        + "'; valid: == != < <= > >= contains";
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
  if (!parseUnsigned(indexText, index)) {
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
      char *end = nullptr;
      const std::string pair = token.substr(j, 2);
      const long value = std::strtol(pair.c_str(), &end, 16);
      if (*end != '\0') {
        error = "not a hex byte: " + pair;
        return false;
      }
      out.push_back(std::byte(value));
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

std::string argCountError(const Command &command, const char *usage)
{
  return "usage: " + command.name + " " + usage;
}

const char *frameEncodingName(FrameEncoding encoding)
{
  return toString(encoding);
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
  drainEvents();
  return ok;
}

bool CommandRunner::runCommand(const Command &command)
{
  const auto outcome = execute(command);
  if (outcome) {
    ++m_failures;
    printRecord("FAIL " + command.text + ": " + *outcome);
    return false;
  }
  printRecord("OK " + command.text);
  return true;
}

size_t CommandRunner::failures() const
{
  return m_failures;
}

const std::vector<std::string> &CommandRunner::assertNames()
{
  static const std::vector<std::string> names = {"state",
      "scene.objects",
      "scene.layers",
      "scene.cameras",
      "scene.renderers",
      "project.activeShot",
      "project.shots",
      "project.datasets",
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
      "ping                         send Ping, await Pong",
      "await-lost                   wait until the connection is Lost",
      "reconnect                    connect again to the last host and port",
      "sleep MS                     keep polling for MS milliseconds",
      "expect-error [SUBSTRING]     the next server message must be an Error",
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
      "Waiting commands take a trailing timeout=MS. assert values:"};
  // clang-format on
  return lines;
}

CommandRunner::Outcome CommandRunner::execute(Command command)
{
  std::string error;
  const auto suffix = takeTimeoutSuffix(command, &error);
  if (!error.empty())
    return error;
  const Deadline deadline = suffix ? *suffix : m_options.timeout;

  const auto &name = command.name;
  if (name == "connect")
    return connect(command, deadline);
  if (name == "disconnect")
    return disconnect(command);
  if (name == "shutdown")
    return shutdown(command, deadline);
  if (name == "ping")
    return ping(command, deadline);
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

CommandRunner::Outcome CommandRunner::connect(
    const Command &command, Deadline deadline)
{
  if (command.args.size() > 2)
    return argCountError(command, "[host] [port]");
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
  drainEvents();
  if (!ok)
    return error;
  return {};
}

CommandRunner::Outcome CommandRunner::disconnect(const Command &command)
{
  if (!command.args.empty())
    return argCountError(command, "");
  if (m_session->state() != SessionState::Connected
      && m_session->state() != SessionState::Lost)
    return std::string("not connected (") + toString(m_session->state()) + ")";
  m_session->disconnect();
  drainEvents();
  return {};
}

CommandRunner::Outcome CommandRunner::shutdown(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return argCountError(command, "");
  std::string error;
  // Events keep flowing while the socket is still open; print them after.
  const bool ok = m_session->shutdown(deadline, &error);
  drainEvents();
  if (!ok)
    return error;
  return {};
}

CommandRunner::Outcome CommandRunner::ping(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return argCountError(command, "");
  std::string error;
  if (!m_session->ping(&error))
    return error;
  if (!pumpUntilEvent(
          [](const Event &e) { return e.name == "Pong"; }, deadline)) {
    return "no Pong within " + std::to_string(deadline.count()) + " ms";
  }
  return {};
}

CommandRunner::Outcome CommandRunner::awaitLost(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return argCountError(command, "");
  if (!pumpUntil(
          [&] { return m_session->state() == SessionState::Lost; }, deadline)) {
    return std::string("still ") + toString(m_session->state()) + " after "
        + std::to_string(deadline.count()) + " ms";
  }
  return {};
}

CommandRunner::Outcome CommandRunner::reconnect(
    const Command &command, Deadline deadline)
{
  if (!command.args.empty())
    return argCountError(command, "");
  if (m_session->state() == SessionState::Connected)
    return "already connected";
  std::string error;
  const bool ok = m_session->reconnect(deadline, &error);
  drainEvents();
  if (!ok)
    return error;
  return {};
}

CommandRunner::Outcome CommandRunner::sleep(const Command &command)
{
  unsigned long long ms = 0;
  if (command.args.size() != 1 || !parseUnsigned(command.args[0], ms))
    return argCountError(command, "<ms>");
  pumpUntil([] { return false; }, std::chrono::milliseconds(ms));
  return {};
}

CommandRunner::Outcome CommandRunner::expectError(
    const Command &command, Deadline deadline)
{
  if (command.args.size() > 1)
    return argCountError(command, "[substring]");
  Event next;
  if (!pumpUntilEvent([](const Event &) { return true; }, deadline, &next)) {
    return "no server message within " + std::to_string(deadline.count())
        + " ms";
  }
  if (next.name != "Error")
    return "expected Error, got " + next.text();
  if (!command.args.empty()
      && m_session->lastError().find(command.args[0]) == std::string::npos) {
    return "Error " + quoted(m_session->lastError()) + " does not contain "
        + quoted(command.args[0]);
  }
  return {};
}

CommandRunner::Outcome CommandRunner::sendRaw(const Command &command)
{
  unsigned long long type = 0;
  if (command.args.empty() || !parseUnsigned(command.args[0], type)
      || type > 0xff)
    return argCountError(command, "<typeByte 0..255> [hex bytes...]");
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

CommandRunner::Outcome CommandRunner::setFrameConfig(
    const Command &command, Deadline deadline)
{
  unsigned long long width = 0;
  unsigned long long height = 0;
  if (command.args.size() != 2 || !parseUnsigned(command.args[0], width)
      || !parseUnsigned(command.args[1], height))
    return argCountError(command, "<width> <height>");
  std::string error;
  if (!m_session->setFrameConfig(uint32_t(width), uint32_t(height), &error))
    return error;
  if (!pumpUntilEvent(
          [](const Event &e) { return e.name == "FrameConfig"; }, deadline)) {
    return "no FrameConfig ack within " + std::to_string(deadline.count())
        + " ms";
  }
  return {};
}

CommandRunner::Outcome CommandRunner::setEncodings(const Command &command)
{
  if (command.args.size() != 1)
    return argCountError(command, "<name>[,<name>...]");
  std::vector<FrameEncoding> preferred;
  std::stringstream list(command.args[0]);
  std::string item;
  while (std::getline(list, item, ',')) {
    const auto wanted = lower(item);
    std::optional<FrameEncoding> match;
    for (auto encoding : {FrameEncoding::Raw, FrameEncoding::TurboJpeg}) {
      if (lower(frameEncodingName(encoding)) == wanted)
        match = encoding;
    }
    if (!match)
      return "unknown frame encoding '" + item + "'; valid: raw, turbojpeg";
    preferred.push_back(*match);
  }
  std::string error;
  if (!m_session->setEncodings(preferred, &error))
    return error;
  drainEvents();
  return {};
}

CommandRunner::Outcome CommandRunner::startRendering(const Command &command)
{
  if (!command.args.empty())
    return argCountError(command, "");
  std::string error;
  if (!m_session->startRendering(&error))
    return error;
  drainEvents();
  return {};
}

CommandRunner::Outcome CommandRunner::stopRendering(const Command &command)
{
  if (!command.args.empty())
    return argCountError(command, "");
  std::string error;
  if (!m_session->stopRendering(&error))
    return error;
  drainEvents();
  return {};
}

CommandRunner::Outcome CommandRunner::awaitFrame(
    const Command &command, Deadline deadline)
{
  unsigned long long count = 1;
  if (command.args.size() > 1
      || (!command.args.empty() && !parseUnsigned(command.args[0], count)))
    return argCountError(command, "[count]");
  if (m_session->state() != SessionState::Connected)
    return std::string("not connected (") + toString(m_session->state()) + ")";
  size_t seen = 0;
  if (!pumpUntilEvent(
          [&](const Event &e) {
            if (e.name == "Frame")
              ++seen;
            return seen >= count;
          },
          deadline)) {
    return std::to_string(seen) + " of " + std::to_string(count)
        + " frame(s) within " + std::to_string(deadline.count()) + " ms";
  }
  return {};
}

CommandRunner::Outcome CommandRunner::saveFrame(const Command &command)
{
  if (command.args.size() != 1)
    return argCountError(command, "<path.ppm>");
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

CommandRunner::Outcome CommandRunner::setParam(const Command &command)
{
  if (command.args.size() < 5)
    return argCountError(command, "<type> <index> <name> <anariType> <value...>");
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
  drainEvents();
  return {};
}

CommandRunner::Outcome CommandRunner::removeParam(const Command &command)
{
  if (command.args.size() != 3)
    return argCountError(command, "<type> <index> <name>");
  SceneObjectRef ref;
  std::string error;
  if (!parseObjectRef(command.args[0], command.args[1], ref, error))
    return error;
  if (!m_session->removeParameter(ref, command.args[2], &error))
    return error;
  drainEvents();
  return {};
}

CommandRunner::Outcome CommandRunner::setNodeTransform(const Command &command)
{
  if (command.args.size() != 18)
    return argCountError(command, "<layer> <node> <16 floats>");
  SceneNodeRef node;
  node.layerName = command.args[0];
  unsigned long long index = 0;
  if (!parseUnsigned(command.args[1], index))
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
  drainEvents();
  return {};
}

// Inspection /////////////////////////////////////////////////////////////////

CommandRunner::Outcome CommandRunner::dumpScene(const Command &command)
{
  if (!command.args.empty())
    return argCountError(command, "");
  drainEvents();
  const auto &scene = m_session->mirror();
  const auto &db = scene.objectDB();
  const auto dump = [&](anari::DataType type, const auto &pool) {
    vsr::core::foreach_item_const(pool, [&](const vsr::scene::Object *obj) {
      if (!obj)
        return;
      printRecord("EVT Object type=" + shortTypeName(type)
          + " index=" + std::to_string(obj->index()) + " subtype="
          + obj->subtype().str() + " name=" + quoted(obj->name())
          + " params=" + std::to_string(obj->numParameters()));
    });
  };
  dump(ANARI_ARRAY, db.array);
  dump(ANARI_SURFACE, db.surface);
  dump(ANARI_GEOMETRY, db.geometry);
  dump(ANARI_MATERIAL, db.material);
  dump(ANARI_SAMPLER, db.sampler);
  dump(ANARI_VOLUME, db.volume);
  dump(ANARI_SPATIAL_FIELD, db.field);
  dump(ANARI_LIGHT, db.light);
  dump(ANARI_CAMERA, db.camera);
  dump(ANARI_RENDERER, db.renderer);
  return {};
}

CommandRunner::Outcome CommandRunner::dumpLayers(const Command &command)
{
  if (!command.args.empty())
    return argCountError(command, "");
  drainEvents();
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

CommandRunner::Outcome CommandRunner::dumpProject(const Command &command)
{
  if (!command.args.empty())
    return argCountError(command, "");
  drainEvents();
  const auto *project = m_session->project();
  if (!project)
    return "no Project Replica";
  printRecord("EVT Project name=" + quoted(project->name)
      + " activeShot=" + project->activeShotId
      + " shots=" + std::to_string(project->shots.size())
      + " datasets=" + std::to_string(project->datasets.size())
      + " lightRigs=" + std::to_string(project->lightRigs.size())
      + " cameraRigs=" + std::to_string(project->cameraRigs.size())
      + " dirty=" + (project->dirty ? "true" : "false"));
  return {};
}

CommandRunner::Outcome CommandRunner::dumpFrame(const Command &command)
{
  if (!command.args.empty())
    return argCountError(command, "");
  drainEvents();
  const auto &header = m_session->lastFrameHeader();
  if (!header)
    return "no frame received yet";
  printRecord("EVT Frame width=" + std::to_string(header->width)
      + " height=" + std::to_string(header->height)
      + " encoding=" + toString(header->encoding)
      + " pixelFormat=" + toString(header->pixelFormat)
      + " shotId=" + header->shotId + " frame=" + std::to_string(header->frame)
      + " bytes=" + std::to_string(m_session->lastFrame().payload.size()));
  return {};
}

// Assertions /////////////////////////////////////////////////////////////////

CommandRunner::Outcome CommandRunner::assertValue(const Command &command)
{
  if (command.args.size() != 3)
    return argCountError(command, "<value> <op> <rhs>");
  drainEvents();
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
  if (name == "scene.objects") {
    size_t n = 0;
    for (auto type : OBJECT_TYPES)
      n += scene.numberOfObjects(type);
    return std::to_string(n);
  }
  if (name == "scene.layers")
    return std::to_string(scene.numberOfLayers());
  if (name == "scene.cameras")
    return std::to_string(scene.numberOfObjects(ANARI_CAMERA));
  if (name == "scene.renderers")
    return std::to_string(scene.numberOfObjects(ANARI_RENDERER));
  if (name == "project.activeShot")
    return project ? std::optional(project->activeShotId) : needProject();
  if (name == "project.shots") {
    return project ? std::optional(std::to_string(project->shots.size()))
                   : needProject();
  }
  if (name == "project.datasets") {
    return project ? std::optional(std::to_string(project->datasets.size()))
                   : needProject();
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

// Pumping and output /////////////////////////////////////////////////////////

bool CommandRunner::pumpUntil(
    const std::function<bool()> &done, Deadline deadline)
{
  const auto end = std::chrono::steady_clock::now() + deadline;
  while (true) {
    m_session->poll();
    drainEvents();
    if (done())
      return true;
    if (std::chrono::steady_clock::now() >= end)
      return false;
    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

bool CommandRunner::pumpUntilEvent(
    const std::function<bool(const Event &)> &accept,
    Deadline deadline,
    Event *matched)
{
  const auto end = std::chrono::steady_clock::now() + deadline;
  while (true) {
    m_session->poll();
    Event event;
    while (m_session->takeEvent(event)) {
      printEvent(event);
      if (accept(event)) {
        if (matched)
          *matched = std::move(event);
        return true;
      }
    }
    if (std::chrono::steady_clock::now() >= end)
      return false;
    std::this_thread::sleep_for(POLL_INTERVAL);
  }
}

void CommandRunner::drainEvents()
{
  m_session->poll();
  Event event;
  while (m_session->takeEvent(event))
    printEvent(event);
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
