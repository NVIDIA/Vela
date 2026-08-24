# Context Map

## Contexts

- [VSR I/O](./src/vsr/io/CONTEXT.md) — moves data between VSR and external
  representations and persists native VSR state
- [VSR App](./src/vsr/app/CONTEXT.md) — composes reusable application state
  around VSR scenes, animations, rendering, and interaction
- [VSR Rendering](./src/vsr/rendering/CONTEXT.md) — turns VSR scenes into
  images via render indexes and an image pipeline of composable passes
- [SciVis Studio](./src/apps/interactive/scivisStudio/CONTEXT.md) — organizes
  scientific-visualization assets into projects and shots

## Relationships

- **SciVis Studio → VSR I/O**: SciVis Studio uses generic VSR I/O mechanisms,
  but owns its application-specific project, dataset, rig, and shot language.
  SciVis Studio terminology does not define the generic VSR I/O vocabulary.
- **VSR App → VSR I/O**: VSR App composes Archives produced by VSR I/O with
  application-level state to create Application Dumps. VSR I/O does not depend
  on or create Application Dumps.
