// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/core/DataTreeText.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/core/Logging.hpp"
// std
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsr::core {

namespace {

// Formatting constants ///////////////////////////////////////////////////////

constexpr int INDENT_WIDTH = 2;
constexpr size_t SCALARS_PER_LINE = 8;
constexpr char ANONYMOUS_MARKER = '-';
constexpr char COMMENT_START = '#';
constexpr char OBJECT_REFERENCE_SEPARATOR = '@';
constexpr std::string_view ANARI_TYPE_PREFIX = "ANARI_";

// Type spelling //////////////////////////////////////////////////////////////

// A type is spelled as its ANARI name with the prefix stripped and lowercased,
// a bijection with anari::toString(): there is exactly one spelling per type
// and no alias table.
std::string typeName(anari::DataType type)
{
  std::string name = anari::toString(type);
  if (name.compare(0, ANARI_TYPE_PREFIX.size(), ANARI_TYPE_PREFIX) == 0)
    name.erase(0, ANARI_TYPE_PREFIX.size());
  for (char &c : name) {
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  }
  return name;
}

anari::DataType typeFromName(std::string_view name)
{
  // anari::toString() answers ANARI_UNKNOWN's name for every value that is not
  // a type, so scanning the enumeration's range is how the table is built
  // without a second list of types to keep in step with the SDK.
  static const auto table = [] {
    std::unordered_map<std::string, anari::DataType> result;
    const char *unknown = anari::toString(ANARI_UNKNOWN);
    for (int type = 1; type < 4096; ++type) {
      if (std::strcmp(anari::toString(type), unknown) == 0)
        continue;
      result.emplace(typeName(anari::DataType(type)), anari::DataType(type));
    }
    return result;
  }();

  auto it = table.find(std::string(name));
  return it == table.end() ? ANARI_UNKNOWN : it->second;
}

// Type layout ////////////////////////////////////////////////////////////////

// How a type's bytes split into components, which decides its literal. Opaque
// covers every type the Binary Encoding merely copies bytes for (pointers,
// callbacks, lists): none of those has a meaningful text literal.
enum class Base
{
  Bool,
  Int8,
  UInt8,
  Int16,
  UInt16,
  Int32,
  UInt32,
  Int64,
  UInt64,
  Float16,
  Float32,
  Float64,
  Object,
  Opaque
};

struct Layout
{
  Base base{Base::Opaque};
  size_t baseSize{0};
  size_t components{0};
};

Layout layoutOf(anari::DataType type)
{
  if (type == ANARI_UNKNOWN || type == ANARI_STRING)
    return {};
  if (type == ANARI_BOOL)
    return {Base::Bool, 1, 1};
  if (type == ANARI_DATA_TYPE)
    return {Base::Int32, sizeof(int32_t), 1};
  if (anari::isObject(type)) {
    // An object reference is stored as its index, which the binary writer
    // emits as a size_t; the ANARI size of a handle is a pointer's.
    if (anari::sizeOf(type) != sizeof(size_t))
      return {};
    return {Base::Object, sizeof(size_t), 1};
  }

  struct BaseSpelling
  {
    const char *prefix;
    Base base;
    size_t size;
  };
  // Fixed-point types write their underlying integer storage.
  // clang-format off
  static constexpr BaseSpelling BASES[] = {
      {"INT8", Base::Int8, 1},        {"UINT8", Base::UInt8, 1},
      {"FIXED8", Base::Int8, 1},      {"UFIXED8", Base::UInt8, 1},
      {"INT16", Base::Int16, 2},      {"UINT16", Base::UInt16, 2},
      {"FIXED16", Base::Int16, 2},    {"UFIXED16", Base::UInt16, 2},
      {"INT32", Base::Int32, 4},      {"UINT32", Base::UInt32, 4},
      {"FIXED32", Base::Int32, 4},    {"UFIXED32", Base::UInt32, 4},
      {"INT64", Base::Int64, 8},      {"UINT64", Base::UInt64, 8},
      {"FIXED64", Base::Int64, 8},    {"UFIXED64", Base::UInt64, 8},
      {"FLOAT16", Base::Float16, 2},  {"FLOAT32", Base::Float32, 4},
      {"FLOAT64", Base::Float64, 8},
  };
  // clang-format on

  std::string_view name = anari::toString(type);
  if (name.compare(0, ANARI_TYPE_PREFIX.size(), ANARI_TYPE_PREFIX) == 0)
    name.remove_prefix(ANARI_TYPE_PREFIX.size());

  for (const auto &b : BASES) {
    const size_t n = std::strlen(b.prefix);
    const bool matches = name.size() >= n && name.compare(0, n, b.prefix) == 0
        && (name.size() == n || name[n] == '_');
    if (!matches)
      continue;
    const size_t total = anari::sizeOf(type);
    if (total == 0 || total % b.size != 0)
      return {};
    return {b.base, b.size, total / b.size};
  }

  return {};
}

bool hasTextLiteral(const Layout &layout)
{
  return layout.base != Base::Opaque;
}

// Half floats ////////////////////////////////////////////////////////////////

float halfToFloat(uint16_t h)
{
  const uint32_t sign = uint32_t(h >> 15) & 1u;
  const uint32_t exponent = uint32_t(h >> 10) & 0x1fu;
  const uint32_t mantissa = uint32_t(h) & 0x3ffu;

  uint32_t bits = sign << 31;
  if (exponent == 0) {
    if (mantissa != 0) {
      // Subnormal: renormalize into a float, which has the range to spare.
      uint32_t m = mantissa;
      int e = 0;
      while ((m & 0x400u) == 0) {
        m <<= 1;
        e++;
      }
      bits |= uint32_t(127 - 15 - e + 1) << 23 | ((m & 0x3ffu) << 13);
    }
  } else if (exponent == 0x1fu) {
    bits |= 0x7f800000u | (mantissa << 13);
  } else {
    bits |= (exponent + 112u) << 23 | (mantissa << 13);
  }

  float f = 0.f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

uint16_t floatToHalf(float f)
{
  uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(bits));

  const uint32_t sign = (bits >> 16) & 0x8000u;
  const uint32_t rawExponent = (bits >> 23) & 0xffu;
  uint32_t mantissa = bits & 0x7fffffu;

  if (rawExponent == 0xffu) // inf or nan
    return uint16_t(sign | 0x7c00u | (mantissa ? 0x200u : 0u));

  const int32_t exponent = int32_t(rawExponent) - 127 + 15;
  if (exponent >= 31)
    return uint16_t(sign | 0x7c00u);

  if (exponent <= 0) {
    if (exponent < -10)
      return uint16_t(sign);
    mantissa |= 0x800000u;
    const uint32_t shift = uint32_t(14 - exponent);
    uint32_t half = mantissa >> shift;
    const uint32_t remainder = mantissa & ((1u << shift) - 1u);
    const uint32_t halfway = 1u << (shift - 1);
    if (remainder > halfway || (remainder == halfway && (half & 1u)))
      half++;
    return uint16_t(sign | half);
  }

  uint32_t half = (uint32_t(exponent) << 10) | (mantissa >> 13);
  const uint32_t remainder = mantissa & 0x1fffu;
  // Round to nearest even; a carry out of the mantissa correctly bumps the
  // exponent, and a carry out of the largest finite value lands on infinity.
  if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u)))
    half++;
  return uint16_t(sign | half);
}

