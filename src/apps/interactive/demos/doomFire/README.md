## Doom Fire Demo (`vsrDemoDoomFire`)

This demo runs a 3D version of the
[Doom PSX fire algorithm](https://github.com/filipedeschamps/doom-fire-algorithm)
on the GPU and volume renders the result in real time.

The fire lives in a `UFIXED8` `structuredRegular` spatial field (+Y is up).
The bottom layer is the fire source, held at the base intensity. Each step,
every cell above it copies the intensity of a randomly chosen cell in the layer
below, minus a random decay; the wind shifts which cell is chosen. The volume
uses a `transferFunction1D` built from the original 37-color Doom palette.

### Build Notes

- Requires `VSR_USE_CUDA=ON`.

### Run

```bash
./vsrDemoDoomFire
```

### In-App Controls

- Playback: `play/pause`, `step` (while paused), `reset`.
- Grid size per axis (applied when you release the drag).
- Base intensity, `0`-`255` (normalized `0.0`-`1.0`); set it to `0` to put the
  fire out.
- Wind direction (in the XZ plane) and strength.
- Maximum decay per layer (lower values give taller flames).
- Sideways jitter on/off, steps per frame.
- Compute/data path: GPU array interop on/off.
