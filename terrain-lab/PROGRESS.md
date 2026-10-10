# Terrain Lab — Development Progress

The planned implementation is complete. This document summarizes the development milestones.

See [README.md](README.md) for build instructions and controls, and [REPORT.md](REPORT.md) for the final technical report.

## Completed milestones

| Stage | Result | Screenshot |
| --- | --- | --- |
| Project setup | MiniFB window and Windows build script | — |
| Flat grid | Grid vertices, triangle indices and wireframe rendering | [01-flat-grid.png](screenshots/01-flat-grid.png) |
| Gaussian hill | A predictable height function for initial validation | [02-hill.png](screenshots/02-hill.png) |
| Value noise | Deterministic terrain generated from a seed | [03-value-noise-seed42.png](screenshots/03-value-noise-seed42.png) |
| Fractal noise | Multiple noise octaves with normalized contributions | [04-fractal-noise.png](screenshots/04-fractal-noise.png) |
| Terrain controls | Keyboard adjustments and reset | [05-keyboard-seed43.png](screenshots/05-keyboard-seed43.png) |
| Camera | Orthographic rotation, tilt and zoom | [06-camera-view.png](screenshots/06-camera-view.png) |
| Filled rendering | Barycentric rasterization and depth testing | [07-filled-depth.png](screenshots/07-filled-depth.png) |
| Lighting | Face normals, ambient light and Lambert diffuse shading | [08-flat-lighting.png](screenshots/08-flat-lighting.png) |
| Terrain colors | Color blending based on height and slope | [09-height-slope-colors.png](screenshots/09-height-slope-colors.png) |
| Water | Adjustable water plane using the shared depth buffer | [10-water.png](screenshots/10-water.png) |
| Interface | On-screen settings and keyboard instructions | [11-hud.png](screenshots/11-hud.png) |
| Documentation | Updated README and final technical report | — |

## Validation

Manual checks were performed during development.

The final reset check confirmed that changing the seed and resetting restores seed 42 and the original terrain appearance.

The final water check confirmed that raising the level from -0.25 to 0.00 increases water coverage, and lowering it restores the previous coverage.

The on-screen values update with keyboard input.

Detailed validation results and implementation limitations are documented in REPORT.md.

## Development history

Implementation and documentation were recorded through separate Git commits.

Earlier versions of this document are preserved in Git history.