// Literal formatting /////////////////////////////////////////////////////////

// Floats use the shortest decimal that parses back to the same value, so 0.1
// stays 0.1 and no precision is lost. std::to_chars picks between plain and
// scientific spelling by length, so 60 is "60" and 1e30 is "1e+30"; a
// precision search over %g would spell 60 as "6e+01".
template <typename T>
std::string formatFloating(T value)
{
  if (std::isnan(value))
    return "nan";
  if (std::isinf(value))
    return value < T(0) ? "-inf" : "inf";

  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

std::string formatFloat(float value)
{
  return formatFloating<float>(value);
}

std::string formatDouble(double value)
{
  return formatFloating<double>(value);
}

template <typename T>
T loadAs(const uint8_t *ptr)
{
  T value;
  std::memcpy(&value, ptr, sizeof(T));
  return value;
}

void appendObjectReference(std::string &out, anari::DataType type, size_t idx)
{
  out += typeName(type);
  out += OBJECT_REFERENCE_SEPARATOR;
  out += std::to_string(idx);
}

// One component of a value, as its literal.
void appendComponent(std::string &out,
    const Layout &layout,
    anari::DataType type,
    const uint8_t *ptr)
{
  switch (layout.base) {
  case Base::Bool:
    out += loadAs<uint8_t>(ptr) ? "true" : "false";
    break;
  case Base::Int8:
    out += std::to_string(loadAs<int8_t>(ptr));
    break;
  case Base::UInt8:
    out += std::to_string(loadAs<uint8_t>(ptr));
    break;
  case Base::Int16:
    out += std::to_string(loadAs<int16_t>(ptr));
    break;
  case Base::UInt16:
    out += std::to_string(loadAs<uint16_t>(ptr));
    break;
  case Base::Int32:
    out += std::to_string(loadAs<int32_t>(ptr));
    break;
  case Base::UInt32:
    out += std::to_string(loadAs<uint32_t>(ptr));
    break;
  case Base::Int64:
    out += std::to_string(loadAs<int64_t>(ptr));
    break;
  case Base::UInt64:
    out += std::to_string(loadAs<uint64_t>(ptr));
    break;
  case Base::Float16:
    out += formatFloat(halfToFloat(loadAs<uint16_t>(ptr)));
    break;
  case Base::Float32:
    out += formatFloat(loadAs<float>(ptr));
    break;
  case Base::Float64:
    out += formatDouble(loadAs<double>(ptr));
    break;
  case Base::Object:
    appendObjectReference(out, type, loadAs<size_t>(ptr));
    break;
  case Base::Opaque:
    break;
  }
}

// Names //////////////////////////////////////////////////////////////////////

bool isBareNameChar(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
      || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
}

// Bare when it matches [A-Za-z0-9_.-]+ and is not the anonymous marker itself.
bool isBareName(std::string_view name)
{
  if (name.empty())
    return false;
  if (name.size() == 1 && name[0] == ANONYMOUS_MARKER)
    return false;
  for (char c : name) {
    if (!isBareNameChar(c))
      return false;
  }
  return true;
}

bool isReservedAnonymousName(std::string_view name)
{
  return isDelimitedNumber(name, ANONYMOUS_NAME_OPEN, ANONYMOUS_NAME_CLOSE);
}

// Double-quoted with C-style escapes; UTF-8 passes through untouched.
void appendQuoted(std::string &out, std::string_view text)
{
  out += '"';
  for (unsigned char c : text) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20 || c == 0x7f) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "\\x%02x", unsigned(c));
        out += buffer;
      } else {
        out += char(c);
      }
    }
  }
  out += '"';
}

