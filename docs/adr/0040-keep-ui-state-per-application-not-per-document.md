# Keep UI State per application, not per document

Every VSR imgui application keeps its **UI State**, meaning its Dock Layout
and each window's presentation settings, in its own file under the User
Config Directory. The file is keyed by a declared **Application Identifier**,
written automatically at exit, and restored at startup over the application's
Default Layout. Application Dumps and SciVis Studio Projects no longer write
UI State and ignore it when they load, so opening a document never moves a
panel. The SciVis Studio client already worked this way; this decision
extends it to every application, including the local Studio.

The alternative was to leave things as they were. Before this change, a
session dump or a Studio project restored the panel arrangement it was saved
with. That was rejected because an arrangement describes how one person
works in one application, not the data. With projects shared between people
and machines, each open would override the user's choice with whoever saved
last. The executable name and the window title were rejected as the per-app
key because renaming a binary or retitling a window would silently lose the
file.

## Consequences

- A window's settings are split in two. Presentation (visibility, overlays,
  tone mapping, gizmo mode and so on) goes to UI State. The few keys that refer
  to scene objects (a viewport's ANARI library, renderer and current camera)
  stay in the Application Dump, at the same path as before. Old dumps
  therefore still restore their camera and renderer, and old builds can
  still read new dumps.
- The font scale is an Application Preference, shared by every application and
  saved only when the user asks. It is not UI State.
- The Studio client's earlier `studioClientUI.vsr` is abandoned rather than
  migrated.
