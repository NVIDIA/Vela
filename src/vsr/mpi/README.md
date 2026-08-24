## MPI Library (`vsr_mpi`)

`vsr_mpi` provides lightweight MPI helpers for replicated runtime state.

### High-Level Concepts

- `ReplicatedObject<T>` mirrors POD state from rank 0 to all ranks.
- Versioned sync semantics:
  `write()` on rank 0 marks state dirty, `sync()` broadcasts updates,
  `read()` is available on all ranks.
- Compile-time constraints enforce that `T` is trivially copyable and standard
  layout, making broadcasts predictable.

### Why Use This Library

- You need a simple way to share frame/camera/light/render settings in
  distributed viewer or render-loop code.
- You want to avoid hand-writing repeated `MPI_Bcast` plumbing for small state
  structs.
- You want rank-0-authoritative control flow with low conceptual overhead.

### Build Notes

- This library is enabled with `-DVSR_USE_MPI=ON`.
- The CMake target is an INTERFACE library (`vsr_mpi`) that links `MPI::MPI_CXX`.
