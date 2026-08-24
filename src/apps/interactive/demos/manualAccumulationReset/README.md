## Manual Accumulation Reset Demo (`vsrDemoAccumulationReset`)

This demo shows how to control accumulation reset manually by driving a custom
frame parameter (`accumulationVersion`) from the UI.

It renders a material-orb scene and exposes quick actions to force an
accumulation restart without changing scene geometry.

### Build Notes

- No CUDA requirement.
- Demo constrains the visible ANARI library list to `visrtx` in code.

### Run

```bash
./vsrDemoAccumulationReset
```

### In-App Controls

- Menu: `Accumulation Controls -> Reset` increments accumulation version.
- Menu: `Use Automatic Reset` sets version to `0`.
- Keyboard shortcut: `A` triggers reset.
