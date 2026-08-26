# VSR Style Guide

This document is the complete set of coding conventions for this repository —
both the general C++/CUDA rules and the VSR-specific ones. It has no parent
document. Mechanical formatting is handled by `.clang-format`; everything else
lives here.

Sections 1–14 are general C++/CUDA conventions. Sections 15+ are VSR-specific
rules that build on them.

---

## 1. Formatting

- **Canonical formatter**: `clang-format` with the project's `.clang-format` file.
  Run it before committing:
  ```bash
  clang-format -i <file>
  ```
- **Line length**: 80 columns (`ColumnLimit: 80`). Longer lines are acceptable
  only when breaking would harm readability (e.g., long string literals, complex
  template signatures).
- **Brace style** (set by `.clang-format`, mixed): opening brace on its **own
  line** for classes/structs, enums, and function definitions; on the **same
  line** for control statements (`if`/`else`, loops), namespaces, and
  `extern "C"` blocks.
- **`clang-format` overrides**: use `// clang-format off` / `// clang-format on`
  sparingly — only for data tables, enum lists, or structured blocks where
  column alignment would otherwise be destroyed.
- **File header**: every source file starts with the NVIDIA copyright line and
  the SPDX identifier:
  ```cpp
  // SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
  // SPDX-License-Identifier: Apache-2.0
  ```

  The entity string is exactly `NVIDIA CORPORATION & AFFILIATES.` (trailing
  period included) — this is the form OSRB requires.

---

## 2. Naming Conventions

| Entity | Style | Example |
|---|---|---|
| Classes / structs | PascalCase | `ObjectPool`, `RenderIndex` |
| Functions / methods | camelCase | `setParameter()`, `valid()` |
| Private member variables | `m_` + camelCase | `m_storage`, `m_freeIndices` |
| Public data members | camelCase | `size`, `colorType` |
| Local variables | camelCase | `controlPoints`, `diff` |
| Namespaces | lowercase | `vsr::core`, `vsr::scene` |
| Macros | UPPER_SNAKE_CASE | `VSR_NOT_COPYABLE`, `VSR_DEVICE_FCN` |
| `constexpr` constants | UPPER_SNAKE_CASE | `INVALID_INDEX`, `MAX_LOCAL_STORAGE` |
| Type aliases | PascalCase with suffix | `Ptr`, `Ref`, `element_t` |
| GPU data structs | PascalCase + `GPUData` suffix | `FrameGPUData`, `FieldGPUData` |

Additional rules:

- No `C`/`I` prefix on class or interface names.
- Getter methods prefer the bare property name (`name()`, `type()`) over
  `getName()`/`getType()`.
- Boolean query methods follow the pattern: `is<T>()`, `valid()`, `empty()`,
  `contains()`.

---

## 3. Headers

- **`#pragma once`** exclusively — no `#ifndef`/`#define` include guards.
- **Include order**, with a `//`-comment label naming each group and no blank
  lines between them:
  1. Project headers (`"vsr/..."`), labeled by library (`// vsr_core`,
     `// vsr_rendering`, …)
  2. External library headers (`<anari/...>`, `<helium/...>`, `<imgui.h>`, …),
     labeled `// anari`, `// helium`, `// imgui`, …
  3. Standard library headers, labeled `// std`

  ```cpp
  // vsr_core
  #include "vsr/scene/Scene.hpp"
  // vsr_rendering
  #include "vsr/rendering/index/RenderIndex.hpp"
  // anari
  #include <anari/anari_cpp.hpp>
  // std
  #include <vector>
  ```
- Forward-declare types in headers where possible to minimize include depth.
  Use fully-qualified names in forward declarations.

---

## 4. Namespaces

- Hierarchical, 2–3 levels deep: `vsr::core`, `vsr::scene`, `vsr::rendering`,
  `vsr::io`, `vsr::app`, `vsr::animation`, `vsr::network`, `vsr::scripting`.
- Sub-namespaces for implementation details: `detail`, `tokens`, `colormap`.
- Anonymous namespaces (`namespace { ... }`) for translation-unit–local linkage
  in `.cpp` files.
- `using namespace` is allowed inside `.cpp` files and inside namespace bodies.
  **Never** at global scope in a header.

---

## 5. Class Layout