// Lexing /////////////////////////////////////////////////////////////////////

enum class TokenKind
{
  Word,
  String,
  OpenBrace,
  CloseBrace,
  OpenBracket,
  CloseBracket,
  Equals,
  At,
  End,
  Error
};

struct Token
{
  TokenKind kind{TokenKind::End};
  std::string_view text; // as spelled
  std::string value; // decoded, for a String
  int line{1};
  int column{1};
};

bool isSpace(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v'
      || c == '\f';
}

// Words are runs of the characters a bare name, a type, or a numeric literal
// can contain. The anonymous-name delimiters are included so that a bare
// name of the reserved shape reaches the parser and gets its own error.
bool isWordChar(char c)
{
  return isBareNameChar(c) || c == '+' || c == ANONYMOUS_NAME_OPEN
      || c == ANONYMOUS_NAME_CLOSE;
}

int hexDigit(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

struct Lexer
{
  explicit Lexer(std::string_view source) : m_source(source) {}

  Token next();

 private:
  void advance();
  void skipSpaceAndComments();
  Token makeToken(TokenKind kind, size_t begin, int line, int column) const;
  Token lexString(int line, int column);

  std::string_view m_source;
  size_t m_pos{0};
  int m_line{1};
  int m_column{1};
};

void Lexer::advance()
{
  if (m_source[m_pos] == '\n') {
    m_line++;
    m_column = 1;
  } else {
    m_column++;
  }
  m_pos++;
}

void Lexer::skipSpaceAndComments()
{
  while (m_pos < m_source.size()) {
    const char c = m_source[m_pos];
    if (c == COMMENT_START) {
      while (m_pos < m_source.size() && m_source[m_pos] != '\n')
        advance();
    } else if (isSpace(c)) {
      advance();
    } else {
      break;
    }
  }
}

Token Lexer::makeToken(TokenKind kind, size_t begin, int line, int column) const
{
  Token t;
  t.kind = kind;
  t.text = m_source.substr(begin, m_pos - begin);
  t.line = line;
  t.column = column;
  return t;
}

Token Lexer::lexString(int line, int column)
{
  const size_t begin = m_pos;
  advance(); // opening quote

  Token t;
  t.kind = TokenKind::String;
  t.line = line;
  t.column = column;

  auto fail = [&](const char *message) {
    t.kind = TokenKind::Error;
    t.value = message;
    return t;
  };

  while (true) {
    if (m_pos >= m_source.size() || m_source[m_pos] == '\n')
      return fail("unterminated string literal");

    const char c = m_source[m_pos];
    if (c == '"') {
      advance();
      break;
    }

    if (c != '\\') {
      t.value += c;
      advance();
      continue;
    }

    advance(); // backslash
    if (m_pos >= m_source.size() || m_source[m_pos] == '\n')
      return fail("unterminated string literal");

    const char escaped = m_source[m_pos];
    switch (escaped) {
    case '"':
      t.value += '"';
      break;
    case '\\':
      t.value += '\\';
      break;
    case 'n':
      t.value += '\n';
      break;
    case 't':
      t.value += '\t';
      break;
    case 'x': {
      if (m_pos + 2 >= m_source.size())
        return fail("escape '\\x' needs two hexadecimal digits");
      const int hi = hexDigit(m_source[m_pos + 1]);
      const int lo = hexDigit(m_source[m_pos + 2]);
      if (hi < 0 || lo < 0)
        return fail("escape '\\x' needs two hexadecimal digits");
      t.value += char(hi * 16 + lo);
      advance();
      advance();
      break;
    }
    default:
      return fail("unknown escape sequence in string literal");
    }
    advance();
  }

  t.text = m_source.substr(begin, m_pos - begin);
  return t;
}

Token Lexer::next()
{
  skipSpaceAndComments();

  const int line = m_line;
  const int column = m_column;
  const size_t begin = m_pos;

  if (m_pos >= m_source.size())
    return makeToken(TokenKind::End, begin, line, column);

  const char c = m_source[m_pos];
  switch (c) {
  case '{':
    advance();
    return makeToken(TokenKind::OpenBrace, begin, line, column);
  case '}':
    advance();
    return makeToken(TokenKind::CloseBrace, begin, line, column);
  case '[':
    advance();
    return makeToken(TokenKind::OpenBracket, begin, line, column);
  case ']':
    advance();
    return makeToken(TokenKind::CloseBracket, begin, line, column);
  case '=':
    advance();
    return makeToken(TokenKind::Equals, begin, line, column);
  case OBJECT_REFERENCE_SEPARATOR:
    advance();
    return makeToken(TokenKind::At, begin, line, column);
  case '"':
    return lexString(line, column);
  default:
    break;
  }

  if (!isWordChar(c)) {
    Token t = makeToken(TokenKind::Error, begin, line, column);
    t.value = "unexpected character '";
    t.value += c;
    t.value += "'";
    return t;
  }

  while (m_pos < m_source.size() && isWordChar(m_source[m_pos]))
    advance();
  return makeToken(TokenKind::Word, begin, line, column);
}

// Literal parsing ////////////////////////////////////////////////////////////

bool parseSigned(std::string_view text, int64_t min, int64_t max, int64_t &out)
{
  if (text.empty())
    return false;
  const std::string copy(text);
  errno = 0;
  char *end = nullptr;
  const long long value = std::strtoll(copy.c_str(), &end, 10);
  if (end != copy.c_str() + copy.size() || errno == ERANGE)
    return false;
  if (value < min || value > max)
    return false;
  out = value;
  return true;
}

bool parseUnsigned(std::string_view text, uint64_t max, uint64_t &out)
{
  // strtoull accepts a leading minus and wraps; an unsigned literal may not.
  if (text.empty() || text.front() == '-')
    return false;
  const std::string copy(text);
  errno = 0;
  char *end = nullptr;
  const unsigned long long value = std::strtoull(copy.c_str(), &end, 10);
  if (end != copy.c_str() + copy.size() || errno == ERANGE)
    return false;
  if (value > max)
    return false;
  out = value;
  return true;
}

bool parseDouble(std::string_view text, double &out)
{
  if (text.empty())
    return false;
  const std::string copy(text);
  char *end = nullptr;
  out = std::strtod(copy.c_str(), &end);
  return end == copy.c_str() + copy.size();
}

bool parseFloat(std::string_view text, float &out)
{
  if (text.empty())
    return false;
  const std::string copy(text);
  char *end = nullptr;
  out = std::strtof(copy.c_str(), &end);
  return end == copy.c_str() + copy.size();
}

bool looksNumeric(std::string_view text)
{
  double ignored = 0.0;
  return parseDouble(text, ignored);
}

template <typename T>
void storeAs(uint8_t *ptr, T value)
{
  std::memcpy(ptr, &value, sizeof(T));
}

} // namespace

