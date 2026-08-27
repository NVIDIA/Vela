# Serialize a Data Node as the tree it roots

Any Data Node can now be written, saved, read, and loaded on its own, and
`DataTree`'s four I/O methods are forwarders to the root node's. What a node
writes is the tree it roots and nothing else: its own name and its own value
are not part of its serialized form. The visible consequence is that saving a
leaf produces a file containing an empty tree, and that a subtree read into a
node called `surface` keeps that name no matter what the node was called when
it was written.

A reader who assumes this was overlooked will be tempted to fix it, so the
reasoning is worth stating. The format writes one record per leaf, consisting
of the leaf's own name and the Parent Path of its ancestors, and it never
writes anything about the node the traversal started from -- level 0 is
skipped. That was already true when the traversal root was always a tree's
root, which is why a subtree file and a whole-tree file are byte-identical
kinds of thing and either can be read back into either. Making a node's own
name travel with its bytes would mean adding a record type to describe it,
which is the format change ADR 0026 declined to make for Data Paths and
declines again here: every `.vsr` file, every embedded Archive, and every
`StructuredMessage` on the wire carries this encoding, and a compatibility
shim is not worth buying to make a node's file remember what it used to be
called.

Nor would remembering it be right. A Data Node's identity comes from its
position in the tree, and the position a loaded subtree lands in belongs to
whoever loaded it -- the same rule that already makes `DataNode`
copy-assignment keep the destination's name rather than the source's. The
alternative of rejecting a leaf outright was considered and dropped: a leaf
roots an empty tree, an empty tree is a perfectly good thing to write, and
failing would make the caller special-case a distinction the format does not
draw.

The consequence for `vsr_core` is that node I/O moves nodes and nothing else.
It does not write, require, or interpret a `DataTreeMetadata` node, so a
subtree saved out of a tree that has metadata at its root produces a file with
none. A caller who wants a self-describing subtree file calls
`writeDataTreeMetadata()` on the node first -- it already takes a `DataNode &`
-- and a convenience that pairs the two belongs in `vsr_io`, where Archive is
the vocabulary and a Data Tree file is merely the carrier.
