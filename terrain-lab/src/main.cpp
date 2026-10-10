#include <MiniFB.h>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    constexpr unsigned width = 1000;
    constexpr unsigned height = 700;

    mfb_window* window = mfb_open("Terrain Lab", width, height);
    if (!window) {
        std::fprintf(stderr, "Failed to open Terrain Lab window.\n");
        return 1;
    }

    // One color value for each pixel: a dark blue background.
    std::vector<std::uint32_t> pixels(
        width * height, MFB_RGB(25, 35, 50));

    std::puts("Terrain Lab started. Close the window to exit.");

    do {
        if (mfb_update_ex(window, pixels.data(), width, height) != STATE_OK) {
            break;
        }
    } while (mfb_wait_sync(window));

    return 0;
}