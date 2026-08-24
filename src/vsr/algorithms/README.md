# vsr_algorithms

Compute kernels with CPU and optional CUDA backends. Each algorithm
has matching implementations in both backends with identical semantics.

## Directory Layout

```
algorithms/
  cpu/             CPU implementations (.hpp + .cpp)
  cpu/detail/      Parallelization helpers (TBB or serial fallback)
  cuda/            CUDA implementations (.hpp + .cu)
  math/            Shared device-agnostic math (color, tone map curves, macros)
```

## Backend Selection

The CPU backend (`vsr::algorithms::cpu::`) is always available.

When built with `VSR_USE_CUDA=ON`, CMake defines the public compile definition
`VSR_ALGORITHMS_HAS_CUDA` and the CUDA backend (`vsr::algorithms::cuda::`)
becomes available:

```cpp
#include <vsr/algorithms/cpu/toneMap.hpp>         // always available
#ifdef VSR_ALGORITHMS_HAS_CUDA
#include <vsr/algorithms/cuda/toneMap.hpp>        // requires CUDA build
#endif
```

TBB vs serial execution within the CPU backend is an internal detail controlled
by `VSR_USE_TBB` — callers don't need to know.

## Algorithms

| Header | Function(s) | Description |
|--------|-------------|-------------|
| `toneMap` | `toneMap` | In-place HDR tone mapping with exposure scale. Operators: NONE, REINHARD, ACES, HABLE, KHRONOS_PBR_NEUTRAL, AGX. |
| `autoExposure` | `sumLogLuminance` | Sum of log2(luminance) over strided samples. Caller divides by count and applies exp2 for average luminance. |
| `outputTransform` | `outputTransform` | Gamma correction and conversion to packed uint32 RGBA. Reads float4 or packed uint32 input depending on `anari::DataType`. |
| `visualizeAOV` | `visualizeId`, `visualizeDepth`, `visualizeAlbedo`, `visualizeNormal`, `visualizeEdges` | Convert render AOVs to displayable colors. ID AOVs (object/primitive/instance) map to pseudo-random colors; depth normalizes to grayscale; normals remap from [-1,1] to [0,1]; edges detect object boundaries. |
| `outline` | `outline` | Highlight selected-object edges by blending orange on boundary pixels (3x3 neighborhood test). |
| `depthCompositeFrame` | `depthCompositeFrame` | Per-pixel depth composite: keep the nearer of two frames' color, depth, and optional objectId. |
| `clearBuffers` | `fill` | Fill uint32 or float buffer with a constant value. |
| `convertColorBuffer` | `convertFloatToUint8` | Clamp-and-scale float [0,1] to uint8 [0,255]. |

All buffers are flat pixel arrays (interleaved RGBA where applicable).

## CUDA Stream API

Every CUDA function provides two overloads:

```cpp
// explicit stream — for async pipeline control
void toneMap(cudaStream_t stream, float *hdrColor, ...);

// default stream — convenience wrapper
void toneMap(float *hdrColor, ...);
```

## Adding a New Algorithm

1. Add `cpu/<name>.hpp` and `cpu/<name>.cpp` with functions in
   `vsr::algorithms::cpu::`.
2. Add `cuda/<name>.hpp` and `cuda/<name>.cu` with matching functions in
   `vsr::algorithms::cuda::` (both stream and stream-less overloads).
3. Register the `.cpp` and `.cu` sources in `CMakeLists.txt`.
4. For shared math, add device-agnostic helpers in `math/` using the macros
   from `math/device_macros.h` (`VSR_DEVICE_FCN_INLINE`, etc.).
