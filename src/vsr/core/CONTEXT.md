# VSR Core

VSR Core holds the foundational data structures and vocabulary that every other
VSR library builds on. Terms defined here are upstream of all other contexts:
VSR I/O, VSR App, and VSR Rendering consume this language and must not redefine
it.

## Language

### Hierarchical Data

**Data Tree**:
A hierarchical container of typed values rooted at a single unnamed node.
A Data Tree is the unit of ownership and of serialization; nodes have no
meaning apart from the tree that holds them.
_Avoid_: Property tree, blackboard, dictionary

**Data Node**:
One position in a Data Tree, holding at most one typed value and an ordered
list of children. A Data Node's identity comes from its position in the tree,
not from its value.
_Avoid_: Entry, field, property

**Anonymous Node**:
A Data Node whose creator supplied no name, because its position among its
siblings is what identifies it. Anonymous Nodes are how a Data Tree expresses
an ordered sequence.
_Avoid_: Unnamed node, indexed node, array element

**External Array**:
Array data that a Data Node refers to but does not own. The owner may change
its contents at any time, so a Data Tree cannot vouch for an External Array's
value the way it can for the values it stores.
_Avoid_: Borrowed array, unowned array, array view

### Addressing

**Data Path**:
The location of one Data Node within its Data Tree, expressed as an ordered
sequence of Path Segments from the root. A Data Path denotes a position, not a
node: it stays meaningful when the node it named is gone, which is what makes
it the currency of change notification.
_Avoid_: Node path, key path, address

**Path Segment**:
One step of a Data Path, identifying a child of the node reached so far. A
segment names a child when the child has a name, and gives its ordinal position
when the child is an Anonymous Node.
_Avoid_: Path component, path element, key

**Parent Path**:
The Data Path of a node's parent. Serialized forms store a leaf's Parent Path
and its own name separately, so "path" alone is ambiguous in that setting and
the two terms are kept distinct.
_Avoid_: Prefix, ancestor path, path (unqualified)

### Change Notification

**Data Tree Observer**:
A party that a Data Tree notifies when its contents change. An Observer learns
what changed and where, and may read the tree; it does not participate in the
change and cannot alter it.
_Avoid_: Listener, callback, delegate, subscriber

**Signal**:
One notification delivered to a Data Tree Observer, reporting a single
semantic edit. A compound edit that touches many nodes is one Signal about the
subtree, not many Signals about its nodes.
_Avoid_: Event, notification, message
