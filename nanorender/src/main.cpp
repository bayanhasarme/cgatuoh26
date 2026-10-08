#include "MiniFB.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

static int g_show_hw1_tools = 0;
static int g_show_pattern_background = 0;
static int g_show_wireframe = 1;
static int g_show_transforms = 1;

// HW2 Part 4: independent Local and World transformation values.
// Rotation values are stored in degrees.
// These values will be applied to the mesh in Part 5.
struct TransformState {
  glm::vec3 translation{0.0f};
  glm::vec3 rotation{0.0f};
  glm::vec3 scale{1.0f};
};

static TransformState g_local_transform;
static TransformState g_world_transform;

struct Face {
  std::array<size_t, 3> indices;
};

struct Mesh {
  std::vector<glm::vec3> vertices;
  std::vector<Face> faces;
};

static Mesh g_mesh;
static const char *g_mesh_path = "models/hw2_test.obj";
static bool g_mesh_loaded = false;
static std::string g_mesh_error;

// Preserve the original vertices and store the fitted vertices separately.
struct ViewportFit {
  glm::vec3 minimum{0.0f};
  glm::vec3 maximum{0.0f};
  glm::vec3 center{0.0f};
  glm::vec3 translation{0.0f};
  float scale = 1.0f;
  std::vector<glm::vec3> fitted_vertices;
};

static ViewportFit g_fit;

static void calculate_viewport_fit(const Mesh &mesh) {
  g_fit = ViewportFit{};
  if (mesh.vertices.empty())
    return;

  g_fit.minimum = mesh.vertices.front();
  g_fit.maximum = mesh.vertices.front();

  for (const glm::vec3 &vertex : mesh.vertices) {
    g_fit.minimum = glm::min(g_fit.minimum, vertex);
    g_fit.maximum = glm::max(g_fit.maximum, vertex);
  }

  g_fit.center = (g_fit.minimum + g_fit.maximum) * 0.5f;
  const glm::vec3 extent = g_fit.maximum - g_fit.minimum;
  const float largest_extent =
      glm::max(extent.x, glm::max(extent.y, extent.z));
  const float available_size =
      0.8f * glm::min(float(WIDTH), float(HEIGHT));

  g_fit.scale =
      largest_extent > 0.0f ? available_size / largest_extent : 1.0f;

  const glm::vec3 screen_center(
      WIDTH * 0.5f, HEIGHT * 0.5f, 0.0f);
  g_fit.translation = screen_center - g_fit.scale * g_fit.center;

  g_fit.fitted_vertices.reserve(mesh.vertices.size());
  for (const glm::vec3 &vertex : mesh.vertices) {
    g_fit.fitted_vertices.push_back(
        g_fit.scale * vertex + g_fit.translation);
  }
}

// Positive OBJ indices start at 1.
// Negative indices count backwards from the vertices read so far.
// For slash-separated tokens, only the vertex index is used.
static bool parse_obj_index(const std::string &token, size_t vertex_count,
                            size_t &index) {
  const std::string number = token.substr(0, token.find('/'));
  try {
    size_t consumed = 0;
    const long long value = std::stoll(number, &consumed);

    if (consumed != number.size() || value == 0)
      return false;

    if (value > 0) {
      if (static_cast<unsigned long long>(value) > vertex_count)
        return false;
      index = static_cast<size_t>(value - 1);
    } else {
      const long long resolved =
          static_cast<long long>(vertex_count) + value;
      if (resolved < 0 ||
          static_cast<unsigned long long>(resolved) >= vertex_count)
        return false;
      index = static_cast<size_t>(resolved);
    }
    return true;
  } catch (...) {
    return false;
  }
}

