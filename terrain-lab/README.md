# Terrain Lab

An interactive procedural terrain project for the Computer Graphics course.

The application generates a terrain mesh and renders it with a CPU software rasterizer. MiniFB displays the resulting pixel buffer and provides keyboard input.

![Terrain Lab](screenshots/11-hud.png)

## Features

- A regular grid with 4,225 vertices and 8,192 triangles.
- Deterministic seeded value noise.
- Multi-octave fractal noise with adjustable height, frequency and octave count.
- An orthographic camera with rotation, tilt and zoom.
- Triangle rasterization using barycentric coordinates.
- A depth buffer for hidden-surface removal.
- Face normals and flat Lambert shading with ambient light.
- Terrain colors based on height and slope.
- A water plane with adjustable level and visibility.
- On-screen settings and keyboard instructions.

## Build and run

The project is developed and tested on Windows using:

- Visual Studio C++ Build Tools.
- CMake.
- Ninja.
- Git.
- PowerShell.

Open a terminal in the `terrain-lab` folder and run:

```powershell
.\build_and_run.ps1
```

The script configures the project, builds it and opens the application.

An internet connection is required during the initial configuration to download MiniFB through CMake FetchContent.

## Controls

Click the application window before using the keyboard.
Press and release each key for each adjustment.

| Keys | Action |
| --- | --- |
| Up / Down | Increase / decrease terrain height |
| Right / Left | Increase / decrease noise frequency |
| N | Generate terrain using the next seed |
| O / P | Increase / decrease octave count |
| A / D | Rotate the camera |
| W / S | Adjust camera tilt |
| Q / E | Zoom out / in |
| M | Show / hide water |
| J / K | Lower / raise the water level |
| R | Reset terrain and water settings |
| C | Reset the camera |

## Default settings

| Setting | Value |
| --- | --- |
| Seed | 42 |
| Height amplitude | 3.0 |
| Noise frequency | 0.3 |
| Octaves | 4 |
| Persistence | 0.5 |
| Lacunarity | 2.0 |
| Water level | -0.25 |
| Camera yaw | 45 degrees |
| Camera pitch | 30 degrees |
| Camera scale | 31 pixels per world unit |

## Rendering pipeline

1. Generate the grid vertices and triangle indices.
2. Assign vertex heights using fractal value noise.
3. Transform and project the vertices using the camera.
4. Calculate each triangle's face normal.
5. Choose a base color using average triangle height and slope.
6. Apply ambient and Lambert diffuse lighting.
7. Rasterize triangles and compare interpolated depth at each pixel.
8. Render the water plane using the same depth buffer.
9. Draw the settings and keyboard help over the scene.

## Project files

- `src/main.cpp`: terrain generation, camera, rasterization, lighting, water and input.
- `src/hud.h`: bitmap text and on-screen information.
- `CMakeLists.txt`: build configuration and MiniFB dependency.
- `build_and_run.ps1`: Windows build and run script.
- `screenshots/`: images documenting development stages.
- `PROGRESS.md`: development notes.

## Validation

The following checks were performed manually during development:

- Changing the seed changes the terrain.
- Resetting restores seed 42 and the original terrain.
- Terrain height and frequency respond to keyboard input.
- Camera rotation, tilt and zoom respond to keyboard input.
- A flat terrain receives uniform lighting.
- Water can be hidden and shown.
- Raising and lowering water changes the covered terrain area.
- The displayed water level updates with keyboard input.
- The on-screen settings and instructions are readable.

## Current limitations

- Orthographic projection only.
- Flat shading with one normal and one color per triangle.
- A fixed directional light.
- No cast shadows.
- Water is a flat opaque surface without waves or reflections.
- Keyboard controls use individual presses rather than continuous movement.
- Large zoom values can move parts of the terrain behind the information panels or outside the window.

## Development approach

The project was built incrementally, with separate Git commits for small implementation and documentation tasks.
Screenshots record the main visual milestones.