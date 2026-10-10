# Terrain Lab — Development Progress

## 1. Application setup

Created a standalone C++ application using MiniFB.
The application opens a 1000 × 700 window and displays a pixel buffer.

Build and run on Windows:

```powershell
.\build_and_run.ps1
```

## 2. Flat triangle grid

Generated a regular grid on the XZ plane.
Each cell contains two triangles, and neighboring triangles share vertices.

Initial configuration:
- 16 × 16 cells.
- 289 vertices.
- 512 triangles.

Rendered the triangle edges using line rasterization and a fixed
angled projection.

![Flat grid](screenshots/01-flat-grid.png)

## 3. Gaussian hill

Assigned a height to each vertex using:

y = H * exp(-(x² + z²) / (2 * r²))

Used H = 5 and r = 3.
The height is greatest at the center and decreases toward the edges.

![Gaussian hill](screenshots/02-hill.png)

## 4. Seeded value noise

Implemented deterministic lattice values and smooth interpolation
between neighboring values to generate hills and depressions.

Parameters:
- Seed: 42.
- Amplitude: 3.
- Frequency: 0.3.

Increased grid resolution to 64 × 64 cells while keeping the terrain
width and depth at 16 units:
- 4,225 vertices.
- 8,192 triangles.

Visual check: running the application twice with the same parameters
produced the same terrain appearance.

![Value noise](screenshots/03-value-noise-seed42.png)

## 5. Multi-octave noise

Combined four layers of value noise.
Each successive layer doubles the frequency and halves the weight.

Parameters:
- Octaves: 4.
- Persistence: 0.5.
- Lacunarity: 2.

Divided the weighted sum by the sum of weights to keep the noise
within the range [-1, 1].

![Multi-octave noise](screenshots/04-fractal-noise.png)

## Current limitations

- The view is fixed.
- Terrain parameters are currently set in the source code.
- Rendering displays all triangle edges, including hidden edges.
- Filled surfaces, depth testing, lighting and water are not yet implemented.

## Next step

Add keyboard controls to change terrain parameters during execution.