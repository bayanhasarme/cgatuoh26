#include "MiniFB.h"

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <array>

#include <cmath>

#include <fstream>
#include <random>

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

// HW4 Part 1: filled screen-space rectangles, one per projected face.
static int g_show_triangle_boxes = 0;

static int g_show_transforms = 1;

static int g_enable_arrow_controls = 1;

// HW3 Part 1 debug geometry.

static int g_show_local_axes = 1;

static int g_show_world_axes = 1;

static int g_show_bounding_box = 1;

// HW3 Part 4: cyan face normals, magenta vertex normals.

static int g_show_face_normals = 1;

static int g_show_vertex_normals = 1;

static constexpr int VIEW_X = 20;

static constexpr int VIEW_Y = 20;

static constexpr int VIEW_WIDTH = 755;

static constexpr int VIEW_HEIGHT = 385;

static bool g_clip_mesh_pixels = false;

struct TransformState {

  glm::vec3 translation{0.0f};

  glm::vec3 rotation{0.0f}; // Degrees.

  glm::vec3 scale{1.0f};

};

static TransformState g_local_transform;

static TransformState g_world_transform;

// HW3 Part 2: a camera has a rigid world pose (no scale).

struct Camera {

  glm::vec3 position{0.0f, 0.0f, 5.0f};

  glm::vec3 rotation{0.0f}; // Degrees; same Rz * Ry * Rx convention.

};

static Camera g_camera;

static int g_show_camera_controls = 1;

static glm::mat4 g_view_matrix(1.0f);

// HW3 Part 3: GLM right-handed projections, OpenGL depth range [-1, 1].

static int g_use_perspective = 1;

static float g_fov_degrees = 45.0f;

static constexpr float NEAR_PLANE = 0.1f;

static constexpr float FAR_PLANE = 100.0f;

static glm::mat4 g_projection_matrix(1.0f);

// HW2 Part 6: intercept arrow input before ui_bridge_input.

// Each new press changes World Translation by 0.1 model units.

// Holding a key does not repeatedly move the model.

static void handle_transform_keyboard(mu_Context *ctx,

                                      struct mfb_window *window) {

  static std::array<uint8_t, 4> previous{};

  const std::array<int, 4> arrow_keys = {

      MFB_KB_KEY_LEFT, MFB_KB_KEY_RIGHT,

      MFB_KB_KEY_UP, MFB_KB_KEY_DOWN};

  const uint8_t *keys = mfb_get_key_buffer(window);

  if (!keys)

    return;

  std::array<bool, 4> pressed{};

  for (size_t i = 0; i < arrow_keys.size(); ++i) {

    const uint8_t current = keys[arrow_keys[i]];

    pressed[i] = current && !previous[i];

    previous[i] = current;

  }

  // Always update previous states, even while controls are disabled.

  // Avoid scene movement while editing a GUI control or using HW1 tools.

  const bool modifier_down =

      keys[MFB_KB_KEY_LEFT_SHIFT] ||

      keys[MFB_KB_KEY_RIGHT_SHIFT] ||

      keys[MFB_KB_KEY_LEFT_CONTROL] ||

      keys[MFB_KB_KEY_RIGHT_CONTROL] ||

      keys[MFB_KB_KEY_LEFT_ALT] ||

      keys[MFB_KB_KEY_RIGHT_ALT];

  if (!g_enable_arrow_controls || g_show_hw1_tools ||

      ctx->focus != 0 || modifier_down)

    return;

  constexpr float step = 0.1f;

  const float dx = step * (int(pressed[1]) - int(pressed[0]));

  const float dy = step * (int(pressed[3]) - int(pressed[2]));

  if (dx == 0.0f && dy == 0.0f)

    return;

  // Match the slider range so keyboard and GUI share the same state.

  g_world_transform.translation.x = glm::clamp(

      g_world_transform.translation.x + dx, -2.0f, 2.0f);

  g_world_transform.translation.y = glm::clamp(

      g_world_transform.translation.y + dy, -2.0f, 2.0f);

  printf("HW2 Part 6: World Translation = (%.2f, %.2f, %.2f)\n",

         g_world_transform.translation.x,

         g_world_transform.translation.y,

         g_world_transform.translation.z);

  fflush(stdout);

}

