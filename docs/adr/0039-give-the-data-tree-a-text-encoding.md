# Give the Data Tree a Text Encoding as a peer of the binary one

A `vsr::core::DataTree` gains a second serialized form, the **Text Encoding**:
a brace-delimited, human-readable and human-editable representation that any
node can write and read alongside the existing **Binary Encoding**. The two
are peers. Either may be the carrier of a `.vsr` file, the extension does not
change, and every reader that accepts one accepts the other: `read()` and
`load()` detect the encoding from the leading bytes, while `write()` and
`save()` take an explicit choice that defaults to binary. No existing producer
switches its default, and the network wire stays binary.

The obvious alternatives were a distinct extension, a JSON or YAML dialect,
and an inspect-only dump converted by a tool. A distinct extension was rejected
because an Archive is defined as independent of its carrier (see the VSR I/O
glossary) and SciVis Studio's project layout hardcodes `.vsr` paths in a dozen
places; a reader that has to sniff anyway gains nothing from a second suffix.
JSON and YAML were rejected because neither can spell an ANARI type, a typed
array, or an object reference without an escaping convention that is worse to
edit than a bespoke grammar; the point of the format is that a person opens
it. An inspect-only dump was rejected because a format nobody can load back
is a debugging aid, not a representation, and would leave the binary form as
the only thing an editor could not touch.

The Text Encoding is deliberately *strictly richer* than the Binary Encoding
rather than equivalent to it. Binary writes one record per leaf and so drops
the value of any interior node (ADR 0026, ADR 0027); a nested syntax preserves
those values for free, and crippling it to match would be a cost with no
buyer. The consequence is that `binary -> text -> binary` round-trips
byte-identically while `text -> binary -> text` may not, and the reference
document says so. Anonymous Nodes appear as marked unnamed entries rather
than by their synthesized `<n>` names, and names of that shape are rejected
by the text reader so that a file cannot describe a node the binary reader
would re-interpret. Array data is always inline text; dataset-scale trees are
legal but not the target, and a sidecar or opaque-bulk extension can be added
under the header's version without breaking any reader written today.

A required first line carries the encoding name and a version. The
repository has paid once already for a format-adjacent change without one
(ADR 0024), so the Text Encoding starts life with the field the binary form
never had.