// Writer /////////////////////////////////////////////////////////////////////

struct DataTreeTextCodec::Writer
{
  explicit Writer(std::string &out) : m_out(out) {}

  void entries(const DataNode &parent, int depth);

 private:
  void entry(const DataNode &node, int depth);
  bool value(const DataNode &node, int depth);
  bool array(const DataNode &node, int depth);
  void components(const uint8_t *ptr,
      const Layout &layout,
      anari::DataType type,
      const char *separator);
  void indent(int depth);
  void warnNoLiteral(const DataNode &node, anari::DataType type);

  std::string &m_out;
};

void DataTreeTextCodec::Writer::entries(const DataNode &parent, int depth)
{
  parent.foreach_child_const(
      [&](const DataNode &child) { entry(child, depth); });
}

void DataTreeTextCodec::Writer::entry(const DataNode &node, int depth)
{
  indent(depth);

  // Anonymity comes from the marker and nowhere else: the synthesized '<n>'
  // name a node carries never reaches the file (ADR 0039).
  if (node.m_data.anonymous)
    m_out += ANONYMOUS_MARKER;
  else if (isBareName(node.name()))
    m_out += node.name();
  else
    appendQuoted(m_out, node.name());

  const bool hasValue = value(node, depth);
  const bool hasChildren = !node.isLeaf();

  if (hasChildren) {
    m_out += " {\n";
    entries(node, depth + 1);
    indent(depth);
    m_out += "}\n";
  } else if (!hasValue) {
    // The absence of '=' always means the absence of a value, so a value-less
    // node is spelled as an empty block rather than a bare name.
    m_out += " {}\n";
  } else {
    m_out += '\n';
  }
}

bool DataTreeTextCodec::Writer::value(const DataNode &node, int depth)
{
  if (node.holdsArray())
    return array(node, depth);

  const Any &v = node.getValue();
  if (!v)
    return false;

  const anari::DataType type = v.type();

  // The two unambiguous literals carry no type.
  if (type == ANARI_STRING) {
    m_out += " = ";
    appendQuoted(m_out, v.getCStr());
    return true;
  }
  if (type == ANARI_BOOL) {
    m_out += " = ";
    m_out += v.get<bool>() ? "true" : "false";
    return true;
  }

  const Layout layout = layoutOf(type);
  if (!hasTextLiteral(layout)) {
    warnNoLiteral(node, type);
    return false;
  }

  m_out += " = ";
  if (layout.base == Base::Object) {
    // The reference literal carries its own type: 'geometry@12'.
    appendObjectReference(m_out, type, v.getAsObjectIndex());
  } else {
    m_out += typeName(type);
    m_out += ' ';
    components(static_cast<const uint8_t *>(v.data()), layout, type, " ");
  }
  return true;
}

bool DataTreeTextCodec::Writer::array(const DataNode &node, int depth)
{
  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;
  // External Arrays are written by value, exactly as the binary form does.
  node.getValueAsArray(&type, &data, &size);

  const Layout layout = layoutOf(type);
  if (!hasTextLiteral(layout)) {
    warnNoLiteral(node, type);
    return false;
  }

  m_out += " = ";
  m_out += typeName(type);
  m_out += "[]";

  if (size == 0) {
    m_out += " []";
    return true;
  }

  m_out += " [\n";
  const auto *bytes = static_cast<const uint8_t *>(data);
  const size_t elementSize = anari::sizeOf(type);

  if (layout.components > 1) {
    // One vector or matrix per line, so a diff of a changed vertex is one line.
    for (size_t i = 0; i < size; ++i) {
      indent(depth + 1);
      components(bytes + i * elementSize, layout, type, " ");
      m_out += '\n';
    }
  } else {
    for (size_t i = 0; i < size; ++i) {
      if (i % SCALARS_PER_LINE == 0)
        indent(depth + 1);
      else
        m_out += ' ';
      appendComponent(m_out, layout, type, bytes + i * elementSize);
      if (i % SCALARS_PER_LINE == SCALARS_PER_LINE - 1 || i + 1 == size)
        m_out += '\n';
    }
  }

  indent(depth);
  m_out += ']';
  return true;
}