// Column vectors: X rotation is applied first.

static glm::mat4 rotation_matrix(const glm::vec3 &degrees) {

  const glm::mat4 identity(1.0f);

  const glm::mat4 rx = glm::rotate(

      identity, glm::radians(degrees.x), glm::vec3(1, 0, 0));

  const glm::mat4 ry = glm::rotate(

      identity, glm::radians(degrees.y), glm::vec3(0, 1, 0));

  const glm::mat4 rz = glm::rotate(

      identity, glm::radians(degrees.z), glm::vec3(0, 0, 1));

  return rz * ry * rx;

}

static glm::mat4 transformation_matrix(const TransformState &state) {

  const glm::mat4 identity(1.0f);

  const glm::mat4 t = glm::translate(identity, state.translation);

  const glm::mat4 r = rotation_matrix(state.rotation);

  const glm::mat4 s = glm::scale(identity, state.scale);

  return t * r * s;

}

// Invert the entire camera pose: inverse(T * R) = inverse(R) * inverse(T).

// Negating Euler angles in the original order would give the wrong inverse.

static glm::mat4 camera_view_matrix(const Camera &camera) {

  const glm::mat4 pose = glm::translate(glm::mat4(1.0f), camera.position) *

                         rotation_matrix(camera.rotation);

  return glm::inverse(pose);

}

static void set_comparison_demo(bool orbit) {

  g_local_transform = TransformState{};

  g_world_transform = TransformState{};

  g_local_transform.scale = glm::vec3(0.5f);

  if (orbit) {

    g_local_transform.translation.x = 0.8f;

    g_world_transform.rotation.z = 45.0f;

  } else {

    g_world_transform.translation.x = 0.8f;

    g_local_transform.rotation.z = 45.0f;

  }

  g_show_wireframe = 1;

  g_show_pattern_background = 0;

}

struct Face {

  std::array<size_t, 3> indices;

};

struct Mesh {

  std::vector<glm::vec3> vertices;

  std::vector<Face> faces;

  std::vector<uint32_t> face_colors;

  std::vector<glm::vec3> face_normals;

  std::vector<glm::vec3> face_centers;

  std::vector<glm::vec3> vertex_normals;

};

static Mesh g_mesh;

static const char *g_mesh_path = "models/hw2_test.obj";

static bool g_mesh_loaded = false;

static std::string g_mesh_error;

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

// A degenerate triangle or cancelled sum has no defined unit normal.

static glm::vec3 safe_unit_vector(const glm::vec3 &v) {

  const float length = glm::length(v);

  if (!std::isfinite(length) || length <= 0.0f)

    return glm::vec3(0.0f);

  return v / length;

}

static void calculate_normals(Mesh &mesh) {

  mesh.face_normals.assign(mesh.faces.size(), glm::vec3(0.0f));

  mesh.face_centers.resize(mesh.faces.size());

  mesh.vertex_normals.assign(mesh.vertices.size(), glm::vec3(0.0f));

  for (size_t i = 0; i < mesh.faces.size(); ++i) {

    const Face &face = mesh.faces[i];

    const glm::vec3 &a = mesh.vertices[face.indices[0]];

    const glm::vec3 &b = mesh.vertices[face.indices[1]];

    const glm::vec3 &c = mesh.vertices[face.indices[2]];

    const glm::vec3 normal = safe_unit_vector(glm::cross(b - a, c - a));

    mesh.face_normals[i] = normal;

    mesh.face_centers[i] = (a + b + c) / 3.0f;

    // Equal weight for each adjacent triangle. Normalizing the sum gives

    // the same direction as normalizing the arithmetic average.

    for (size_t index : face.indices)

      mesh.vertex_normals[index] += normal;

  }

  for (glm::vec3 &normal : mesh.vertex_normals)

    normal = safe_unit_vector(normal);

}