Order within a class/struct body:

1. Public type aliases and nested types
2. Public constructors / destructor
3. Lifetime macros (`VSR_NOT_COPYABLE`, `VSR_DEFAULT_MOVEABLE`, …)
4. Public method *declarations* (queries before mutators)
5. `protected` virtual hooks / overrides — *declarations* only
6. `private` data members

**Method definitions are never written inside the class/struct body** — even
trivial one-liners. Sole exception: a truly empty body (`{}`), including
constructors that are only a member-init list, may stay inline. All other
definitions (inline, `constexpr`, and template) go in a clearly delimited
section *after* the class declaration:

```cpp
struct Foo
{
  int bar() const;
  void setBaz(int v);
  // ...
 private:
  int m_bar{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline int Foo::bar() const
{
  return m_bar;
}

inline void Foo::setBaz(int v)
{
  m_bar = v;
}
```

This keeps the class declaration readable as an interface, with all
implementation detail below.

---

## 6. Lifetime and Memory Management

- **No raw `new`/`delete`.**
  - `std::unique_ptr<T>` for exclusive ownership.
  - `std::shared_ptr<T>` for shared ownership (use sparingly).
- Non-owning references use raw pointers or project ref wrappers
  (`ObjectPoolRef<T>`) — see §16 for the stored-member rule.
- Express copy/move intent explicitly using the macros from
  `vsr/core/TypeMacros.hpp` rather than hand-rolled `= delete`/`= default`
  lists:
  ```cpp
  VSR_NOT_COPYABLE(MyClass)
  VSR_DEFAULT_MOVEABLE(MyClass)
  ```
  (`VSR_DEFAULT_COPYABLE` and `VSR_NOT_MOVEABLE` round out the set. Scene object
  types use `DECLARE_OBJECT_DEFAULT_LIFETIME` instead — see §22.)
- Code with no dependency on `vsr_core` spells intent with explicit
  `= delete`/`= default` instead.
- RAII everywhere — resources are owned and released by objects, never managed
  manually.

---

## 7. C++17 Usage

The project builds as C++17 (`CMAKE_CXX_STANDARD 17`). Use C++17 features
freely where they improve clarity:

- `if constexpr` — compile-time branching in templates.
- `std::optional<T>` — nullable return values.
- `std::string_view` — read-only string parameters.
- `std::byte` — raw byte buffers.
- Structured bindings — where they aid readability.

Avoid features that obscure intent or have poor tooling support.

---

## 8. `auto`

**Use `auto` when:**
- The type is immediately obvious from the right-hand side.
- The spelled-out type would be verbose (iterators, template instantiations,
  results of explicit casts).

**Avoid `auto` in:**
- Public API declarations and function signatures.
- Situations where the type is not clear without additional context.

---

## 9. `const` and `constexpr`

- Mark all member functions that do not mutate state `const`.
- Pass large or non-trivial types by `const &`; pass scalars and cheap types
  by value.
- Use `constexpr` for all compile-time constants — not `#define` or
  `static const`.
- Prefer `const` local variables whenever the value does not change after
  initialization.

---

## 10. Templates

- Place full template definitions in headers. They belong in the *Inlined
  definitions* section after the class declaration (same rule as non-template
  inline methods — never inside the class body).
- Use `static_assert` to enforce template parameter constraints early, with a
  clear diagnostic message.
- Avoid CRTP unless virtual dispatch is genuinely unacceptable for performance.
  Prefer virtual functions (see §13) for most extensibility patterns.

---

## 11. Error Handling

| Scenario | Mechanism |
|---|---|
| API misuse (programmer error) | `throw std::runtime_error(...)` |
| Compile-time invariants | `static_assert(condition, "message")` |
| Recoverable / expected failure | Return `bool` or `std::optional` |

Do not use `try`/`catch` inside library code — let exceptions propagate to the
application layer.

### Fallible Returns

Distinguish the two kinds of "not found" by return type:

- **Missing value** → `std::optional<T>`, returned as `return {};` on absence.
- **Missing object / pointer** → raw `T *`, returned as `nullptr` on absence.
- **Missing pooled object** → an empty ref (`GeometryRef{}`), which is the
  ref-wrapper spelling of the same idea.

Do not mix these for the same kind of lookup within a type.