void DataTreeTextCodec::Writer::components(const uint8_t *ptr,
    const Layout &layout,
    anari::DataType type,
    const char *separator)
{
  for (size_t c = 0; c < layout.components; ++c) {
    if (c != 0)
      m_out += separator;
    appendComponent(m_out, layout, type, ptr + c * layout.baseSize);
  }
}

void DataTreeTextCodec::Writer::indent(int depth)
{
  m_out.append(size_t(depth) * INDENT_WIDTH, ' ');
}

void DataTreeTextCodec::Writer::warnNoLiteral(
    const DataNode &node, anari::DataType type)
{
  logWarning(
      "[DataNode] '%s' holds a %s value, which has no Text Encoding literal; "
      "it is written as a value-less node",
      node.name().c_str(),
      anari::toString(type));
}

// Parser /////////////////////////////////////////////////////////////////////

struct DataTreeTextCodec::Parser
{
  explicit Parser(std::string_view text) : m_source(text), m_lexer(text) {}

  bool parse(DataNode &root);

  const std::string &error() const;
  int errorLine() const;
  int errorColumn() const;

 private:
  // A value decoded ahead of the node it lands in. It is applied only once
  // the entry's block, if any, has been read: a DataNode is either a value or
  // a container, so a value that turns out to sit on an interior node is
  // dropped with a warning rather than placed.
  struct PendingValue
  {
    bool present{false};
    bool isArray{false};
    anari::DataType type{ANARI_UNKNOWN};
    Any scalar;
    std::vector<uint8_t> bytes;
  };

  bool fail(const Token &at, std::string message);
  bool advance();
  Token peek();

  bool parseHeader();
  bool parseEntries(DataNode &parent, const Token *openBrace);
  bool parseEntry(DataNode &parent);
  bool parseValue(PendingValue &out);
  bool parseScalar(
      anari::DataType type, const Token &typeToken, PendingValue &out);
  bool parseArray(
      anari::DataType type, const Token &typeToken, PendingValue &out);
  bool parseObjectIndex(size_t &idx);
  bool parseComponent(const Layout &layout,
      anari::DataType type,
      const Token &token,
      uint8_t *dst);
  void apply(PendingValue &value, DataNode &node);

  std::string_view m_source;
  Lexer m_lexer;
  Token m_token;
  std::string m_error;
  int m_errorLine{0};
  int m_errorColumn{0};
};

const std::string &DataTreeTextCodec::Parser::error() const
{
  return m_error;
}

int DataTreeTextCodec::Parser::errorLine() const
{
  return m_errorLine;
}

int DataTreeTextCodec::Parser::errorColumn() const
{
  return m_errorColumn;
}

bool DataTreeTextCodec::Parser::fail(const Token &at, std::string message)
{
  // The first failure is the one reported; anything after it is fallout.
  if (m_error.empty()) {
    m_error = std::move(message);
    m_errorLine = at.line;
    m_errorColumn = at.column;
  }
  return false;
}

bool DataTreeTextCodec::Parser::advance()
{
  m_token = m_lexer.next();
  if (m_token.kind == TokenKind::Error)
    return fail(m_token, m_token.value);
  return true;
}

Token DataTreeTextCodec::Parser::peek()
{
  Lexer lookahead = m_lexer;
  return lookahead.next();
}

bool DataTreeTextCodec::Parser::parse(DataNode &root)
{
  if (!parseHeader())
    return false;
  return parseEntries(root, nullptr);
}

bool DataTreeTextCodec::Parser::parseHeader()
{
  Token start;
  if (!isDataTreeText(m_source)) {
    return fail(start,
        "missing header: a Text Encoding file begins with '"
            + std::string(DATA_TREE_TEXT_HEADER) + " "
            + std::to_string(DATA_TREE_TEXT_VERSION) + "'");
  }

  if (!advance()) // the header token itself, guaranteed by the check above
    return false;
  if (!advance())
    return false;

  int64_t version = 0;
  if (m_token.kind != TokenKind::Word || m_token.line != 1
      || !parseSigned(
          m_token.text, 1, std::numeric_limits<int>::max(), version)) {
    return fail(m_token,
        "malformed header: expected the encoding version after '"
            + std::string(DATA_TREE_TEXT_HEADER) + "'");
  }
  if (version > DATA_TREE_TEXT_VERSION) {
    return fail(m_token,
        "unsupported Text Encoding version " + std::to_string(version)
            + "; this reader understands version "
            + std::to_string(DATA_TREE_TEXT_VERSION));
  }

  if (!advance())
    return false;
  if (m_token.kind != TokenKind::End && m_token.line == 1)
    return fail(m_token, "unexpected text after the header");

  return true;
}

// The top level has no enclosing braces: it is the block of the node being
// read into, so it ends at the end of input rather than at a '}'.
bool DataTreeTextCodec::Parser::parseEntries(
    DataNode &parent, const Token *openBrace)
{
  while (true) {
    if (m_token.kind == TokenKind::End) {
      if (openBrace == nullptr)
        return true;
      return fail(*openBrace, "unterminated block: missing '}'");
    }
    if (m_token.kind == TokenKind::CloseBrace) {
      if (openBrace == nullptr)
        return fail(m_token, "unexpected '}' with no open block");
      return advance();
    }
    if (!parseEntry(parent))
      return false;
  }
}

