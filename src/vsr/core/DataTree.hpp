// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/core/Any.hpp"
#include "vsr/core/DataPath.hpp"
#include "vsr/core/DataStream.hpp"
#include "vsr/core/DataTreeObserver.hpp"
#include "vsr/core/Forest.hpp"
#include "vsr/core/Logging.hpp"
#include "vsr/core/TypeMacros.hpp"
// std
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vsr::core {

struct DataTree;

/*
 * Named node in a hierarchical tree that holds an Any value and an ordered
 * list of named child nodes; supports typed get/set helpers and tree traversal.
 *
 * Every mutation below produces a Signal on the tree's DataTreeObserver, if
 * one is installed. Ask a node for its path() to learn where in the tree it
 * lives; a DataPath is only built when asked for.
 *
 * A node name may not contain the DataPath separator. A name that contains one
 * is repaired rather than rejected (see DataPath.hpp), on creation and on
 * name-based lookup alike, so that the string used to append a node is the
 * string that finds it again.
 *
 * Example:
 *   DataNode &n = tree.root()["material"];
 *   n["roughness"] = 0.4f;
 *   float r = n.child("roughness")->getValueAs<float>();
 */
struct DataNode
{
  using Ptr = std::unique_ptr<DataNode>;
  using Ref = ForestNodeRef<DataNode::Ptr>;

  DataNode() = default;
  ~DataNode();

  VSR_DEFAULT_MOVEABLE(DataNode)

  DataNode(const DataNode &o);
  DataNode &operator=(const DataNode &o);

  const std::string &name() const;

  // Where this node lives in its tree. Built on demand, never cached: an
  // Observer that filters to a small subtree does not pay for the paths it
  // would discard.
  DataPath path() const;

  void reset(); // clear value and remove children

  // Setting values //

  template <typename T>
  DataNode &operator=(const T &v);

  void setValue(const Any &v);
  template <typename T>
  void setValue(const T &v);
  void setValue(anari::DataType type, const void *v);

  template <typename T>
  void setValueAsArray(const std::vector<T> &v);
  template <typename T>
  void setValueAsArray(const T *v, size_t numElements);
  void setValueAsArray(anari::DataType type, const void *v, size_t numElements);

  // An External Array's contents belong to the caller, so a DataTree cannot
  // observe them changing. Call signalExternalArrayChanged() after writing
  // through the pointer to keep an Observer correct.
  void setValueAsExternalArray(
      anari::DataType type, const void *v, size_t numElements);
  void signalExternalArrayChanged();

  void setValueObject(anari::DataType type, size_t idx);

  void clearValue(); // only clear value if present

  // Getting values //

  template <typename T>
  T getValueAs() const;
  template <typename T>
  T getValueOr(const T &alt) const;
  const Any &getValue() const;

  // NOTE: If getting ANARI_STRING, pass ptr to std::string
  bool getValue(anari::DataType type, void *ptr) const;

  template <typename T>
  void getValueAsArray(const T **ptr, size_t *size) const;
  void getValueAsArray(
      anari::DataType *type, const void **ptr, size_t *size) const;

  void getValueAsObjectIdx(anari::DataType *type, size_t *idx) const;

  bool holdsObjectIdx() const;
  bool holdsArray() const;
  bool holdsExternalArray() const; // cannot get mutable pointer to data
  anari::DataType arrayType() const;
  bool empty() const;
  bool isLeaf() const;

  // Access children //

  size_t numChildren() const;

  DataNode *child(std::string_view childName);
  const DataNode *child(const std::string_view childName) const;
  DataNode *child(size_t childIdx);
  const DataNode *child(size_t childIdx) const;
  DataNode &operator[](std::string_view childName);

  DataNode &append(std::string_view newChildName = "");
  void remove(std::string_view name);
  void remove(DataNode &childNode);

  // Algorithms //

  void traverse(std::function<bool(DataNode &n, int level)> &&fcn);
  void forall_children(std::function<void(DataNode &)> &&fcn);
  void foreach_child(std::function<void(DataNode &)> &&fcn);

  // Const counterparts, so that an Observer handed a doomed subtree by
  // signalNodeRemoved() can walk it before it is destroyed. Named rather than
  // overloaded, following Forest's own _const suffix: a generic lambda is
  // convertible to both signatures, and the overload it picked would be a
  // coin toss at every call site.
  void traverse_const(
      std::function<bool(const DataNode &n, int level)> &&fcn) const;
  void forall_children_const(std::function<void(const DataNode &)> &&fcn) const;
  void foreach_child_const(std::function<void(const DataNode &)> &&fcn) const;

#ifndef VSR_DATA_TREE_TEST_MODE // allow access to self() in unit tests only (!)
 private:
#endif
  Ref self() const;
  Ref parent() const;

 private:
  friend struct DataTree;

  DataNode(const std::string &name); // only Data[Node|Tree] can construct nodes

  // Mutation that no Observer can see is not offered publicly: a caller
  // handed a raw pointer into a node's storage can change an owned value
  // without a Signal. The loader reaches these through friendship.
  void *setValueAsArray(anari::DataType type, size_t numElements);
  template <typename T>
  void getValueAsArray(T **ptr, size_t *size);
  void getValueAsArray(anari::DataType *type, void **ptr, size_t *size);

  // Signal helpers //

  DataTreeObserver *observer() const;
  void signalValueChanged() const;
  void signalValueCleared() const;
  void signalNodeAdded() const;
  void signalNodeRemoved() const;

  // Mutation helpers that deliberately produce no Signal of their own, used
  // where a compound edit reports itself as one semantic event.
  void clearValueSilently();
  void removeChildren(); // signals one removal per direct child, not per node

  DataNode *parentNode() const; // nullptr at the root
  size_t ordinal() const; // position among siblings, for an Anonymous Node

  // Data members //

  struct NodeData
  {
    Ref self;
    DataTree *tree{nullptr};
    std::string name;
    bool anonymous{false};
    Any value;
    std::vector<uint8_t> arrayBytes;
    anari::DataType arrayType{ANARI_UNKNOWN};
    const void *externalArray{nullptr};
    size_t externalArraySize{0};
  } m_data;
};

using DataTreeVisitorEntryFunction =
    std::function<bool(DataNode &n, int level)>;
using DataTreeVisitorExitFunction = std::function<void(DataNode &n, int level)>;

/*
 * Non-copyable, non-movable tree of DataNode instances rooted at a single
 * unnamed root node; supports serialization and depth-first traversal.
 *
 * A tree notifies at most one DataTreeObserver of each semantic edit made to
 * it. A tree with no Observer installed costs one null test per edit, so the
 * archive and serialization paths that build trees in bulk are unaffected.
 *
 * Mutating a tree from inside a Signal is not supported, and neither is
 * touching a tree from more than one thread: like the rest of VSR, a DataTree
 * makes no thread-safety guarantee and callers synchronize.
 *
 * Example:
 *   DataTree tree;
 *   tree.setObserver(&myObserver);
 *   tree.root()["width"] = 1920;
 *   tree.root()["height"] = 1080;
 *   tree.traverse([](DataNode &n, int lvl){ return true; }, {});
 */
struct DataTree
{
  DataTree();
  ~DataTree();

  // Root node access //

  DataNode &root();
  const DataNode &root() const;

  // Addressing //

  // Resolve a DataPath back to the node it denotes, or nullptr when no such
  // node exists. Deliberately non-creating, unlike DataNode::operator[]: there
  // is no sane node for an ordinal segment to create, since nothing answers
  // what the eighth child of a three-child node should be.
  DataNode *node(const DataPath &path);
  const DataNode *node(const DataPath &path) const;

  // Change notification //

  // The Observer is registered non-owning, so it must outlive its
  // registration or be removed first -- a destroyed Observer left installed
  // is a dangling pointer the tree will call through. Installing a second
  // Observer replaces the first; pass nullptr to stop observing.
  void setObserver(DataTreeObserver *observer);
  DataTreeObserver *observer() const;

  // Bracket a run of edits that a consumer downstream of the Observer should
  // coalesce. Nesting is counted, so an inner bracket does not end an outer
  // one. Compound tree operations self-bracket when the number of Signals they
  // produce scales with the size of a tree -- DataNode copy-assignment does,
  // DataNode::reset() does not. Loading does neither: it collapses to a single
  // signalTreeReplaced().
  void beginUpdateBatch();
  void endUpdateBatch();

  // Traverse nodes //

  void traverse(DataTreeVisitorEntryFunction &&onNodeEntry,
      DataTreeVisitorExitFunction &&onNodeExit = {});
  void traverse(DataNode::Ref start,
      DataTreeVisitorEntryFunction &&onNodeEntry,
      DataTreeVisitorExitFunction &&onNodeExit = {});

  // Buffer I/O //

  bool write(std::vector<std::byte> &buffer);
  bool read(const std::vector<std::byte> &buffer);

  // File I/O //

  bool save(const char *filename);
  bool load(const char *filename);

  // Visual inspection //

  void print();

  VSR_NOT_MOVEABLE(DataTree)
  VSR_NOT_COPYABLE(DataTree)

 private:
  friend struct DataNode;

  bool saveImpl(DataWriter &writer);
  bool loadImpl(DataReader &reader);

  // The serialized form stores a leaf's Parent Path -- the NUL-separated chain
  // of its ancestors' names -- not a DataPath. The two are deliberately
  // distinct spellings of a location, converted only here (ADR 0026).
  void writeDataNode(DataWriter &writer,
      const DataNode &node,
      const std::string &parentPath) const;
  std::string printableParentPath(const std::string &parentPath) const;

  // The Observer to signal right now, or nullptr when there is none or when
  // signals are being collapsed into one signalTreeReplaced().
  DataTreeObserver *activeObserver() const;

  Forest<std::unique_ptr<DataNode>> m_tree;
  DataTreeObserver *m_observer{nullptr};
  int m_updateBatchDepth{0};
  bool m_signalsSuppressed{false};
};

/*
 * Scoped DataTree update batch: brackets a run of edits so that a consumer
 * downstream of the Observer can coalesce the work they trigger, and ends the
 * bracket however the scope is left.
 *
 * Example:
 *   {
 *     DataTreeUpdateBatch batch(tree);
 *     for (auto &v : values)
 *       tree.root()[v.name] = v.value;
 *   }
 */
struct DataTreeUpdateBatch
{
  explicit DataTreeUpdateBatch(DataTree &tree);
  ~DataTreeUpdateBatch();

  VSR_NOT_COPYABLE(DataTreeUpdateBatch)
  VSR_NOT_MOVEABLE(DataTreeUpdateBatch)

 private:
  DataTree *m_tree{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

// DataNode //

inline DataNode::~DataNode() = default;

inline DataNode::DataNode(const DataNode &o) : m_data(o.m_data)
{
  m_data.self = {};
  m_data.tree = nullptr;
}

inline DataNode &DataNode::operator=(const DataNode &o)
{
  if (this == &o)
    return *this;

  auto selfRef = m_data.self;
  auto *tree = m_data.tree;
  const bool heldSomething = !empty();

  // Copying a subtree produces a Signal per node copied, so it brackets
  // itself: its Signal count scales with the size of the source tree.
  if (tree)
    tree->beginUpdateBatch();

  if (selfRef)
    removeChildren();

  // Copy-assignment copies a node's *value*; a node's identity comes from its
  // position in the tree, so the destination keeps its own name and anonymity
  // along with its self-reference and its tree. Taking the source's name
  // instead would re-key the node under a name its parent's child list was
  // never built with, and re-aim every DataPath already held for it.
  auto name = std::move(m_data.name);
  const bool anonymous = m_data.anonymous;
  m_data = o.m_data;
  m_data.self = selfRef;
  m_data.tree = tree;
  m_data.name = std::move(name);
  m_data.anonymous = anonymous;

  if (!empty())
    signalValueChanged();
  else if (heldSomething)
    signalValueCleared();

  if (selfRef) {
    for (size_t i = 0; i < o.numChildren(); ++i) {
      if (auto *child = o.child(i))
        append(child->name()) = *child;
    }
  }

  if (tree)
    tree->endUpdateBatch();

  return *this;
}

inline const std::string &DataNode::name() const
{
  return m_data.name;
}

inline DataPath DataNode::path() const
{
  std::vector<const DataNode *> ancestry;
  for (const DataNode *n = this; n != nullptr; n = n->parentNode()) {
    auto ref = n->self();
    if (!ref || ref->isRoot())
      break;
    ancestry.push_back(n);
  }

  DataPath path;
  for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it) {
    const DataNode *n = *it;
    path =
        n->m_data.anonymous ? path.child(n->ordinal()) : path.child(n->name());
  }

  return path;
}

inline void DataNode::reset()
{
  removeChildren();
  clearValue();
}

template <typename T>
inline DataNode &DataNode::operator=(const T &v)
{
  setValue(Any(v));
  return *this;
}

template <>
inline DataNode &DataNode::operator=(const std::string &v)
{
  setValue(Any(v.c_str()));
  return *this;
}

inline void DataNode::setValue(const Any &v)
{
  removeChildren();
  clearValueSilently();
  m_data.value = v;
  signalValueChanged();
}

template <typename T>
inline void DataNode::setValue(const T &v)
{
  setValue(Any(v));
}

inline void DataNode::setValue(anari::DataType type, const void *v)
{
  setValue(Any(type, v));
}

template <typename T>
inline void DataNode::setValueAsArray(const std::vector<T> &v)
{
  setValueAsArray(v.data(), v.size());
}

template <typename T>
inline void DataNode::setValueAsArray(const T *v, size_t numElements)
{
  setValueAsArray(anari::ANARITypeFor<T>::value, v, numElements);
}

inline void DataNode::setValueAsArray(
    anari::DataType type, const void *v, size_t numElements)
{
  auto *ptr = setValueAsArray(type, numElements);
  std::memcpy(ptr, v, m_data.arrayBytes.size());
  signalValueChanged();
}

inline void *DataNode::setValueAsArray(anari::DataType type, size_t numElements)
{
  // Silent by design: the caller still has to fill the storage this returns,
  // so the value has not finished changing yet. Callers that hand the filled
  // array back through the public overload get the Signal there.
  removeChildren();
  clearValueSilently();
  m_data.arrayType = type;
  m_data.arrayBytes.resize(numElements * anari::sizeOf(type));
  return m_data.arrayBytes.data();
}

inline void DataNode::setValueAsExternalArray(
    anari::DataType type, const void *v, size_t numElements)
{
  removeChildren();
  clearValueSilently();
  m_data.arrayType = type;
  m_data.externalArray = v;
  m_data.externalArraySize = numElements * anari::sizeOf(type);
  signalValueChanged();
}

inline void DataNode::signalExternalArrayChanged()
{
  if (!holdsExternalArray()) {
    logWarning(
        "DataNode '%s' holds no External Array to signal a change for; the "
        "values a DataTree owns signal for themselves",
        m_data.name.c_str());
    return;
  }

  signalValueChanged();
}

inline void DataNode::setValueObject(anari::DataType type, size_t idx)
{
  setValue(Any(type, idx));
}

inline void DataNode::clearValue()
{
  const bool heldSomething = !empty();
  clearValueSilently();
  if (heldSomething)
    signalValueCleared();
}

inline void DataNode::clearValueSilently()
{
  m_data.value.reset();
  m_data.arrayBytes.clear();
  m_data.arrayType = ANARI_UNKNOWN;
  m_data.externalArray = nullptr;
  m_data.externalArraySize = 0;
}

template <typename T>
inline T DataNode::getValueAs() const
{
  return getValue().getAs<T>();
}

template <>
inline std::string DataNode::getValueAs() const
{
  return getValue().getString();
}

template <typename T>
inline T DataNode::getValueOr(const T &alt) const
{
  return getValue().getValueOr(alt);
}

template <>
inline std::string DataNode::getValueOr(const std::string &alt) const
{
  return getValue().is(ANARI_STRING) ? getValueAs<std::string>() : alt;
}

inline const Any &DataNode::getValue() const
{
  return m_data.value;
}

inline bool DataNode::getValue(anari::DataType type, void *ptr) const
{
  const bool invalidQueryType = type == ANARI_STRING_LIST
      || type == ANARI_DATA_TYPE_LIST || anari::isObject(type);

  if (invalidQueryType || !m_data.value.is(type))
    return false;
  else if (m_data.value.is(ANARI_STRING)) {
    *(std::string *)ptr = getValue().getCStr();
    return true;
  } else {
    std::memcpy(ptr, m_data.value.data(), anari::sizeOf(type));
    return true;
  }
}

template <typename T>
inline void DataNode::getValueAsArray(T **ptr, size_t *size)
{
  static_assert(!anari::isObject(anari::ANARITypeFor<T>::value),
      "getValueAsArray<T> does not work for ANARI object types");

  *ptr = nullptr;
  *size = 0;

  const bool compatible = holdsArray()
      && m_data.arrayType == anari::ANARITypeFor<T>::value
      && !m_data.arrayBytes.empty();

  if (compatible) {
    *ptr = (T *)m_data.arrayBytes.data();
    *size = m_data.arrayBytes.size() / anari::sizeOf(m_data.arrayType);
  }
}

template <typename T>
inline void DataNode::getValueAsArray(const T **ptr, size_t *size) const
{
  static_assert(!anari::isObject(anari::ANARITypeFor<T>::value),
      "getValueAsArray<T> does not work for ANARI object types");
  if (!holdsArray() || m_data.arrayType != anari::ANARITypeFor<T>::value) {
    *ptr = nullptr;
    *size = 0;
  } else {
    if (!m_data.arrayBytes.empty()) {
      *ptr = (const T *)m_data.arrayBytes.data();
      *size = m_data.arrayBytes.size() / anari::sizeOf(m_data.arrayType);
    } else {
      *ptr = (const T *)m_data.externalArray;
      *size = m_data.externalArraySize / anari::sizeOf(m_data.arrayType);
    }
  }
}

inline void DataNode::getValueAsArray(
    anari::DataType *type, void **ptr, size_t *size)
{
  *type = ANARI_UNKNOWN;
  *ptr = nullptr;
  *size = 0;

  if (holdsArray() && !m_data.arrayBytes.empty()) {
    *type = m_data.arrayType;
    *ptr = m_data.arrayBytes.data();
    *size = m_data.arrayBytes.size() / anari::sizeOf(m_data.arrayType);
  }
}

inline void DataNode::getValueAsArray(
    anari::DataType *type, const void **ptr, size_t *size) const
{
  *type = ANARI_UNKNOWN;
  *ptr = nullptr;
  *size = 0;

  if (holdsArray()) {
    *type = m_data.arrayType;
    if (!m_data.arrayBytes.empty()) {
      *ptr = m_data.arrayBytes.data();
      *size = m_data.arrayBytes.size() / anari::sizeOf(m_data.arrayType);
    } else {
      *ptr = m_data.externalArray;
      *size = m_data.externalArraySize / anari::sizeOf(m_data.arrayType);
    }
  }
}

inline void DataNode::getValueAsObjectIdx(
    anari::DataType *type, size_t *idx) const
{
  if (!holdsObjectIdx()) {
    *type = ANARI_UNKNOWN;
    *idx = INVALID_INDEX;
  } else {
    *type = m_data.value.type();
    *idx = m_data.value.getAsObjectIndex();
  }
}

inline bool DataNode::holdsObjectIdx() const
{
  return m_data.value.holdsObject();
}

inline bool DataNode::holdsArray() const
{
  return arrayType() != ANARI_UNKNOWN;
}

inline bool DataNode::holdsExternalArray() const
{
  return holdsArray() && m_data.externalArray != nullptr;
}

inline anari::DataType DataNode::arrayType() const
{
  return m_data.arrayType;
}

inline bool DataNode::empty() const
{
  return !m_data.value && !holdsArray();
}

inline bool DataNode::isLeaf() const
{
  return self()->isLeaf();
}

inline size_t DataNode::numChildren() const
{
  size_t num = 0;
  ::vsr::core::foreach_child(self(), [&](auto &n) { num++; });
  return num;
}

inline DataNode *DataNode::child(std::string_view childName)
{
  return const_cast<DataNode *>(std::as_const(*this).child(childName));
}

inline const DataNode *DataNode::child(std::string_view childName) const
{
  std::string sanitized;
  if (dataNodeNameNeedsSanitizing(childName)) {
    sanitized = sanitizeDataNodeName(childName);
    childName = sanitized;
  }
  auto n = find_first_child(
      self(), [&](DataNode::Ptr &cn) { return cn->name() == childName; });
  return n ? (**n).get() : nullptr;
}

inline DataNode &DataNode::operator[](std::string_view childName)
{
  auto *n = child(childName);
  return n ? *n : append(childName);
}

inline DataNode *DataNode::child(size_t childIdx)
{
  return const_cast<DataNode *>(std::as_const(*this).child(childIdx));
}

inline const DataNode *DataNode::child(size_t childIdx) const
{
  size_t i = 0;
  auto n = find_first_child(
      self(), [&](DataNode::Ptr &cn) { return i++ == childIdx; });
  return n ? (**n).get() : nullptr;
}

inline DataNode &DataNode::append(std::string_view newChildName)
{
  clearValue();

  std::string name = sanitizeDataNodeName(newChildName);

  // Anonymity is recorded on the node rather than inferred from its name: the
  // synthesized name is still what goes to disk, so nothing in the serialized
  // form or in name-based lookup moves. The name a node carries stays an
  // opaque counter, and a DataPath spells it as an ordinal instead (ADR 0025).
  const bool anonymous = name.empty();
  if (anonymous) {
    static int counter = 0;
    name = '<' + std::to_string(counter++) + '>';
  }

  if (auto *c = child(name); c != nullptr)
    return *c;

  auto ref = self()->insert_last_child(DataNode::Ptr{new DataNode(name)});
  auto &newNode = ***ref;
  newNode.m_data.self = ref;
  newNode.m_data.tree = m_data.tree;
  newNode.m_data.anonymous = anonymous;
  newNode.signalNodeAdded();
  return newNode;
}

inline void DataNode::remove(std::string_view name)
{
  if (auto *c = child(name); c != nullptr)
    remove(*c);
}

inline void DataNode::remove(DataNode &childNode)
{
  // Signal first: an Observer given the node on its way out can still read it
  // and walk what is going away with it. Only a real child is announced, so
  // that a caller passing an unrelated node cannot fabricate a Signal.
  if (childNode.parentNode() == this)
    childNode.signalNodeRemoved();
  self()->container()->erase(childNode.self());
}

inline void DataNode::removeChildren()
{
  auto ref = self();
  if (!ref || ref->isLeaf())
    return;

  if (observer() != nullptr) {
    ::vsr::core::foreach_child(
        ref, [](DataNode::Ptr &cn) { cn->signalNodeRemoved(); });
  }

  ref->erase_subtree();
}

inline DataNode *DataNode::parentNode() const
{
  auto ref = self();
  auto parentRef = ref ? ref->parent() : Ref{};
  return parentRef ? (**parentRef).get() : nullptr;
}

inline size_t DataNode::ordinal() const
{
  auto ref = self();
  auto parentRef = ref ? ref->parent() : Ref{};
  if (!parentRef)
    return 0;

  size_t position = 0;
  find_first_child(parentRef, [&](DataNode::Ptr &cn) {
    if (cn.get() == this)
      return true;
    position++;
    return false;
  });

  return position;
}

inline void DataNode::traverse(
    std::function<bool(DataNode &n, int level)> &&fcn)
{
  self()->container()->traverse(
      self(), [&](auto &ref, int level) { return fcn(**ref, level); });
}

inline void DataNode::forall_children(std::function<void(DataNode &)> &&fcn)
{
  ::vsr::core::forall_children(self(), [&](auto &ref) { fcn(*ref); });
}

inline void DataNode::foreach_child(std::function<void(DataNode &)> &&fcn)
{
  ::vsr::core::foreach_child(self(), [&](auto &ref) { fcn(*ref); });
}

inline void DataNode::traverse_const(
    std::function<bool(const DataNode &n, int level)> &&fcn) const
{
  self()->container()->traverse_const(
      self(), [&](const auto &ref, int level) { return fcn(**ref, level); });
}

inline void DataNode::forall_children_const(
    std::function<void(const DataNode &)> &&fcn) const
{
  ::vsr::core::forall_children_const(
      self(), [&](const auto &ref) { fcn(*ref); });
}

inline void DataNode::foreach_child_const(
    std::function<void(const DataNode &)> &&fcn) const
{
  ::vsr::core::foreach_child_const(self(), [&](const auto &ref) { fcn(*ref); });
}

inline DataNode::Ref DataNode::self() const
{
  return m_data.self;
}

inline DataNode::Ref DataNode::parent() const
{
  return m_data.self->parent();
}

inline DataNode::DataNode(const std::string &name)
{
  m_data.name = name;
}

inline DataTreeObserver *DataNode::observer() const
{
  return m_data.tree ? m_data.tree->activeObserver() : nullptr;
}

inline void DataNode::signalValueChanged() const
{
  if (auto *o = observer(); o != nullptr)
    o->signalValueChanged(*this);
}

inline void DataNode::signalValueCleared() const
{
  if (auto *o = observer(); o != nullptr)
    o->signalValueCleared(*this);
}

inline void DataNode::signalNodeAdded() const
{
  if (auto *o = observer(); o != nullptr)
    o->signalNodeAdded(*this);
}

inline void DataNode::signalNodeRemoved() const
{
  if (auto *o = observer(); o != nullptr)
    o->signalNodeRemoved(*this);
}

// DataTree //

inline DataTree::DataTree() : m_tree(DataNode::Ptr{new DataNode("<root>")})
{
  root().m_data.self = m_tree.root();
  root().m_data.tree = this;
}

inline DataTree::~DataTree() = default;

inline DataNode &DataTree::root()
{
  return ***m_tree.root();
}

inline const DataNode &DataTree::root() const
{
  return ***m_tree.root();
}

inline DataNode *DataTree::node(const DataPath &path)
{
  DataNode *current = &root();
  for (auto segment : path) {
    current = segment.isOrdinal() ? current->child(segment.ordinal())
                                  : current->child(segment.name());
    if (current == nullptr)
      return nullptr;
  }
  return current;
}

inline const DataNode *DataTree::node(const DataPath &path) const
{
  return const_cast<DataTree *>(this)->node(path);
}

inline void DataTree::setObserver(DataTreeObserver *observer)
{
  m_observer = observer;
}

inline DataTreeObserver *DataTree::observer() const
{
  return m_observer;
}

inline void DataTree::beginUpdateBatch()
{
  if (m_updateBatchDepth++ != 0)
    return;
  if (auto *o = activeObserver(); o != nullptr)
    o->signalUpdateBatchBegin();
}

inline void DataTree::endUpdateBatch()
{
  if (m_updateBatchDepth == 0 || --m_updateBatchDepth != 0)
    return;
  if (auto *o = activeObserver(); o != nullptr)
    o->signalUpdateBatchEnd();
}

inline DataTreeObserver *DataTree::activeObserver() const
{
  return m_signalsSuppressed ? nullptr : m_observer;
}

inline void DataTree::traverse(DataTreeVisitorEntryFunction &&onNodeEntry,
    DataTreeVisitorExitFunction &&onNodeExit)
{
  traverse(root().self(), std::move(onNodeEntry), std::move(onNodeExit));
}

inline void DataTree::traverse(DataNode::Ref start,
    DataTreeVisitorEntryFunction &&onNodeEntry,
    DataTreeVisitorExitFunction &&onNodeExit)
{
  // clang-format off
  m_tree.traverse(
      start,
      [&](auto &n, int l) { return onNodeEntry(**n, l); },
      [&](auto &n, int l) { if (onNodeExit) onNodeExit(**n, l); });
  // clang-format on
}

inline bool DataTree::save(const char *filename)
{
  FileWriter writer(filename);
  if (!writer)
    return false;
  return saveImpl(writer);
}

inline bool DataTree::write(std::vector<std::byte> &buffer)
{
  BufferWriter writer;
  bool res = saveImpl(writer);
  buffer = writer.take();
  return res;
}

inline bool DataTree::load(const char *filename)
{
  FileReader reader(filename);
  if (!reader)
    return false;
  return loadImpl(reader);
}

inline bool DataTree::read(const std::vector<std::byte> &buffer)
{
  BufferReader reader(buffer);
  return loadImpl(reader);
}

inline void DataTree::print()
{
  traverse([](vsr::core::DataNode &node, int level) {
    if (level == 0)
      return true;

    for (int i = 1; i < level; i++)
      printf("    ");

    if (!node.isLeaf())
      printf("%s:\n", node.name().c_str());
    else {
      printf("%s: ", node.name().c_str());

      if (node.holdsObjectIdx()) {
        anari::DataType type = ANARI_UNKNOWN;
        size_t index = 0;
        node.getValueAsObjectIdx(&type, &index);
        printf("%s @%zu", anari::toString(type), index);
      } else if (node.holdsArray()) {
        anari::DataType type = ANARI_UNKNOWN;
        const void *data = nullptr;
        size_t size = 0;
        node.getValueAsArray(&type, &data, &size);
        printf("%s[%zu]", anari::toString(type), size);
      } else {
        auto &value = node.getValue();
        printf("%s", anari::toString(value.type()));
        if (value.is(ANARI_STRING))
          printf(" | \"%s\"", value.getCStr());
        else if (value.is<bool>())
          printf(" | %s", value.get<bool>() ? "true" : "false");
        else if (value.is<int>())
          printf(" | %d", value.get<int>());
        else if (value.is<uint32_t>())
          printf(" | %d", value.get<uint32_t>());
        else if (value.is<float>())
          printf(" | %f", value.get<float>());
        else if (value.is<double>())
          printf(" | %f", value.get<double>());
      }

      printf("\n");
    }

    return true;
  });

  printf("\n");
}

inline bool DataTree::saveImpl(DataWriter &writer)
{
  // Count + write number of leaf nodes //

  size_t numLeafNodes = 0;
  m_tree.traverse(m_tree.root(), [&](auto &nodeRef, int level) {
    if (level == 0)
      return true;
    else if (auto &node = **nodeRef; nodeRef.isLeaf())
      numLeafNodes++;
    return true;
  });

  writer.write(&numLeafNodes, sizeof(size_t), 1);

  // Travese tree and write nodes //

  // This is a Parent Path, not a DataPath: the chain of ancestor names, with
  // the leaf's own name written separately. The file format is deliberately
  // left byte-identical to what it was before Data Paths existed (ADR 0026).
  std::string parentPath;
  parentPath.reserve(256);
  m_tree.traverse(
      m_tree.root(),
      [&](auto &nodeRef, int level) {
        if (level == 0)
          return true;

        if (auto &node = **nodeRef; nodeRef.isLeaf()) {
          writeDataNode(writer, node, parentPath);
          numLeafNodes++;
        } else {
          const auto &name = node.name();
          std::copy(name.begin(), name.end(), std::back_inserter(parentPath));
          parentPath.push_back('\0');
        }

        return true;
      },
      [&](auto &nodeRef, int level) {
        if (level == 0)
          return;
        else if (level == 1) {
          parentPath.clear();
          return;
        }

        if (!nodeRef.isLeaf())
          parentPath.resize(
              parentPath.size() - ((*nodeRef)->name().size() + 1));
      });

  return true;
}

inline bool DataTree::loadImpl(DataReader &reader)
{
  auto splitNullSeparatedStrings =
      [](const char *buffer, size_t bufferSize) -> std::vector<std::string> {
    std::vector<std::string> result;
    for (size_t start = 0; start < bufferSize;) {
      size_t end = start;
      while (end < bufferSize && buffer[end] != '\0')
        ++end;
      if (end > start)
        result.emplace_back(buffer + start, end - start);
      start = end + 1;
    }

    return result;
  };

  // Files written before Anonymous Nodes carried a flag identify them only by
  // the shape of the name the writer synthesized, so the inference lives here
  // and nowhere else.
  auto appendLoadedNode = [](DataNode &parent,
                              const std::string &name) -> DataNode & {
    auto &node = parent.append(name);
    node.m_data.anonymous = isDelimitedNumber(node.name(), '<', '>');
    return node;
  };

  /////////////////////////////////////////////////////////////////////////////

  // Loading is one semantic event, not thousands: the individual edits below
  // are collapsed into the single signalTreeReplaced() at the end.
  m_signalsSuppressed = true;

  m_tree.root()->erase_subtree();
  auto &rootNode = root();

  size_t numLeafNodes = 0;
  auto r = reader.read(&numLeafNodes, sizeof(size_t), 1);

  for (size_t i = 0; i < numLeafNodes; i++) {
    size_t size = 0;

    // name
    r = reader.read(&size, sizeof(size_t), 1);
    std::string name(size, '\0');
    r = reader.read(name.data(), sizeof(char), size);

    // path
    r = reader.read(&size, sizeof(size_t), 1);
    std::string fullPath(size, '\0');
    r = reader.read(fullPath.data(), sizeof(char), size);

    // isArray
    uint8_t isArray = 0;
    r = reader.read(&isArray, sizeof(uint8_t), 1);

    // type
    anari::DataType type = ANARI_UNKNOWN;
    r = reader.read(&type, sizeof(anari::DataType), 1);

    // Create node //

    auto parentPath =
        splitNullSeparatedStrings(fullPath.c_str(), fullPath.size());

    DataNode *parentPtr = &rootNode;
    for (auto &loc : parentPath) {
      if (!loc.empty())
        parentPtr = &appendLoadedNode(*parentPtr, loc);
    }

    auto &node = appendLoadedNode(*parentPtr, name);

    // Read node value //

    if (isArray) {
      // array size + data
      r = reader.read(&size, sizeof(size_t), 1);
      void *dataPtr = node.setValueAsArray(type, size);
      r = reader.read(dataPtr, anari::sizeOf(type), size);
    } else {
      if (anari::isObject(type)) {
        size_t idx = INVALID_INDEX;
        r = reader.read(&idx, sizeof(size_t), 1);
        node.setValueObject(type, idx);
      } else if (type == ANARI_STRING) {
        r = reader.read(&size, sizeof(size_t), 1);
        std::string str(size, '\0');
        r = reader.read(str.data(), sizeof(char), size);
        node = str.c_str();
      } else if (type != ANARI_UNKNOWN) {
        constexpr int MAX_SIZE = 16 * sizeof(float);
        if (anari::sizeOf(type) <= MAX_SIZE) {
          uint8_t data[MAX_SIZE];
          r = reader.read(data, anari::sizeOf(type), 1);
          node.setValue(type, (void *)data);
        } else {
          printf("ERROR: type %s is too large to read when parsing DataTree\n",
              anari::toString(type));
          fflush(stdout);
          abort();
        }
      }
    }
  }

  // Suppression is lifted before the one Signal a load does deliver, so that
  // an Observer reading the tree back sees it behaving normally.
  m_signalsSuppressed = false;
  if (auto *o = activeObserver(); o != nullptr)
    o->signalTreeReplaced();

  return true;
}

inline void DataTree::writeDataNode(DataWriter &writer,
    const DataNode &node,
    const std::string &parentPath) const
{
  // name
  size_t size = node.name().size();
  writer.write(&size, sizeof(size_t), 1);
  writer.write(node.name().c_str(), sizeof(char), size);
  // parent path
  size = parentPath.size();
  writer.write(&size, sizeof(size_t), 1);
  writer.write(parentPath.c_str(), sizeof(char), size);
  // isArray
  const uint8_t isArray = node.holdsArray();
  writer.write(&isArray, sizeof(uint8_t), 1);

  if (isArray) {
    // array info + data
    anari::DataType type = ANARI_UNKNOWN;
    const void *data = nullptr;
    size = 0;
    node.getValueAsArray(&type, &data, &size);
    writer.write(&type, sizeof(anari::DataType), 1);
    writer.write(&size, sizeof(size_t), 1);
    writer.write(data, sizeof(uint8_t), size * anari::sizeOf(type));
  } else {
    // value info + data
    auto &v = node.getValue();
    auto type = v.type();
    writer.write(&type, sizeof(anari::DataType), 1);
    if (anari::isObject(type)) {
      size_t idx = v.getAsObjectIndex();
      writer.write(&idx, sizeof(size_t), 1);
    } else if (type == ANARI_STRING) {
      const char *data = v.getCStr();
      size = data ? std::strlen(data) : 0;
      writer.write(&size, sizeof(size_t), 1);
      writer.write(data, sizeof(char), size);
    } else if (type != ANARI_UNKNOWN) {
      writer.write(v.data(), anari::sizeOf(type), 1);
    }
  }
}

inline std::string DataTree::printableParentPath(
    const std::string &parentPath) const
{
  std::string printable = parentPath;
  std::replace(printable.begin(), printable.end(), '\0', DATA_PATH_SEPARATOR);
  return printable;
}

// DataTreeUpdateBatch //

inline DataTreeUpdateBatch::DataTreeUpdateBatch(DataTree &tree) : m_tree(&tree)
{
  m_tree->beginUpdateBatch();
}

inline DataTreeUpdateBatch::~DataTreeUpdateBatch()
{
  m_tree->endUpdateBatch();
}

} // namespace vsr::core
