## Array Instancing Demo (`vsrDemoArrayInstancing`)

This demo focuses on large instance counts and per-instance data.

It can generate:

- Many random sphere particles.
- Many mesh instances (monkey geometry) driven by a transform array.
- Per-instance color attributes.

The demo is useful for testing scene update behavior and renderer scaling with
instancing-heavy content.

### Build Notes

- No CUDA requirement.
- Built with interactive apps (`vsr_ui_imgui`).

### Run

```bash
./vsrDemoArrayInstancing
```

### In-App Controls

- Toggle particle and mesh-instance generation.
- Adjust particle radius, instance count, and spatial spread.
- Rebuild scene with updated parameters.
- Edit directional light parameters.
