// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/Logging.hpp"
#include "vsr/core/ObjectPool.hpp"
// std
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

namespace vsr::core {

// Node name sanitization /////////////////////////////////////////////////////

// A DataNode name may not contain the DataPath separator, because a Data Path
// that embedded one would parse as two segments. Rather than reject such a
// name -- metadata keys come from user data and a write must not fail -- the
// separator is replaced with an underscore. The same repair is applied to
// name-based lookup, so the string a caller appended with is the string that
// finds the node again.

constexpr char DATA_PATH_SEPARATOR = '/';
constexpr char DATA_PATH_NAME_REPLACEMENT = '_';

// An ordinal Path Segment is bracketed so that it cannot be confused with a
// node legitimately named "3" (ADR 0025).
constexpr char ORDINAL_OPEN = '[';
constexpr char ORDINAL_CLOSE = ']';

bool dataNodeNameNeedsSanitizing(std::string_view name);
std::string sanitizeDataNodeName(std::string_view name);

// True for text of the form <open><digits><close>, e.g. "[3]". Both spellings
// of a position -- the "[3]" of an ordinal Path Segment and the "<3>" of the
// name a DataTree synthesizes for an Anonymous Node -- are recognized here, so
// that the two stay visibly parallel.
bool isDelimitedNumber(std::string_view text, char open, char close);

// DataPath ///////////////////////////////////////////////////////////////////

/*
 * One step of a DataPath, identifying a child of the node reached so far. A
 * segment names a child when the child has a name, and gives its ordinal
 * position when the child is an Anonymous Node. An ordinal is spelled in
 * square brackets so that it cannot be confused with a node legitimately
 * named "3" (ADR 0025).
 *
 * Example:
 *   for (auto segment : node.path()) {
 *     if (segment.isOrdinal())
 *       printf("[%zu]\n", segment.ordinal());
 *     else
 *       printf("%.*s\n", int(segment.name().size()), segment.name().data());
 *   }
 */
struct PathSegment
{
  PathSegment() = default;
  explicit PathSegment(std::string_view text);

  bool isOrdinal() const;
  size_t ordinal() const; // INVALID_INDEX when the segment is a name
  std::string_view name() const; // empty when the segment is an ordinal
  std::string_view text() const; // as spelled in the path

 private:
  std::string_view m_text;
  size_t m_ordinal{INVALID_INDEX};
};

/*
 * The location of one DataNode within its DataTree, expressed as an ordered
 * sequence of PathSegments from the root and stored as a canonical string
 * ("/objectDB/surface/[1]/name"). A DataPath denotes a position, not a node:
 * it stays well-formed when the node it named is gone, which is what makes it
 * the currency of change notification.
 *
 * Note that an ordinal segment is positional, so inserting or removing an
 * earlier sibling re-aims a DataPath stored beforehand. Clients needing an
 * identity that survives sibling edits should name their nodes.
 *
 * Example:
 *   auto path = node.path();
 *   if (DataPath("/objectDB").isPrefixOf(path))
 *     refresh(path);
 */
struct DataPath
{
  struct Iterator;
  using iterator = Iterator;
  using const_iterator = Iterator;

  DataPath(); // the root path, "/"
  explicit DataPath(std::string_view path);

  // Queries //

  const std::string &str() const;
  bool isRoot() const;
  size_t numSegments() const;

  // True when other is this path or lies below it; the test respects segment
  // boundaries, so "/a" is not a prefix of "/ab".
  bool isPrefixOf(const DataPath &other) const;

  // Extension //

  DataPath child(std::string_view name) const; // named child
  DataPath child(size_t ordinal) const; // Anonymous Node child

  // Iteration //

  Iterator begin() const;
  Iterator end() const;

 private:
  std::string m_path;
};

bool operator==(const DataPath &a, const DataPath &b);
bool operator!=(const DataPath &a, const DataPath &b);

/*
 * Forward iterator that parses PathSegments out of a DataPath's string on
 * demand, so that a client filtering by string prefix never pays to parse.
 */
struct DataPath::Iterator
{
  using value_type = PathSegment;
  using reference = PathSegment;
  using pointer = void;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  Iterator() = default;
  Iterator(std::string_view path, size_t begin);

  PathSegment operator*() const;
  Iterator &operator++();
  Iterator operator++(int);

  bool operator==(const Iterator &o) const;
  bool operator!=(const Iterator &o) const;

 private:
  size_t end() const;