static void reload_mesh() {

  g_mesh_loaded = load_obj(g_mesh_path, g_mesh, g_mesh_error);

  g_fit = ViewportFit{};

  if (g_mesh_loaded) {

    printf("HW2 Part 1: loaded %s\n", g_mesh_path);

    printf("Vertices: %zu\n", g_mesh.vertices.size());

    printf("Faces: %zu\n", g_mesh.faces.size());

    calculate_viewport_fit(g_mesh);

    calculate_normals(g_mesh);

    // Assign once per load, not once per frame: colors never flicker.
    // Fixed seed makes screenshots reproducible across runs.
    std::mt19937 generator(42);
    std::uniform_int_distribution<int> channel(70, 240);
    g_mesh.face_colors.resize(g_mesh.faces.size());
    for (uint32_t &color : g_mesh.face_colors) {
      const int red = channel(generator);
      const int green = channel(generator);
      const int blue = channel(generator);
      color = MFB_RGB(red, green, blue);
    }

    printf("HW3 Part 4: calculated %zu face normals and %zu vertex normals\n",

           g_mesh.face_normals.size(), g_mesh.vertex_normals.size());

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

static float g_red_strength = 1.0f;

static float g_green_strength = 1.0f;

static float g_ring_scale = 1800.0f;

static int g_boost_blue = 1;

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

static void draw_pixel_to_buffer(int x, int y, uint32_t color) {

  for (int dy = -1; dy <= 1; dy++) {

    for (int dx = -1; dx <= 1; dx++) {

      int px = x + dx;

      int py = y + dy;

      if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT)

        continue;

      if (g_clip_mesh_pixels &&

          !point_inside_rect(px, py, VIEW_X, VIEW_Y,

                             VIEW_WIDTH, VIEW_HEIGHT))

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

// Camera looks along -Z. Use the actual scene viewport aspect ratio.

static glm::mat4 projection_matrix() {

  const float aspect = float(VIEW_WIDTH) / float(VIEW_HEIGHT);

  if (g_use_perspective)

    return glm::perspectiveRH_NO(glm::radians(g_fov_degrees), aspect,

                                 NEAR_PLANE, FAR_PLANE);

  // Match the previous HW2 pixel scale; ortho size is independent of camera Z.

  const float half_height = float(VIEW_HEIGHT) / g_fit.scale;

  const float half_width = half_height * aspect;

  return glm::orthoRH_NO(-half_width, half_width, -half_height, half_height,

                         NEAR_PLANE, FAR_PLANE);

}

static bool finite_clip_point(const glm::vec4 &p) {

  return std::isfinite(p.x) && std::isfinite(p.y) &&

         std::isfinite(p.z) && std::isfinite(p.w);

}

// Signed distances to the six homogeneous frustum planes (inside >= 0).

static float clip_plane_distance(const glm::vec4 &p, int plane) {

  switch (plane) {

  case 0: return p.w + p.x;

  case 1: return p.w - p.x;

  case 2: return p.w + p.y;

  case 3: return p.w - p.y;

  case 4: return p.w + p.z;

  default: return p.w - p.z;

  }

}

// Clip BEFORE dividing by W: even an edge crossing the near plane is valid.

static bool clip_segment(glm::vec4 &a, glm::vec4 &b) {

  if (!finite_clip_point(a) || !finite_clip_point(b))

    return false;

  float enter = 0.0f, leave = 1.0f;

  for (int plane = 0; plane < 6; ++plane) {

    const float da = clip_plane_distance(a, plane);

    const float db = clip_plane_distance(b, plane);

    if (da < 0.0f && db < 0.0f)

      return false;

    if (da < 0.0f || db < 0.0f) {

      const float t = da / (da - db);

      if (da < 0.0f) enter = glm::max(enter, t);

      else leave = glm::min(leave, t);

      if (enter > leave) return false;

    }

  }

  const glm::vec4 original = a;

  const glm::vec4 delta = b - a;

  a = original + enter * delta;

  b = original + leave * delta;

  return a.w > 0.000001f && b.w > 0.000001f;

}

static glm::ivec2 clip_to_screen(const glm::vec4 &clip) {

  const glm::vec3 ndc = glm::vec3(clip) / clip.w; // Perspective divide.

  // Keep the previous assignments' convention: positive Y goes down.

  return glm::ivec2(

      int(std::lround(VIEW_X + (glm::clamp(ndc.x, -1.0f, 1.0f) + 1.0f) *

                              0.5f * VIEW_WIDTH)),

      int(std::lround(VIEW_Y + (glm::clamp(ndc.y, -1.0f, 1.0f) + 1.0f) *

                              0.5f * VIEW_HEIGHT)));

}

static bool world_to_screen(const glm::vec3 &point, glm::ivec2 &screen) {

  const glm::vec4 clip = g_projection_matrix * g_view_matrix * glm::vec4(point, 1);

  if (!finite_clip_point(clip) || clip.w <= 0.000001f)

    return false;

  for (int plane = 0; plane < 6; ++plane)

    if (clip_plane_distance(clip, plane) < 0.0f) return false;

  screen = clip_to_screen(clip);

  return true;

}

static void draw_world_segment(const glm::vec3 &a, const glm::vec3 &b,

                               uint32_t color) {

  // The endpoints already contain Model; this completes P * V * M * vertex.

  glm::vec4 ca = g_projection_matrix * g_view_matrix * glm::vec4(a, 1);

  glm::vec4 cb = g_projection_matrix * g_view_matrix * glm::vec4(b, 1);

  if (!clip_segment(ca, cb)) return;

  const glm::ivec2 pa = clip_to_screen(ca);

  const glm::ivec2 pb = clip_to_screen(cb);

  draw_line_bresenham(pa.x, pa.y, pb.x, pb.y, color);

}

// Clip the triangle as a polygon before dividing by W. A triangle crossing
// the near plane can still contribute a visible screen-space rectangle.
static std::vector<glm::vec4> clip_triangle_polygon(
    const glm::vec4 &a, const glm::vec4 &b, const glm::vec4 &c) {
  if (!finite_clip_point(a) || !finite_clip_point(b) || !finite_clip_point(c))
    return {};
  std::vector<glm::vec4> polygon{a, b, c};
  for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
    std::vector<glm::vec4> output;
    glm::vec4 previous = polygon.back();
    float previous_distance = clip_plane_distance(previous, plane);
    for (const glm::vec4 &current : polygon) {
      const float distance = clip_plane_distance(current, plane);
      const bool previous_inside = previous_distance >= 0.0f;
      const bool current_inside = distance >= 0.0f;
      if (previous_inside != current_inside) {
        const float t = previous_distance / (previous_distance - distance);
        output.push_back(previous + t * (current - previous));
      }
      if (current_inside) output.push_back(current);
      previous = current;
      previous_distance = distance;
    }
    polygon = std::move(output);
  }
  return polygon;
}

static void draw_triangle_bounding_rectangle(
    const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c,
    uint32_t color) {
  const glm::mat4 pv = g_projection_matrix * g_view_matrix;
  const auto polygon = clip_triangle_polygon(
      pv * glm::vec4(a, 1), pv * glm::vec4(b, 1), pv * glm::vec4(c, 1));
  if (polygon.size() < 3) return;

  glm::vec2 minimum{float(VIEW_X + VIEW_WIDTH), float(VIEW_Y + VIEW_HEIGHT)};
  glm::vec2 maximum{float(VIEW_X), float(VIEW_Y)};
  for (const glm::vec4 &clip : polygon) {
    if (!finite_clip_point(clip) || clip.w <= 0.000001f) return;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const glm::vec2 screen(
        VIEW_X + (ndc.x + 1.0f) * 0.5f * VIEW_WIDTH,
        VIEW_Y + (ndc.y + 1.0f) * 0.5f * VIEW_HEIGHT);
    minimum = glm::min(minimum, screen);
    maximum = glm::max(maximum, screen);
  }

  const int min_x = clamp_int(int(std::floor(minimum.x)),
                              VIEW_X, VIEW_X + VIEW_WIDTH - 1);
  const int max_x = clamp_int(int(std::ceil(maximum.x)),
                              VIEW_X, VIEW_X + VIEW_WIDTH - 1);
  const int min_y = clamp_int(int(std::floor(minimum.y)),
                              VIEW_Y, VIEW_Y + VIEW_HEIGHT - 1);
  const int max_y = clamp_int(int(std::ceil(maximum.y)),
                              VIEW_Y, VIEW_Y + VIEW_HEIGHT - 1);
  // Exact one-pixel writes; the line brush would expand the rectangle.
  // Later faces overwrite earlier ones. No triangle test or depth test yet.
  for (int y = min_y; y <= max_y; ++y)
    for (int x = min_x; x <= max_x; ++x)
      g_buffer[y * WIDTH + x] = color;
}

// Axes use the same matrix as their frame: identity for world, M for local.

static void draw_coordinate_axes(const glm::mat4 &frame, float length) {

  const glm::vec3 origin(frame * glm::vec4(0, 0, 0, 1));

  const glm::vec3 ends[] = {

      glm::vec3(length, 0, 0), glm::vec3(0, length, 0),

      glm::vec3(0, 0, length)};

  const uint32_t colors[] = {

      MFB_RGB(255, 70, 70), MFB_RGB(70, 255, 100), MFB_RGB(70, 140, 255)};

  for (int i = 0; i < 3; ++i) {

    const glm::vec3 end(frame * glm::vec4(ends[i], 1));

    draw_world_segment(origin, end, colors[i]);

  }

}

static void draw_mesh_wireframe() {

  if (!g_mesh_loaded)

    return;

  const glm::mat4 model = transformation_matrix(g_world_transform) *

                          transformation_matrix(g_local_transform);

  const auto transform_point = [&](const glm::vec3 &original) {

    return glm::vec3(model * glm::vec4(original - g_fit.center, 1));

  };

  g_view_matrix = camera_view_matrix(g_camera);

  g_projection_matrix = projection_matrix();

  g_clip_mesh_pixels = true;

  glm::ivec2 origin;

  if (world_to_screen(glm::vec3(0), origin)) {

    const uint32_t orange = MFB_RGB(255, 190, 60);

    draw_line_bresenham(origin.x - 7, origin.y, origin.x + 7, origin.y, orange);

    draw_line_bresenham(origin.x, origin.y - 7, origin.x, origin.y + 7, orange);

  }

  if (g_show_triangle_boxes) {
    for (size_t i = 0; i < g_mesh.faces.size(); ++i) {
      const Face &face = g_mesh.faces[i];
      draw_triangle_bounding_rectangle(
          transform_point(g_mesh.vertices[face.indices[0]]),
          transform_point(g_mesh.vertices[face.indices[1]]),
          transform_point(g_mesh.vertices[face.indices[2]]),
          g_mesh.face_colors[i]);
    }
  }

  if (g_show_wireframe && !g_show_triangle_boxes) {

    const uint32_t white = MFB_RGB(255, 255, 255);

    for (const Face &face : g_mesh.faces) {

      const glm::vec3 a = transform_point(g_mesh.vertices[face.indices[0]]);

      const glm::vec3 b = transform_point(g_mesh.vertices[face.indices[1]]);

      const glm::vec3 c = transform_point(g_mesh.vertices[face.indices[2]]);

      draw_world_segment(a, b, white);

      draw_world_segment(b, c, white);

      draw_world_segment(c, a, white);

    }

  }

  if (g_show_bounding_box) {

    // Every combination of min/max X, Y, Z gives one of the eight corners.

    std::array<glm::vec3, 8> corners;

    for (int i = 0; i < 8; ++i) {

      const glm::vec3 original(

          (i & 1) ? g_fit.maximum.x : g_fit.minimum.x,

          (i & 2) ? g_fit.maximum.y : g_fit.minimum.y,

          (i & 4) ? g_fit.maximum.z : g_fit.minimum.z);

      corners[i] = transform_point(original);

    }

    // Connect corners differing in exactly one bit: 12 unique edges.

    const uint32_t yellow = MFB_RGB(255, 220, 70);

    for (int i = 0; i < 8; ++i) {

      for (int bit = 1; bit <= 4; bit *= 2) {

        if ((i & bit) == 0)

          draw_world_segment(corners[i], corners[i | bit], yellow);

      }

    }

  }

  const glm::vec3 extent = g_fit.maximum - g_fit.minimum;

  const float axis_length = 0.4f * glm::max(extent.x,

                                          glm::max(extent.y, extent.z));

  if (g_show_world_axes)

    draw_coordinate_axes(glm::mat4(1.0f), axis_length);

  if (g_show_local_axes)

    draw_coordinate_axes(model, axis_length);

  if (g_show_face_normals || g_show_vertex_normals) {

    // Directions require inverse-transpose, especially for nonuniform scale.

    // Translation affects the base point only, never the normal direction.

    const glm::mat3 linear(model);

    const float determinant = glm::determinant(linear);

    if (std::isfinite(determinant) && std::fabs(determinant) > 1e-12f) {

      const glm::mat3 normal_matrix = glm::transpose(glm::inverse(linear));

      const float normal_length = 0.2f * glm::max(extent.x,

                                                 glm::max(extent.y, extent.z));

      const auto draw_normal = [&](const glm::vec3 &base,

                                   const glm::vec3 &normal, uint32_t color) {

        const glm::vec3 direction = safe_unit_vector(normal_matrix * normal);

        if (glm::dot(direction, direction) == 0.0f) return;

        const glm::vec3 world_base = transform_point(base);

        draw_world_segment(world_base, world_base + normal_length * direction,

                           color);

      };

      if (g_show_face_normals) {

        for (size_t i = 0; i < g_mesh.faces.size(); ++i)

          draw_normal(g_mesh.face_centers[i], g_mesh.face_normals[i],

                      MFB_RGB(0, 230, 255));

      }

      if (g_show_vertex_normals) {

        for (size_t i = 0; i < g_mesh.vertices.size(); ++i)

          draw_normal(g_mesh.vertices[i], g_mesh.vertex_normals[i],

                      MFB_RGB(255, 90, 220));

      }

    }

  }

  g_clip_mesh_pixels = false;

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

                                  mu_Rect rect, TransformState &state,

                                  bool is_local) {

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

    if (mu_button(ctx, is_local ? "Demo: orbit" : "Demo: spin"))

      set_comparison_demo(is_local);

    mu_end_window(ctx);

  }

}

static void camera_window(mu_Context *ctx) {

  const int options = MU_OPT_NORESIZE | MU_OPT_NOCLOSE | MU_OPT_NOTITLE;

  if (mu_begin_window_ex(ctx, "HW3 Camera", mu_rect(405, 420, 370, 260), options)) {

    int full_width[] = {-1};

    int axis_widths[] = {108, 108, -1};

    mu_layout_row(ctx, 1, full_width, 0);

    mu_label(ctx, "HW3 Camera (world pose)");

    mu_layout_row(ctx, 3, axis_widths, 0);

    mu_label(ctx, "X");

    mu_label(ctx, "Y");

    mu_label(ctx, "Z");

    transform_vector_controls(ctx, "Camera position (model units)",

                              g_camera.position, -10.0f, 10.0f);

    transform_vector_controls(ctx, "Camera rotation (degrees)",

                              g_camera.rotation, -180.0f, 180.0f);

    mu_layout_row(ctx, 1, full_width, 0);

    if (mu_button(ctx, "Reset camera"))

      g_camera = Camera{};

    mu_layout_row(ctx, 1, full_width, 0);

    if (mu_button(ctx, g_use_perspective ? "Projection: Perspective" :

                                         "Projection: Orthographic"))

      g_use_perspective = !g_use_perspective;

    int fov_widths[] = {110, -1};

    mu_layout_row(ctx, 2, fov_widths, 0);

    mu_label(ctx, "FOV (degrees)");

    mu_slider(ctx, &g_fov_degrees, 20.0f, 100.0f);

    mu_layout_row(ctx, 1, full_width, 0);

    mu_label(ctx, "Near: 0.1 / Far: 100");

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

  if (!ctx) {

    mfb_close(window);

    return 1;

  }

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

    // Handle scene keyboard input before passing input to MicroUI.

    handle_transform_keyboard(ctx, window);

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

      mu_checkbox(ctx, "Arrow controls", &g_enable_arrow_controls);

      mu_layout_row(ctx, 1, widths, 0);

      mu_checkbox(ctx, "Camera controls", &g_show_camera_controls);

      mu_layout_row(ctx, 1, widths, 0);

      if (mu_button(ctx, "Quit"))

        quit_requested = true;

      mu_end_window(ctx);

    }

    if (mu_begin_window(ctx, "HW3 Debug Geometry",

                        mu_rect(790, 20, 200, 300))) {

      int widths[] = {-1};

      if (g_mesh_loaded) {

        // Compact bounds leave room for all five debug toggles.

        const auto bounds_row = [&](const char *name, const glm::vec3 &value) {

          char text[96];

          snprintf(text, sizeof(text), "%s: %.1f %.1f %.1f",

                   name, value.x, value.y, value.z);

          mu_layout_row(ctx, 1, widths, 0);

          mu_text(ctx, text);

        };

        bounds_row("Min", g_fit.minimum);

        bounds_row("Max", g_fit.maximum);

        bounds_row("Center", g_fit.center);

        mu_layout_row(ctx, 1, widths, 0);

        mu_checkbox(ctx, "Triangle boxes", &g_show_triangle_boxes);
        mu_layout_row(ctx, 1, widths, 0);
        mu_checkbox(ctx, "Local axes", &g_show_local_axes);

        mu_layout_row(ctx, 1, widths, 0);

        mu_checkbox(ctx, "World axes", &g_show_world_axes);

        mu_layout_row(ctx, 1, widths, 0);

        mu_checkbox(ctx, "Bounding box", &g_show_bounding_box);

        mu_layout_row(ctx, 1, widths, 0);

        mu_checkbox(ctx, "Face normals", &g_show_face_normals);

        mu_layout_row(ctx, 1, widths, 0);

        mu_checkbox(ctx, "Vertex normals", &g_show_vertex_normals);

        mu_layout_row(ctx, 1, widths, 0);

        mu_text(ctx, "Face: cyan. Vertex: pink. Axes: RGB.");

    } else {

        mu_layout_row(ctx, 1, widths, 0);

        mu_label(ctx, "No mesh loaded.");

      }

      mu_end_window(ctx);

    }

    if (g_show_transforms && !g_show_hw1_tools) {

      transformation_window(ctx, "Local Transformations",

                            mu_rect(20, 420, 370, 260),

                            g_local_transform, true);

      // Share the lower-right panel; the checkbox selects its controls.

      if (g_show_camera_controls)

        camera_window(ctx);

      else

        transformation_window(ctx, "World Transformations",

                              mu_rect(405, 420, 370, 260),

                              g_world_transform, false);

    }

    mu_end(ctx);

    if (quit_requested)

      break;

    draw_mesh_wireframe();

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