bool DataTreeTextCodec::Parser::parseEntry(DataNode &parent)
{
  const Token nameToken = m_token;
  bool anonymous = false;
  std::string name;

  if (m_token.kind == TokenKind::Word) {
    if (m_token.text.size() == 1 && m_token.text[0] == ANONYMOUS_MARKER) {
      anonymous = true;
    } else {
      name = std::string(m_token.text);
      if (isReservedAnonymousName(name)) {
        return fail(m_token,
            "name '" + name
                + "' has the shape of a synthesized anonymous name; use the "
                  "'-' marker for an anonymous node");
      }
      if (!isBareName(name)) {
        return fail(m_token,
            "name '" + name + "' must be double-quoted (bare names match "
                + "[A-Za-z0-9_.-]+)");
      }
    }
  } else if (m_token.kind == TokenKind::String) {
    name = m_token.value;
    if (name.empty())
      return fail(m_token, "a node name may not be empty");
    if (isReservedAnonymousName(name)) {
      return fail(m_token,
          "name \"" + name
              + "\" has the shape of a synthesized anonymous name; use the "
                "'-' marker for an anonymous node");
    }
  } else {
    return fail(m_token, "expected a node name or the '-' marker");
  }

  if (!advance())
    return false;

  if (m_token.kind != TokenKind::Equals
      && m_token.kind != TokenKind::OpenBrace) {
    return fail(m_token,
        "expected '=' or '{' after "
            + (anonymous ? std::string("the '-' marker")
                         : "name '" + name + "'"));
  }

  DataNode *node = nullptr;
  if (anonymous) {
    // Minted exactly as appending an unnamed child does, so the name is
    // claimed against the process counter by construction.
    node = &parent.append();
  } else {
    if (parent.child(name) != nullptr)
      return fail(nameToken, "duplicate name '" + name + "' in this scope");
    node = &parent.append(name);
  }

  PendingValue value;
  if (m_token.kind == TokenKind::Equals) {
    if (!advance())
      return false;
    if (!parseValue(value))
      return false;
  }

  if (m_token.kind == TokenKind::OpenBrace) {
    const Token openBrace = m_token;
    if (!advance())
      return false;
    if (!parseEntries(*node, &openBrace))
      return false;
  }

  // The in-memory model keeps a node either a value or a container:
  // append() clears a value and setValue() removes children. The Text
  // Encoding is exactly as expressive and no more, so a value on an entry
  // that also has children is dropped here, loudly, instead of building a
  // node no other code path can (ADR 0039).
  if (value.present && !node->isLeaf()) {
    logWarning(
        "[DataNode] Text Encoding line %d, column %d: '%s' has both a value "
        "and children; a Data Node holds one or the other, so the value is "
        "dropped",
        nameToken.line,
        nameToken.column,
        anonymous ? "-" : name.c_str());
    value.present = false;
  }

  apply(value, *node);
  return true;
}

bool DataTreeTextCodec::Parser::parseValue(PendingValue &out)
{
  out.present = true;

  if (m_token.kind == TokenKind::String) {
    out.scalar = Any(std::string(m_token.value));
    return advance();
  }

  if (m_token.kind != TokenKind::Word) {
    if (m_token.kind == TokenKind::OpenBrace)
      return fail(m_token, "expected a value after '='");
    return fail(m_token, "expected a value");
  }

  if (m_token.text == "true" || m_token.text == "false") {
    out.scalar = Any(m_token.text == "true");
    return advance();
  }

  if (m_token.text.size() == 1 && m_token.text[0] == ANONYMOUS_MARKER)
    return fail(m_token, "anonymous marker '-' outside a block");

  const Token typeToken = m_token;
  const anari::DataType type = typeFromName(m_token.text);
  if (type == ANARI_UNKNOWN) {
    if (looksNumeric(m_token.text)) {
      return fail(m_token,
          "numeric literal '" + std::string(m_token.text)
              + "' needs a type; numeric literals never infer one");
    }
    return fail(m_token, "unknown type '" + std::string(m_token.text) + "'");
  }
  if (!advance())
    return false;

  if (m_token.kind == TokenKind::OpenBracket) {
    if (!advance())
      return false;
    if (m_token.kind != TokenKind::CloseBracket)
      return fail(m_token, "expected ']' to complete the array type");
    if (!advance())
      return false;
    return parseArray(type, typeToken, out);
  }

  return parseScalar(type, typeToken, out);
}

bool DataTreeTextCodec::Parser::parseObjectIndex(size_t &idx)
{
  if (m_token.kind != TokenKind::At) {
    return fail(m_token,
        std::string("expected '") + OBJECT_REFERENCE_SEPARATOR
            + "' and an index after an object type");
  }
  if (!advance())
    return false;
  uint64_t value = 0;
  if (m_token.kind != TokenKind::Word
      || !parseUnsigned(
          m_token.text, std::numeric_limits<size_t>::max(), value)) {
    return fail(m_token,
        std::string("expected an object index after '")
            + OBJECT_REFERENCE_SEPARATOR + "'");
  }
  idx = size_t(value);
  return advance();
}

