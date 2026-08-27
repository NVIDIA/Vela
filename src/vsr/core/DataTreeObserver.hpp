// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"

namespace vsr::core {

struct DataNode;

/*
 * Abstract observer interface that receives a Signal for each semantic edit
 * made to a DataTree. Subclass to keep a downstream system -- a UI panel, a
 * network peer, an undo stack -- in step with a tree without polling it.
 *
 * The node handed to a Signal is const: an Observer that mutated the tree it
 * is observing would be reentering a tree mid-notification, which is not
 * supported. Ask the node for its DataPath when you need to know where the
 * change happened; a path is only built when you ask, so an Observer that
 * discards most Signals pays nothing for the ones it drops.
 *
 * No Signal carries the previous value. Capturing one would copy a node's
 * array bytes on every set, an unbounded cost paid by every consumer to serve
 * only undo, which can snapshot on its own terms.
 *
 * This interface makes no thread-safety guarantee, consistent with the rest of
 * VSR: a Signal arrives on whichever thread performed the edit, and callers
 * synchronize.
 *
 * Example:
 *   struct PanelObserver : DataTreeObserver {
 *     void signalValueChanged(const DataNode &n) override
 *       { if (m_watched.isPrefixOf(n.path())) markDirty(n.path()); }
 *     // ... implement remaining pure virtuals ...
 *   };
 *   tree.setObserver(&observer);
 */
struct DataTreeObserver
{
  DataTreeObserver() = default;
  virtual ~DataTreeObserver() = default;

  virtual void signalValueChanged(const DataNode &n) = 0;
  virtual void signalValueCleared(const DataNode &n) = 0;
  virtual void signalNodeAdded(const DataNode &n) = 0;

  // Delivered before the node is destroyed, so that the subtree going away can
  // still be traversed. Removing an interior node produces one Signal about
  // that node, not one per descendant.
  virtual void signalNodeRemoved(const DataNode &n) = 0;

  // Everything beneath n was replaced wholesale, as by reading a subtree from
  // a file. The individual edits that built the new contents are suppressed:
  // replaying thousands of adds says less than "what you knew here is gone".
  //
  // Replacing the whole tree is the degenerate case, not a separate event: n
  // is then the root and n.path() is empty. An Observer watching one subtree
  // can ignore a replacement elsewhere by testing its own path against n's.
  virtual void signalSubtreeReplaced(const DataNode &n) = 0;

  // Bracket a run of edits that should produce at most one downstream rebuild.
  // Nesting is counted, so an outer batch is not ended by an inner one. Every
  // Signal above still arrives; only the work they trigger is coalesced.
  // Optional hooks (STYLEGUIDE section 13): an Observer that has nothing to
  // coalesce need not say so.
  virtual void signalUpdateBatchBegin() {}
  virtual void signalUpdateBatchEnd() {}

  VSR_NOT_COPYABLE(DataTreeObserver)
  VSR_DEFAULT_MOVEABLE(DataTreeObserver)
};

} // namespace vsr::core
