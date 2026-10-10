#include <MiniFB.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

constexpr unsigned WIDTH = 1000;
constexpr unsigned HEIGHT = 700;
constexpr float PI = 3.14159265358979323846f;
constexpr unsigned GRID_CELLS = 64;
constexpr float GRID_SPACING = 0.25f;

struct Vertex {
    float x, y, z;
};

struct ScreenPoint {
    float x, y, depth;
};

struct Color {
    float r, g, b;
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

struct WaterSettings {
    bool visible = true;
    float level = -0.25f;
};

struct InputChanges {
    bool terrain = false;
    bool camera = false;
    bool water = false;
};

Vertex subtract(const Vertex& a, const Vertex& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vertex cross(const Vertex& a, const Vertex& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float dot(const Vertex& a, const Vertex& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vertex normalize(const Vertex& v) {
    float length = std::sqrt(dot(v, v));
    if (length < 0.000001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {v.x / length, v.y / length, v.z / length};
}

Vertex faceNormal(const Vertex& a,
                  const Vertex& b,
                  const Vertex& c) {
    return normalize(cross(subtract(b, a), subtract(c, a)));
}

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
            vertex.z * settings.frequency, settings);
    }
}

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
    float depth =
        cosPitch * forward + sinPitch * vertex.y;

    return {
        WIDTH / 2.0f + horizontal * camera.scale,
        HEIGHT / 2.0f + vertical * camera.scale,
        depth
    };
}

float edgeFunction(const ScreenPoint& a,
                   const ScreenPoint& b,
                   float x, float y) {
    return (b.x - a.x) * (y - a.y)
         - (b.y - a.y) * (x - a.x);
}

void drawTriangle(
    std::vector<std::uint32_t>& pixels,
    std::vector<float>& depthBuffer,
    const ScreenPoint& a,
    const ScreenPoint& b,
    const ScreenPoint& c,
    std::uint32_t color) {

    float area = edgeFunction(a, b, c.x, c.y);
    if (std::abs(area) < 0.00001f) {
        return;
    }

    int minX = std::max(0, static_cast<int>(std::floor(
        std::min({a.x, b.x, c.x}))));
    int maxX = std::min(int(WIDTH) - 1,
        static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    int minY = std::max(0, static_cast<int>(std::floor(
        std::min({a.y, b.y, c.y}))));
    int maxY = std::min(int(HEIGHT) - 1,
        static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = x + 0.5f;
            float py = y + 0.5f;
            float wa = edgeFunction(b, c, px, py) / area;
            float wb = edgeFunction(c, a, px, py) / area;
            float wc = edgeFunction(a, b, px, py) / area;

            if (wa < 0.0f || wb < 0.0f || wc < 0.0f) {
                continue;
            }

            float depth =
                wa * a.depth + wb * b.depth + wc * c.depth;
            unsigned index =
                static_cast<unsigned>(y) * WIDTH
                + static_cast<unsigned>(x);

            if (depth > depthBuffer[index]) {
                depthBuffer[index] = depth;
                pixels[index] = color;
            }
        }
    }
}

Color mixColor(const Color& a, const Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        interpolate(a.r, b.r, t),
        interpolate(a.g, b.g, t),
        interpolate(a.b, b.b, t)
    };
}

float transition(float low, float high, float value) {
    float t = std::clamp(
        (value - low) / (high - low), 0.0f, 1.0f);
    return smoothStep(t);
}

Color terrainColor(float height, const Vertex& normal) {
    const Color sand = {194.0f, 174.0f, 118.0f};
    const Color grass = {75.0f, 155.0f, 80.0f};
    const Color rock = {135.0f, 130.0f, 125.0f};
    const Color snow = {235.0f, 240.0f, 245.0f};

    Color base = mixColor(
        sand, grass, transition(-0.4f, 0.15f, height));

    float slope = 1.0f - std::clamp(normal.y, 0.0f, 1.0f);
    base = mixColor(
        base, rock, transition(0.18f, 0.55f, slope));

    float snowAmount =
        transition(0.8f, 1.5f, height)
        * (1.0f - transition(0.25f, 0.6f, slope));

    return mixColor(base, snow, snowAmount);
}

std::uint32_t shadedColor(const Color& base,
                          const Vertex& normal) {
    static const Vertex lightDirection =
        normalize({-0.6f, 1.0f, -0.4f});

    float diffuse = std::max(0.0f, dot(normal, lightDirection));
    float brightness = 0.25f + 0.75f * diffuse;

    return MFB_RGB(
        static_cast<unsigned>(std::lround(base.r * brightness)),
        static_cast<unsigned>(std::lround(base.g * brightness)),
        static_cast<unsigned>(std::lround(base.b * brightness)));
}

void renderWater(
    const CameraSettings& camera,
    const WaterSettings& water,
    std::vector<std::uint32_t>& pixels,
    std::vector<float>& depthBuffer) {

    if (!water.visible) {
        return;
    }

    float halfSize = GRID_CELLS * GRID_SPACING / 2.0f;

    const std::array<Vertex, 4> corners = {{
        {-halfSize, water.level, -halfSize},
        { halfSize, water.level, -halfSize},
        {-halfSize, water.level,  halfSize},
        { halfSize, water.level,  halfSize}
    }};

    ScreenPoint a = project(corners[0], camera);
    ScreenPoint b = project(corners[1], camera);
    ScreenPoint c = project(corners[2], camera);
    ScreenPoint d = project(corners[3], camera);

    std::uint32_t color = shadedColor(
        {45.0f, 135.0f, 195.0f}, {0.0f, 1.0f, 0.0f});

    // Water shares the terrain depth buffer.
    drawTriangle(pixels, depthBuffer, a, c, b, color);
    drawTriangle(pixels, depthBuffer, b, c, d, color);
}

