## Core Library (`vsr_core`)

`vsr_core` is the foundational utility library used by all other VSR
components.

### High-Level Concepts

- `Any` stores ANARI-typed values in a compact type-erased container.
- `Token` provides cheap string-token identity for parameter and subtype names.
- Data containers and algorithms:
  `ObjectPool`, `FlatMap`, and `Forest`.
- `DataTree` and `DataNode` provide hierarchical typed data serialization to
  file or memory buffers.
- `DataStream` defines stream-like readers/writers (`FileReader`,
  `BufferWriter`, etc.) used by serialization layers.
- Utility systems for runtime behavior:
  `Logging`, `TaskQueue`, `Timer`, transfer-function and colormap helpers, and
  math aliases in `VSRMath.hpp`.
