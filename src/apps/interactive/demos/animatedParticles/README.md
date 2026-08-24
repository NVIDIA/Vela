## Animated Particles Demo (`vsrDemoAnimatedParticles`)

This demo visualizes a CUDA-updated particle system driven by two moving
"black hole" attractors.

Particle positions and per-particle scalar values are updated every step, then
rendered through VSR geometry + material/sampler objects.

### Build Notes

- Requires `VSR_USE_CUDA=ON`.
- Built only when CUDA support is enabled.

### Run

```bash
./vsrDemoAnimatedParticles
```

You can also pass normal scene-loading options (for example `-hdri`).

### In-App Controls

- Simulation playback: `reset`, `play/stop`, `iterate`.
- Particle controls: particles-per-side, randomized initial velocities.
- Compute path toggle: GPU array interop on/off.
- Physical/visual parameters: gravity, particle mass, max distance,
  colormap scaling, timestep, and attractor rotation speed.
