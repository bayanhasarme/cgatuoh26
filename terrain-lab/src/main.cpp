#include <MiniFB.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

constexpr unsigned WIDTH = 1000;
constexpr unsigned HEIGHT = 700;
constexpr float PI = 3.14159265358979323846f;

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

struct CameraSettings {
    float yaw = PI / 4.0f;
    float pitch = PI / 6.0f;
    float scale = 31.0f;
};

struct InputChanges {
    bool terrain = false;
    bool camera = false;
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
        latticeValue(x0 + 1, z0, seed),
        tx);

    float bottom = interpolate(
        latticeValue(x0, z0 + 1, seed),
        latticeValue(x0 + 1, z0 + 1, seed),
        tx);

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

// Rotate the terrain into camera coordinates,
// then use an orthographic projection.
ScreenPoint project(const Vertex& vertex,
                    const CameraSettings& camera) {
    float cosYaw = std::cos(camera.yaw);
    float sinYaw = std::sin(camera.yaw);
    float cosPitch = std::cos(camera.pitch);
    float sinPitch = std::sin(camera.pitch);

    float horizontal =
        cosYaw * vertex.x - sinYaw * vertex.z;

    float forward =
        sinYaw * vertex.x + cosYaw * vertex.z;

    float vertical =
        sinPitch * forward - cosPitch * vertex.y;

    return {
        static_cast<int>(std::lround(
            WIDTH / 2.0f + horizontal * camera.scale)),
        static_cast<int>(std::lround(
            HEIGHT / 2.0f + vertical * camera.scale))
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

        if (x >= 0 && x < int(WIDTH)
            && y >= 0 && y < int(HEIGHT)) {
            pixels[y * WIDTH + x] = MFB_RGB(100, 210, 170);
        }
    }
}

void renderMesh(const Mesh& mesh,
                const CameraSettings& camera,
                std::vector<std::uint32_t>& pixels) {
    std::fill(pixels.begin(), pixels.end(), MFB_RGB(25, 35, 50));

    for (const auto& triangle : mesh.triangles) {
        for (unsigned edge = 0; edge < 3; ++edge) {
            ScreenPoint a = project(
                mesh.vertices[triangle[edge]], camera);

            ScreenPoint b = project(
                mesh.vertices[triangle[(edge + 1) % 3]], camera);

            drawLine(pixels, a, b);
        }
    }
}

InputChanges handleInput(
    mfb_window* window,
    TerrainSettings& settings,
    CameraSettings& camera,
    std::array<bool, 15>& previous) {

    const auto* keys = mfb_get_key_buffer(window);

    const std::array<mfb_key, 15> controls = {
        KB_KEY_UP, KB_KEY_DOWN, KB_KEY_RIGHT, KB_KEY_LEFT,
        KB_KEY_N, KB_KEY_O, KB_KEY_P, KB_KEY_R,
        KB_KEY_A, KB_KEY_D, KB_KEY_W, KB_KEY_S,
        KB_KEY_Q, KB_KEY_E, KB_KEY_C
    };

    InputChanges changed;

    for (unsigned i = 0; i < controls.size(); ++i) {
        bool down = keys[controls[i]] != 0;

        if (down && !previous[i]) {
            if (i < 8) {
                changed.terrain = true;
            } else {
                changed.camera = true;
            }

            switch (controls[i]) {
                case KB_KEY_UP:
                    settings.amplitude =
                        std::min(settings.amplitude + 0.5f, 6.0f);
                    break;

                case KB_KEY_DOWN:
                    settings.amplitude =
                        std::max(settings.amplitude - 0.5f, 0.0f);
                    break;

                case KB_KEY_RIGHT:
                    settings.frequency =
                        std::min(settings.frequency + 0.05f, 0.5f);
                    break;

                case KB_KEY_LEFT:
                    settings.frequency =
                        std::max(settings.frequency - 0.05f, 0.05f);
                    break;

                case KB_KEY_N:
                    ++settings.seed;
                    break;

                case KB_KEY_O:
                    settings.octaves =
                        std::min(settings.octaves + 1, 4u);
                    break;

                case KB_KEY_P:
                    settings.octaves =
                        std::max(settings.octaves - 1, 1u);
                    break;

                case KB_KEY_R:
                    settings = TerrainSettings{};
                    break;

                case KB_KEY_A:
                    camera.yaw -= PI / 18.0f;
                    break;

                case KB_KEY_D:
                    camera.yaw += PI / 18.0f;
                    break;

                case KB_KEY_W:
                    camera.pitch =
                        std::min(camera.pitch + PI / 36.0f, 1.4f);
                    break;

                case KB_KEY_S:
                    camera.pitch =
                        std::max(camera.pitch - PI / 36.0f, 0.15f);
                    break;

                case KB_KEY_Q:
                    camera.scale =
                        std::max(camera.scale - 2.0f, 12.0f);
                    break;

                case KB_KEY_E:
                    camera.scale =
                        std::min(camera.scale + 2.0f, 45.0f);
                    break;

                case KB_KEY_C:
                    camera = CameraSettings{};
                    break;

                default:
                    break;
            }
        }

        previous[i] = down;
    }

    camera.yaw = std::remainder(camera.yaw, 2.0f * PI);
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

void printCamera(const CameraSettings& camera) {
    std::printf(
        "Camera: yaw=%.1f deg | pitch=%.1f deg | zoom=%.1f\n",
        camera.yaw * 180.0f / PI,
        camera.pitch * 180.0f / PI,
        camera.scale);
}

int main() {
    mfb_window* window = mfb_open("Terrain Lab", WIDTH, HEIGHT);

    if (!window) {
        std::fprintf(stderr, "Failed to open Terrain Lab window.\n");
        return 1;
    }

    Mesh mesh = createGrid(64, 0.25f);
    TerrainSettings settings;
    CameraSettings camera;
    std::array<bool, 15> previousKeys{};
    std::vector<std::uint32_t> pixels(WIDTH * HEIGHT);

    std::puts("UP/DOWN: height | RIGHT/LEFT: frequency");
    std::puts("N: next seed | O/P: more/fewer octaves | R: reset terrain");
    std::puts("A/D: rotate | W/S: tilt | Q/E: zoom out/in");
    std::puts("C: reset camera | Press and release each key");

    std::printf("Grid: %zu vertices, %zu triangles\n",
                mesh.vertices.size(), mesh.triangles.size());

    applyNoise(mesh, settings);
    renderMesh(mesh, camera, pixels);
    printSettings(settings);
    printCamera(camera);

    do {
        if (mfb_update_ex(window, pixels.data(), WIDTH, HEIGHT)
            != STATE_OK) {
            break;
        }

        InputChanges changed = handleInput(
            window, settings, camera, previousKeys);

        if (changed.terrain) {
            applyNoise(mesh, settings);
            printSettings(settings);
        }

        if (changed.camera) {
            printCamera(camera);
        }

        if (changed.terrain || changed.camera) {
            renderMesh(mesh, camera, pixels);
        }
    } while (mfb_wait_sync(window));

    return 0;
}