#pragma once

#include <MiniFB.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace hud {

// Each number describes one row of a 5x7 character.
inline std::array<unsigned, 7> glyph(char c) {
    switch (c) {
        case 'A': return {14,17,17,31,17,17,17};
        case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {14,17,16,16,16,17,14};
        case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31};
        case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {14,17,16,23,17,17,15};
        case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {14,4,4,4,4,4,14};
        case 'J': return {7,2,2,2,2,18,12};
        case 'K': return {17,18,20,24,20,18,17};
        case 'L': return {16,16,16,16,16,16,31};
        case 'M': return {17,27,21,21,17,17,17};
        case 'N': return {17,25,21,19,17,17,17};
        case 'O': return {14,17,17,17,17,17,14};
        case 'P': return {30,17,17,30,16,16,16};
        case 'Q': return {14,17,17,17,21,18,13};
        case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30};
        case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14};
        case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10};
        case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4};
        case 'Z': return {31,1,2,4,8,16,31};
        case '0': return {14,17,19,21,25,17,14};
        case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31};
        case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2};
        case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14};
        case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14};
        case '9': return {14,17,17,15,1,1,14};
        case '.': return {0,0,0,0,0,12,12};
        case '-': return {0,0,0,31,0,0,0};
        case ':': return {0,12,12,0,12,12,0};
        case '/': return {1,2,2,4,8,8,16};
        case '|': return {4,4,4,4,4,4,4};
        default: return {};
    }
}

inline void rectangle(
    std::vector<std::uint32_t>& pixels,
    unsigned width, unsigned height,
    int x, int y, int w, int h,
    std::uint32_t color) {

    int left = std::clamp(x, 0, int(width));
    int right = std::clamp(x + w, 0, int(width));
    int top = std::clamp(y, 0, int(height));
    int bottom = std::clamp(y + h, 0, int(height));

    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            pixels[static_cast<unsigned>(py) * width
                   + static_cast<unsigned>(px)] = color;
        }
    }
}

inline void text(
    std::vector<std::uint32_t>& pixels,
    unsigned width, unsigned height,
    int x, int y, std::string_view message,
    std::uint32_t color, int scale = 2) {

    for (char c : message) {
        auto rows = glyph(c);

        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1u << (4 - col))) != 0) {
                    rectangle(
                        pixels, width, height,
                        x + col * scale, y + row * scale,
                        scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

inline void draw(
    std::vector<std::uint32_t>& pixels,
    unsigned width, unsigned height,
    unsigned seed, float amplitude, float frequency,
    unsigned octaves, bool waterVisible, float waterLevel,
    float yawDegrees, float pitchDegrees, float zoom) {

    const auto panel = MFB_RGB(15, 23, 35);
    const auto white = MFB_RGB(225, 235, 245);
    const auto accent = MFB_RGB(100, 210, 170);
    const auto muted = MFB_RGB(170, 190, 210);

    rectangle(pixels, width, height,
              12, 12, int(width) - 24, 104, panel);

    text(pixels, width, height,
         24, 22, "TERRAIN LAB", accent);

    char line[160];

    std::snprintf(
        line, sizeof(line),
        "SEED: %u | HEIGHT: %.2f | FREQUENCY: %.2f | OCTAVES: %u",
        seed, amplitude, frequency, octaves);
    text(pixels, width, height, 24, 46, line, white);

    std::snprintf(
        line, sizeof(line),
        "WATER: %s | LEVEL: %.2f",
        waterVisible ? "ON" : "OFF", waterLevel);
    text(pixels, width, height, 24, 70, line, white);

    std::snprintf(
        line, sizeof(line),
        "CAMERA YAW: %.1f | PITCH: %.1f | ZOOM: %.1f",
        yawDegrees, pitchDegrees, zoom);
    text(pixels, width, height, 24, 94, line, muted);

    int top = int(height) - 112;
    rectangle(pixels, width, height,
              12, top, int(width) - 24, 100, panel);

    text(pixels, width, height, 24, top + 12,
         "UP/DOWN: HEIGHT | LEFT/RIGHT: FREQUENCY | N: NEXT SEED",
         white);

    text(pixels, width, height, 24, top + 34,
         "O/P: OCTAVES | A/D: ROTATE | W/S: TILT | Q/E: ZOOM",
         white);

    text(pixels, width, height, 24, top + 56,
         "M: WATER ON/OFF | J/K: WATER LEVEL | R: RESET | C: CAMERA RESET",
         white);

    text(pixels, width, height, 24, top + 78,
         "CLICK THE WINDOW. PRESS AND RELEASE EACH KEY.",
         accent);
}

} // namespace hud