static bool load_obj(const char *path, Mesh &mesh, std::string &error) {
  mesh = Mesh{};
  error.clear();

  std::ifstream file(path);
  if (!file) {
    error = std::string("Cannot open: ") + path +
            ". Run from the nanorender folder.";
    return false;
  }

  Mesh loaded;
  std::string line;
  size_t line_number = 0;

  while (std::getline(file, line)) {
    ++line_number;
    const size_t comment = line.find('#');
    if (comment != std::string::npos)
      line.erase(comment);

    std::istringstream stream(line);
    std::string type;
    if (!(stream >> type))
      continue;

    if (type == "v") {
      glm::vec3 vertex;
      if (!(stream >> vertex.x >> vertex.y >> vertex.z) ||
          !std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
          !std::isfinite(vertex.z)) {
        error = "Invalid vertex at line " + std::to_string(line_number);
        return false;
      }
      loaded.vertices.push_back(vertex);
    } else if (type == "f") {
      std::array<std::string, 3> tokens;
      std::string extra;

      if (!(stream >> tokens[0] >> tokens[1] >> tokens[2]) ||
          (stream >> extra)) {
        error = "Expected a triangular face at line " +
                std::to_string(line_number);
        return false;
      }

      Face face{};
      for (size_t i = 0; i < 3; ++i) {
        if (!parse_obj_index(tokens[i], loaded.vertices.size(),
                             face.indices[i])) {
          error = "Invalid vertex index at line " +
                  std::to_string(line_number);
          return false;
        }
      }
      loaded.faces.push_back(face);
    }
  }

  if (file.bad()) {
    error = "Error while reading the OBJ file.";
    return false;
  }
  if (loaded.vertices.empty() || loaded.faces.empty()) {
    error = "The OBJ must contain vertices and triangular faces.";
    return false;
  }

  mesh = std::move(loaded);
  return true;
}

static void reload_mesh() {
  g_mesh_loaded = load_obj(g_mesh_path, g_mesh, g_mesh_error);
  g_fit = ViewportFit{};

  if (g_mesh_loaded) {
    printf("HW2 Part 1: loaded %s\n", g_mesh_path);
    printf("Vertices: %zu\n", g_mesh.vertices.size());
    printf("Faces: %zu\n", g_mesh.faces.size());

    calculate_viewport_fit(g_mesh);

    printf("HW2 Part 2: bounding box and viewport fit\n");
    printf("Minimum: (%.2f, %.2f, %.2f)\n",
           g_fit.minimum.x, g_fit.minimum.y, g_fit.minimum.z);
    printf("Maximum: (%.2f, %.2f, %.2f)\n",
           g_fit.maximum.x, g_fit.maximum.y, g_fit.maximum.z);
    printf("Center: (%.2f, %.2f, %.2f)\n",
           g_fit.center.x, g_fit.center.y, g_fit.center.z);
    printf("Uniform scale: %.2f\n", g_fit.scale);
    printf("Translation: (%.2f, %.2f, %.2f)\n",
           g_fit.translation.x, g_fit.translation.y, g_fit.translation.z);

    for (size_t i = 0; i < g_fit.fitted_vertices.size(); ++i) {
      const glm::vec3 &vertex = g_fit.fitted_vertices[i];
      printf("Fitted vertex %zu: (%.2f, %.2f, %.2f)\n",
             i + 1, vertex.x, vertex.y, vertex.z);
    }
  } else {
    printf("HW2 Part 1: %s\n", g_mesh_error.c_str());
  }
  fflush(stdout);
}

// HW1 pattern controls.
static float g_red_strength = 1.0f;
static float g_green_strength = 1.0f;
static float g_ring_scale = 1800.0f;
static int g_boost_blue = 1;

// HW1 line drawing.
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
  if (point_inside_rect(x, y, 790, 20, 200, 300))
    return 1;
  if (point_inside_rect(x, y, 790, 335, 200, 345))
    return 1;

  if (g_show_transforms && !g_show_hw1_tools) {
    if (point_inside_rect(x, y, 20, 420, 370, 260))
      return 1;
    if (point_inside_rect(x, y, 405, 420, 370, 260))
      return 1;
  }

  if (g_show_hw1_tools) {
    if (point_inside_rect(x, y, 20, 20, 360, 540))
      return 1;
    if (point_inside_rect(x, y, 395, 20, 380, 200))
      return 1;
    if (point_inside_rect(x, y, 395, 235, 380, 80))
      return 1;
    if (point_inside_rect(x, y, 395, 330, 380, 220))
      return 1;
  }
  return 0;
}

// The same Bresenham implementation used in HW1.
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

// Orthographic projection drops Z.
static glm::ivec2 project_orthographic(const glm::vec3 &vertex) {
  return glm::ivec2(
      static_cast<int>(std::lround(vertex.x)),
      static_cast<int>(std::lround(vertex.y)));
}