  std::string_view m_path;
  size_t m_begin{std::string_view::npos};
};

// Inlined definitions ////////////////////////////////////////////////////////

// Node name sanitization //

inline bool dataNodeNameNeedsSanitizing(std::string_view name)
{
  return name.find(DATA_PATH_SEPARATOR) != std::string_view::npos;
}

inline std::string sanitizeDataNodeName(std::string_view name)
{
  std::string sanitized(name);
  if (!dataNodeNameNeedsSanitizing(name))
    return sanitized;

  std::replace(sanitized.begin(),
      sanitized.end(),
      DATA_PATH_SEPARATOR,
      DATA_PATH_NAME_REPLACEMENT);

  // Warn once per distinct offending name: these names arrive from loops over
  // user data, and a per-occurrence warning would bury the log.
  static std::set<std::string, std::less<>> alreadyWarned;
  if (alreadyWarned.find(name) == alreadyWarned.end()) {
    alreadyWarned.emplace(name);
    logWarning("DataNode name '%s' contains '%c' and was renamed to '%s'",
        std::string(name).c_str(),
        DATA_PATH_SEPARATOR,
        sanitized.c_str());
  }

  return sanitized;
}

// PathSegment //

inline bool isDelimitedNumber(std::string_view text, char open, char close)
{
  return text.size() > 2 && text.front() == open && text.back() == close
      && std::all_of(text.begin() + 1, text.end() - 1, [](char c) {
           return c >= '0' && c <= '9';
         });
}

inline PathSegment::PathSegment(std::string_view text) : m_text(text)
{
  if (!isDelimitedNumber(text, ORDINAL_OPEN, ORDINAL_CLOSE))
    return;

  size_t ordinal = 0;
  for (size_t i = 1; i < text.size() - 1; i++)
    ordinal = ordinal * 10 + size_t(text[i] - '0');
  m_ordinal = ordinal;
}

inline bool PathSegment::isOrdinal() const
{
  return m_ordinal != INVALID_INDEX;
}

inline size_t PathSegment::ordinal() const
{
  return m_ordinal;
}

inline std::string_view PathSegment::name() const
{
  return isOrdinal() ? std::string_view{} : m_text;
}

inline std::string_view PathSegment::text() const
{
  return m_text;
}

// DataPath //

inline DataPath::DataPath() : m_path(1, DATA_PATH_SEPARATOR) {}

inline DataPath::DataPath(std::string_view path)
{
  while (path.size() > 1 && path.back() == DATA_PATH_SEPARATOR)
    path.remove_suffix(1);

  if (path.empty() || path.front() != DATA_PATH_SEPARATOR)
    m_path.push_back(DATA_PATH_SEPARATOR);
  m_path.append(path);
}

inline const std::string &DataPath::str() const
{
  return m_path;
}

inline bool DataPath::isRoot() const
{
  return m_path.size() == 1;
}

inline size_t DataPath::numSegments() const
{
  return size_t(std::distance(begin(), end()));
}

inline bool DataPath::isPrefixOf(const DataPath &other) const
{
  if (isRoot())
    return true;
  if (other.m_path.size() < m_path.size())
    return false;
  if (other.m_path.compare(0, m_path.size(), m_path) != 0)
    return false;
  return other.m_path.size() == m_path.size()
      || other.m_path[m_path.size()] == DATA_PATH_SEPARATOR;
}

inline DataPath DataPath::child(std::string_view name) const
{
  DataPath result(*this);
  if (!result.isRoot())
    result.m_path.push_back(DATA_PATH_SEPARATOR);
  result.m_path.append(sanitizeDataNodeName(name));
  return result;
}

inline DataPath DataPath::child(size_t ordinal) const
{
  DataPath result(*this);
  if (!result.isRoot())
    result.m_path.push_back(DATA_PATH_SEPARATOR);
  result.m_path.push_back(ORDINAL_OPEN);
  result.m_path.append(std::to_string(ordinal));
  result.m_path.push_back(ORDINAL_CLOSE);
  return result;
}

inline DataPath::Iterator DataPath::begin() const
{
  return isRoot() ? end() : Iterator(m_path, 1);
}

inline DataPath::Iterator DataPath::end() const
{
  return Iterator(m_path, std::string_view::npos);
}

inline bool operator==(const DataPath &a, const DataPath &b)
{
  return a.str() == b.str();
}

inline bool operator!=(const DataPath &a, const DataPath &b)
{
  return !(a == b);
}

// DataPath::Iterator //

inline DataPath::Iterator::Iterator(std::string_view path, size_t begin)
    : m_path(path), m_begin(begin)
{}

inline PathSegment DataPath::Iterator::operator*() const
{
  return PathSegment(m_path.substr(m_begin, end() - m_begin));
}

inline DataPath::Iterator &DataPath::Iterator::operator++()
{
  const size_t next = end();
  m_begin = next < m_path.size() ? next + 1 : std::string_view::npos;
  return *this;
}

inline DataPath::Iterator DataPath::Iterator::operator++(int)
{
  Iterator previous(*this);
  ++(*this);
  return previous;
}

inline bool DataPath::Iterator::operator==(const Iterator &o) const
{
  return m_begin == o.m_begin;
}

inline bool DataPath::Iterator::operator!=(const Iterator &o) const
{
  return !(*this == o);
}

inline size_t DataPath::Iterator::end() const
{
  const size_t next = m_path.find(DATA_PATH_SEPARATOR, m_begin);
  return next == std::string_view::npos ? m_path.size() : next;
}

} // namespace vsr::core
