#include "MiniFB.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

extern "C" {
#include "microui.h"
}
#include "ui_bridge.h"
#include "ui_renderer.h"

#define WIDTH 1000
#define HEIGHT 700
#define MAX_LINES 512

static uint32_t g_buffer[WIDTH * HEIGHT];
static int g_pattern_mode = 0;

// HW1 Part 5: UI-controlled rendering state
static float g_red_strength = 1.0f;
static float g_green_strength = 1.0f;
static float g_ring_scale = 1800.0f;
static int g_boost_blue = 1;

// HW1 Part 6: line drawing state
struct DrawnLine {
  int x0;
  int y0;
  int x1;
  int y1;
  uint32_t color;
};

static DrawnLine g_lines[MAX_LINES];
static int g_line_count = 0;

static int g_is_drawing = 0;
static int g_prev_left_down = 0;
static int g_line_start_x = 0;
static int g_line_start_y = 0;
static int g_line_preview_x = 0;
static int g_line_preview_y = 0;

static int g_enable_drawing = 1;
static float g_line_red = 255.0f;
static float g_line_green = 0.0f;
static float g_line_blue = 0.0f;

static int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value)
    return min_value;
  if (value > max_value)
    return max_value;
  return value;
}

static uint32_t current_line_color() {
  int r = clamp_int((int)g_line_red, 0, 255);
  int g = clamp_int((int)g_line_green, 0, 255);
  int b = clamp_int((int)g_line_blue, 0, 255);
  return MFB_RGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static int point_inside_rect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int point_inside_ui(int x, int y) {
  if (point_inside_rect(x, y, 20, 20, 360, 540))
    return 1;
  if (point_inside_rect(x, y, 395, 20, 380, 200))
    return 1;
  if (point_inside_rect(x, y, 395, 235, 380, 80))
    return 1;
  if (point_inside_rect(x, y, 395, 330, 380, 220))
    return 1;
  return 0;
}

static void draw_pixel_to_buffer(int x, int y, uint32_t color) {
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      int px = x + dx;
      int py = y + dy;

      if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT)
        continue;

      g_buffer[py * WIDTH + px] = color;
    }
  }
}

static void draw_line_bresenham(int x0, int y0, int x1, int y1,
                                uint32_t color) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    draw_pixel_to_buffer(x0, y0, color);

    if (x0 == x1 && y0 == y1)
      break;

    int e2 = 2 * err;

    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }

    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void handle_line_drawing(mu_Context *ctx) {
  int mouse_x = ctx->mouse_pos.x;
  int mouse_y = ctx->mouse_pos.y;

  mouse_x = clamp_int(mouse_x, 0, WIDTH - 1);
  mouse_y = clamp_int(mouse_y, 0, HEIGHT - 1);

  int left_down = (ctx->mouse_down & MU_MOUSE_LEFT) != 0;
  int mouse_over_ui = point_inside_ui(mouse_x, mouse_y);

  if (g_enable_drawing && left_down && !g_prev_left_down && !mouse_over_ui) {
    g_is_drawing = 1;
    g_line_start_x = mouse_x;
    g_line_start_y = mouse_y;
    g_line_preview_x = mouse_x;
    g_line_preview_y = mouse_y;
  }

  if (g_is_drawing && left_down) {
    g_line_preview_x = mouse_x;
    g_line_preview_y = mouse_y;
  }

  if (g_is_drawing && !left_down && g_prev_left_down) {
    if (g_line_count < MAX_LINES) {
      g_lines[g_line_count].x0 = g_line_start_x;
      g_lines[g_line_count].y0 = g_line_start_y;
      g_lines[g_line_count].x1 = g_line_preview_x;
      g_lines[g_line_count].y1 = g_line_preview_y;
      g_lines[g_line_count].color = current_line_color();
      g_line_count++;
    }

    g_is_drawing = 0;
  }

  g_prev_left_down = left_down;
}

