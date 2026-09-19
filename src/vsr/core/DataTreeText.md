# Data Tree Text Encoding

The Text Encoding is the human-readable and human-editable form of a Data
Tree, a full peer of the Binary Encoding: either can be the carrier of a
`.vsr` file, and every reader accepts both. This document specifies the
grammar and every literal rule independently of the code in
`DataTreeText.hpp` / `DataTreeText.cpp`. The decision to have the encoding at
all, and its relationship to the binary form, is recorded in
[ADR 0039](../../../docs/adr/0039-give-the-data-tree-a-text-encoding.md);
the vocabulary used here (Data Tree, Data Node, Anonymous Node, External
Array, Subtree Replacement, Signal) is defined in [CONTEXT.md](CONTEXT.md).

## Example

```text
vsr-text 1
# A camera rig: two named nodes and one ordered sequence.
camera {
  position = float32_vec3 0 1.5 -4
  fovy = float32 0.785398185
  name = "shot_0_camera"
  enabled = true
}
keyframes {
  - {
    time = float32 0
    position = float32_vec3 0 1.5 -4
  }
  - {
    time = float32 1
    position = float32_vec3 0 1.5 -8
  }
}
weights = float32[] [
  0.25 0.5 0.75 1
]
material = material@3
notes {}
```

## API

| Operation | Method | Encoding |
|---|---|---|
| Write to bytes | `DataNode::write(buffer, Encoding)` | argument, default `Encoding::Binary` |
| Write to file | `DataNode::save(filename, Encoding)` | argument, default `Encoding::Binary` |
| Write to string | `DataNode::toText()` | always text |
| Read from bytes | `DataNode::read(buffer)` | detected |
| Read from file | `DataNode::load(filename)` | detected |
| Read from string | `DataNode::fromText(text)` | always text |

`DataTree` offers the same six methods, each forwarding to its root node.
Detection is "does the input begin with the header token `vsr-text`"; anything
else is the Binary Encoding. The file extension is never consulted and stays
`.vsr` for both encodings.

Node-level methods keep ADR 0027 semantics in both encodings: the node's own
name and value are not written, and the body of the file is the node's
children at top level with no enclosing braces. A subtree written from any
node reads back into any node, and a leaf writes an empty tree, which in text
is the header alone.

`DataTree::print()` writes the Text Encoding to standard output. The
`vsrPrint` tool prints a file the same way, and with a second argument
converts a file from whichever encoding it is in to the other.

## Reader contract

Reading is a Subtree Replacement, exactly as it is for the Binary Encoding.
The target node is emptied before decoding begins; a failure leaves it empty
and returns `false`; and exactly one `signalSubtreeReplaced()` is delivered
either way. Every failure logs a warning that names the line and column
(1-based, columns counted in bytes) of the offending token. A node with no
tree behind it refuses to read and returns `false` without a Signal, as it
does for binary.

## Grammar

Whitespace is insignificant except as a separator. A `#` outside a string
literal starts a comment that runs to the end of the line; a writer never
emits comments and a reader discards them.

```text
file      := header entry*
header    := "vsr-text" INTEGER            -- must be the first line
entry     := (name | "-") ("=" value)? block?
block     := "{" entry* "}"
name      := BARE | STRING
value     := STRING                        -- a string; type omitted
           | "true" | "false"              -- a bool; type omitted
           | TYPE components               -- scalar, vector, matrix
           | OBJECT_TYPE "@" INTEGER       -- object reference
           | TYPE "[" "]" "[" element* "]" -- array; count inferred
```

An entry must have a value, a block, or both. The absence of `=` always means
the absence of a value, so a value-less node is spelled as its name followed
by an empty block: `notes {}`. A bare name on its own is an error, which is
what catches a forgotten `=`.

The header must be exactly the token `vsr-text` followed by the integer
version, alone on the first line (a comment may follow on the same line).
This document describes version `1`. A reader accepts version 1 only and
fails with a clear message on anything higher, so an old build fails loudly on
a newer file.

### Entries and blocks

