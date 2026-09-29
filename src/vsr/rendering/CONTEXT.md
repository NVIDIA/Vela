# VSR Rendering

Turns VSR scenes into images: render indexes feed ANARI devices, and an image
pipeline of composable passes produces the final per-pixel output.

## Language

**Image Pipeline**:
An ordered sequence of Image Passes that all read and write a shared set of
per-pixel buffers, executed in insertion order each frame.

**Image Pass**:
A single, independently enable-able stage of an Image Pipeline.

**Outline**:
An image-space silhouette border around an object or primitive, derived from
the rendered ID buffers. An Outline traces what was rendered; it does not
project geometry.
_Avoid_: highlight, selection border

**Box Outline**:
The wireframe formed by the twelve edges of an axis-aligned box, projected
through a camera view. A Box Outline is a generic box drawing; it is not
inherently a bounding box — bounding is one client's interpretation.
_Avoid_: bounding box pass, box wireframe

**Device Identifier**:
The string naming which ANARI device to create, written
`[Device Subtype@]ANARI Library`; an omitted subtype means `default`, and
`default@X` identifies the same device as `X`.
_Avoid_: library name, device name, device spec

**ANARI Library**:
The loadable ANARI implementation named by the part of a Device Identifier
after `@`; one library can provide several Device Subtypes.
_Avoid_: backend, device (when meaning the library)

**Device Subtype**:
The name of one device an ANARI Library provides, given by the part of a
Device Identifier before `@`.
_Avoid_: device type, device flavor
