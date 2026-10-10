#include <MiniFB.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>

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

// A fixed angled view of the 3D coordinates.
ScreenPoint project(const Vertex& vertex) {
    return {
        static_cast<int>(WIDTH / 2.0f + (vertex.x - vertex.z) * 22.0f),
        static_cast<int>(HEIGHT / 2.0f
            + (vertex.x + vertex.z) * 11.0f - vertex.y * 22.0f)
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

        if (x >= 0 && x < int(WIDTH) && y >= 0 && y < int(HEIGHT)) {
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

    Mesh mesh = createGrid(16, 1.0f);
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

    do {
        if (mfb_update_ex(window, pixels.data(), WIDTH, HEIGHT) != STATE_OK) {
            break;
        }
    } while (mfb_wait_sync(window));

    return 0;
}