bool DataTreeTextCodec::Parser::parseScalar(
    anari::DataType type, const Token &typeToken, PendingValue &out)
{
  const std::string spelled = typeName(type);

  if (type == ANARI_STRING) {
    if (m_token.kind != TokenKind::String)
      return fail(m_token, "expected a double-quoted string literal");
    out.scalar = Any(std::string(m_token.value));
    return advance();
  }

  if (type == ANARI_BOOL) {
    if (m_token.kind != TokenKind::Word
        || (m_token.text != "true" && m_token.text != "false"))
      return fail(m_token, "expected 'true' or 'false'");
    out.scalar = Any(m_token.text == "true");
    return advance();
  }

  const Layout layout = layoutOf(type);
  if (!hasTextLiteral(layout)) {
    return fail(typeToken,
        "type '" + spelled + "' has no Text Encoding literal; write it as a "
            + "value-less node");
  }

  if (layout.base == Base::Object) {
    size_t idx = 0;
    if (!parseObjectIndex(idx))
      return false;
    out.scalar = Any(type, idx);
    return true;
  }

  std::vector<uint8_t> bytes(anari::sizeOf(type), 0);
  for (size_t c = 0; c < layout.components; ++c) {
    // A word that cannot be a component is the next entry's name, which
    // means the value ended early.
    const bool isComponent = m_token.kind == TokenKind::Word
        && (layout.base == Base::Bool || looksNumeric(m_token.text));
    if (!isComponent) {
      return fail(m_token,
          "type '" + spelled + "' takes " + std::to_string(layout.components)
              + " component(s), found " + std::to_string(c));
    }
    if (!parseComponent(
            layout, type, m_token, bytes.data() + c * layout.baseSize))
      return false;
    if (!advance())
      return false;
  }

  // A numeric word here is one component too many, unless it is the bare
  // name of the next entry -- which is what its own following token settles.
  if (m_token.kind == TokenKind::Word && looksNumeric(m_token.text)) {
    const Token following = peek();
    if (following.kind != TokenKind::Equals
        && following.kind != TokenKind::OpenBrace) {
      return fail(m_token,
          "type '" + spelled + "' takes " + std::to_string(layout.components)
              + " component(s), found more");
    }
  }

  out.scalar = Any(type, bytes.data());
  return true;
}

bool DataTreeTextCodec::Parser::parseArray(
    anari::DataType type, const Token &typeToken, PendingValue &out)
{
  const std::string spelled = typeName(type);
  const Layout layout = layoutOf(type);
  if (!hasTextLiteral(layout)) {
    return fail(typeToken,
        "type '" + spelled + "' has no Text Encoding literal and cannot be "
            + "an array element");
  }

  if (m_token.kind != TokenKind::OpenBracket)
    return fail(m_token, "expected '[' to open the array elements");
  const Token openBracket = m_token;
  if (!advance())
    return false;

  std::vector<uint8_t> bytes;
  size_t numComponents = 0;

  while (m_token.kind != TokenKind::CloseBracket) {
    if (m_token.kind == TokenKind::End || m_token.kind == TokenKind::CloseBrace
        || m_token.kind == TokenKind::OpenBrace)
      return fail(openBracket, "unterminated array: missing ']'");

    if (layout.base == Base::Object) {
      // Each element is a full reference literal, type and index.
      if (m_token.kind != TokenKind::Word || m_token.text != spelled) {
        return fail(m_token,
            "expected an element of the form '" + spelled
                + OBJECT_REFERENCE_SEPARATOR + "<index>'");
      }
      if (!advance())
        return false;
      size_t idx = 0;
      if (!parseObjectIndex(idx))
        return false;
      bytes.resize(bytes.size() + sizeof(size_t));
      storeAs(bytes.data() + bytes.size() - sizeof(size_t), idx);
      numComponents++;
      continue;
    }

    if (m_token.kind != TokenKind::Word)
      return fail(m_token, "expected an array element or ']'");

    bytes.resize(bytes.size() + layout.baseSize);
    if (!parseComponent(layout,
            type,
            m_token,
            bytes.data() + bytes.size() - layout.baseSize))
      return false;
    numComponents++;
    if (!advance())
      return false;
  }

  if (numComponents % layout.components != 0) {
    return fail(openBracket,
        "array of '" + spelled + "' has " + std::to_string(numComponents)
            + " components, which is not a multiple of "
            + std::to_string(layout.components));
  }

  out.isArray = true;
  out.type = type;
  out.bytes = std::move(bytes);
  return advance();
}

