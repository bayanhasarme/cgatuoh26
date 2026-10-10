#include <MiniFB.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

constexpr unsigned WIDTH = 1000;
constexpr unsigned HEIGHT = 700;

struct Vertex {
    float x, y, z;
};

struct ScreenPoint {
    int x, y;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::array<unsigned, 3>> triangles;
};

// Each square in the grid contains two triangles.
Mesh createGrid(unsigned cells, float spacing) {
    Mesh mesh;
    float halfSize = cells * spacing / 2.0f;

    for (unsigned z = 0; z <= cells; ++z) {
        for (unsigned x = 0; x <= cells; ++x) {
            mesh.vertices.push_back({
                x * spacing - halfSize,
                0.0f,
                z * spacing - halfSize
            });
        }
    }

    unsigned rowSize = cells + 1;
    for (unsigned z = 0; z < cells; ++z) {
        for (unsigned x = 0; x < cells; ++x) {
            unsigned a = z * rowSize + x;
            unsigned b = a + 1;
            unsigned c = a + rowSize;
            unsigned d = c + 1;

            mesh.triangles.push_back({a, c, b});
            mesh.triangles.push_back({b, c, d});
        }
    }

    return mesh;
}

// Keep the hill generator for later comparisons.
void applyHill(Mesh& mesh, float height, float radius) {
    for (auto& vertex : mesh.vertices) {
        float distanceSquared =
            vertex.x * vertex.x + vertex.z * vertex.z;

        vertex.y = height * std::exp(
            -distanceSquared / (2.0f * radius * radius));
    }
}

// Deterministic value in [-1, 1] for an integer grid position.
float latticeValue(int x, int z, std::uint32_t seed) {
    std::uint32_t hash =
        static_cast<std::uint32_t>(x) * 374761393u
        + static_cast<std::uint32_t>(z) * 668265263u
        + seed * 1442695041u;

    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;

    return static_cast<float>(hash & 0x00FFFFFFu)
        / 16777215.0f * 2.0f - 1.0f;
}

float interpolate(float a, float b, float t) {
    return a + (b - a) * t;
}

// Smooth interpolation with zero slope at both ends.
float smoothStep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Blend the values at the four corners around the sample.
float valueNoise(float x, float z, std::uint32_t seed) {
    int x0 = static_cast<int>(std::floor(x));
    int z0 = static_cast<int>(std::floor(z));

    float tx = smoothStep(x - x0);
    float tz = smoothStep(z - z0);

    float top = interpolate(
        latticeValue(x0, z0, seed),
        latticeValue(x0 + 1, z0, seed),
        tx);

    float bottom = interpolate(
        latticeValue(x0, z0 + 1, seed),
        latticeValue(x0 + 1, z0 + 1, seed),
        tx);

    return interpolate(top, bottom, tz);
}

void applyNoise(Mesh& mesh, float amplitude,
                float frequency, std::uint32_t seed) {
    for (auto& vertex : mesh.vertices) {
        vertex.y = amplitude * valueNoise(
            vertex.x * frequency,
            vertex.z * frequency,
            seed);
    }
}

// A fixed angled view of the 3D coordinates.
ScreenPoint project(const Vertex& vertex) {
    return {
        static_cast<int>(
            WIDTH / 2.0f + (vertex.x - vertex.z) * 22.0f),
        static_cast<int>(
            HEIGHT / 2.0f
            + (vertex.x + vertex.z) * 11.0f
            - vertex.y * 22.0f)
    };
}

void drawLine(std::vector<std::uint32_t>& pixels,
              ScreenPoint a, ScreenPoint b) {
    int dx = b.x - a.x;
    int dy = b.y - a.y;
    int steps = std::max(std::abs(dx), std::abs(dy));

    for (int i = 0; i <= steps; ++i) {
        float t = steps == 0 ? 0.0f : float(i) / steps;
        int x = static_cast<int>(std::lround(a.x + dx * t));
        int y = static_cast<int>(std::lround(a.y + dy * t));

        if (x >= 0 && x < int(WIDTH) &&
            y >= 0 && y < int(HEIGHT)) {
            pixels[y * WIDTH + x] = MFB_RGB(100, 210, 170);
        }
    }
}

int main() {
    mfb_window* window = mfb_open("Terrain Lab", WIDTH, HEIGHT);
    if (!window) {
        std::fprintf(stderr, "Failed to open Terrain Lab window.\n");
        return 1;
    }

    // More vertices, while keeping the same terrain size.
    Mesh mesh = createGrid(64, 0.25f);

    constexpr float amplitude = 3.0f;
    constexpr float frequency = 0.3f;
    constexpr std::uint32_t seed = 42;

    applyNoise(mesh, amplitude, frequency, seed);

    std::vector<std::uint32_t> pixels(
        WIDTH * HEIGHT, MFB_RGB(25, 35, 50));

    for (const auto& triangle : mesh.triangles) {
        for (unsigned edge = 0; edge < 3; ++edge) {
            ScreenPoint a = project(mesh.vertices[triangle[edge]]);
            ScreenPoint b = project(
                mesh.vertices[triangle[(edge + 1) % 3]]);
            drawLine(pixels, a, b);
        }
    }

    std::printf("Grid: %zu vertices, %zu triangles\n",
                mesh.vertices.size(), mesh.triangles.size());
    std::printf("Value noise: seed=%u, amplitude=%.2f, frequency=%.2f\n",
                static_cast<unsigned>(seed), amplitude, frequency);

    do {
        if (mfb_update_ex(window, pixels.data(), WIDTH, HEIGHT)
            != STATE_OK) {
            break;
        }
    } while (mfb_wait_sync(window));

    return 0;
}