A named entry is a name, optionally `=` and a value, optionally a block of
children. An entry with a block and no value is an interior node; an entry
with a value and no block is a leaf. An entry with both a value and a
non-empty block is accepted, but the value is dropped with a warning that
names the line and column: a Data Node holds a value or children, never both
(see [Fidelity](#fidelity)). A writer never emits that shape.

An Anonymous Node is written as an entry beginning with the `-` marker in
place of the name. The reader mints a fresh anonymous name for it exactly as
appending an unnamed child does, so anonymity comes from the marker and
nowhere else. Anonymous entries may appear at the top level, which is the
block of the node being read into. The marker anywhere other than the start
of an entry (for instance as a value) is an error. A writer never emits a bare
marker without content: an anonymous value-less leaf is `- {}`.

Two entries in one block may not share a name. A duplicate name is an error
rather than a merge, so a copy-paste mistake cannot silently combine two
nodes.

### Names

A name is bare when it matches `[A-Za-z0-9_.-]+` and is not the single
character `-`. Any other name is double-quoted using the escapes below; UTF-8
bytes pass through untouched in both forms. A quoted name may not be empty.

A name of the synthesized anonymous shape, an integer between the
anonymous-name delimiters (`<7>`), is an error whether bare or quoted. Such
names are how the Binary Encoding spells an Anonymous Node, and a text file
must never describe a node the binary reader would reinterpret; use the `-`
marker instead.

A name containing the Data Path separator `/` is repaired to `_` on read,
as `DataNode::append()` does for every name it is given.

### Types

A type is spelled as its ANARI name with the `ANARI_` prefix stripped and
lowercased: `ANARI_FLOAT32_VEC3` is `float32_vec3`, `ANARI_UFIXED8_RGBA_SRGB`
is `ufixed8_rgba_srgb`, `ANARI_GEOMETRY` is `geometry`. This is a bijection
with `anari::toString()`; there are no aliases, and an unknown type name is
an error.

The type may be omitted in exactly two cases: a double-quoted literal is a
`string`, and `true` / `false` is a `bool`. Both may also be written with an
explicit type (`string "x"`, `bool true`). Numeric literals never infer a
type; `x = 1` is an error.

### Value literals

**Scalars, vectors, matrices, boxes, regions, quaternions.** Whitespace-
separated components with the arity taken from the type, no brackets:
`float32_vec3 1 2 3`, `float32_mat4 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1`,
`int32_box2 0 0 10 10`. Components are stored in ANARI's memory order. A
component count that does not match the type's arity is an error.

**Integers** (`int8` .. `uint64`, `data_type`, and their vectors) are decimal
with an optional sign. A literal out of range for its type is an error, as
is a negative literal for an unsigned type.

**Fixed-point types** (`fixed8` .. `ufixed64`, `ufixed8_*_srgb`) write their
underlying integer storage, not the normalized real they represent:
`ufixed8_vec4 255 128 0 255`.

**Floats** (`float32`, `float64`) use the shortest decimal that parses back
to the same value, so `0.1` stays `0.1` and no precision is lost. Not-a-number
and the infinities are the words `nan`, `inf`, and `-inf`. A reader accepts
any decimal or exponent spelling the C library parses (`1e-3`, `2.5E+7`).

**Half floats** (`float16`) write a decimal via conversion to `float32`, and
read back by converting the parsed float to the nearest half.

**Strings** are always double-quoted. The escapes are `\"`, `\\`, `\n`,
`\t`, and `\xHH` with exactly two hexadecimal digits. A writer escapes every
byte below 0x20 and 0x7f as `\xHH` (lowercase), and passes bytes from 0x80
upward through untouched, so UTF-8 survives. A raw newline inside a string is
an unterminated string, which is an error.

**Booleans** are `true` and `false`.

**Object references** are the object type followed by `@` and the index:
`geometry@12`, `material@3`. The type token is the literal's type, so a
scalar reference is written once, not `geometry geometry@12`.

**Pointer-typed values** (`void_pointer`, `function_pointer`,
`memory_deleter`, `status_callback`, `frame_completion_callback`, `library`,
`device`, the `*_list` types, and any other type the binary form merely
copies bytes for) have no text literal. A writer emits such a node as a
value-less node (`name {}`) and logs a warning; a reader rejects the type
name in a value position.

### Arrays

An array is its element type followed by `[]` with no count, then a
bracketed, whitespace-separated list of elements. The count is inferred, so
adding an element never leaves a count stale. The total component count must
be a multiple of the element type's arity or the array is an error.

Element literals follow the scalar rules above, including `true` / `false`
for `bool[]` and the full reference literal for object arrays:
`geometry[] [ geometry@1 geometry@2 ]`.

A writer lays elements out as follows: an empty array is `[]` on the same
line as the type; vector and matrix elements are written one element per
line, so a diff of a changed vertex is one line; scalar elements wrap at
eight values per line. The closing `]` sits at the entry's indentation.

```text
positions = float32_vec3[] [
  0 0 0
  1 0 0
  0 1 0
]
indices = uint32[] [
  0 1 2 2 1 3 3 1 4
  4 1 5
]
empty = float32[] []
```

External Arrays (array data a node refers to but does not own) are written by
value, exactly as the binary form does; a reader always produces owned
arrays.

### Writer formatting

A writer indents two spaces per block level, ends the file with exactly one
newline, and emits no comments. Every entry is on its own line; a block opens
on the entry's line (`name {`) and closes on its own line at the entry's
indentation. The value-less form is `name {}` on one line. Names are bare
whenever the bare rule allows and quoted otherwise. A file written by the
writer and re-saved without change is byte-identical.

## Hard errors

Every one of the following is a read failure: the node is left empty, one
Subtree Replacement Signal fires, and a warning names the line and column.

- Bad or missing header.
- Unsupported header version (any version above the one the reader knows).
- Duplicate name within one block.
- Unknown type name.
- A name of the reserved anonymous shape (`<7>`), bare or quoted.
- An empty quoted name.
- An entry with neither a value nor a block.
- Integer literal out of range for its type, or malformed.
- A numeric literal without a type.
- Component count not matching the type's arity.
- Array component count not a multiple of the element type's arity.
- Unterminated block, array, or string literal.
- An unknown or malformed escape sequence in a string.
- The `-` marker anywhere other than the start of an entry.
- A `}` with no open block, or any token where an entry is expected that
  cannot begin one.
- A pointer-typed type name in a value position.
- Any character outside the grammar (for instance `$`) outside a string.

## Fidelity

The Text Encoding carries exactly what the in-memory Data Tree can hold, and
so does the Binary Encoding; neither is richer than the other. A `DataNode`
is either a value or a container (`append()` clears a value, `setValue()`
removes children), so a text entry with both a value and children is not a
richer tree but a malformed one, and the reader drops the value with a
warning. A value-less leaf is preserved by both encodings.

The one asymmetry is the spelling of Anonymous Nodes. The binary record
stores a node's synthesized `<n>` name literally, while text writes the `-`
marker and the reader mints a fresh name. The consequences:

- **binary -> text -> binary** produces a binary that is *structurally*
  identical to the original (same shape, names, anonymity, types and values,
  with anonymous nodes matched by ordinal) but not necessarily
  *byte*-identical, because the synthesized names may differ.
- **text -> binary -> text** is byte-identical, since text never spells the
  synthesized names.
- **text -> text** (load then save) is byte-identical for a file in the
  writer's canonical form.
