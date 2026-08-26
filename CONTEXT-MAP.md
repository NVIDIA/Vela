# Context Map

## Contexts

- [VSR Core](./src/vsr/core/CONTEXT.md) — foundational data structures and
  the vocabulary every other VSR library builds on
- [VSR I/O](./src/vsr/io/CONTEXT.md) — moves data between VSR and external
  representations and persists native VSR state
- [VSR App](./src/vsr/app/CONTEXT.md) — composes reusable application state
  around VSR scenes, animations, rendering, and interaction
- [VSR Rendering](./src/vsr/rendering/CONTEXT.md) — turns VSR scenes into
  images via render indexes and an image pipeline of composable passes
- [SciVis Studio](./src/apps/interactive/scivisStudio/CONTEXT.md) — organizes
  scientific-visualization assets into projects and shots

## Relationships

- **VSR I/O → VSR Core**: VSR I/O persists and transports the Data Trees that
  VSR Core defines. VSR Core's language is upstream of every other context and
  is never redefined by them; where VSR I/O's serialized forms need a narrower
  term, it is defined in VSR Core alongside the one it narrows.
- **SciVis Studio → VSR I/O**: SciVis Studio uses generic VSR I/O mechanisms,
  but owns its application-specific project, dataset, rig, and shot language.
  SciVis Studio terminology does not define the generic VSR I/O vocabulary.
- **VSR App → VSR I/O**: VSR App composes Archives produced by VSR I/O with
  application-level state to create Application Dumps. VSR I/O does not depend
  on or create Application Dumps.