void renderScene(
    const Mesh& mesh,
    const CameraSettings& camera,
    const WaterSettings& water,
    std::vector<std::uint32_t>& pixels,
    std::vector<float>& depthBuffer) {

    std::fill(pixels.begin(), pixels.end(), MFB_RGB(25, 35, 50));
    std::fill(depthBuffer.begin(), depthBuffer.end(),
              -std::numeric_limits<float>::infinity());

    std::vector<ScreenPoint> projected;
    projected.reserve(mesh.vertices.size());

    for (const auto& vertex : mesh.vertices) {
        projected.push_back(project(vertex, camera));
    }

    for (const auto& triangle : mesh.triangles) {
        const Vertex& a = mesh.vertices[triangle[0]];
        const Vertex& b = mesh.vertices[triangle[1]];
        const Vertex& c = mesh.vertices[triangle[2]];

        Vertex normal = faceNormal(a, b, c);
        float height = (a.y + b.y + c.y) / 3.0f;
        Color base = terrainColor(height, normal);

        drawTriangle(
            pixels, depthBuffer,
            projected[triangle[0]],
            projected[triangle[1]],
            projected[triangle[2]],
            shadedColor(base, normal));
    }

    renderWater(camera, water, pixels, depthBuffer);
}

InputChanges handleInput(
    mfb_window* window,
    TerrainSettings& settings,
    CameraSettings& camera,
    WaterSettings& water,
    std::array<bool, 18>& previous) {

    const auto* keys = mfb_get_key_buffer(window);
    const std::array<mfb_key, 18> controls = {
        KB_KEY_UP, KB_KEY_DOWN, KB_KEY_RIGHT, KB_KEY_LEFT,
        KB_KEY_N, KB_KEY_O, KB_KEY_P, KB_KEY_R,
        KB_KEY_A, KB_KEY_D, KB_KEY_W, KB_KEY_S,
        KB_KEY_Q, KB_KEY_E, KB_KEY_C,
        KB_KEY_M, KB_KEY_J, KB_KEY_K
    };

    InputChanges changed;

    for (unsigned i = 0; i < controls.size(); ++i) {
        bool down = keys[controls[i]] != 0;

        if (down && !previous[i]) {
            if (i < 8) {
                changed.terrain = true;
            } else if (i < 15) {
                changed.camera = true;
            } else {
                changed.water = true;
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
                    water = WaterSettings{};
                    changed.water = true;
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
                case KB_KEY_M:
                    water.visible = !water.visible;
                    break;
                case KB_KEY_J:
                    water.level =
                        std::max(water.level - 0.25f, -3.0f);
                    break;
                case KB_KEY_K:
                    water.level =
                        std::min(water.level + 0.25f, 3.0f);
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

void printWater(const WaterSettings& water) {
    std::printf("Water: %s | level=%.2f\n",
                water.visible ? "ON" : "OFF", water.level);
}

int main() {
    mfb_window* window = mfb_open("Terrain Lab", WIDTH, HEIGHT);
    if (!window) {
        std::fprintf(stderr, "Failed to open Terrain Lab window.\n");
        return 1;
    }

    Mesh mesh = createGrid(GRID_CELLS, GRID_SPACING);
    TerrainSettings settings;
    CameraSettings camera;
    WaterSettings water;

    std::array<bool, 18> previousKeys{};
    std::vector<std::uint32_t> pixels(WIDTH * HEIGHT);
    std::vector<float> depthBuffer(WIDTH * HEIGHT);

    std::puts("UP/DOWN: height | RIGHT/LEFT: frequency");
    std::puts("N: next seed | O/P: more/fewer octaves");
    std::puts("A/D: rotate | W/S: tilt | Q/E: zoom out/in");
    std::puts("M: toggle water | J/K: lower/raise water");
    std::puts("R: reset terrain and water | C: reset camera");
    std::puts("Press and release each key");

    std::printf("Grid: %zu vertices, %zu triangles\n",
                mesh.vertices.size(), mesh.triangles.size());

    applyNoise(mesh, settings);
    renderScene(mesh, camera, water, pixels, depthBuffer);
    printSettings(settings);
    printCamera(camera);
    printWater(water);

    do {
        if (mfb_update_ex(window, pixels.data(), WIDTH, HEIGHT)
            != STATE_OK) {
            break;
        }

        InputChanges changed = handleInput(
            window, settings, camera, water, previousKeys);

        if (changed.terrain) {
            applyNoise(mesh, settings);
            printSettings(settings);
        }
        if (changed.camera) {
            printCamera(camera);
        }
        if (changed.water) {
            printWater(water);
        }

        if (changed.terrain || changed.camera || changed.water) {
            renderScene(mesh, camera, water, pixels, depthBuffer);
        }
    } while (mfb_wait_sync(window));

    return 0;
}