static void draw_mesh_wireframe() {
  if (!g_mesh_loaded || !g_show_wireframe)
    return;

  const uint32_t color = MFB_RGB(255, 255, 255);
  for (const Face &face : g_mesh.faces) {
    const glm::ivec2 a =
        project_orthographic(g_fit.fitted_vertices[face.indices[0]]);
    const glm::ivec2 b =
        project_orthographic(g_fit.fitted_vertices[face.indices[1]]);
    const glm::ivec2 c =
        project_orthographic(g_fit.fitted_vertices[face.indices[2]]);

    draw_line_bresenham(a.x, a.y, b.x, b.y, color);
    draw_line_bresenham(b.x, b.y, c.x, c.y, color);
    draw_line_bresenham(c.x, c.y, a.x, a.y, color);
  }
}

static void handle_line_drawing(mu_Context *ctx) {
  int mouse_x = clamp_int(ctx->mouse_pos.x, 0, WIDTH - 1);
  int mouse_y = clamp_int(ctx->mouse_pos.y, 0, HEIGHT - 1);
  int left_down = (ctx->mouse_down & MU_MOUSE_LEFT) != 0;

  if (!g_show_hw1_tools) {
    g_is_drawing = 0;
    g_prev_left_down = left_down;
    return;
  }

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

static void fit_vector_label(mu_Context *ctx, const char *name,
                             const glm::vec3 &value) {
  int widths[] = {-1};
  char text[96];

  mu_layout_row(ctx, 1, widths, 0);
  mu_label(ctx, name);
  snprintf(text, sizeof(text), "(%.1f, %.1f, %.1f)",
           value.x, value.y, value.z);
  mu_layout_row(ctx, 1, widths, 0);
  mu_text(ctx, text);
}

// HW2 Part 4: one row of three sliders, in X/Y/Z order.
static void transform_vector_controls(mu_Context *ctx, const char *label,
                                      glm::vec3 &value,
                                      float minimum, float maximum) {
  int full_width[] = {-1};
  int axis_widths[] = {108, 108, -1};

  mu_layout_row(ctx, 1, full_width, 0);
  mu_label(ctx, label);

  mu_layout_row(ctx, 3, axis_widths, 0);
  mu_slider(ctx, &value.x, minimum, maximum);
  mu_slider(ctx, &value.y, minimum, maximum);
  mu_slider(ctx, &value.z, minimum, maximum);
}

static void transformation_window(mu_Context *ctx, const char *title,
                                  mu_Rect rect, TransformState &state) {
  // Fixed positions keep these two control windows side by side.
  int options = MU_OPT_NORESIZE | MU_OPT_NOCLOSE | MU_OPT_NOTITLE;

  if (mu_begin_window_ex(ctx, title, rect, options)) {
    int full_width[] = {-1};
    int axis_widths[] = {108, 108, -1};

    mu_layout_row(ctx, 1, full_width, 0);
    mu_label(ctx, title);

    mu_layout_row(ctx, 3, axis_widths, 0);
    mu_label(ctx, "X");
    mu_label(ctx, "Y");
    mu_label(ctx, "Z");

    transform_vector_controls(ctx, "Translation (model units)",
                              state.translation, -2.0f, 2.0f);
    transform_vector_controls(ctx, "Rotation (degrees)",
                              state.rotation, -180.0f, 180.0f);
    transform_vector_controls(ctx, "Scale",
                              state.scale, 0.1f, 3.0f);

    mu_layout_row(ctx, 1, full_width, 0);
    if (mu_button(ctx, "Reset this frame"))
      state = TransformState{};

    mu_layout_row(ctx, 1, full_width, 0);
    mu_label(ctx, "Part 4: values only");

    mu_end_window(ctx);
  }
}

int main() {
  const glm::vec4 point(1.0f, 2.0f, 3.0f, 1.0f);
  const glm::mat4 translation = glm::translate(
      glm::mat4(1.0f), glm::vec3(10.0f, 20.0f, 30.0f));
  const glm::vec4 result = translation * point;

  printf("HW2 Part 0: GLM translation example\n");
  printf("Original point: (%.1f, %.1f, %.1f)\n",
         point.x, point.y, point.z);
  printf("Translated point: (%.1f, %.1f, %.1f)\n",
         result.x, result.y, result.z);
  fflush(stdout);

  reload_mesh();

  struct mfb_window *window =
      mfb_open_ex("MiniGUI Platform", WIDTH, HEIGHT, MFB_WF_RESIZABLE);
  if (!window)
    return 1;

  mu_Context *ctx = (mu_Context *)malloc(sizeof(mu_Context));
  mu_init(ctx);

  ctx->text_width = [](mu_Font font, const char *str, int len) {
    return (len < 0 ? (int)strlen(str) : len) * 8;
  };
  ctx->text_height = [](mu_Font font) { return 8; };

  UIRenderer renderer(WIDTH, HEIGHT);

  mfb_set_char_input_callback(
      [](struct mfb_window *w, unsigned int c) {
        extern void ui_bridge_char_input(struct mfb_window *, unsigned int);
        if (c == 'p' || c == 'P') {
          g_pattern_mode = 1 - g_pattern_mode;
          printf("HW1 Part 3: pattern mode = %d\n", g_pattern_mode);
          return;
        }
        ui_bridge_char_input(w, c);
      },
      window);

  while (mfb_update_events(window) != MFB_STATE_EXIT) {
    ui_bridge_input(ctx, window);
    handle_line_drawing(ctx);

    for (int i = 0; i < WIDTH * HEIGHT; i++) {
      if (!g_show_pattern_background) {
        g_buffer[i] = MFB_RGB(20, 24, 32);
        continue;
      }

      int x = i % WIDTH;
      int y = i / WIDTH;
      int cx = x - WIDTH / 2;
      int cy = y - HEIGHT / 2;
      int scale = (int)g_ring_scale;
      if (scale < 200)
        scale = 200;

      int dist_pattern = (cx * cx + cy * cy) / scale;
      int checker = ((x / 60) + (y / 60)) % 2;
      int red_value;
      int green_value;
      int blue_value;

      if (g_pattern_mode == 0) {
        red_value =
            (int)(((dist_pattern + x / 5) % 256) * g_red_strength);
        green_value =
            (int)(((dist_pattern + y / 4) % 256) * g_green_strength);
        if (g_boost_blue)
          blue_value = checker ? 240 : (dist_pattern * 4) % 256;
        else
          blue_value = checker ? 90 : (dist_pattern * 2) % 128;
      } else {
        int diagonal = ((x + y) / 45) % 2;
        red_value =
            (int)((diagonal ? 230 : (dist_pattern + 40) % 256) *
                  g_red_strength);
        green_value =
            (int)(((x / 3 + dist_pattern * 2) % 256) * g_green_strength);
        if (g_boost_blue)
          blue_value = diagonal ? 130 :
              (255 - ((y / 3 + dist_pattern) % 256));
        else
          blue_value = diagonal ? 60 :
              (120 - ((y / 6 + dist_pattern) % 120));
      }

      g_buffer[i] = MFB_RGB(
          (uint8_t)clamp_int(red_value, 0, 255),
          (uint8_t)clamp_int(green_value, 0, 255),
          (uint8_t)clamp_int(blue_value, 0, 255));
    }

    draw_mesh_wireframe();

    if (g_show_hw1_tools) {
      for (int i = 0; i < g_line_count; i++) {
        draw_line_bresenham(g_lines[i].x0, g_lines[i].y0, g_lines[i].x1,
                            g_lines[i].y1, g_lines[i].color);
      }
      if (g_is_drawing) {
        draw_line_bresenham(g_line_start_x, g_line_start_y, g_line_preview_x,
                            g_line_preview_y, current_line_color());
      }
    }

    static float slider_val = 50.0f;
    static float number_val = 3.14f;
    static int checkbox_a = 0;
    static int checkbox_b = 1;
    static int show_pattern_info = 1;
    static char textbox_buf[128] = "edit me";
    static bool quit_requested = false;

    mu_begin(ctx);

    if (g_show_hw1_tools) {
      if (mu_begin_window(ctx, "Widgets", mu_rect(20, 20, 360, 540))) {
        int w1[] = {-1};
        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "mu_label: plain static text");
        mu_text(ctx, "mu_text: word-wrapped longer text that will reflow inside "
                     "the window width automatically.");

        mu_layout_row(ctx, 1, w1, 0);
        if (mu_button(ctx, "mu_button: click me"))
          quit_requested = false;

        mu_layout_row(ctx, 1, w1, 0);
        if (mu_button(ctx, "Toggle pattern info"))
          show_pattern_info = !show_pattern_info;

        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, show_pattern_info ?
                 "Pattern: rings + checkerboard" : "Pattern info hidden");

        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "Press P to switch pattern");
        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, g_pattern_mode == 0 ?
                 "Keyboard mode: rings" : "Keyboard mode: alternate");

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

        mu_layout_row(ctx, 1, w1, 0);
        mu_checkbox(ctx, "mu_checkbox A (off)", &checkbox_a);
        mu_checkbox(ctx, "mu_checkbox B (on)", &checkbox_b);

        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "mu_textbox:");
        mu_textbox(ctx, textbox_buf, sizeof(textbox_buf));
        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "mu_slider (0-100):");
        mu_slider(ctx, &slider_val, 0, 100);
        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "mu_number (step 0.1):");
        mu_number(ctx, &number_val, 0.1f);

        if (mu_header(ctx, "mu_header: collapsible section")) {
          mu_layout_row(ctx, 1, w1, 0);
          mu_label(ctx, "Content inside the header.");
        }
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

        mu_layout_row(ctx, 1, w1, 0);
        if (mu_button(ctx, "Quit"))
          quit_requested = true;
        mu_end_window(ctx);
      }

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
          if (mu_button(ctx, "Close"))
            mu_get_current_container(ctx)->open = 0;
          mu_end_window(ctx);
        }
        mu_end_window(ctx);
      }

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
    }

    if (mu_begin_window(ctx, "HW2 Mesh Info",
                        mu_rect(790, 335, 200, 345))) {
      int widths[] = {-1};
      mu_layout_row(ctx, 1, widths, 0);
      mu_text(ctx, g_mesh_path);

      if (g_mesh_loaded) {
        char text[64];
        snprintf(text, sizeof(text), "Vertices: %zu", g_mesh.vertices.size());
        mu_layout_row(ctx, 1, widths, 0);
        mu_label(ctx, text);
        snprintf(text, sizeof(text), "Faces: %zu", g_mesh.faces.size());
        mu_layout_row(ctx, 1, widths, 0);
        mu_label(ctx, text);
      } else {
        mu_layout_row(ctx, 1, widths, 0);
        mu_text(ctx, g_mesh_error.c_str());
      }

      mu_layout_row(ctx, 1, widths, 0);
      if (mu_button(ctx, "Reload OBJ"))
        reload_mesh();

      mu_layout_row(ctx, 1, widths, 0);
      mu_checkbox(ctx, "Show wireframe", &g_show_wireframe);
      mu_layout_row(ctx, 1, widths, 0);
      mu_checkbox(ctx, "Pattern background", &g_show_pattern_background);
      mu_layout_row(ctx, 1, widths, 0);
      mu_checkbox(ctx, "Show HW1 tools", &g_show_hw1_tools);
      mu_layout_row(ctx, 1, widths, 0);
      mu_checkbox(ctx, "Show transforms", &g_show_transforms);

      mu_layout_row(ctx, 1, widths, 0);
      mu_text(ctx, "Orthographic: use X,Y and drop Z.");

      mu_layout_row(ctx, 1, widths, 0);
      if (mu_button(ctx, "Quit"))
        quit_requested = true;

      mu_end_window(ctx);
    }

    if (mu_begin_window(ctx, "HW2 Bounding Box",
                        mu_rect(790, 20, 200, 300))) {
      int widths[] = {-1};
      if (g_mesh_loaded) {
        fit_vector_label(ctx, "Minimum XYZ:", g_fit.minimum);
        fit_vector_label(ctx, "Maximum XYZ:", g_fit.maximum);
        fit_vector_label(ctx, "Model center:", g_fit.center);

        char text[64];
        snprintf(text, sizeof(text), "Scale: %.2f", g_fit.scale);
        mu_layout_row(ctx, 1, widths, 0);
        mu_label(ctx, text);
        fit_vector_label(ctx, "Translation XYZ:", g_fit.translation);
        mu_layout_row(ctx, 1, widths, 0);
        mu_text(ctx, "Fit uses 80% of the smaller window dimension.");
      } else {
        mu_layout_row(ctx, 1, widths, 0);
        mu_label(ctx, "No mesh loaded.");
      }
      mu_end_window(ctx);
    }

    // Hide transformation controls when displaying the original HW1 tools.
    if (g_show_transforms && !g_show_hw1_tools) {
      transformation_window(ctx, "Local Transformations",
                            mu_rect(20, 420, 370, 260),
                            g_local_transform);
      transformation_window(ctx, "World Transformations",
                            mu_rect(405, 420, 370, 260),
                            g_world_transform);
    }

    mu_end(ctx);

    if (quit_requested)
      break;

    renderer.render(ctx, g_buffer);

    mfb_update_state state = mfb_update_ex(window, g_buffer, WIDTH, HEIGHT);
    if (state < 0)
      break;
    mfb_wait_sync(window);
  }

  mfb_close(window);
  free(ctx);
  return 0;
}