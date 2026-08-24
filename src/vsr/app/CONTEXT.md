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
