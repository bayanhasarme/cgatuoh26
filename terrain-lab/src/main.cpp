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

struct TerrainSettings {
    float amplitude = 3.0f;
    float frequency = 0.3f;
    std::uint32_t seed = 42;
    unsigned octaves = 4;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
};

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

void applyHill(Mesh& mesh, float height, float radius) {
    for (auto& vertex : mesh.vertices) {
        float distanceSquared =
            vertex.x * vertex.x + vertex.z * vertex.z;
        vertex.y = height * std::exp(
            -distanceSquared / (2.0f * radius * radius));
    }
}

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

float smoothStep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

float valueNoise(float x, float z, std::uint32_t seed) {
    int x0 = static_cast<int>(std::floor(x));
    int z0 = static_cast<int>(std::floor(z));
    float tx = smoothStep(x - x0);
    float tz = smoothStep(z - z0);

    float top = interpolate(
        latticeValue(x0, z0, seed),
        latticeValue(x0 + 1, z0, seed), tx);

    float bottom = interpolate(
        latticeValue(x0, z0 + 1, seed),
        latticeValue(x0 + 1, z0 + 1, seed), tx);

    return interpolate(top, bottom, tz);
}

float fractalNoise(float x, float z,
                   const TerrainSettings& settings) {
    float total = 0.0f;
    float weight = 1.0f;
    float totalWeight = 0.0f;

    for (unsigned layer = 0; layer < settings.octaves; ++layer) {
        total += weight * valueNoise(x, z, settings.seed);
        totalWeight += weight;
        weight *= settings.persistence;
        x *= settings.lacunarity;
        z *= settings.lacunarity;
    }

    return totalWeight > 0.0f ? total / totalWeight : 0.0f;
}

void applyNoise(Mesh& mesh, const TerrainSettings& settings) {
    for (auto& vertex : mesh.vertices) {
        vertex.y = settings.amplitude * fractalNoise(
            vertex.x * settings.frequency,
            vertex.z * settings.frequency,
            settings);
    }
}

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

void renderMesh(const Mesh& mesh,
                std::vector<std::uint32_t>& pixels) {
    // Clear the old image before drawing the updated terrain.
    std::fill(pixels.begin(), pixels.end(), MFB_RGB(25, 35, 50));

    for (const auto& triangle : mesh.triangles) {
        for (unsigned edge = 0; edge < 3; ++edge) {
            drawLine(
                pixels,
                project(mesh.vertices[triangle[edge]]),
                project(mesh.vertices[triangle[(edge + 1) % 3]]));
        }
    }
}

// Each key changes a setting once per press.
bool handleInput(mfb_window* window, TerrainSettings& settings,
                 std::array<bool, 8>& previous) {
    const auto* keys = mfb_get_key_buffer(window);
    const std::array<mfb_key, 8> controls = {
        KB_KEY_UP, KB_KEY_DOWN, KB_KEY_RIGHT, KB_KEY_LEFT,
        KB_KEY_N, KB_KEY_O, KB_KEY_P, KB_KEY_R
    };

    bool changed = false;

    for (unsigned i = 0; i < controls.size(); ++i) {
        bool down = keys[controls[i]] != 0;

        if (down && !previous[i]) {
            switch (controls[i]) {
                case KB_KEY_UP:
                    settings.amplitude = std::min(
                        settings.amplitude + 0.5f, 6.0f);
                    break;
                case KB_KEY_DOWN:
                    settings.amplitude = std::max(
                        settings.amplitude - 0.5f, 0.0f);
                    break;
                case KB_KEY_RIGHT:
                    settings.frequency = std::min(
                        settings.frequency + 0.05f, 0.5f);
                    break;
                case KB_KEY_LEFT:
                    settings.frequency = std::max(
                        settings.frequency - 0.05f, 0.05f);
                    break;
                case KB_KEY_N:
                    ++settings.seed;
                    break;
                case KB_KEY_O:
                    settings.octaves = std::min(
                        settings.octaves + 1, 4u);
                    break;
                case KB_KEY_P:
                    settings.octaves = std::max(
                        settings.octaves - 1, 1u);
                    break;
                case KB_KEY_R:
                    settings = TerrainSettings{};
                    break;
                default:
                    break;
            }
            changed = true;
        }

        previous[i] = down;
    }

    return changed;
}

void printSettings(const TerrainSettings& settings) {
    std::printf(
        "Seed=%u | amplitude=%.2f | frequency=%.2f | octaves=%u\n",
        static_cast<unsigned>(settings.seed),
        settings.amplitude,
        settings.frequency,
        settings.octaves);
}

int main() {
    mfb_window* window = mfb_open("Terrain Lab", WIDTH, HEIGHT);
    if (!window) {
        std::fprintf(stderr, "Failed to open Terrain Lab window.\n");
        return 1;
    }

    Mesh mesh = createGrid(64, 0.25f);
    TerrainSettings settings;
    std::array<bool, 8> previousKeys{};

    std::vector<std::uint32_t> pixels(WIDTH * HEIGHT);

    std::puts("UP/DOWN: height | RIGHT/LEFT: frequency");
    std::puts("N: next seed | O/P: more/fewer octaves | R: reset");
    std::printf("Grid: %zu vertices, %zu triangles\n",
                mesh.vertices.size(), mesh.triangles.size());

    applyNoise(mesh, settings);
    renderMesh(mesh, pixels);
    printSettings(settings);

    do {
        if (mfb_update_ex(window, pixels.data(), WIDTH, HEIGHT)
            != STATE_OK) {
            break;
        }

        if (handleInput(window, settings, previousKeys)) {
            applyNoise(mesh, settings);
            renderMesh(mesh, pixels);
            printSettings(settings);
        }
    } while (mfb_wait_sync(window));

    return 0;
}