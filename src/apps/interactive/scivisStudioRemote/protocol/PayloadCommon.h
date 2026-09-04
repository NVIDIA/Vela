// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace vsr::scivis_studio::protocol {

/*
 * Building blocks shared by every payload codec. Writers put each scalar
 * under a named child (ADR 0027); readers never create children and never
 * throw on untrusted input. One policy for every field: a required child
 * that is absent or mistyped is a malformed payload (false); an optional
 * child that is absent leaves the struct's default, and one that is present
 * but mistyped is malformed too. A hand-written fromNode() leaves its output
 * unspecified on failure (decode<T>() discards it); the fields()-derived one
 * leaves it untouched.
 *
 * A payload describes its wire shape once, as a fields() template over a
 * visitor, and toNode()/fromNode() are derived from it (see Field visitors
 * below). The scalar/list/path helpers here are what the visitors, and the
 * few hand-written codecs (Shot, ProjectSnapshot), are built from.
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

// Scalars ////////////////////////////////////////////////////////////////////

// `parent[name] = value`. Pass std::string, not a string literal (Any has no
// char[N] constructor); size_t travels as uint64_t.
template <typename T>
void writeChild(vsr::core::DataNode &parent, const char *name, const T &value);

// False when the child is absent or its value is not a T.
template <typename T>
bool readChild(const vsr::core::DataNode &parent, const char *name, T &out);

bool hasChild(const vsr::core::DataNode &parent, const char *name);

// An optional scalar: absent keeps `out` as is (true); present but mistyped
// is a malformed payload (false).
template <typename T>
bool readOptionalChild(
    const vsr::core::DataNode &parent, const char *name, T &out);

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

// Enums //////////////////////////////////////////////////////////////////////

// Strict inverse of a toString(E) over the contiguous enumerators
// first..last (inclusive): the E whose toString() spelling equals `name`,
// empty otherwise. Model parsers fall back to a default on unknown text,
// which would hide a corrupt payload; every protocol *FromString uses this.
template <typename E>
std::optional<E> enumFromName(
    std::string_view name, E first, E last, const char *(*toString)(E));

// Enum fields travel as strings. `fromString` is the payload's own parser
// returning std::optional<E>; an unknown string reads as false.
template <typename E, typename FromString>
bool readEnumChild(const vsr::core::DataNode &parent,
    const char *name,
    E &out,
    FromString &&fromString);

// readEnumChild() with the readOptionalChild() contract.
template <typename E, typename FromString>
bool readOptionalEnumChild(const vsr::core::DataNode &parent,
    const char *name,
    E &out,
    FromString &&fromString);

// ANARI type names ("ANARI_CAMERA", ...), the wire form of anari::DataType.
const char *toString(anari::DataType type);
std::optional<anari::DataType> anariTypeFromString(std::string_view name);

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
 *   child(name, nested)     a nested payload (its own toNode()/fromNode())
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

// Scene identity /////////////////////////////////////////////////////////////

// SceneObjectRef: {type: ANARI type name, objectIndex: uint64}
template <typename V>
void fields(V &v, vsr::scivis_studio::SceneObjectRef &ref);

// SceneNodeRef: {layerName: string, nodeIndex: uint64}. nodeIndex is the
// server's layer forest index; the layer transfers rebuild the Structural
// Mirror with the same numbering, so it names the same node on both sides.
template <typename V>
void fields(V &v, vsr::scivis_studio::SceneNodeRef &ref);

// Nested payloads ////////////////////////////////////////////////////////////

// toNode(value, parent[name]) for any type with a toNode() overload.
template <typename T>
void writeChildNode(
    vsr::core::DataNode &parent, const char *name, const T &value);

// fromNode(*child(name), out) for any type with a fromNode() overload; false
// when the child is absent.
template <typename T>
bool readChildNode(const vsr::core::DataNode &parent, const char *name, T &out);

// An optional nested payload, same contract as readOptionalChild().
template <typename T>
bool readOptionalChildNode(
    const vsr::core::DataNode &parent, const char *name, T &out);

// Ordered lists travel as children "0", "1", ... of parent[name]; an empty
// list writes nothing (leaf-only serialization would drop the node anyway)
// and an absent child reads as an empty list. The two-argument forms call
// toNode()/fromNode() on each item; pass `writeItem(item, node)` /
// `readItem(node, item) -> bool` for item types without those overloads.
template <typename T>
void writeNodeList(
    vsr::core::DataNode &parent, const char *name, const std::vector<T> &items);
template <typename T, typename WriteItem>
void writeNodeList(vsr::core::DataNode &parent,
    const char *name,
    const std::vector<T> &items,
    WriteItem &&writeItem);
template <typename T>
bool readNodeList(
    const vsr::core::DataNode &parent, const char *name, std::vector<T> &out);
template <typename T, typename ReadItem>
bool readNodeList(const vsr::core::DataNode &parent,
    const char *name,
    std::vector<T> &out,
    ReadItem &&readItem);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline void writeChild(
    vsr::core::DataNode &parent, const char *name, const T &value)
{
  if constexpr (std::is_same_v<T, size_t> && !std::is_same_v<T, uint64_t>)
    parent[name] = static_cast<uint64_t>(value);
  else
    parent[name] = value;
}

template <typename T>
inline bool readChild(
    const vsr::core::DataNode &parent, const char *name, T &out)
{
  const auto *c = parent.child(name);
  if (!c)
    return false;
  if constexpr (std::is_same_v<T, size_t> && !std::is_same_v<T, uint64_t>) {
    if (!c->getValue().is<uint64_t>())
      return false;
    out = static_cast<size_t>(c->getValueAs<uint64_t>());
  } else {
    if (!c->getValue().is<T>())
      return false;
    out = c->getValueAs<T>();
  }
  return true;
}

inline bool hasChild(const vsr::core::DataNode &parent, const char *name)
{
  return parent.child(name) != nullptr;
}

template <typename T>
inline bool readOptionalChild(
    const vsr::core::DataNode &parent, const char *name, T &out)
{
  return !hasChild(parent, name) || readChild(parent, name, out);
}

template <typename E>
inline std::optional<E> enumFromName(
    std::string_view name, E first, E last, const char *(*toString)(E))
{
  using U = std::underlying_type_t<E>;
  for (U v = U(first); v <= U(last); ++v) {
    if (name == toString(E(v)))
      return E(v);
  }
  return {};
}

template <typename E, typename FromString>
inline bool readEnumChild(const vsr::core::DataNode &parent,
    const char *name,
    E &out,
    FromString &&fromString)
{
  std::string text;
  if (!readChild(parent, name, text))
    return false;
  const std::optional<E> parsed = fromString(text);
  if (!parsed)
    return false;
  out = *parsed;
  return true;
}

template <typename E, typename FromString>
inline bool readOptionalEnumChild(const vsr::core::DataNode &parent,
    const char *name,
    E &out,
    FromString &&fromString)
{
  return !hasChild(parent, name)
      || readEnumChild(parent, name, out, std::forward<FromString>(fromString));
}

template <typename T>
inline void writeChildNode(
    vsr::core::DataNode &parent, const char *name, const T &value)
{
  toNode(value, parent[name]);
}

template <typename T>
inline bool readChildNode(
    const vsr::core::DataNode &parent, const char *name, T &out)
{
  const auto *c = parent.child(name);
  return c && fromNode(*c, out);
}

template <typename T>
inline bool readOptionalChildNode(
    const vsr::core::DataNode &parent, const char *name, T &out)
{
  return !hasChild(parent, name) || readChildNode(parent, name, out);
}

template <typename T>
inline void writeNodeList(
    vsr::core::DataNode &parent, const char *name, const std::vector<T> &items)
{
  writeNodeList(parent, name, items, [](const T &item, vsr::core::DataNode &n) {
    toNode(item, n);
  });
}

template <typename T, typename WriteItem>
inline void writeNodeList(vsr::core::DataNode &parent,
    const char *name,
    const std::vector<T> &items,
    WriteItem &&writeItem)
{
  if (items.empty())
    return;
  auto &list = parent[name];
  for (size_t i = 0; i < items.size(); ++i)
    writeItem(items[i], list[std::to_string(i)]);
}

template <typename T>
inline bool readNodeList(
    const vsr::core::DataNode &parent, const char *name, std::vector<T> &out)
{
  return readNodeList(
      parent, name, out, [](const vsr::core::DataNode &n, T &item) {
        return fromNode(n, item);
      });
}

template <typename T, typename ReadItem>
inline bool readNodeList(const vsr::core::DataNode &parent,
    const char *name,
    std::vector<T> &out,
    ReadItem &&readItem)
{
  out.clear();
  const auto *list = parent.child(name);
  if (!list)
    return true;
  bool ok = true;
  list->foreach_child_const([&](const vsr::core::DataNode &item) {
    T value;
    if (!readItem(item, value)) {
      ok = false;
      return;
    }
    out.push_back(std::move(value));
  });
  return ok;
}

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

// Scene identity /////////////////////////////////////////////////////////////

template <typename V>
inline void fields(V &v, vsr::scivis_studio::SceneObjectRef &ref)
{
  v.requiredEnum("type", ref.type, toString, anariTypeFromString);
  v.required("objectIndex", ref.objectIndex);
}

template <typename V>
inline void fields(V &v, vsr::scivis_studio::SceneNodeRef &ref)
{
  v.required("layerName", ref.layerName);
  v.required("nodeIndex", ref.nodeIndex);
}

} // namespace vsr::scivis_studio::protocol
