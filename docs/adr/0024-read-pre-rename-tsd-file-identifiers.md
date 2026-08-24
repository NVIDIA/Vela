# Read pre-rename TSD file identifiers, but never write them

The TSD -> VSR rename changed three identifiers that outlive the source tree
because they are baked into files already on disk: the metadata envelope node
(`__tsd_metadata` -> `__vsr_metadata`), every schema string (`tsd.*` ->
`vsr.*`), and the project Archive extension (`.tsd` -> `.vsr`, including
`project.tsd` -> `project.vsr`). Files written before the rename remain
readable: nothing about their payload changed, only what they are called.

Loading accepts both spellings; saving only ever emits the VSR spelling. The
old names are a read-side compatibility shim, not a supported output format,
so no code path needs a "which generation am I writing?" branch and no file
can be produced that mixes the two.

The read-side acceptance is centralized rather than spread over call sites.
`vsr::core::readDataTreeMetadata` falls back to the legacy envelope node and
normalizes a `tsd.*` schema to its `vsr.*` equivalent before returning, so
every loader and validator compares against the current schema constants
without knowing the rename happened. The current node wins when a file somehow
carries both. Filename fallback is the one thing metadata cannot cover, so
SciVis Studio resolves each project-relative read through
`resolveProjectFileForRead`, which prefers `.vsr` and falls back to `.tsd`;
"missing file" diagnostics still name the current spelling.

The consequence is that a legacy project directory opens untouched and is
migrated to the current names by the next explicit save, in the same spirit as
[0006](0006-migrate-embedded-datasets-on-save.md). Until that save, the
directory keeps its `.tsd` files, so Save As treats a directory holding a
legacy manifest as already containing a project.