bool DataTreeTextCodec::Parser::parseComponent(const Layout &layout,
    anari::DataType type,
    const Token &token,
    uint8_t *dst)
{
  const std::string_view text = token.text;

  auto integerError = [&](const char *kind) {
    return fail(token,
        "integer literal '" + std::string(text) + "' is not a valid " + kind
            + " or is out of range for type '" + typeName(type) + "'");
  };
  auto floatError = [&]() {
    return fail(token,
        "'" + std::string(text) + "' is not a valid number for type '"
            + typeName(type) + "'");
  };

  int64_t s = 0;
  uint64_t u = 0;
  float f = 0.f;
  double d = 0.0;

  switch (layout.base) {
  case Base::Bool:
    if (text != "true" && text != "false")
      return fail(token, "expected 'true' or 'false'");
    storeAs<uint8_t>(dst, text == "true" ? 1 : 0);
    return true;
  case Base::Int8:
    if (!parseSigned(text, INT8_MIN, INT8_MAX, s))
      return integerError("int8");
    storeAs<int8_t>(dst, int8_t(s));
    return true;
  case Base::UInt8:
    if (!parseUnsigned(text, UINT8_MAX, u))
      return integerError("uint8");
    storeAs<uint8_t>(dst, uint8_t(u));
    return true;
  case Base::Int16:
    if (!parseSigned(text, INT16_MIN, INT16_MAX, s))
      return integerError("int16");
    storeAs<int16_t>(dst, int16_t(s));
    return true;
  case Base::UInt16:
    if (!parseUnsigned(text, UINT16_MAX, u))
      return integerError("uint16");
    storeAs<uint16_t>(dst, uint16_t(u));
    return true;
  case Base::Int32:
    if (!parseSigned(text, INT32_MIN, INT32_MAX, s))
      return integerError("int32");
    storeAs<int32_t>(dst, int32_t(s));
    return true;
  case Base::UInt32:
    if (!parseUnsigned(text, UINT32_MAX, u))
      return integerError("uint32");
    storeAs<uint32_t>(dst, uint32_t(u));
    return true;
  case Base::Int64:
    if (!parseSigned(text, INT64_MIN, INT64_MAX, s))
      return integerError("int64");
    storeAs<int64_t>(dst, s);
    return true;
  case Base::UInt64:
    if (!parseUnsigned(text, UINT64_MAX, u))
      return integerError("uint64");
    storeAs<uint64_t>(dst, u);
    return true;
  case Base::Float16:
    if (!parseFloat(text, f))
      return floatError();
    storeAs<uint16_t>(dst, floatToHalf(f));
    return true;
  case Base::Float32:
    if (!parseFloat(text, f))
      return floatError();
    storeAs<float>(dst, f);
    return true;
  case Base::Float64:
    if (!parseDouble(text, d))
      return floatError();
    storeAs<double>(dst, d);
    return true;
  case Base::Object:
  case Base::Opaque:
    break;
  }

  return fail(token, "type '" + typeName(type) + "' has no component literal");
}

void DataTreeTextCodec::Parser::apply(PendingValue &value, DataNode &node)
{
  if (!value.present)
    return;

  // The node is a leaf by the time a value is applied (see parseEntry), so
  // this is what the public setters would do minus their Signal, which the
  // caller collapses into one Subtree Replacement anyway.
  if (value.isArray) {
    node.m_data.arrayType = value.type;
    node.m_data.arrayBytes = std::move(value.bytes);
  } else {
    node.m_data.value = std::move(value.scalar);
  }
}

// Detection //////////////////////////////////////////////////////////////////

bool isDataTreeText(std::string_view leadingBytes)
{
  const size_t n = DATA_TREE_TEXT_HEADER.size();
  if (leadingBytes.size() < n
      || leadingBytes.compare(0, n, DATA_TREE_TEXT_HEADER) != 0)
    return false;
  return leadingBytes.size() == n || isSpace(leadingBytes[n]);
}

bool isDataTreeTextFile(const char *filename)
{
  std::FILE *file = std::fopen(filename, "rb");
  if (file == nullptr)
    return false;
  char lead[DATA_TREE_TEXT_HEADER.size() + 1] = {};
  const size_t numRead = std::fread(lead, 1, sizeof(lead), file);
  std::fclose(file);
  return isDataTreeText(std::string_view(lead, numRead));
}

// DataTreeTextCodec //////////////////////////////////////////////////////////

void DataTreeTextCodec::write(const DataNode &node, std::string &text)
{
  text += DATA_TREE_TEXT_HEADER;
  text += ' ';
  text += std::to_string(DATA_TREE_TEXT_VERSION);
  text += '\n';

  // Level 0 is this node, and nothing about it is written: what goes out is
  // the tree it roots, so a detached node and a leaf alike write an empty one.
  if (!node.self())
    return;

  Writer writer(text);
  writer.entries(node, 0);
}

bool DataTreeTextCodec::read(DataNode &node, std::string_view text)
{
  // Reading fills a node's storage, which a node with no tree behind it does
  // not have. Refusing is how a caller finds that out short of a crash.
  if (!node.self() || node.m_data.tree == nullptr)
    return false;

  auto &tree = *node.m_data.tree;

  // Reading is one semantic event: the edits below are collapsed into the
  // single signalSubtreeReplaced() at the end.
  tree.m_signalsSuppressed = true;

  // Replace, not merge: what this node held is gone before the first token is
  // decoded, and a failure leaves it empty rather than half-built.
  node.clearValueSilently();
  node.self()->erase_subtree();

  Parser parser(text);
  const bool ok = parser.parse(node);
  if (!ok) {
    node.clearValueSilently();
    node.self()->erase_subtree();
    logWarning("[DataNode] Text Encoding read failed at line %d, column %d: %s",
        parser.errorLine(),
        parser.errorColumn(),
        parser.error().c_str());
  }

  tree.m_signalsSuppressed = false;
  node.signalSubtreeReplaced();
  return ok;
}

bool DataTreeTextCodec::load(DataNode &node, const char *filename)
{
  std::FILE *file = std::fopen(filename, "rb");
  if (file == nullptr)
    return false;

  std::string text;
  char chunk[64 * 1024];
  for (size_t n = 0; (n = std::fread(chunk, 1, sizeof(chunk), file)) > 0;)
    text.append(chunk, n);
  std::fclose(file);

  return read(node, text);
}

} // namespace vsr::core
