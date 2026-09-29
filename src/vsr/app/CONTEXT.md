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
between runs: Application Preferences, each application's own state, and
the user's own color maps and scripts.
_Avoid_: VSR config directory, settings folder

**Application Preferences**:
Settings shared by every VSR application, such as the ANARI devices on offer
and the font scale, saved only when the user asks.
_Avoid_: App settings, defaults (for the file)
