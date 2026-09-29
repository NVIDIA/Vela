# VSR App

VSR App composes reusable application state around VSR scenes, animations,
rendering, and interaction. It owns application-level persistence without
making the lower-level VSR I/O library depend on application concepts.

## Language

**Application Dump**:
A native application-level file snapshot containing required Scene and
Animation Manager Archives plus other application state; individual
applications may extend it. Application Dumps belong to VSR App rather than
VSR I/O, and reconstruction loads the scene before animations that bind to it.
_Avoid_: Context dump, scene dump, archive

### Application-owned state

**Vela**:
The product: the applications a user installs and runs. It is distinct from
VSR, the libraries and file formats those applications are built on.
Per-user state belongs to Vela; library and format names stay VSR.
_Avoid_: VSR (for the product)

**User Config Directory**:
The one per-user directory where Vela keeps everything it remembers
between runs: Application Preferences, each application's UI State, and
the user's own color maps and scripts.
_Avoid_: VSR config directory, settings folder

**UI State**:
How one application's interface is arranged and presented: its dock layout
and each window's presentation settings. It belongs to the application, never
to a document, so opening an Application Dump or a Project leaves it alone,
and it never travels between a Studio client and server.
_Avoid_: Layout (for the whole), Client Layout, session, workspace

**Dock Layout**:
The part of UI State that records where windows sit and how they are docked.
_Avoid_: ImGui ini, layout file

**Default Layout**:
The built-in Dock Layout an application starts with when it has no saved UI
State, and returns to on request.

**Application Identifier**:
The stable name an application declares so its UI State is kept apart from
every other application's.
_Avoid_: Executable name, window title

**Application Preferences**:
Settings shared by every VSR application, such as the ANARI devices on offer
and the font scale, saved only when the user asks.
_Avoid_: App settings, defaults (for the file)
