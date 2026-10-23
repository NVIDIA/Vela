# Align and pad host Array memory

Every ANARI array Vela creates is a captured array: `Array::makeANARIObject`
hands the device Vela's own pointer and a deleter, so a device that can use the
memory in place never copies it. Embree-based devices (helide, halcyon, OSPRay,
Barney-CPU) pass `vertex.position` and index arrays straight to
`rtcSetSharedGeometryBuffer`. Embree needs that pointer 4-byte aligned, and it
reads the last element of a vertex buffer with a 16-byte SSE load, so a tightly
packed float3 buffer must have at least 4 readable bytes after it. A device
that checks this has to copy any buffer that fails. One that doesn't (helide
and halcyon today) reads past the end of an exactly-sized `malloc`, and gets
away with it only because of allocator slack.

Host `Array` memory is now allocated with `std::aligned_alloc` on a 64-byte
boundary (`Array::HOST_DATA_ALIGNMENT`) and with at least 16 bytes
(`Array::HOST_DATA_PADDING`) past the last element. This covers every element
type and shape, not just float3. Vertex attributes, normals, `uint3` indices
and curve buffers hit the same SSE loads, and a single rule costs at most about
80 bytes per array. 64 bytes is a cache line and covers AVX-512 loads, so the
same guarantee also serves ISPC kernels.

All host data reaches devices through `vsr::scene::Array`, so this one change
covers importers, archives, clones, network transfer (via
`convertProxyToHost`), and app arrays that the `anari_vsr` device copies into
its scene.

## Consequences

- MSVC has no `std::aligned_alloc`, so `vsr::core::alignedAlloc` uses
  `_aligned_malloc` on Windows. Host Array memory must be released with
  `vsr::core::alignedFree`, never `std::free`.
- `size()`, `elementSize()` and serialized byte counts are unchanged. The
  padding is slack, never data, and its contents are unspecified.
- CUDA arrays are unaffected: `cudaMalloc` is already 256-byte aligned, and
  Embree never reads device memory.
- ANARI has no way to tell a device a buffer is padded, so a device that copies
  defensively keeps doing so until it checks alignment itself or an extension
  advertises the guarantee. Apps that hand their own memory to a device
  directly, without Vela, are outside this.