---

## 12. Comments

- `/* ... */` block comments for class-level and file-level documentation.
- `//` inline comments sparingly, only where the logic is non-obvious.
- Section divider pattern:
  ```cpp
  // Section name ///////////////////////////////////////////////////////////////
  ```
- No Doxygen `///` triple-slash style.
- Comments explain *why*, not *what* — the code itself should convey what it
  does.

---

## 13. Polymorphic Class Hierarchies

The project's canonical extensibility shape is an abstract base with a
**public non-virtual driving API**, a small set of **protected pure-virtual
hooks** subclasses must implement, and optional virtual hooks with default
no-op bodies. Prefer this over CRTP (see §10).

```cpp
struct ImagePass
{
  // public non-virtual API driven by the owner
  void setEnabled(bool e);
  virtual const char *name() const = 0;

 protected:
  virtual void render(ImageBuffers &b, int stageId) = 0; // required hook
  virtual void updateSize();                             // optional, no-op default

 private:
  friend struct ImagePipeline; // owner drives the protected API
};
```

- Notification/observer bases (e.g. `BaseUpdateDelegate`, `RenderIndex`) expose
  many narrow `signalXxx(...)`/`preChildren`/`postChildren` virtual hooks;
  subclasses override only the ones they care about.
- Provide a Null-Object subclass (all hooks no-op) where a "do nothing" default
  is useful (`EmptyUpdateDelegate`).
- Always null-check an optional delegate/observer before signaling it:
  `if (m_delegate) m_delegate->signalX(...);`

---

## 14. CUDA and GPU Computation (`VSR_USE_CUDA`)

### File Types

| Extension | Purpose |
|---|---|
| `.cu` | CUDA kernels and Thrust-based algorithm implementations |
| `.cuh` | Device-side inline utilities and declarations |
| `.cpp` | Host-side management: object lifetime, memory upload, launch setup |
| `.h` / `.hpp` | Shared definitions visible to both host and device (guarded by `#ifdef __CUDACC__`) |

### Qualifier Macros

Always use the project macros from `vsr/algorithms/math/device_macros.h` —
never raw CUDA qualifiers directly:

| Macro | Expands to (device build) | Use for |
|---|---|---|
| `VSR_HOST_DEVICE_FCN` | `__host__ __device__` | Math helpers callable from both sides |
| `VSR_DEVICE_FCN` | `__device__` | Device-only helper functions |
| `VSR_DEVICE_FCN_INLINE` | `__forceinline__ __device__` | Hot device-only helpers |

Outside a CUDA build these expand to nothing (or `inline`), which is what keeps
shared math headers compilable by both the C++ and CUDA compilers without
duplication.

### Prefer Thrust Over Hand-Written Kernels

```cpp
// Prefer:
thrust::transform(thrust::cuda::par.on(stream), begin, end, out, op);

// Over:
myCustomKernel<<<grid, block, 0, stream>>>(begin, end, out);
```

Write a custom `__global__` kernel only when no suitable Thrust primitive
exists and the algorithm cannot be composed from existing ones.

### GPU Data Structures

Structures passed to the device via launch parameters or constant memory:

- Suffix with `GPUData`: `FrameGPUData`, `FieldGPUData`.
- Fields must be device pointers (`const T *`) — never host-side smart pointers.
- Host-side array objects expose a `gpuData()` method returning the
  corresponding `GPUData` struct with device pointers.

### Memory and the Host/Device Boundary

- Device allocations are owned by the object that makes them: allocate in the
  constructor (or first use), free in the destructor, and declare the type
  non-copyable (§6). No raw `cudaMalloc` whose matching `cudaFree` lives in
  caller code.
- Check the return status of every CUDA API call — a silently failed allocation
  surfaces later as an unrelated launch failure.
- `#ifdef __CUDACC__` guards control CUDA-specific paths in shared headers.
- Data crosses the boundary via explicit `cudaMemcpy` or buffer upload — never
  implicitly.

---

## 15. Prefer VSR Primitives Over Standard Alternatives

Before reaching for a standard container or writing a data structure from
scratch, check whether a VSR primitive already fits:

| Need | Use instead of | VSR type |
|---|---|---|
| Ordered key→value map (small, stable keys) | `std::map` / `std::unordered_map` | `FlatMap<K,V>` (`vsr/core/FlatMap.hpp`) |
| Linked list or parent–child tree | `std::list` / hand-rolled | `Forest<T>` / `ForestNode<T>` (`vsr/core/Forest.hpp`) |
| Stable-handle object pool | `std::vector` + index | `ObjectPool<T>` + `ObjectPoolRef<T>` (`vsr/core/ObjectPool.hpp`) |
| ANARI-typed parameter value | `void *` / `std::any` | `vsr::core::Any` (`vsr/core/Any.hpp`) |

---

## 16. Store Non-Owning Members as Pointers, Never References

§6 allows raw pointers or ref wrappers for non-owning references; in VSR a
class member that refers to something it does not own is **always a raw
pointer**, default-initialized to `nullptr`:

```cpp
class ImageCache
{
 public:
  ImageCache(Scene *scene);
  Scene *scene() const;

 private:
  Scene *m_scene{nullptr};   // not Scene &
};
```

This is the established shape — `Layer::m_scene`, `AnariHandleCache::m_scene`,
`AnyObjectUsePtr::m_scene`, and every `vsr/network/messages/` type store the
scene this way. A reference member silently deletes assignment and forces the
binding at construction, which breaks the movable-not-copyable lifetime that
`VSR_DEFAULT_MOVEABLE` and `DECLARE_OBJECT_DEFAULT_LIFETIME` declare
everywhere else.

The cost is that null becomes representable. Handle it at the boundary rather
than pushing the check onto callers: return the type's existing empty/failure
value (see Fallible Returns in §11), the way `self()` does in the object
skeleton in §22.

**This rule is about stored members only.** Function parameters stay
references where the argument is required and non-null — the importer and
exporter signatures in §21 take `Scene &` deliberately, and that does not
change.

---

## 17. Scene Mutation and Notification

- Subclass `BaseUpdateDelegate` for any consumer that needs to react to scene
  mutations (renderer synchronization, network replication, UI refresh).
- Use `MultiUpdateDelegate` to fan notifications out to multiple consumers
  without writing fan-out logic yourself.
- Never bypass the delegate system by mutating scene objects and then calling
  renderer internals directly — that breaks the synchronization contract.
- `vsr::core::DataTreeObserver` is a separate, peer mechanism covering
  `DataTree` mutations, not a scene one: a tree notifies at most one Observer,
  registered non-owning via `DataTree::setObserver()`. Use it for consumers of
  hierarchical data; `BaseUpdateDelegate` still governs everything in a
  `Scene`.

---

## 18. Parameter Builder Pattern

Chain `Parameter` setters rather than setting each property separately:

```cpp
p->setDescription("Sphere radius")
  .setValue(0.5f)
  .setMin(0.f)
  .setMax(10.f);
```

---

## 19. Library Layering

Respect the dependency order — never introduce an upward dependency:

```
vsr_core  →  vsr_scene  →  vsr_io  →  vsr_rendering  →  vsr_app
```

Optional libraries (`vsr_ui_imgui`, `vsr_mpi`, `vsr_network`, `vsr_lua`) may
depend on any layer but must remain optional (CMake-gated).

---

## 20. Mirrored CPU/CUDA Algorithms

Algorithms live in `vsr/algorithms/` as **free functions** mirrored across
`vsr::algorithms::cpu::` and `vsr::algorithms::cuda::` with identical
signatures and semantics. CUDA variants ship two overloads: an explicit-stream
version and a convenience wrapper forwarding to stream `0`. Shared enums are
declared once in the CPU header and re-included by the CUDA header.

Call sites select the backend with a guarded fall-through, never an `#else`:

```cpp
#ifdef VSR_ALGORITHMS_HAS_CUDA
  if (b.stream) {
    vsr::algorithms::cuda::outlineObject(b.stream, ...);
    return;
  }
#endif
  vsr::algorithms::cpu::outlineObject(...);
```

Portable device functions use the `VSR_HOST_DEVICE_FCN` / `VSR_DEVICE_FCN`
decorators from §14; CPU parallelism goes through the `parallel_for` /
`parallel_reduce` shims (which fall back to serial loops without TBB) — never
call TBB directly.

---

## 21. File I/O: `verb_FORMAT` Free Functions