int main() {
  // HW2 Part 0: demonstrate a GLM translation.
  const glm::vec4 point(1.0f, 2.0f, 3.0f, 1.0f);

  const glm::mat4 translation = glm::translate(
      glm::mat4(1.0f),
      glm::vec3(10.0f, 20.0f, 30.0f));

  const glm::vec4 result = translation * point;

  printf("HW2 Part 0: GLM translation example\n");
  printf("Original point: (%.1f, %.1f, %.1f)\n",
         point.x, point.y, point.z);
  printf("Translated point: (%.1f, %.1f, %.1f)\n",
         result.x, result.y, result.z);
  fflush(stdout);

  struct mfb_window *window =
      mfb_open_ex("MiniGUI Platform", WIDTH, HEIGHT, MFB_WF_RESIZABLE);
  if (!window)
    return 1;

  mu_Context *ctx = (mu_Context *)malloc(sizeof(mu_Context));
  mu_init(ctx);

  // Set font callbacks for microui
  ctx->text_width = [](mu_Font font, const char *str, int len) {
    return (len < 0 ? (int)strlen(str) : len) * 8;
  };
  ctx->text_height = [](mu_Font font) { return 8; };

  UIRenderer renderer(WIDTH, HEIGHT);

  // Set up char input callback for textbox input
  mfb_set_char_input_callback(
      [](struct mfb_window *w, unsigned int c) {
        extern void ui_bridge_char_input(struct mfb_window *, unsigned int);

        // HW1 Part 3: press P to toggle the background pattern
        if (c == 'p' || c == 'P') {
          g_pattern_mode = 1 - g_pattern_mode;
          printf("HW1 Part 3: pattern mode = %d\n", g_pattern_mode);
          return;
        }

        ui_bridge_char_input(w, c);
      },
      window);

  while (mfb_update_events(window) != MFB_STATE_EXIT) {
    // 1. Input
    ui_bridge_input(ctx, window);
    handle_line_drawing(ctx);

    // 2. Scene Rendering (Background)
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
      int x = i % WIDTH;
      int y = i / WIDTH;

      // Center of the screen
      int cx = x - WIDTH / 2;
      int cy = y - HEIGHT / 2;

      // Distance-like value from the center.
      // We use cx*cx + cy*cy to create circular/ring patterns.
      int scale = (int)g_ring_scale;
      if (scale < 200) {
        scale = 200;
      }
      int dist_pattern = (cx * cx + cy * cy) / scale;

      // Checkerboard value based on both x and y.
      int checker = ((x / 60) + (y / 60)) % 2;

      uint8_t r;
      uint8_t g;
      uint8_t b;

      if (g_pattern_mode == 0) {
        // Creative 2D color pattern: rings + checker influence
        int red_value = (int)(((dist_pattern + x / 5) % 256) * g_red_strength);
        int green_value =
            (int)(((dist_pattern + y / 4) % 256) * g_green_strength);
        int blue_value;

        if (g_boost_blue) {
          blue_value = checker ? 240 : (dist_pattern * 4) % 256;
        } else {
          blue_value = checker ? 90 : (dist_pattern * 2) % 128;
        }

        if (red_value > 255)
          red_value = 255;
        if (green_value > 255)
          green_value = 255;
        if (blue_value > 255)
          blue_value = 255;

        r = (uint8_t)red_value;
        g = (uint8_t)green_value;
        b = (uint8_t)blue_value;
      } else {
        // HW1 Part 3: alternate keyboard-controlled pattern
        int diagonal = ((x + y) / 45) % 2;

        int red_value =
            (int)((diagonal ? 230 : (dist_pattern + 40) % 256) *
                  g_red_strength);
        int green_value =
            (int)(((x / 3 + dist_pattern * 2) % 256) * g_green_strength);
        int blue_value;

        if (g_boost_blue) {
          blue_value = diagonal ? 130 : (255 - ((y / 3 + dist_pattern) % 256));
        } else {
          blue_value = diagonal ? 60 : (120 - ((y / 6 + dist_pattern) % 120));
        }

        if (red_value > 255)
          red_value = 255;
        if (green_value > 255)
          green_value = 255;
        if (blue_value > 255)
          blue_value = 255;
        if (blue_value < 0)
          blue_value = 0;

        r = (uint8_t)red_value;
        g = (uint8_t)green_value;
        b = (uint8_t)blue_value;
      }

      g_buffer[i] = MFB_RGB(r, g, b);
    }

    // HW1 Part 6: draw all saved lines on top of the framebuffer background
    for (int i = 0; i < g_line_count; i++) {
      draw_line_bresenham(g_lines[i].x0, g_lines[i].y0, g_lines[i].x1,
                          g_lines[i].y1, g_lines[i].color);
    }

    if (g_is_drawing) {
      draw_line_bresenham(g_line_start_x, g_line_start_y, g_line_preview_x,
                          g_line_preview_y, current_line_color());
    }

    // 3. UI Logic
    static float slider_val = 50.0f;
    static float number_val = 3.14f;
    static int checkbox_a = 0;
    static int checkbox_b = 1;
    static int show_pattern_info = 1;
    static char textbox_buf[128] = "edit me";
    static bool quit_requested = false;

    mu_begin(ctx);

    // --- Widgets window ---
    if (mu_begin_window(ctx, "Widgets", mu_rect(20, 20, 360, 540))) {
      int w1[] = {-1};

      // label / text
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "mu_label: plain static text");
      mu_text(ctx, "mu_text: word-wrapped longer text that will reflow inside "
                   "the window width automatically.");

      // button
      mu_layout_row(ctx, 1, w1, 0);
      if (mu_button(ctx, "mu_button: click me")) {
        quit_requested = false;
      }

      // HW1 Part 2: custom immediate-mode UI widget
      mu_layout_row(ctx, 1, w1, 0);
      if (mu_button(ctx, "Toggle pattern info")) {
        show_pattern_info = !show_pattern_info;
      }

      mu_layout_row(ctx, 1, w1, 0);
      if (show_pattern_info) {
        mu_label(ctx, "Pattern: rings + checkerboard");
      } else {
        mu_label(ctx, "Pattern info hidden");
      }

      // HW1 Part 3: keyboard shortcut information
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Press P to switch pattern");

      mu_layout_row(ctx, 1, w1, 0);
      if (g_pattern_mode == 0) {
        mu_label(ctx, "Keyboard mode: rings");
      } else {
        mu_label(ctx, "Keyboard mode: alternate");
      }

      // HW1 Part 5: pattern controls
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "HW1 Part 5: pattern controls");

      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Red strength:");
      mu_slider(ctx, &g_red_strength, 0.2f, 2.0f);

      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Green strength:");
      mu_slider(ctx, &g_green_strength, 0.2f, 2.0f);

      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Ring scale:");
      mu_slider(ctx, &g_ring_scale, 400.0f, 4000.0f);

      mu_layout_row(ctx, 1, w1, 0);
      mu_checkbox(ctx, "Boost blue channel", &g_boost_blue);

      // checkbox
      mu_layout_row(ctx, 1, w1, 0);
      mu_checkbox(ctx, "mu_checkbox A (off)", &checkbox_a);
      mu_checkbox(ctx, "mu_checkbox B (on)", &checkbox_b);

      // textbox
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "mu_textbox:");
      mu_textbox(ctx, textbox_buf, sizeof(textbox_buf));

      // slider
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "mu_slider (0-100):");
      mu_slider(ctx, &slider_val, 0, 100);

      // number
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "mu_number (step 0.1):");
      mu_number(ctx, &number_val, 0.1f);

      // header
      if (mu_header(ctx, "mu_header: collapsible section")) {
        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "Content inside the header.");
      }

      // treenode
      if (mu_begin_treenode(ctx, "mu_treenode: root")) {
        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "child item A");
        if (mu_begin_treenode(ctx, "nested node")) {
          mu_layout_row(ctx, 1, w1, 0);
          mu_label(ctx, "deeply nested item");
          mu_end_treenode(ctx);
        }
        mu_end_treenode(ctx);
      }

      // quit button
      mu_layout_row(ctx, 1, w1, 0);
      if (mu_button(ctx, "Quit")) {
        quit_requested = true;
      }

      mu_end_window(ctx);
    }

    // --- Panel window ---
    if (mu_begin_window(ctx, "Panel Demo", mu_rect(395, 20, 380, 200))) {
      int w2[] = {-1};
      mu_layout_row(ctx, 1, w2, 120);
      mu_begin_panel(ctx, "scrollable panel");
      int wp[] = {-1};
      for (int i = 1; i <= 12; i++) {
        mu_layout_row(ctx, 1, wp, 0);
        char line[32];
        snprintf(line, sizeof(line), "Panel row %d", i);
        mu_label(ctx, line);
      }
      mu_end_panel(ctx);
      mu_end_window(ctx);
    }

    // --- Popup demo window ---
    if (mu_begin_window(ctx, "Popup Demo", mu_rect(395, 235, 380, 80))) {
      int w3[] = {-1};
      mu_layout_row(ctx, 1, w3, 0);
      if (mu_button(ctx, "Open popup")) {
        mu_Container *popup = mu_get_container(ctx, "my popup");
        popup->rect = mu_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, 260, 84);
        popup->open = 1;
        ctx->hover_root = ctx->next_hover_root = popup;
        mu_bring_to_front(ctx, popup);
      }
      int popup_opt = MU_OPT_POPUP | MU_OPT_NORESIZE | MU_OPT_NOSCROLL |
                      MU_OPT_NOTITLE | MU_OPT_CLOSED;
      if (mu_begin_window_ex(ctx, "my popup", mu_rect(0, 0, 260, 84),
                             popup_opt)) {
        int wp[] = {-1};
        mu_layout_row(ctx, 1, wp, 0);
        mu_label(ctx, "mu_popup: click outside to close");
        if (mu_button(ctx, "Close")) {
          mu_get_current_container(ctx)->open = 0;
        }
        mu_end_window(ctx);
      }
      mu_end_window(ctx);
    }

    // --- HW1 Part 6: line drawing tools ---
    if (mu_begin_window(ctx, "Line Drawing", mu_rect(395, 330, 380, 220))) {
      int w4[] = {-1};

      mu_layout_row(ctx, 1, w4, 0);
      mu_label(ctx, "Click-drag-release outside UI");

      mu_layout_row(ctx, 1, w4, 0);
      mu_checkbox(ctx, "Enable drawing", &g_enable_drawing);

      mu_layout_row(ctx, 1, w4, 0);
      mu_label(ctx, "Line red:");
      mu_slider(ctx, &g_line_red, 0.0f, 255.0f);

      mu_layout_row(ctx, 1, w4, 0);
      mu_label(ctx, "Line green:");
      mu_slider(ctx, &g_line_green, 0.0f, 255.0f);

      mu_layout_row(ctx, 1, w4, 0);
      mu_label(ctx, "Line blue:");
      mu_slider(ctx, &g_line_blue, 0.0f, 255.0f);

      mu_layout_row(ctx, 1, w4, 0);
      if (mu_button(ctx, "Clear lines")) {
        g_line_count = 0;
        g_is_drawing = 0;
      }

      mu_end_window(ctx);
    }

    mu_end(ctx);

    if (quit_requested) {
      mfb_close(window);
      break;
    }

    // 4. UI Rendering
    renderer.render(ctx, g_buffer);

    // 5. Display
    mfb_update_state state = mfb_update_ex(window, g_buffer, WIDTH, HEIGHT);
    if (state < 0)
      break;

    mfb_wait_sync(window);
  }

  mfb_close(window);
  free(ctx);
  return 0;
}