## App Library (`vsr_app`)

`vsr_app` provides reusable application-level glue code on top of `vsr_scene`,
`vsr_io`, and `vsr_rendering`.

### High-Level Concepts

- `Context` bundles command-line options, scene state, ANARI device state,
  camera state, and offline render settings in one place.
- `ANARIDeviceManager` discovers and loads ANARI libraries, tracks live
  devices/extensions, and manages per-device `RenderIndex` instances.
- Command-line importer routing is centralized via
  `Context::parseCommandLine()` and `Context::setupSceneFromCommandLine()`.
- `OfflineRenderSequenceConfig` and `renderAnimationSequence()` provide
  sequence rendering for turntables and keyframed animations.
- Selection and camera pose helpers support viewer/editor workflows without
  duplicating utility code in each app.

### Why Use This Library

- You are building a CLI tool or viewer and want a ready-made app state model.
- You need consistent file-import and ANARI-device bootstrapping behavior across
  multiple executables.
- You want to render offline sequences without manually wiring scene updates,
  camera selection, AOV settings, and output paths.