Importers, exporters, and generators are **free functions** (never methods),
one per file, named `verb_NOUN` where the noun is an ALL-CAPS format token or a
PascalCase type. The filename mirrors the primary function.

```cpp
void import_OBJ(Scene &, animation::AnimationManager &, const char *filename,
    LayerNodeRef location = {}, bool useDefaultMaterial = false);   // import_OBJ.cpp
SpatialFieldRef import_RAW(Scene &, const char *filename);          // import_RAW.cpp
bool export_SceneToUSD(const Scene &, const char *filename);        // export_*.cpp
void generate_randomSpheres(Scene &, LayerNodeRef location = {});   // generate_*.cpp
```

Full-scene importers share the leading signature
`(Scene &, animation::AnimationManager &, const char *filename, LayerNodeRef location = {}, ...)`.
Resolve the target location with the standard idiom:

```cpp
auto root = location ? location : scene.defaultLayer()->root();
```

**Serializable archives** expose a fixed five-verb family, with the verbs kept
semantically distinct (see [`src/vsr/io/CONTEXT.md`](src/vsr/io/CONTEXT.md)):

```cpp
bool                    serialize_ObjectArchive(const Object &, DataNode &);
ArchiveValidationResult validate_ObjectArchive(DataNode &);
Object *                deserialize_ObjectArchive(Scene &, DataNode &, ...);
bool                    save_ObjectArchive(const Object &, const char *filename);
Object *                load_ObjectArchive(Scene &, const char *filename, ...);
```

**Dispatch is a deliberate `if`/`else if` chain** keyed on a format enum or file
extension — there is intentionally **no self-registration factory**. Do not add
one; extend the existing chain (e.g. `vsr/io/importers/import_file.cpp`).

---

## 22. Concrete ANARI Object Skeleton

Every object type under `vsr/scene/objects/` follows the same skeleton. Use it
verbatim for new object types:

```cpp
struct Geometry : public Object
{
  DECLARE_OBJECT_DEFAULT_LIFETIME(Geometry); // movable, not copyable

  Geometry(Token subtype = tokens::unknown);
  virtual ~Geometry() = default;

  ObjectPoolRef<Geometry> self() const;
  anari::Object makeANARIObject(anari::Device d) const override;
};

using GeometryRef = ObjectPoolRef<Geometry>;

namespace tokens::geometry {
extern const Token cone;   // ... subtype constants, defined in the .cpp
} // namespace tokens::geometry
```

- `DECLARE_OBJECT_DEFAULT_LIFETIME(T)` (`vsr/scene/Object.hpp`) is the required
  lifetime declaration for object types — it *is* the object-layer spelling of
  "movable, not copyable." Use it here; use the general `VSR_*` macros (§6)
  everywhere else. Do not hand-roll `= delete`/`= default` lists.
- `self()` reacquires a handle null-safely and is always the same one-liner:
  ```cpp
  return scene() ? scene()->getObject<Geometry>(index()) : GeometryRef{};
  ```
- Subtypes and other well-known keys are `extern const Token` constants declared
  in a `namespace tokens::<type>` and defined in the `.cpp` — never bare string
  literals at call sites.
- Type aliases: `XRef = ObjectPoolRef<X>` for a raw pooled handle;
  `XAppRef = ObjectUsePtr<X, Object::UseKind::APP>` for a use-counted handle.
- Seed default parameters in the constructor with the fluent builder (§18).

---

## 23. Container Traversal and Sentinels

- VSR containers (`ObjectPool`, `Forest`) are traversed with the free
  `foreach_*` / `forall_*` / `find_*_if` function family taking a forwarding
  functor, **not** STL iterators. Const traversals use the `_const` name suffix
  (`foreach_item_const`, `forall_children_const`) because the callback signature
  cannot disambiguate an overload.
- The "container mechanics mirror `std::`" methods use `snake_case`
  (`insert_first_child`, `erase_subtree`, `is_dense`); higher-level domain
  methods use `camelCase` (`isLeaf`, `numChildren`). This split is deliberate.
- Use the `INVALID_INDEX` sentinel (`vsr/core/ObjectPool.hpp`) for absent indices;
  prefer the `VSR_INVALID_INDEX` macro alias at call sites.
