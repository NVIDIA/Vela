## Interactive Demos

This directory contains focused interactive applications that demonstrate
specific VSR/ANARI workflows.

| Demo | Executable | Directory | Summary | Build Requirements |
| --- | --- | --- | --- | --- |
| [Animated Particles](animatedParticles/README.md) | `vsrDemoAnimatedParticles` | [animatedParticles/](animatedParticles/) | CUDA particle simulation with moving attractors, rendered via VSR geometry and colormapping. | `VSR_USE_CUDA=ON` |
| [Animated Volume](animatedVolume/README.md) | `vsrDemoAnimatedVolume` | [animatedVolume/](animatedVolume/) | CUDA Jacobi 3D volume solver with live transfer-function/isosurface visualization. | `VSR_USE_CUDA=ON` |
| [Array Instancing](arrayInstancing/README.md) | `vsrDemoArrayInstancing` | [arrayInstancing/](arrayInstancing/) | Stress/demo scene for large instancing, transform arrays, and per-instance attributes. | Interactive apps enabled |
| [Manual Accumulation Reset](manualAccumulationReset/README.md) | `vsrDemoAccumulationReset` | [manualAccumulationReset/](manualAccumulationReset/) | Demonstrates manual accumulation reset control through custom frame parameters. | Interactive apps enabled |
| [Viskores](viskores/README.md) | `vsrDemoViskores` | [viskores/](viskores/) | Viskores graph editor integrated with VSR UI and ANARI device pass-through. | `BUILD_VISKORES_DEMO=ON`, `viskores_graph` |
