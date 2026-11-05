# Import ANARI Captures directly, with Vela's own reader

`import_ANARI_CAPTURE()` reads an ANARI Capture, the text record the ANARI-SDK
`capture` device writes at a render, and builds VSR scene objects and layer
nodes from it itself. Two existing pieces of code look like they should be
reused, and neither is.

- **Not by replaying through `anari_vsr`.** `anari_vsr` already turns ANARI
  calls into a VSR scene, but it sits above `vsr_rendering` and on helium, so
  `vsr_io` would depend on a device library. Its mapping is also not exact: it
  keeps parameters that were unset. An Import also has to report what it could
  not represent, and replaying calls has nowhere to put that report. The only
  translation the importer really needs is the instancing walk. Everything
  else maps one to one, because VSR objects store ANARI-typed parameters.
- **Not with the SDK's `anari_capture_diff` parser.** That parser is internal
  to the SDK. Using it would take a new public API in the SDK and an SDK newer
  than the 0.15 Vela requires, so the importer would sit behind a build flag
  most builds leave off. The format is versioned and documented, and it was
  kept small so that a reader stays small. Vela reads the versions it knows
  (1 and 2) and rejects any other with an error, so a format change cannot be
  misread.

Decisions that follow:

- **Only what the application committed is live.** The VSR parameters an
  object seeds with stay on the object for editors to show, but they are
  disabled. A device sees exactly the captured parameters.
- **Frame and renderer state are reported, not applied.** Renderer subtypes
  and parameters belong to the device the application ran on, not the one
  viewing the import.
- **Problems are reported, and the rest still imports.** A missing or
  wrongly sized blob, an `<opaque>` value, or a motion instance is recorded in
  the Import Report under its capture-diff path, and the import carries on.
  Only a file that cannot be read as a capture at all imports nothing.

`anari_vsr` and its `live_capture.vsr` (a Scene Archive) are unrelated to
this, and this decision leaves them unchanged.
