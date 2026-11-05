// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/importers/detail/AnariCaptureText.hpp"
// vsr_core
#include "vsr/core/HalfFloat.hpp"
// std
#include <charconv>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace vsr::io::detail::anari_capture {

namespace {

// Text helpers ///////////////////////////////////////////////////////////////

std::string_view trim(std::string_view s)
{
  const auto *ws = " \t\r";
  const auto b = s.find_first_not_of(ws);
  if (b == std::string_view::npos)
    return {};
  const auto e = s.find_last_not_of(ws);
  return s.substr(b, e - b + 1);
}

// Splits off the first space-separated word; `rest` is what follows, trimmed.
std::string_view firstWord(std::string_view line, std::string_view &rest)
{
  const auto sp = line.find(' ');
  if (sp == std::string_view::npos) {
    rest = {};
    return line;
  }
  rest = trim(line.substr(sp + 1));
  return line.substr(0, sp);
}

std::vector<std::string_view> splitTuple(std::string_view text)
{
  text = trim(text);
  if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
    text = text.substr(1, text.size() - 2);
  std::vector<std::string_view> parts;
  while (true) {
    const auto comma = text.find(',');
    parts.push_back(trim(text.substr(0, comma)));
    if (comma == std::string_view::npos)
      break;
    text = text.substr(comma + 1);
  }
  return parts;
}

bool isArrayLabel(std::string_view label)
{
  return label == "array1d" || label == "array2d" || label == "array3d";
}

// Component decoding /////////////////////////////////////////////////////////

template <typename T>
bool parseInteger(std::string_view s, std::byte *out)
{
  T v{};
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  if (r.ec != std::errc() || r.ptr != s.data() + s.size())
    return false;
  std::memcpy(out, &v, sizeof(T));
  return true;
}

template <typename T>
bool parseFloat(std::string_view s, T &v)
{
  // from_chars reads "nan", "inf" and "-inf" but not a leading '+', which the
  // capture never writes.
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

bool parseComponent(std::string_view cType,
    anari::DataType type,
    std::string_view s,
    std::byte *out)
{
  // Half floats store as uint16_t but are written as decimals.
  if (type == ANARI_FLOAT16 || type == ANARI_FLOAT16_VEC2
      || type == ANARI_FLOAT16_VEC3 || type == ANARI_FLOAT16_VEC4) {
    float f = 0.f;
    if (!parseFloat(s, f))
      return false;
    const uint16_t h = vsr::core::floatToHalf(f);
    std::memcpy(out, &h, sizeof(h));
    return true;
  }
  if (cType == "float") {
    float f = 0.f;
    if (!parseFloat(s, f))
      return false;
    std::memcpy(out, &f, sizeof(f));
    return true;
  }
  if (cType == "double") {
    double d = 0.0;
    if (!parseFloat(s, d))
      return false;
    std::memcpy(out, &d, sizeof(d));
    return true;
  }
  if (cType == "int8_t")
    return parseInteger<int8_t>(s, out);
  if (cType == "uint8_t")
    return parseInteger<uint8_t>(s, out);
  if (cType == "int16_t")
    return parseInteger<int16_t>(s, out);
  if (cType == "uint16_t")
    return parseInteger<uint16_t>(s, out);
  if (cType == "int32_t")
    return parseInteger<int32_t>(s, out);
  if (cType == "uint32_t")
    return parseInteger<uint32_t>(s, out);
  if (cType == "int64_t")
    return parseInteger<int64_t>(s, out);
  if (cType == "uint64_t")
    return parseInteger<uint64_t>(s, out);
  return false;
}

// Block reading //////////////////////////////////////////////////////////////

// Hands out a capture's lines, trimmed, and counts them for error messages.
struct LineReader
{
  bool next(std::string_view &line);
  bool nextContent(std::string_view &line); // the next line that is not blank
  std::string where() const;

  std::string_view text;
  size_t lineNumber{0};
};

bool LineReader::next(std::string_view &line)
{
  if (text.empty())
    return false;
  const auto nl = text.find('\n');
  line = trim(text.substr(0, nl));
  text =
      nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
  lineNumber++;
  return true;
}

bool LineReader::nextContent(std::string_view &line)
{
  while (next(line)) {
    if (!line.empty())
      return true;
  }
  return false;
}

std::string LineReader::where() const
{
  return "line " + std::to_string(lineNumber) + ": ";
}

// Reads the lines of a list up to its closing `]`.
bool readList(
    LineReader &in, std::vector<std::string> &items, std::string &error)
{
  std::string_view line;
  while (in.nextContent(line)) {
    if (line == "]")
      return true;
    items.emplace_back(line);
  }
  error = in.where() + "list is not closed";
  return false;
}

bool readStringList(
    LineReader &in, std::vector<std::string> &out, std::string &error)
{
  std::vector<std::string> quoted;
  if (!readList(in, quoted, error))
    return false;
  for (auto &q : quoted) {
    auto s = unquote(q);
    if (!s) {
      error = in.where() + "expected a quoted string, got " + q;
      return false;
    }
    out.push_back(std::move(*s));
  }
  return true;
}

bool readDevice(LineReader &in, Capture &capture, std::string &error)
{
  std::string_view line;
  while (in.nextContent(line)) {
    if (line == "}")
      return true;
    if (line == "extensions ANARI_STRING_LIST []")
      continue;
    if (line == "extensions ANARI_STRING_LIST [") {
      if (!readStringList(in, capture.extensions, error))
        return false;
      continue;
    }
    error = in.where() + "unexpected line in the device block";
    return false;
  }
  error = in.where() + "device block is not closed";
  return false;
}

// The `calls` block: "total <n>", then "<function> <n>" lines.
bool readCalls(LineReader &in, Capture &capture, std::string &error)
{
  std::string_view line;
  while (in.nextContent(line)) {
    if (line == "}")
      return true;
    std::string_view count;
    firstWord(line, count);
    uint64_t n = 0;
    const auto r =
        std::from_chars(count.data(), count.data() + count.size(), n);
    if (count.empty() || r.ec != std::errc()
        || r.ptr != count.data() + count.size()) {
      error = in.where() + "expected '<function> <count>' in the calls block";
      return false;
    }
    capture.callCounts.emplace_back(line);
  }
  error = in.where() + "calls block is not closed";
  return false;
}

bool readWarnings(LineReader &in, Capture &capture, std::string &error)
{
  std::string_view line;
  while (in.nextContent(line)) {
    if (line == "}")
      return true;
    auto s = unquote(line);
    if (!s) {
      error = in.where() + "expected a quoted warning";
      return false;
    }
    capture.warnings.push_back(std::move(*s));
  }
  error = in.where() + "warnings block is not closed";
  return false;
}

bool knownArrayField(std::string_view name)
{
  return name == "elementType" || name == "dimensions" || name == "data"
      || name == "count" || name == "min" || name == "max" || name == "blob";
}

bool readObject(LineReader &in, Block &block, std::string &error)
{
  const bool array = block.isArray();
  std::string_view line;
  while (in.nextContent(line)) {
    if (line == "}")
      return true;

    Entry e;
    std::string_view rest;
    e.name = std::string(firstWord(line, rest));
    if (array) {
      if (!knownArrayField(e.name)) {
        error = in.where() + "unknown array field '" + e.name + "'";
        return false;
      }
    } else {
      std::string_view value;
      e.type = std::string(firstWord(rest, value));
      rest = value;
      if (e.type.empty()) {
        error = in.where() + "parameter '" + e.name + "' has no type";
        return false;
      }
    }

    if (rest == "[") {
      e.isList = true;
      if (!readList(in, e.items, error))
        return false;
    } else if (rest == "[]") {
      e.isList = true;
    } else {
      e.value = std::string(rest);
    }
    block.entries.push_back(std::move(e));
  }
  error = in.where() + "block " + block.id + " is not closed";
  return false;
}

} // namespace

// Block definitions //////////////////////////////////////////////////////////

bool Block::isArray() const
{
  return isArrayLabel(label);
}

const Entry *Block::find(std::string_view name) const
{
  for (auto &e : entries) {
    if (e.name == name)
      return &e;
  }
  return nullptr;
}

// Free functions /////////////////////////////////////////////////////////////

std::optional<Capture> parseCapture(std::string_view text, std::string &error)
{
  LineReader in{text};
  Capture capture;

  std::string_view line;
  if (!in.nextContent(line)) {
    error = "empty file, not an ANARI Capture";
    return {};
  }
  std::string_view versionText;
  if (firstWord(line, versionText) != "anari-capture") {
    error = "not an ANARI Capture (no 'anari-capture' header)";
    return {};
  }
  const auto r = std::from_chars(versionText.data(),
      versionText.data() + versionText.size(),
      capture.version);
  if (r.ec != std::errc() || r.ptr != versionText.data() + versionText.size()) {
    error =
        "unreadable ANARI Capture version '" + std::string(versionText) + "'";
    return {};
  }
  if (capture.version < OLDEST_SUPPORTED_VERSION
      || capture.version > NEWEST_SUPPORTED_VERSION) {
    error = "ANARI Capture version " + std::to_string(capture.version)
        + " is not supported (this reader knows versions "
        + std::to_string(OLDEST_SUPPORTED_VERSION) + " to "
        + std::to_string(NEWEST_SUPPORTED_VERSION) + ")";
    return {};
  }

  while (in.nextContent(line)) {
    if (line.size() < 2 || line.substr(line.size() - 2) != " {") {
      error = in.where() + "expected a block, got '" + std::string(line) + "'";
      return {};
    }
    const auto head = trim(line.substr(0, line.size() - 2));

    if (head == "device") {
      if (!readDevice(in, capture, error))
        return {};
      continue;
    }
    if (head == "calls" && capture.version >= 2) {
      if (!readCalls(in, capture, error))
        return {};
      continue;
    }
    if (head == "warnings") {
      if (!readWarnings(in, capture, error))
        return {};
      continue;
    }

    Block block;
    std::string_view subtype;
    block.id = std::string(firstWord(head, subtype));
    const auto at = block.id.find('@');
    if (at == std::string::npos || at == 0) {
      error = in.where() + "malformed object ID '" + block.id + "'";
      return {};
    }
    block.label = block.id.substr(0, at);
    if (!subtype.empty()) {
      block.subtype = unquote(subtype);
      if (!block.subtype) {
        error = in.where() + "malformed subtype on " + block.id;
        return {};
      }
    }
    if (!readObject(in, block, error))
      return {};
    if (block.label == "frame" && capture.frameId.empty())
      capture.frameId = block.id;
    const auto id = block.id;
    if (!capture.blocks.emplace(id, std::move(block)).second) {
      error = in.where() + "object " + id + " is written twice";
      return {};
    }
  }

  if (capture.frameId.empty()) {
    error = "the ANARI Capture has no frame block";
    return {};
  }

  return capture;
}

anari::DataType dataTypeFromName(std::string_view name)
{
  static const auto byName = [] {
    std::unordered_map<std::string, anari::DataType> m;
    // ANARIDataType values are spread over a few thousand codes; any code
    // without a name prints as ANARI_UNKNOWN and is skipped.
    for (int t = 1; t < 4096; t++) {
      const std::string n = anari::toString(anari::DataType(t));
      if (n != "ANARI_UNKNOWN")
        m.emplace(n, anari::DataType(t));
    }
    return m;
  }();
  auto it = byName.find(std::string(name));
  return it == byName.end() ? ANARI_UNKNOWN : it->second;
}

bool decodeValue(anari::DataType type, std::string_view text, std::byte *out)
{
  text = trim(text);
  if (type == ANARI_BOOL) {
    if (text != "true" && text != "false")
      return false;
    const int8_t b = text == "true" ? 1 : 0;
    std::memcpy(out, &b, sizeof(b));
    return true;
  }
  if (type == ANARI_DATA_TYPE) {
    const anari::DataType t = dataTypeFromName(text);
    if (t == ANARI_UNKNOWN && text != "ANARI_UNKNOWN")
      return false;
    std::memcpy(out, &t, sizeof(t));
    return true;
  }
  if (anari::isObject(type) || type == ANARI_STRING || type == ANARI_STRING_LIST
      || type == ANARI_UNKNOWN)
    return false;

  const size_t n = anari::componentsOf(type);
  const size_t size = anari::sizeOf(type);
  if (n == 0 || size == 0 || size % n != 0)
    return false;
  const std::string_view cType = anari::typenameOf(type);
  const auto parts = splitTuple(text);
  if (parts.size() != n)
    return false;
  const size_t step = size / n;
  for (size_t i = 0; i < n; i++) {
    if (!parseComponent(cType, type, parts[i], out + i * step))
      return false;
  }
  return true;
}

std::optional<std::string> unquote(std::string_view q)
{
  q = trim(q);
  if (q.size() < 2 || q.front() != '"' || q.back() != '"')
    return {};
  q = q.substr(1, q.size() - 2);

  std::string s;
  for (size_t i = 0; i < q.size(); i++) {
    const char c = q[i];
    if (c == '"')
      return {}; // an unescaped quote cannot be inside
    if (c != '\\') {
      s.push_back(c);
      continue;
    }
    if (++i == q.size())
      return {};
    switch (q[i]) {
    case '"':
      s.push_back('"');
      break;
    case '\\':
      s.push_back('\\');
      break;
    case 'n':
      s.push_back('\n');
      break;
    case 'r':
      s.push_back('\r');
      break;
    case 't':
      s.push_back('\t');
      break;
    case 'x': {
      if (i + 2 >= q.size())
        return {};
      unsigned v = 0;
      const auto r = std::from_chars(q.data() + i + 1, q.data() + i + 3, v, 16);
      if (r.ec != std::errc() || r.ptr != q.data() + i + 3)
        return {};
      s.push_back(char(v));
      i += 2;
      break;
    }
    default:
      return {};
    }
  }
  return s;
}

} // namespace vsr::io::detail::anari_capture
