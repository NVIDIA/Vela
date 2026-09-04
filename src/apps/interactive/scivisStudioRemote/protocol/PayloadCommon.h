// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_model
#include "DataNodeFields.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// std
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::scivis_studio::protocol {

/*
 * Building blocks shared by every payload codec. The typed field access
 * (writeChild/readChild, the enum and nested-node helpers, the lists) is the
 * model's DataNodeFields.h, shared with its own serializers and re-exported
 * below; writers put each scalar under a named child (ADR 0027), readers
 * never create children and never throw on untrusted input. One policy for
 * every field: a required child that is absent or mistyped is a malformed
 * payload (false); an optional child that is absent leaves the struct's
 * default, and one that is present but mistyped is malformed too. A
 * hand-written fromNode() leaves its output unspecified on failure
 * (decode<T>() discards it); the fields()-derived one leaves it untouched.
 *
 * A payload describes its wire shape once, as a fields() template over a
 * visitor, and toNode()/fromNode() are derived from it (see Field visitors
 * below). The string-list, path and subtree helpers here are what the
 * visitors, and the few hand-written codecs, are built from.
 *
 * Example:
 *   struct Foo
 *   {
 *     uint64_t requestId{0};
 *     SceneNodeRef target;
 *     bool fast{false};
 *   };
 *   template <typename V>
 *   void fields(V &v, Foo &f)
 *   {
 *     v.required("requestId", f.requestId);
 *     v.child("target", f.target);
 *     v.optional("fast", f.fast);
 *   }
 */

// Typed field access /////////////////////////////////////////////////////////

// The model's, re-exported so a payload codec, and a test with
// `using namespace protocol`, spells them unqualified.
using vsr::scivis_studio::enumFromName;
using vsr::scivis_studio::hasChild;
using vsr::scivis_studio::readChild;
using vsr::scivis_studio::readChildNode;
using vsr::scivis_studio::readEnumChild;
using vsr::scivis_studio::readNodeList;
using vsr::scivis_studio::readOptionalChild;
using vsr::scivis_studio::readOptionalChildNode;
using vsr::scivis_studio::readOptionalEnumChild;
using vsr::scivis_studio::writeChild;
using vsr::scivis_studio::writeChildNode;
using vsr::scivis_studio::writeNodeList;

// Strings, lists, paths //////////////////////////////////////////////////////

// Writes items as children "0", "1", ... of `parent[name]`. An empty list
// writes nothing: leaf-only serialization would drop the empty node anyway.
void writeStringList(vsr::core::DataNode &parent,
    const char *name,
    const std::vector<std::string> &items);

// An absent child reads as an empty list (true); a non-string item is false.
bool readStringList(const vsr::core::DataNode &parent,
    const char *name,
    std::vector<std::string> &out);

// std::filesystem::path travels as its generic_string().
void writePath(vsr::core::DataNode &parent,
    const char *name,
    const std::filesystem::path &path);
bool readPath(const vsr::core::DataNode &parent,
    const char *name,
    std::filesystem::path &out);

// A path list is a string list of generic_string()s.
void writePathList(vsr::core::DataNode &parent,
    const char *name,
    const std::vector<std::filesystem::path> &paths);
bool readPathList(const vsr::core::DataNode &parent,
    const char *name,
    std::vector<std::filesystem::path> &out);

// Opaque subtrees ////////////////////////////////////////////////////////////

// A DataTree is neither copyable nor movable, so payloads hold an opaque
// subtree (UI state, op results) through a shared pointer; null means absent.
using SubtreePtr = std::shared_ptr<vsr::core::DataTree>;

SubtreePtr makeSubtree();

// Deep-copies subtree->root() into parent[name]; a null subtree writes nothing.
void writeSubtree(
    vsr::core::DataNode &parent, const char *name, const SubtreePtr &subtree);

// Deep-copies child(name) into a fresh tree's root; null when absent.
SubtreePtr readSubtree(const vsr::core::DataNode &parent, const char *name);

// Result subtrees ////////////////////////////////////////////////////////////

// A payload with a `SubtreePtr results` member (ProjectOpReply, TaskCompleted,
// TaskFailed) carries an op- or task-specific *Result payload opaquely: the
// sender fills the subtree with toNode(result), the receiver decodes it with
// results<R>(). Null means the op or task has nothing to return.

// Replaces carrier.results with a fresh subtree holding toNode(result).
template <typename R, typename Carrier>
void setResults(Carrier &carrier, const R &result);

// Decodes carrier.results as an R; empty when there are no results or
// fromNode() rejects them.
template <typename R, typename Carrier>
std::optional<R> results(const Carrier &carrier);

// Field visitors /////////////////////////////////////////////////////////////

/*
 * A payload's wire shape is one fields(V &, T &) template: each call names a
 * child and the struct member behind it. Writer walks it to serialize, Reader
 * to parse, and the generic toNode()/fromNode() below wrap the walk, so a
 * payload never spells a wire name twice. The vocabulary is exactly what the
 * payloads need:
 *
 *   required(name, x)       scalar or path; absent or mistyped -> reject
 *   optional(name, x)       scalar or path; absent keeps x, mistyped -> reject
 *   optional(name, opt)     std::optional<T>: written when engaged, read as
 *                           engaged when present
 *   requiredEnum(name, e, toString, fromString)
 *   optionalEnum(name, e, toString, fromString)
 *                           enums travel as their toString() spelling; an
 *                           unknown spelling is rejected
 *   child(name, nested)     a nested payload or model type (its own
 *                           toNode()/fromNode(), found by ADL)
 *   optionalChild(name, opt)
 *                           std::optional<Nested>
 *   list(name, items)       std::vector of strings, paths or nested payloads
 *                           under "0", "1", ...; an empty list writes
 *                           nothing and an absent list reads as empty
 *   subtree(name, ptr)      an opaque SubtreePtr; null writes nothing
 *
 * Reader stops at the first failure and reports it through ok().
 */

class Writer
{
 public:
  explicit Writer(vsr::core::DataNode &node);

  template <typename T>
  void required(const char *name, const T &value);
  template <typename T>
  void optional(const char *name, const T &value);
  template <typename T>
  void optional(const char *name, const std::optional<T> &value);
  // Both visitors take toString and fromString so one fields() call serves
  // both; Writer uses only the former, Reader only the latter.
  template <typename E, typename FromString>
  void requiredEnum(const char *name,
      const E &value,
      const char *(*toString)(E),
      FromString &&fromString);
  template <typename E, typename FromString>
  void optionalEnum(const char *name,
      const E &value,
      const char *(*toString)(E),
      FromString &&fromString);
  template <typename T>
  void child(const char *name, const T &value);
  template <typename T>
  void optionalChild(const char *name, const std::optional<T> &value);
  void list(const char *name, const std::vector<std::string> &items);
  void list(const char *name, const std::vector<std::filesystem::path> &items);
  template <typename T>
  void list(const char *name, const std::vector<T> &items);
  void subtree(const char *name, const SubtreePtr &subtree);

 private:
  template <typename T>
  void write(const char *name, const T &value);
  void write(const char *name, const std::filesystem::path &value);

  vsr::core::DataNode &m_node;
};

class Reader
{
 public:
  explicit Reader(const vsr::core::DataNode &node);

  // False once any field was rejected; later calls are skipped.
  bool ok() const;

  template <typename T>
  void required(const char *name, T &out);
  template <typename T>
  void optional(const char *name, T &out);
  template <typename T>
  void optional(const char *name, std::optional<T> &out);
  template <typename E, typename FromString>
  void requiredEnum(const char *name,
      E &out,
      const char *(*toString)(E),
      FromString &&fromString);
  template <typename E, typename FromString>
  void optionalEnum(const char *name,
      E &out,
      const char *(*toString)(E),
      FromString &&fromString);
  template <typename T>
  void child(const char *name, T &out);
  template <typename T>
  void optionalChild(const char *name, std::optional<T> &out);
  void list(const char *name, std::vector<std::string> &out);
  void list(const char *name, std::vector<std::filesystem::path> &out);
  template <typename T>
  void list(const char *name, std::vector<T> &out);
  void subtree(const char *name, SubtreePtr &out);

 private:
  template <typename T>
  bool read(const char *name, T &out);
  bool read(const char *name, std::filesystem::path &out);

  const vsr::core::DataNode &m_node;
  bool m_ok{true};
};

// Well-formed when T has a fields() description; the generic codec below
// exists only for those T, so a hand-written toNode()/fromNode() pair for
// any other type is never ambiguous with it.
template <typename T>
using HasFields =
    decltype(fields(std::declval<Writer &>(), std::declval<T &>()));

// toNode() walks a Writer over fields(); fromNode() walks a Reader over a
// fresh T and assigns it on success, so absent optional children read as the
// struct's defaults and a rejected payload leaves `out` untouched.
template <typename T, typename = HasFields<T>>
void toNode(const T &payload, vsr::core::DataNode &node);
template <typename T, typename = HasFields<T>>
bool fromNode(const vsr::core::DataNode &node, T &out);

// Inlined definitions ////////////////////////////////////////////////////////

// Field visitors /////////////////////////////////////////////////////////////

inline Writer::Writer(vsr::core::DataNode &node) : m_node(node) {}

template <typename T>
inline void Writer::required(const char *name, const T &value)
{
  write(name, value);
}

template <typename T>
inline void Writer::optional(const char *name, const T &value)
{
  write(name, value);
}

template <typename T>
inline void Writer::optional(const char *name, const std::optional<T> &value)
{
  if (value)
    write(name, *value);
}

template <typename E, typename FromString>
inline void Writer::requiredEnum(
    const char *name, const E &value, const char *(*toString)(E), FromString &&)
{
  writeChild(m_node, name, std::string(toString(value)));
}

template <typename E, typename FromString>
inline void Writer::optionalEnum(
    const char *name, const E &value, const char *(*toString)(E), FromString &&)
{
  writeChild(m_node, name, std::string(toString(value)));
}

template <typename T>
inline void Writer::child(const char *name, const T &value)
{
  writeChildNode(m_node, name, value);
}

template <typename T>
inline void Writer::optionalChild(
    const char *name, const std::optional<T> &value)
{
  if (value)
    writeChildNode(m_node, name, *value);
}

inline void Writer::list(
    const char *name, const std::vector<std::string> &items)
{
  writeStringList(m_node, name, items);
}

inline void Writer::list(
    const char *name, const std::vector<std::filesystem::path> &items)
{
  writePathList(m_node, name, items);
}

template <typename T>
inline void Writer::list(const char *name, const std::vector<T> &items)
{
  writeNodeList(m_node, name, items);
}

inline void Writer::subtree(const char *name, const SubtreePtr &subtree)
{
  writeSubtree(m_node, name, subtree);
}

template <typename T>
inline void Writer::write(const char *name, const T &value)
{
  writeChild(m_node, name, value);
}

inline void Writer::write(const char *name, const std::filesystem::path &value)
{
  writePath(m_node, name, value);
}

inline Reader::Reader(const vsr::core::DataNode &node) : m_node(node) {}

inline bool Reader::ok() const
{
  return m_ok;
}

template <typename T>
inline void Reader::required(const char *name, T &out)
{
  if (m_ok)
    m_ok = read(name, out);
}

template <typename T>
inline void Reader::optional(const char *name, T &out)
{
  if (m_ok && hasChild(m_node, name))
    m_ok = read(name, out);
}

template <typename T>
inline void Reader::optional(const char *name, std::optional<T> &out)
{
  out.reset();
  if (!m_ok || !hasChild(m_node, name))
    return;
  T value{};
  m_ok = read(name, value);
  if (m_ok)
    out = std::move(value);
}

template <typename E, typename FromString>
inline void Reader::requiredEnum(
    const char *name, E &out, const char *(*)(E), FromString &&fromString)
{
  if (m_ok)
    m_ok =
        readEnumChild(m_node, name, out, std::forward<FromString>(fromString));
}

template <typename E, typename FromString>
inline void Reader::optionalEnum(
    const char *name, E &out, const char *(*)(E), FromString &&fromString)
{
  if (m_ok)
    m_ok = readOptionalEnumChild(
        m_node, name, out, std::forward<FromString>(fromString));
}

template <typename T>
inline void Reader::child(const char *name, T &out)
{
  if (m_ok)
    m_ok = readChildNode(m_node, name, out);
}

template <typename T>
inline void Reader::optionalChild(const char *name, std::optional<T> &out)
{
  out.reset();
  if (!m_ok || !hasChild(m_node, name))
    return;
  T value{};
  m_ok = readChildNode(m_node, name, value);
  if (m_ok)
    out = std::move(value);
}

inline void Reader::list(const char *name, std::vector<std::string> &out)
{
  if (m_ok)
    m_ok = readStringList(m_node, name, out);
}

inline void Reader::list(
    const char *name, std::vector<std::filesystem::path> &out)
{
  if (m_ok)
    m_ok = readPathList(m_node, name, out);
}

template <typename T>
inline void Reader::list(const char *name, std::vector<T> &out)
{
  if (m_ok)
    m_ok = readNodeList(m_node, name, out);
}

inline void Reader::subtree(const char *name, SubtreePtr &out)
{
  if (m_ok)
    out = readSubtree(m_node, name);
}

template <typename T>
inline bool Reader::read(const char *name, T &out)
{
  return readChild(m_node, name, out);
}

inline bool Reader::read(const char *name, std::filesystem::path &out)
{
  return readPath(m_node, name, out);
}

template <typename R, typename Carrier>
inline void setResults(Carrier &carrier, const R &result)
{
  carrier.results = makeSubtree();
  toNode(result, carrier.results->root());
}

template <typename R, typename Carrier>
inline std::optional<R> results(const Carrier &carrier)
{
  if (!carrier.results)
    return {};
  R result;
  if (!fromNode(carrier.results->root(), result))
    return {};
  return result;
}

template <typename T, typename>
inline void toNode(const T &payload, vsr::core::DataNode &node)
{
  Writer writer(node);
  // fields() takes T& so one description serves both directions; a Writer
  // only reads through it.
  fields(writer, const_cast<T &>(payload));
}

template <typename T, typename>
inline bool fromNode(const vsr::core::DataNode &node, T &out)
{
  T payload{};
  Reader reader(node);
  fields(reader, payload);
  if (!reader.ok())
    return false;
  out = std::move(payload);
  return true;
}

} // namespace vsr::scivis_studio::protocol
