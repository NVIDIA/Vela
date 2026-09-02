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
 * Building blocks shared by every payload's toNode()/fromNode() pair. Writers
 * put each scalar under a named child (ADR 0027); readers never create
 * children and never throw on untrusted input -- absent or mistyped children
 * report `false`. On failure the output is unspecified; decode<T>() discards
 * it.
 *
 * Example:
 *   void toNode(const Foo &f, DataNode &n)
 *   {
 *     writeChild(n, "requestId", f.requestId);
 *     toNode(f.target, n["target"]);
 *   }
 *   bool fromNode(const DataNode &n, Foo &f)
 *   {
 *     return readChild(n, "requestId", f.requestId)
 *         && readChild(n, "target", f.target);
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

// readChild() with a fallback for optional children.
template <typename T>
T readChildOr(
    const vsr::core::DataNode &parent, const char *name, const T &alt);

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

// Scene identity /////////////////////////////////////////////////////////////

// SceneObjectRef: {type: ANARI type name, objectIndex: uint64}
void toNode(
    const vsr::scivis_studio::SceneObjectRef &ref, vsr::core::DataNode &node);
bool fromNode(
    const vsr::core::DataNode &node, vsr::scivis_studio::SceneObjectRef &ref);

// SceneNodeRef: {layerName: string, nodeIndex: uint64}. nodeIndex is the
// server's layer forest index; the layer transfers rebuild the Structural
// Mirror with the same numbering, so it names the same node on both sides.
void toNode(
    const vsr::scivis_studio::SceneNodeRef &ref, vsr::core::DataNode &node);
bool fromNode(
    const vsr::core::DataNode &node, vsr::scivis_studio::SceneNodeRef &ref);

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

template <typename T>
inline T readChildOr(
    const vsr::core::DataNode &parent, const char *name, const T &alt)
{
  T value{};
  return readChild(parent, name, value) ? value : alt;
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

} // namespace vsr::scivis_studio::protocol
