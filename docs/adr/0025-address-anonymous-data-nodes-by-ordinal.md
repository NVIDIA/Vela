# Address anonymous Data Nodes by ordinal, not by name

Introducing change notification on `DataTree` requires a stable way to tell a
client *where* a change happened, which means Data Paths become a public
currency clients hold onto. That forced a decision about anonymous nodes that
the tree had previously been able to leave unresolved.

`DataNode::append()` with no name synthesizes one from a process-global
counter, producing names like `<4712>`. Nothing about that name is meaningful:
it is unrelated to the node's position among its siblings, it differs between
two runs that build the same tree, and it leaks the global allocation order of
every anonymous node in the process. It is stable across a save and load only
because the serialized form stores it literally and the loader re-appends it.

This matters more than it first appears, because anonymous nodes are where the
interesting data lives. `io/serialization`, `io/archives`, `io/animation`,
`network/messages/ParameterChange.cpp`, and `scivisStudio/CameraRig.cpp` all
build their sequences with bare `append()`. A path through a serialized scene
is therefore mostly counters: `/objectDB/surface/<4712>/name`. Handing that to
a client as the answer to "what changed?" tells it almost nothing, and any
client that stored such a path would find it worthless in the next session.

A Path Segment is therefore a name *or* an ordinal. A named child appears in a
Data Path by name; an anonymous child appears by its position among its
siblings, spelled `[3]`. The ordinal is computed from the tree at the moment
the path is built, so it never consults the counter and never depends on it.

The obvious alternative was to fix the counter instead -- name anonymous
children `<0>`, `<1>`, ... by sibling index, which would make the names
themselves positional and leave paths purely name-based. That was tried before:
`DataTree.hpp` still carries the `numChildren()` version under `#if 0` with the
note that it "breaks in VSR context export," and `test_DataTree.cpp` still
carries the two disabled assertions that expected it. Whatever broke was never
diagnosed. Reviving it would also make a node's name change when an earlier
sibling is removed, which is worse than a path changing: names are serialized,
paths are not.

The other alternative was to accept opaque `<N>` segments and treat a Data Path
as an identity token clients compare but never read. That keeps the addressing
scheme uniform, but it gives up the thing the feature is for. A client watching
`/objectDB/surface/[3]` can act on it; a client watching
`/objectDB/surface/<4712>` can only wait to be handed the same string again.

The consequence to be aware of is that an ordinal segment is invalidated by
insertion or removal of an earlier sibling, so a stored Data Path into an
anonymous sequence can silently come to mean a different node. That is a true
statement about a positional container and it is better stated than concealed:
the alternative schemes did not make the aliasing go away, they only made it
harder to see. Clients that need an identity surviving sibling edits should
give their nodes names.

Nothing about this reaches the serialized form; see ADR 0026.
