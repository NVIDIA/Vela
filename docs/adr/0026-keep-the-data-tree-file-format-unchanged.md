# Keep the Data Tree file format unchanged when introducing Data Paths

Data Paths (ADR 0025) give `vsr_core` a first-class notion of "where a node
lives," and `DataTree`'s serializer already computes something that looks like
one. `writeDataNode` emits, for every leaf, the leaf's own name plus a
NUL-separated chain of its ancestors' names, and `loadImpl` splits that chain
back apart to rebuild the tree. Unifying the two -- writing a Data Path string
where the chain is today -- is the change a reader of this code will assume was
simply overlooked. It was not: the format is deliberately left byte-identical,
and the serializer's local variable is renamed from `path` to `parentPath` to
say so.

The rationale is that the two things are not the same concept. What the file
stores is a Parent Path composed purely of names, and it can be, because the
loader reconstructs anonymous children by appending them in document order --
their positions are implicit in the order of the records, so the file has no
need to name them and no need for ordinals. A Data Path, by contrast, exists to
be handed to a client that was not present when the tree was built, which is
exactly why it needs ordinals. Making the file speak the richer language would
be storing an answer the loader already has.

Against that near-zero gain sits a real cost. Every `.vsr` file, every embedded
Archive, and every `StructuredMessage` on the wire carries this encoding, and
this repository has already paid for one format-adjacent rename: ADR 0024 and
the `LEGACY_DATA_TREE_METADATA_NODE` constant in `DataTreeMetadata.hpp` exist
solely to keep reading files written before the TSD-to-VSR rename. A second
compatibility shim, bought to make two internal representations rhyme, is not a
trade worth making.

The consequence is that `vsr_core` deliberately carries two spellings for a
location: the Data Path in memory and the NUL-separated Parent Path on disk.
They are converted at the serialization boundary and nowhere else. Anyone
tempted to collapse them should establish first that the file format can absorb
an ordinal segment -- it currently has no way to express one -- and that the
resulting reader still accepts every file written before the change.
