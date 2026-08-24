#include "MiniFB.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <string.h>
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>


extern "C" {
#include "microui.h"
}
#include "ui_bridge.h"
#include "ui_renderer.h"

#define WIDTH 1600
#define HEIGHT 1200

static uint32_t g_buffer[WIDTH * HEIGHT];
static float g_z_buffer[WIDTH * HEIGHT];

static int g_pattern_mode = 0;
static int g_color_shift = 0;
static float g_background_intensity = 1.0f;
static float g_ring_size = 900.0f;
static float g_blue_strength = 220.0f;

// Background mode for clearer model visibility
static int use_solid_background = 0;
static float solid_bg_r = 30.0f;
static float solid_bg_g = 30.0f;
static float solid_bg_b = 30.0f;

// Assignment 3 - Part 3 projection mode
// 0 = Orthographic / old viewport mode
// 1 = Perspective projection
static int use_perspective_projection = 0;

static float perspective_fov = 60.0f;
static float perspective_near = 0.1f;
static float perspective_far = 100.0f;


static int enable_interactive_lines = 0;
struct Line {
  int x0;
  int y0;
  int x1;
  int y1;
  uint32_t color;
};
struct Face {
  int v0;
  int v1;
  int v2;

  // Texture coordinate indices for the three corners.
  // Default to -1 so existing faces without UV data remain valid.
  int vt0 = -1;
  int vt1 = -1;
  int vt2 = -1;
};

struct Texture {
  int width = 0;
  int height = 0;
  std::vector<uint32_t> pixels;
  bool loaded = false;
  std::string path;

  void clear() {
    width = 0;
    height = 0;
    pixels.clear();
    loaded = false;
    path.clear();
  }

  bool has_texture() const {
    return loaded && width > 0 && height > 0;
  }

  uint32_t sample(float u, float v) const {
    if (!has_texture()) {
      return MFB_RGB(255, 0, 255);
    }

    u = std::fmod(u, 1.0f);
    if (u < 0.0f) {
      u += 1.0f;
    }

    v = std::fmod(v, 1.0f);
    if (v < 0.0f) {
      v += 1.0f;
    }

    int x = (int)(u * (width - 1) + 0.5f);
    int y = (int)((1.0f - v) * (height - 1) + 0.5f);

    x = std::max(0, std::min(x, width - 1));
    y = std::max(0, std::min(y, height - 1));

    return pixels[y * width + x];
  }
  uint32_t sample_bilinear(float u, float v) const {
    if (!has_texture()) {
      return MFB_RGB(255, 0, 255);
    }

    u = std::fmod(u, 1.0f);
    if (u < 0.0f) {
      u += 1.0f;
    }

    v = std::fmod(v, 1.0f);
    if (v < 0.0f) {
      v += 1.0f;
    }

    float x = u * (width - 1);
    float y = (1.0f - v) * (height - 1);

    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);

    int x1 = std::min(x0 + 1, width - 1);
    int y1 = std::min(y0 + 1, height - 1);

    float tx = x - x0;
    float ty = y - y0;

    uint32_t c00 = pixels[y0 * width + x0];
    uint32_t c10 = pixels[y0 * width + x1];
    uint32_t c01 = pixels[y1 * width + x0];
    uint32_t c11 = pixels[y1 * width + x1];

    float r00 = (float)((c00 >> 16) & 0xFF);
    float g00 = (float)((c00 >> 8) & 0xFF);
    float b00 = (float)(c00 & 0xFF);

    float r10 = (float)((c10 >> 16) & 0xFF);
    float g10 = (float)((c10 >> 8) & 0xFF);
    float b10 = (float)(c10 & 0xFF);

    float r01 = (float)((c01 >> 16) & 0xFF);
    float g01 = (float)((c01 >> 8) & 0xFF);
    float b01 = (float)(c01 & 0xFF);

    float r11 = (float)((c11 >> 16) & 0xFF);
    float g11 = (float)((c11 >> 8) & 0xFF);
    float b11 = (float)(c11 & 0xFF);

    float r_top = r00 * (1.0f - tx) + r10 * tx;
    float g_top = g00 * (1.0f - tx) + g10 * tx;
    float b_top = b00 * (1.0f - tx) + b10 * tx;

    float r_bottom = r01 * (1.0f - tx) + r11 * tx;
    float g_bottom = g01 * (1.0f - tx) + g11 * tx;
    float b_bottom = b01 * (1.0f - tx) + b11 * tx;

    int r = (int)(r_top * (1.0f - ty) + r_bottom * ty);
    int g = (int)(g_top * (1.0f - ty) + g_bottom * ty);
    int b = (int)(b_top * (1.0f - ty) + b_bottom * ty);

    return MFB_RGB(r, g, b);
  }
};

struct Mesh {
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec2> texture_coords;
  std::vector<Face> faces;
  Texture texture;

  // Assignment 3 - Part 4: Normals
  std::vector<glm::vec3> face_normals;
  std::vector<glm::vec3> vertex_normals;

  bool loaded = false;
  std::string path;
};

// Assignment 5 - Part 1: Light source and material properties
struct PointLight {
  glm::vec3 position;

  glm::vec3 ambient;
  glm::vec3 diffuse;
  glm::vec3 specular;
};

struct Material {
  glm::vec3 ambient;
  glm::vec3 diffuse;
  glm::vec3 specular;

  float shininess;
};

struct MeshBounds {
  glm::vec3 min;
  glm::vec3 max;
  glm::vec3 center;
  glm::vec3 size;
  glm::vec3 viewport_translation;
  float scale = 1.0f;
  bool valid = false;
};

static std::vector<Line> saved_lines;

static bool is_drawing = false;
static int line_start_x = 0;
static int line_start_y = 0;
static int current_mouse_x = 0;
static int current_mouse_y = 0;
static bool prev_left_down = false;

static float line_r = 255.0f;
static float line_g = 255.0f;
static float line_b = 255.0f;

// Part 4: Local transformation parameters
static float local_translate_x = 0.0f;
static float local_translate_y = 0.0f;
static float local_translate_z = 0.0f;

// Part 6: Keyboard input modes
// 1 = Move, 2 = Rotate, 3 = Scale
static int keyboard_operation_mode = 1;

// 1 = Local, 2 = World
static int keyboard_frame_mode = 2;

static float local_rotate_x = 0.0f;
static float local_rotate_y = 0.0f;
static float local_rotate_z = 0.0f;

static float local_scale_x = 1.0f;
static float local_scale_y = 1.0f;
static float local_scale_z = 1.0f;

// Part 4: World transformation parameters
static float world_translate_x = 0.0f;
static float world_translate_y = 0.0f;
static float world_translate_z = 0.0f;

static float world_rotate_x = 0.0f;
static float world_rotate_y = 0.0f;
static float world_rotate_z = 0.0f;

static float world_scale_x = 1.0f;
static float world_scale_y = 1.0f;
static float world_scale_z = 1.0f;

// Assignment 3 - Part 1 debug toggles
static int show_mesh_wireframe = 1;
static int show_local_axes = 1;
static int show_world_axes = 1;
static int show_bounding_box = 1;

// Assignment 4 - Part 1: Triangle bounding box rasterization debug
static int show_triangle_bounding_boxes = 0;

// Assignment 4 - Part 2: Filled triangle rasterization
static int show_filled_triangles = 0;

// Assignment 4 - Part 3: Z-buffer rasterization
static int show_zbuffered_triangles = 0;
static int show_z_buffer_depth_view = 0;

// Assignment 5 - Part 1: Ambient lighting
static int show_ambient_lighting = 0;

// Assignment 5 - Part 2: Flat shading with diffuse lighting
static int show_flat_shading = 0;

// Assignment 5 - Part 3: Specular highlights
static int show_specular_lighting = 0;
static int show_reflection_debug_vectors = 0;

// Assignment 5 - Part 4: Phong shading
static int show_phong_shading = 0;
static int show_texture_mapping = 0;

// Final Project - Texture filtering
// 0 = Nearest Neighbor
// 1 = Bilinear Filtering
static int texture_filter_mode = 0;

static PointLight g_light = {
  glm::vec3(0.0f, 0.0f, 3.0f),  // position
  glm::vec3(0.4f, 0.4f, 0.4f),  // ambient
  glm::vec3(0.8f, 0.8f, 0.8f),  // diffuse
  glm::vec3(1.0f, 1.0f, 1.0f)   // specular
};

static Material g_material = {
  glm::vec3(0.8f, 0.2f, 0.2f),  // ambient
  glm::vec3(0.8f, 0.2f, 0.2f),  // diffuse
  glm::vec3(1.0f, 1.0f, 1.0f),  // specular
  32.0f                         // shininess
};

// Assignment 3 - Part 4 normals debug toggles
static int show_face_normals = 0;
static int show_vertex_normals = 0;
static float normals_length = 0.2f;


// UI navigation state
static int ui_show_widgets = 0;
static int ui_show_mesh_info = 0;
static int ui_show_drawing_controls = 0;
static int ui_show_panel_demo = 0;
static int ui_show_part1_debug = 0;
static int ui_show_local_transform = 0;
static int ui_show_world_transform = 0;
static int ui_show_camera_position = 0;
static int ui_show_camera_rotation = 0;
static int ui_show_projection_mode = 0;
static int ui_show_texture_mapping = 0;
static int ui_show_part4_normals = 0;
static int ui_show_triangle_bbox_debug = 0;
static int ui_show_lighting_material = 0;

// Assignment 3 - Part 2 camera
struct Camera {
  glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
  glm::vec3 rotation = glm::vec3(0.0f, 0.0f, 0.0f);
};
static Camera camera;

void reset_all_transforms() {
  local_translate_x = 0.0f;
  local_translate_y = 0.0f;
  local_translate_z = 0.0f;

  local_rotate_x = 0.0f;
  local_rotate_y = 0.0f;
  local_rotate_z = 0.0f;

  local_scale_x = 1.0f;
  local_scale_y = 1.0f;
  local_scale_z = 1.0f;

  world_translate_x = 0.0f;
  world_translate_y = 0.0f;
  world_translate_z = 0.0f;

  world_rotate_x = 0.0f;
  world_rotate_y = 0.0f;
  world_rotate_z = 0.0f;

  world_scale_x = 1.0f;
  world_scale_y = 1.0f;
  world_scale_z = 1.0f;

  keyboard_operation_mode = 1; // Move
  keyboard_frame_mode = 2;     // World
}

void close_all_ui_windows() {
  ui_show_widgets = 0;
  ui_show_mesh_info = 0;
  ui_show_drawing_controls = 0;
  ui_show_panel_demo = 0;
  ui_show_part1_debug = 0;
  ui_show_local_transform = 0;
  ui_show_world_transform = 0;
  ui_show_camera_position = 0;
  ui_show_camera_rotation = 0;
  ui_show_projection_mode = 0;
  ui_show_part4_normals = 0;
  ui_show_triangle_bbox_debug = 0;
  ui_show_lighting_material = 0;
}


void put_pixel(int x, int y, uint32_t color) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
    return;
  }

  g_buffer[y * WIDTH + x] = color;
}

void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);

  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;

  int err = dx - dy;

  while (true) {
    put_pixel(x0, y0, color);

    if (x0 == x1 && y0 == y1) {
      break;
    }

    int e2 = 2 * err;

    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }

    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}
int parse_obj_index(const std::string& token) {
  if (token.empty()) {
    return -1;
  }

  // OBJ indices start from 1, but C++ vector indices start from 0.
  size_t slash_pos = token.find('/');
  std::string index_text = (slash_pos == std::string::npos)
                               ? token
                               : token.substr(0, slash_pos);

  if (index_text.empty()) {
    return -1;
  }

  return std::stoi(index_text) - 1;
}

int parse_obj_texcoord_index(const std::string& token) {
  if (token.empty()) {
    return -1;
  }

  size_t first_slash = token.find('/');
  if (first_slash == std::string::npos) {
    return -1;
  }

  size_t second_slash = token.find('/', first_slash + 1);
  std::string index_text;

  if (second_slash == std::string::npos) {
    index_text = token.substr(first_slash + 1);
  } else {
    index_text = token.substr(first_slash + 1,
                              second_slash - first_slash - 1);
  }

  if (index_text.empty()) {
    return -1;
  }

  return std::stoi(index_text) - 1;
}

static glm::vec2 get_face_uv(const Mesh& mesh, const Face& face, int vertex_index) {
  int tex_index = -1;
  if (vertex_index == 0) {
    tex_index = face.vt0;
  } else if (vertex_index == 1) {
    tex_index = face.vt1;
  } else if (vertex_index == 2) {
    tex_index = face.vt2;
  }

  if (tex_index < 0 || tex_index >= (int)mesh.texture_coords.size()) {
    return glm::vec2(0.0f, 0.0f);
  }

  return mesh.texture_coords[tex_index];
}

static uint32_t sample_face_texture(const Mesh& mesh,
                                    const Face& face,
                                    float alpha,
                                    float beta,
                                    float gamma) {
  if (!mesh.texture.has_texture()) {
    return MFB_RGB(255, 0, 255);
  }

  if (face.vt0 < 0 || face.vt1 < 0 || face.vt2 < 0) {
    return MFB_RGB(255, 0, 255);
  }

  glm::vec2 uv0 = get_face_uv(mesh, face, 0);
  glm::vec2 uv1 = get_face_uv(mesh, face, 1);
  glm::vec2 uv2 = get_face_uv(mesh, face, 2);

  glm::vec2 uv = uv0 * alpha + uv1 * beta + uv2 * gamma;

  if (texture_filter_mode == 1) {
    return mesh.texture.sample_bilinear(uv.x, uv.y);
  }

  return mesh.texture.sample(uv.x, uv.y);
}

bool load_obj(const std::string& path, Mesh& mesh) {
  std::ifstream file(path);

  if (!file.is_open()) {
    printf("Failed to open OBJ file: %s\n", path.c_str());
    return false;
  }

  mesh.vertices.clear();
  mesh.texture_coords.clear();
  mesh.texture.clear();
  mesh.faces.clear();
  mesh.loaded = false;
  mesh.path = path;

  std::string line;

  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::stringstream ss(line);
    std::string type;
    ss >> type;

    if (type == "v") {
      float x, y, z;
      ss >> x >> y >> z;
      mesh.vertices.push_back(glm::vec3(x, y, z));
    }
    else if (type == "vt") {
      float u, v;
      ss >> u >> v;
      mesh.texture_coords.push_back(glm::vec2(u, v));
    }
    else if (type == "f") {
      std::string a, b, c;
      ss >> a >> b >> c;

      Face face;
      face.v0 = parse_obj_index(a);
      face.v1 = parse_obj_index(b);
      face.v2 = parse_obj_index(c);
      face.vt0 = parse_obj_texcoord_index(a);
      face.vt1 = parse_obj_texcoord_index(b);
      face.vt2 = parse_obj_texcoord_index(c);

      mesh.faces.push_back(face);
    }
  }

  mesh.loaded = !mesh.vertices.empty() && !mesh.faces.empty();

  printf("OBJ loaded: %s\n", path.c_str());
  printf("Vertices: %zu\n", mesh.vertices.size());
  printf("Texture coordinates: %zu\n", mesh.texture_coords.size());
  printf("Faces: %zu\n", mesh.faces.size());

  return mesh.loaded;
}

static std::string to_lower(const std::string& value) {
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return result;
}

static std::string get_file_extension(const std::string& path) {
  size_t pos = path.find_last_of('.');
  if (pos == std::string::npos) {
    return std::string();
  }
  return to_lower(path.substr(pos + 1));
}

static bool load_bmp_texture(const std::string& path, Texture& texture) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  uint16_t signature = 0;
  file.read(reinterpret_cast<char*>(&signature), sizeof(signature));
  if (signature != 0x4D42) {
    return false;
  }

  uint32_t file_size = 0;
  uint32_t reserved = 0;
  uint32_t data_offset = 0;
  file.read(reinterpret_cast<char*>(&file_size), sizeof(file_size));
  file.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));
  file.read(reinterpret_cast<char*>(&data_offset), sizeof(data_offset));

  uint32_t dib_header_size = 0;
  file.read(reinterpret_cast<char*>(&dib_header_size), sizeof(dib_header_size));
  if (dib_header_size < 40) {
    return false;
  }

  int32_t width = 0;
  int32_t height = 0;
  uint16_t planes = 0;
  uint16_t bits_per_pixel = 0;
  uint32_t compression = 0;
  file.read(reinterpret_cast<char*>(&width), sizeof(width));
  file.read(reinterpret_cast<char*>(&height), sizeof(height));
  file.read(reinterpret_cast<char*>(&planes), sizeof(planes));
  file.read(reinterpret_cast<char*>(&bits_per_pixel), sizeof(bits_per_pixel));
  file.read(reinterpret_cast<char*>(&compression), sizeof(compression));

  if (planes != 1 || compression != 0) {
    return false;
  }

  if (bits_per_pixel != 24 && bits_per_pixel != 32) {
    return false;
  }

  int bytes_per_pixel = bits_per_pixel / 8;
  int row_size = ((width * bytes_per_pixel + 3) / 4) * 4;

  texture.width = width;
  texture.height = std::abs(height);
  texture.pixels.clear();
  texture.pixels.resize(texture.width * texture.height);
  texture.path = path;
  texture.loaded = false;

  file.seekg(data_offset, std::ios::beg);

  bool top_down = (height < 0);
  std::vector<uint8_t> row_data(row_size);

  for (int row = 0; row < texture.height; row++) {
    int target_row = top_down ? row : (texture.height - row - 1);
    file.read(reinterpret_cast<char*>(row_data.data()), row_size);
    if (!file) {
      texture.clear();
      return false;
    }

    for (int col = 0; col < texture.width; col++) {
      int pixel_index = target_row * texture.width + col;
      int offset = col * bytes_per_pixel;
      uint8_t b = row_data[offset + 0];
      uint8_t g = row_data[offset + 1];
      uint8_t r = row_data[offset + 2];
      texture.pixels[pixel_index] = MFB_RGB(r, g, b);
    }
  }

  texture.loaded = true;
  return true;
}

static bool load_texture(const std::string& path, Texture& texture) {
  std::string ext = get_file_extension(path);
  if (ext == "bmp") {
    if (load_bmp_texture(path, texture)) {
      printf("Texture loaded: %s (%dx%d)\n", path.c_str(), texture.width, texture.height);
      return true;
    }
    printf("Failed to load BMP texture: %s\n", path.c_str());
    return false;
  }

  printf("Unsupported texture format: %s\n", path.c_str());
  return false;
}

MeshBounds compute_mesh_bounds(const Mesh& mesh) {
  MeshBounds bounds;

  if (mesh.vertices.empty()) {
    return bounds;
  }

  bounds.min = mesh.vertices[0];
  bounds.max = mesh.vertices[0];

  for (const glm::vec3& v : mesh.vertices) {
    bounds.min.x = std::min(bounds.min.x, v.x);
    bounds.min.y = std::min(bounds.min.y, v.y);
    bounds.min.z = std::min(bounds.min.z, v.z);

    bounds.max.x = std::max(bounds.max.x, v.x);
    bounds.max.y = std::max(bounds.max.y, v.y);
    bounds.max.z = std::max(bounds.max.z, v.z);
  }

  bounds.size = bounds.max - bounds.min;
  bounds.center = (bounds.min + bounds.max) * 0.5f;

  float max_extent = std::max(bounds.size.x,
                     std::max(bounds.size.y, bounds.size.z));

  if (max_extent > 0.0f) {
    float screen_size = (float)std::min(WIDTH, HEIGHT);
    bounds.scale = (screen_size * 0.8f) / max_extent;
  }

  bounds.viewport_translation = glm::vec3(WIDTH * 0.5f, HEIGHT * 0.5f, 0.0f);
  bounds.valid = true;

  printf("Bounds min: %.2f, %.2f, %.2f\n",
         bounds.min.x, bounds.min.y, bounds.min.z);

  printf("Bounds max: %.2f, %.2f, %.2f\n",
         bounds.max.x, bounds.max.y, bounds.max.z);

  printf("Bounds center: %.2f, %.2f, %.2f\n",
         bounds.center.x, bounds.center.y, bounds.center.z);

  printf("Viewport scale: %.2f\n", bounds.scale);
  printf("Viewport translation: %.2f, %.2f, %.2f\n",
         bounds.viewport_translation.x,
         bounds.viewport_translation.y,
         bounds.viewport_translation.z);

  return bounds;
}

void compute_mesh_normals(Mesh& mesh) {
  mesh.face_normals.clear();
  mesh.vertex_normals.clear();

  mesh.face_normals.resize(mesh.faces.size(), glm::vec3(0.0f, 0.0f, 1.0f));
  mesh.vertex_normals.resize(mesh.vertices.size(), glm::vec3(0.0f));

  if (!mesh.loaded || mesh.vertices.empty() || mesh.faces.empty()) {
    return;
  }

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 v0 = mesh.vertices[face.v0];
    glm::vec3 v1 = mesh.vertices[face.v1];
    glm::vec3 v2 = mesh.vertices[face.v2];

    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;

    glm::vec3 face_normal = glm::cross(edge1, edge2);

    if (glm::length(face_normal) > 0.0001f) {
      face_normal = glm::normalize(face_normal);
    }
    else {
      face_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    mesh.face_normals[i] = face_normal;

    mesh.vertex_normals[face.v0] += face_normal;
    mesh.vertex_normals[face.v1] += face_normal;
    mesh.vertex_normals[face.v2] += face_normal;
  }

  for (int i = 0; i < (int)mesh.vertex_normals.size(); i++) {
    if (glm::length(mesh.vertex_normals[i]) > 0.0001f) {
      mesh.vertex_normals[i] = glm::normalize(mesh.vertex_normals[i]);
    }
    else {
      mesh.vertex_normals[i] = glm::vec3(0.0f, 0.0f, 1.0f);
    }
  }

  printf("Normals computed: %zu face normals, %zu vertex normals\n",
         mesh.face_normals.size(),
         mesh.vertex_normals.size());
}

glm::vec3 normalize_to_viewport(const glm::vec3& v, const MeshBounds& bounds) {
  glm::vec3 centered = v - bounds.center;
  glm::vec3 scaled = centered * bounds.scale;

  return glm::vec3(
      scaled.x + bounds.viewport_translation.x,
      bounds.viewport_translation.y - scaled.y,
      scaled.z
  );
}

glm::mat4 build_rotation_matrix(float x_deg, float y_deg, float z_deg) {
  glm::mat4 rotation(1.0f);

  rotation = glm::rotate(rotation, glm::radians(x_deg), glm::vec3(1.0f, 0.0f, 0.0f));
  rotation = glm::rotate(rotation, glm::radians(y_deg), glm::vec3(0.0f, 1.0f, 0.0f));
  rotation = glm::rotate(rotation, glm::radians(z_deg), glm::vec3(0.0f, 0.0f, 1.0f));

  return rotation;
}

glm::mat4 build_local_matrix() {
  glm::mat4 translation =
      glm::translate(glm::mat4(1.0f),
                     glm::vec3(local_translate_x,
                               local_translate_y,
                               local_translate_z));

  glm::mat4 rotation =
      build_rotation_matrix(local_rotate_x,
                            local_rotate_y,
                            local_rotate_z);

  glm::mat4 scale =
      glm::scale(glm::mat4(1.0f),
                 glm::vec3(local_scale_x,
                           local_scale_y,
                           local_scale_z));

  return translation * rotation * scale;
}

glm::mat4 build_world_matrix() {
  glm::mat4 translation =
      glm::translate(glm::mat4(1.0f),
                     glm::vec3(world_translate_x,
                               world_translate_y,
                               world_translate_z));

  glm::mat4 rotation =
      build_rotation_matrix(world_rotate_x,
                            world_rotate_y,
                            world_rotate_z);

  glm::mat4 scale =
      glm::scale(glm::mat4(1.0f),
                 glm::vec3(world_scale_x,
                           world_scale_y,
                           world_scale_z));

  return translation * rotation * scale;
}

glm::mat4 build_model_matrix() {
  glm::mat4 local_matrix = build_local_matrix();
  glm::mat4 world_matrix = build_world_matrix();

  return world_matrix * local_matrix;
}

glm::mat4 build_camera_world_matrix(const Camera& cam) {
  glm::mat4 translation =
      glm::translate(glm::mat4(1.0f), cam.position);

  glm::mat4 rotation =
      build_rotation_matrix(cam.rotation.x,
                            cam.rotation.y,
                            cam.rotation.z);

  return translation * rotation;
}

glm::mat4 build_view_matrix(const Camera& cam) {
  glm::mat4 camera_world_matrix = build_camera_world_matrix(cam);

  // View matrix is the inverse of the camera transform
  return glm::inverse(camera_world_matrix);
}

glm::mat4 build_perspective_projection_matrix() {
  float aspect_ratio = (float)WIDTH / (float)HEIGHT;

  return glm::perspective(glm::radians(perspective_fov),
                          aspect_ratio,
                          perspective_near,
                          perspective_far);
}

bool project_point_to_screen(const glm::vec3& point,
                             const glm::mat4& mvp_matrix,
                             glm::vec3& screen_point) {
  glm::vec4 clip = mvp_matrix * glm::vec4(point, 1.0f);

  // If W is too small or negative, the point is behind the camera.
  if (clip.w <= 0.0001f) {
    return false;
  }

  // Perspective Divide: clip coordinates -> NDC
  glm::vec3 ndc = glm::vec3(clip) / clip.w;

  // Clip points outside the visible depth range
  if (ndc.z < -1.0f || ndc.z > 1.0f) {
    return false;
  }

  // NDC [-1, 1] -> screen coordinates
  screen_point.x = (ndc.x * 0.5f + 0.5f) * WIDTH;
  screen_point.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * HEIGHT;
  screen_point.z = ndc.z;

  return true;
}

void draw_projected_3d_line(const glm::vec3& a,
                            const glm::vec3& b,
                            const glm::mat4& mvp_matrix,
                            uint32_t color) {
  glm::vec3 pa;
  glm::vec3 pb;

  bool visible_a = project_point_to_screen(a, mvp_matrix, pa);
  bool visible_b = project_point_to_screen(b, mvp_matrix, pb);

  if (!visible_a || !visible_b) {
    return;
  }

  draw_line((int)pa.x, (int)pa.y, (int)pb.x, (int)pb.y, color);
}

void draw_mesh_wireframe_projected(const Mesh& mesh,
                                   const glm::mat4& mvp_matrix,
                                   uint32_t color) {
  if (!mesh.loaded) {
    return;
  }

  for (const Face& face : mesh.faces) {
    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 v0 = mesh.vertices[face.v0];
    glm::vec3 v1 = mesh.vertices[face.v1];
    glm::vec3 v2 = mesh.vertices[face.v2];

    draw_projected_3d_line(v0, v1, mvp_matrix, color);
    draw_projected_3d_line(v1, v2, mvp_matrix, color);
    draw_projected_3d_line(v2, v0, mvp_matrix, color);
  }
}

void draw_local_axes_debug_projected(const MeshBounds& bounds,
                                     const glm::mat4& mvp_matrix) {
  if (!bounds.valid) {
    return;
  }

  float axis_len = std::max(bounds.size.x,
                   std::max(bounds.size.y, bounds.size.z)) * 0.6f;

  glm::vec3 origin = bounds.center;

  draw_projected_3d_line(origin,
                         origin + glm::vec3(axis_len, 0.0f, 0.0f),
                         mvp_matrix,
                         MFB_RGB(255, 0, 0));

  draw_projected_3d_line(origin,
                         origin + glm::vec3(0.0f, axis_len, 0.0f),
                         mvp_matrix,
                         MFB_RGB(0, 255, 0));

  draw_projected_3d_line(origin,
                         origin + glm::vec3(0.0f, 0.0f, axis_len),
                         mvp_matrix,
                         MFB_RGB(0, 0, 255));
}

void draw_world_axes_debug_projected(const MeshBounds& bounds,
                                     const glm::mat4& projection_view_matrix) {
  if (!bounds.valid) {
    return;
  }

  float axis_len = std::max(bounds.size.x,
                   std::max(bounds.size.y, bounds.size.z)) * 1.5f;

  glm::vec3 origin(0.0f, 0.0f, 0.0f);

  draw_projected_3d_line(origin,
                         origin + glm::vec3(axis_len, 0.0f, 0.0f),
                         projection_view_matrix,
                         MFB_RGB(255, 0, 0));

  draw_projected_3d_line(origin,
                         origin + glm::vec3(0.0f, axis_len, 0.0f),
                         projection_view_matrix,
                         MFB_RGB(0, 255, 0));

  draw_projected_3d_line(origin,
                         origin + glm::vec3(0.0f, 0.0f, axis_len),
                         projection_view_matrix,
                         MFB_RGB(0, 255, 255));
}

void draw_bounding_box_debug_projected(const MeshBounds& bounds,
                                       const glm::mat4& mvp_matrix) {
  if (!bounds.valid) {
    return;
  }

  glm::vec3 mn = bounds.min;
  glm::vec3 mx = bounds.max;

  glm::vec3 corners[8] = {
    glm::vec3(mn.x, mn.y, mn.z),
    glm::vec3(mx.x, mn.y, mn.z),
    glm::vec3(mx.x, mx.y, mn.z),
    glm::vec3(mn.x, mx.y, mn.z),

    glm::vec3(mn.x, mn.y, mx.z),
    glm::vec3(mx.x, mn.y, mx.z),
    glm::vec3(mx.x, mx.y, mx.z),
    glm::vec3(mn.x, mx.y, mx.z)
  };

  int edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
  };

  for (int i = 0; i < 12; i++) {
    draw_projected_3d_line(corners[edges[i][0]],
                           corners[edges[i][1]],
                           mvp_matrix,
                           MFB_RGB(255, 255, 0));
  }
}

void draw_mesh_wireframe(const Mesh& mesh,
                         const MeshBounds& bounds,
                         const glm::mat4& model_matrix,
                         uint32_t color) { 
    if (!mesh.loaded || !bounds.valid) {
    return;
  }

  for (const Face& face : mesh.faces) {
    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec4 transformed_v0 = model_matrix * glm::vec4(mesh.vertices[face.v0], 1.0f);
    glm::vec4 transformed_v1 = model_matrix * glm::vec4(mesh.vertices[face.v1], 1.0f);
    glm::vec4 transformed_v2 = model_matrix * glm::vec4(mesh.vertices[face.v2], 1.0f);

    glm::vec3 p0 = normalize_to_viewport(glm::vec3(transformed_v0), bounds);
    glm::vec3 p1 = normalize_to_viewport(glm::vec3(transformed_v1), bounds);
    glm::vec3 p2 = normalize_to_viewport(glm::vec3(transformed_v2), bounds);
    draw_line((int)p0.x, (int)p0.y, (int)p1.x, (int)p1.y, color);
    draw_line((int)p1.x, (int)p1.y, (int)p2.x, (int)p2.y, color);
    draw_line((int)p2.x, (int)p2.y, (int)p0.x, (int)p0.y, color);
  }
}
void draw_transformed_3d_line(const glm::vec3& a,
                              const glm::vec3& b,
                              const MeshBounds& bounds,
                              const glm::mat4& model_matrix,
                              uint32_t color) {
  glm::vec4 ta = model_matrix * glm::vec4(a, 1.0f);
  glm::vec4 tb = model_matrix * glm::vec4(b, 1.0f);

  glm::vec3 pa = normalize_to_viewport(glm::vec3(ta), bounds);
  glm::vec3 pb = normalize_to_viewport(glm::vec3(tb), bounds);

  draw_line((int)pa.x, (int)pa.y, (int)pb.x, (int)pb.y, color);
}

void draw_local_axes_debug(const MeshBounds& bounds,
                           const glm::mat4& model_matrix) {
  if (!bounds.valid) {
    return;
  }

  float axis_len = std::max(bounds.size.x,
                   std::max(bounds.size.y, bounds.size.z)) * 0.6f;

  glm::vec3 origin = bounds.center;

  // X axis - red
  draw_transformed_3d_line(origin,
                           origin + glm::vec3(axis_len, 0.0f, 0.0f),
                           bounds,
                           model_matrix,
                           MFB_RGB(255, 0, 0));

  // Y axis - green
  draw_transformed_3d_line(origin,
                           origin + glm::vec3(0.0f, axis_len, 0.0f),
                           bounds,
                           model_matrix,
                           MFB_RGB(0, 255, 0));

  // Z axis - blue
  draw_transformed_3d_line(origin,
                           origin + glm::vec3(0.0f, 0.0f, axis_len),
                           bounds,
                           model_matrix,
                           MFB_RGB(0, 0, 255));
}

void draw_world_axes_debug(const MeshBounds& bounds,
                           const glm::mat4& view_matrix) {
  if (!bounds.valid) {
    return;
  }

  float axis_len = std::max(bounds.size.x,
                   std::max(bounds.size.y, bounds.size.z)) * 1.5f;

  glm::vec3 origin(0.0f, 0.0f, 0.0f);

  // World X - red
  draw_transformed_3d_line(origin,
                           origin + glm::vec3(axis_len, 0.0f, 0.0f),
                           bounds,
                           view_matrix,
                           MFB_RGB(255, 0, 0));

  // World Y - green
  draw_transformed_3d_line(origin,
                           origin + glm::vec3(0.0f, axis_len, 0.0f),
                           bounds,
                           view_matrix,
                           MFB_RGB(0, 255, 0));

  // World Z - cyan
  draw_transformed_3d_line(origin,
                           origin + glm::vec3(0.0f, 0.0f, axis_len),
                           bounds,
                           view_matrix,
                           MFB_RGB(0, 255, 255));
}

void draw_bounding_box_debug(const MeshBounds& bounds,
                             const glm::mat4& model_matrix) {
  if (!bounds.valid) {
    return;
  }

  glm::vec3 mn = bounds.min;
  glm::vec3 mx = bounds.max;

  glm::vec3 corners[8] = {
    glm::vec3(mn.x, mn.y, mn.z),
    glm::vec3(mx.x, mn.y, mn.z),
    glm::vec3(mx.x, mx.y, mn.z),
    glm::vec3(mn.x, mx.y, mn.z),

    glm::vec3(mn.x, mn.y, mx.z),
    glm::vec3(mx.x, mn.y, mx.z),
    glm::vec3(mx.x, mx.y, mx.z),
    glm::vec3(mn.x, mx.y, mx.z)
  };

  int edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
  };

  for (int i = 0; i < 12; i++) {
    draw_transformed_3d_line(corners[edges[i][0]],
                             corners[edges[i][1]],
                             bounds,
                             model_matrix,
                             MFB_RGB(255, 255, 0));
  }
}
void draw_face_normals_projected(const Mesh& mesh,
                                 const MeshBounds& bounds,
                                 const glm::mat4& mvp_matrix,
                                 uint32_t color) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  if (mesh.face_normals.size() != mesh.faces.size()) {
    return;
  }

  float max_extent = std::max(bounds.size.x,
                     std::max(bounds.size.y, bounds.size.z));

  float normal_len = max_extent * normals_length;

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 v0 = mesh.vertices[face.v0];
    glm::vec3 v1 = mesh.vertices[face.v1];
    glm::vec3 v2 = mesh.vertices[face.v2];

    glm::vec3 center = (v0 + v1 + v2) / 3.0f;
    glm::vec3 end = center + mesh.face_normals[i] * normal_len;

    draw_projected_3d_line(center, end, mvp_matrix, color);
  }
}

void draw_vertex_normals_projected(const Mesh& mesh,
                                   const MeshBounds& bounds,
                                   const glm::mat4& mvp_matrix,
                                   uint32_t color) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  if (mesh.vertex_normals.size() != mesh.vertices.size()) {
    return;
  }

  float max_extent = std::max(bounds.size.x,
                     std::max(bounds.size.y, bounds.size.z));

  float normal_len = max_extent * normals_length;

  for (int i = 0; i < (int)mesh.vertices.size(); i++) {
    glm::vec3 start = mesh.vertices[i];
    glm::vec3 end = start + mesh.vertex_normals[i] * normal_len;

    draw_projected_3d_line(start, end, mvp_matrix, color);
  }
}

void draw_face_normals(const Mesh& mesh,
                       const MeshBounds& bounds,
                       const glm::mat4& model_matrix,
                       uint32_t color) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  if (mesh.face_normals.size() != mesh.faces.size()) {
    return;
  }

  float max_extent = std::max(bounds.size.x,
                     std::max(bounds.size.y, bounds.size.z));

  float normal_len = max_extent * normals_length;

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 v0 = mesh.vertices[face.v0];
    glm::vec3 v1 = mesh.vertices[face.v1];
    glm::vec3 v2 = mesh.vertices[face.v2];

    glm::vec3 center = (v0 + v1 + v2) / 3.0f;
    glm::vec3 end = center + mesh.face_normals[i] * normal_len;

    draw_transformed_3d_line(center, end, bounds, model_matrix, color);
  }
}

void draw_vertex_normals(const Mesh& mesh,
                         const MeshBounds& bounds,
                         const glm::mat4& model_matrix,
                         uint32_t color) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  if (mesh.vertex_normals.size() != mesh.vertices.size()) {
    return;
  }

  float max_extent = std::max(bounds.size.x,
                     std::max(bounds.size.y, bounds.size.z));

  float normal_len = max_extent * normals_length;

  for (int i = 0; i < (int)mesh.vertices.size(); i++) {
    glm::vec3 start = mesh.vertices[i];
    glm::vec3 end = start + mesh.vertex_normals[i] * normal_len;

    draw_transformed_3d_line(start, end, bounds, model_matrix, color);
  }
}

glm::vec3 calculate_ambient_lighting() {
  return g_light.ambient * g_material.ambient;
}

glm::vec3 calculate_flat_lighting(const glm::vec3& face_center,
                                  const glm::vec3& face_normal) {
  glm::vec3 ambient =
      g_light.ambient * g_material.ambient;

  glm::vec3 normal = face_normal;

  if (glm::length(normal) > 0.0001f) {
    normal = glm::normalize(normal);
  }
  else {
    normal = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  glm::vec3 light_direction =
      g_light.position - face_center;

  if (glm::length(light_direction) > 0.0001f) {
    light_direction = glm::normalize(light_direction);
  }
  else {
    light_direction = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  float diffuse_strength =
      glm::max(glm::dot(normal, light_direction), 0.0f);

  glm::vec3 diffuse =
      diffuse_strength * g_light.diffuse * g_material.diffuse;

  return ambient + diffuse;
}

glm::vec3 calculate_reflection_vector(const glm::vec3& light_direction,
                                      const glm::vec3& normal) {
  glm::vec3 n = normal;
  glm::vec3 l = light_direction;

  if (glm::length(n) > 0.0001f) {
    n = glm::normalize(n);
  }
  else {
    n = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  if (glm::length(l) > 0.0001f) {
    l = glm::normalize(l);
  }
  else {
    l = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  return glm::normalize(2.0f * glm::dot(n, l) * n - l);
}

glm::vec3 calculate_specular_lighting(const glm::vec3& face_center,
                                      const glm::vec3& face_normal,
                                      const glm::vec3& camera_position) {
  glm::vec3 ambient =
      g_light.ambient * g_material.ambient;

  glm::vec3 normal = face_normal;

  if (glm::length(normal) > 0.0001f) {
    normal = glm::normalize(normal);
  }
  else {
    normal = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  glm::vec3 view_direction =
      camera_position - face_center;

  if (glm::length(view_direction) > 0.0001f) {
    view_direction = glm::normalize(view_direction);
  }
  else {
    view_direction = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  // Make the face normal point toward the camera.
  // This makes the specular calculation visible also for simple OBJ models
  // whose face winding may point some normals inward.
  if (glm::dot(normal, view_direction) < 0.0f) {
    normal = -normal;
  }

  glm::vec3 light_direction =
      g_light.position - face_center;

  if (glm::length(light_direction) > 0.0001f) {
    light_direction = glm::normalize(light_direction);
  }
  else {
    light_direction = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  float diffuse_strength =
      glm::max(glm::dot(normal, light_direction), 0.0f);

  glm::vec3 diffuse =
      diffuse_strength * g_light.diffuse * g_material.diffuse;

  glm::vec3 reflection_direction =
      calculate_reflection_vector(light_direction, normal);

float reflection_view_alignment =
    glm::abs(glm::dot(reflection_direction, view_direction));

float specular_strength =
    glm::pow(reflection_view_alignment,
             g_material.shininess);

  glm::vec3 specular =
      specular_strength * g_light.specular * g_material.specular;

  return ambient + diffuse + specular;
}

glm::vec3 calculate_phong_lighting(const glm::vec3& pixel_position,
                                   const glm::vec3& pixel_normal,
                                   const glm::vec3& camera_position) {
  glm::vec3 ambient =
      g_light.ambient * g_material.ambient;

  glm::vec3 normal = pixel_normal;

  if (glm::length(normal) > 0.0001f) {
    normal = glm::normalize(normal);
  }
  else {
    normal = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  glm::vec3 view_direction =
      camera_position - pixel_position;

  if (glm::length(view_direction) > 0.0001f) {
    view_direction = glm::normalize(view_direction);
  }
  else {
    view_direction = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  // Same two-sided normal handling from Part 3.
  // It keeps the normal oriented toward the camera.
  if (glm::dot(normal, view_direction) < 0.0f) {
    normal = -normal;
  }

  glm::vec3 light_direction =
      g_light.position - pixel_position;

  if (glm::length(light_direction) > 0.0001f) {
    light_direction = glm::normalize(light_direction);
  }
  else {
    light_direction = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  float diffuse_strength =
      glm::max(glm::dot(normal, light_direction), 0.0f);

  glm::vec3 diffuse =
      diffuse_strength * g_light.diffuse * g_material.diffuse;

  glm::vec3 reflection_direction =
      calculate_reflection_vector(light_direction, normal);

  float reflection_view_alignment =
      glm::abs(glm::dot(reflection_direction, view_direction));

  float specular_strength =
      glm::pow(reflection_view_alignment,
               g_material.shininess);

  glm::vec3 specular =
      specular_strength * g_light.specular * g_material.specular;

  return ambient + diffuse + specular;
}


uint32_t vec3_to_color(const glm::vec3& color) {
  float r = glm::clamp(color.r, 0.0f, 1.0f);
  float g = glm::clamp(color.g, 0.0f, 1.0f);
  float b = glm::clamp(color.b, 0.0f, 1.0f);

  return MFB_RGB((int)(r * 255.0f),
                 (int)(g * 255.0f),
                 (int)(b * 255.0f));
}

uint32_t get_face_debug_color(int face_index) {
  int r = (face_index * 97 + 80) % 256;
  int g = (face_index * 57 + 130) % 256;
  int b = (face_index * 137 + 200) % 256;

  return MFB_RGB(r, g, b);
}

void draw_filled_screen_rect(int min_x,
                             int min_y,
                             int max_x,
                             int max_y,
                             uint32_t color) {
  if (max_x < 0 || max_y < 0 || min_x >= WIDTH || min_y >= HEIGHT) {
    return;
  }

  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= WIDTH) max_x = WIDTH - 1;
  if (max_y >= HEIGHT) max_y = HEIGHT - 1;

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      put_pixel(x, y, color);
    }
  }
}

void draw_triangle_bounding_boxes_projected(const Mesh& mesh,
                                            const glm::mat4& mvp_matrix) {
  if (!mesh.loaded) {
    return;
  }

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;

    bool ok0 = project_point_to_screen(mesh.vertices[face.v0], mvp_matrix, p0);
    bool ok1 = project_point_to_screen(mesh.vertices[face.v1], mvp_matrix, p1);
    bool ok2 = project_point_to_screen(mesh.vertices[face.v2], mvp_matrix, p2);

    if (!ok0 || !ok1 || !ok2) {
      continue;
    }

    float min_xf = std::min(p0.x, std::min(p1.x, p2.x));
    float max_xf = std::max(p0.x, std::max(p1.x, p2.x));

    float min_yf = std::min(p0.y, std::min(p1.y, p2.y));
    float max_yf = std::max(p0.y, std::max(p1.y, p2.y));

    int min_x = (int)std::floor(min_xf);
    int max_x = (int)std::ceil(max_xf);

    int min_y = (int)std::floor(min_yf);
    int max_y = (int)std::ceil(max_yf);

    uint32_t color = get_face_debug_color(i);

    draw_filled_screen_rect(min_x, min_y, max_x, max_y, color);
  }
}

void draw_triangle_bounding_boxes(const Mesh& mesh,
                                  const MeshBounds& bounds,
                                  const glm::mat4& model_matrix) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec4 tv0 = model_matrix * glm::vec4(mesh.vertices[face.v0], 1.0f);
    glm::vec4 tv1 = model_matrix * glm::vec4(mesh.vertices[face.v1], 1.0f);
    glm::vec4 tv2 = model_matrix * glm::vec4(mesh.vertices[face.v2], 1.0f);

    glm::vec3 p0 = normalize_to_viewport(glm::vec3(tv0), bounds);
    glm::vec3 p1 = normalize_to_viewport(glm::vec3(tv1), bounds);
    glm::vec3 p2 = normalize_to_viewport(glm::vec3(tv2), bounds);

    float min_xf = std::min(p0.x, std::min(p1.x, p2.x));
    float max_xf = std::max(p0.x, std::max(p1.x, p2.x));

    float min_yf = std::min(p0.y, std::min(p1.y, p2.y));
    float max_yf = std::max(p0.y, std::max(p1.y, p2.y));

    int min_x = (int)std::floor(min_xf);
    int max_x = (int)std::ceil(max_xf);

    int min_y = (int)std::floor(min_yf);
    int max_y = (int)std::ceil(max_yf);

    uint32_t color = get_face_debug_color(i);

    draw_filled_screen_rect(min_x, min_y, max_x, max_y, color);
  }
}

bool compute_barycentric_weights(float px,
                                 float py,
                                 const glm::vec3& p0,
                                 const glm::vec3& p1,
                                 const glm::vec3& p2,
                                 float& alpha,
                                 float& beta,
                                 float& gamma) {
  float denominator =
      ((p1.y - p2.y) * (p0.x - p2.x)) +
      ((p2.x - p1.x) * (p0.y - p2.y));

  if (std::fabs(denominator) < 0.00001f) {
    return false;
  }

  alpha =
      (((p1.y - p2.y) * (px - p2.x)) +
       ((p2.x - p1.x) * (py - p2.y))) /
      denominator;

  beta =
      (((p2.y - p0.y) * (px - p2.x)) +
       ((p0.x - p2.x) * (py - p2.y))) /
      denominator;

  gamma = 1.0f - alpha - beta;

  return true;
}

void draw_filled_triangle_screen(const glm::vec3& p0,
                                 const glm::vec3& p1,
                                 const glm::vec3& p2,
                                 uint32_t color) {
  float min_xf = std::min(p0.x, std::min(p1.x, p2.x));
  float max_xf = std::max(p0.x, std::max(p1.x, p2.x));

  float min_yf = std::min(p0.y, std::min(p1.y, p2.y));
  float max_yf = std::max(p0.y, std::max(p1.y, p2.y));

  int min_x = (int)std::floor(min_xf);
  int max_x = (int)std::ceil(max_xf);

  int min_y = (int)std::floor(min_yf);
  int max_y = (int)std::ceil(max_yf);

  if (max_x < 0 || max_y < 0 || min_x >= WIDTH || min_y >= HEIGHT) {
    return;
  }

  if (min_x < 0) {
    min_x = 0;
  }

  if (min_y < 0) {
    min_y = 0;
  }

  if (max_x >= WIDTH) {
    max_x = WIDTH - 1;
  }

  if (max_y >= HEIGHT) {
    max_y = HEIGHT - 1;
  }

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      float alpha;
      float beta;
      float gamma;

      bool valid =
          compute_barycentric_weights((float)x + 0.5f,
                                      (float)y + 0.5f,
                                      p0,
                                      p1,
                                      p2,
                                      alpha,
                                      beta,
                                      gamma);

      if (!valid) {
        continue;
      }

      if (alpha >= -0.0001f &&
          beta >= -0.0001f &&
          gamma >= -0.0001f) {
        put_pixel(x, y, color);
      }
    }
  }
}

void draw_filled_triangles_projected(const Mesh& mesh,
                                     const glm::mat4& mvp_matrix) {
  if (!mesh.loaded) {
    return;
  }

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;

    bool ok0 = project_point_to_screen(mesh.vertices[face.v0],
                                       mvp_matrix,
                                       p0);

    bool ok1 = project_point_to_screen(mesh.vertices[face.v1],
                                       mvp_matrix,
                                       p1);

    bool ok2 = project_point_to_screen(mesh.vertices[face.v2],
                                       mvp_matrix,
                                       p2);

    if (!ok0 || !ok1 || !ok2) {
      continue;
    }

    uint32_t color = get_face_debug_color(i);

    draw_filled_triangle_screen(p0, p1, p2, color);
  }
}

// Forward declaration for the Final Project textured rasterizer
void draw_textured_phong_triangle_screen_zbuffer(const Mesh& mesh,
                                                 const Face& face,
                                                 const glm::vec3& screen_p0,
                                                 const glm::vec3& screen_p1,
                                                 const glm::vec3& screen_p2,
                                                 const glm::vec3& world_p0,
                                                 const glm::vec3& world_p1,
                                                 const glm::vec3& world_p2,
                                                 const glm::vec3& normal_0,
                                                 const glm::vec3& normal_1,
                                                 const glm::vec3& normal_2,
                                                 const glm::vec3& camera_position);

void draw_filled_triangles(const Mesh& mesh,
                           const MeshBounds& bounds,
                           const glm::mat4& model_matrix) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec4 tv0 = model_matrix * glm::vec4(mesh.vertices[face.v0], 1.0f);
    glm::vec4 tv1 = model_matrix * glm::vec4(mesh.vertices[face.v1], 1.0f);
    glm::vec4 tv2 = model_matrix * glm::vec4(mesh.vertices[face.v2], 1.0f);

    glm::vec3 p0 = normalize_to_viewport(glm::vec3(tv0), bounds);
    glm::vec3 p1 = normalize_to_viewport(glm::vec3(tv1), bounds);
    glm::vec3 p2 = normalize_to_viewport(glm::vec3(tv2), bounds);

    // If texture mapping is enabled and the face has valid UVs, render textured
    if (show_texture_mapping && mesh.texture.has_texture() && face.vt0 >= 0 && face.vt1 >= 0 && face.vt2 >= 0) {
      // Compute world positions and normals for textured Phong rasterizer
      glm::vec3 world_v0 = glm::vec3(tv0);
      glm::vec3 world_v1 = glm::vec3(tv1);
      glm::vec3 world_v2 = glm::vec3(tv2);

      glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(model_matrix)));

      glm::vec3 normal_0 = normal_matrix * mesh.vertex_normals[face.v0];
      glm::vec3 normal_1 = normal_matrix * mesh.vertex_normals[face.v1];
      glm::vec3 normal_2 = normal_matrix * mesh.vertex_normals[face.v2];

      if (glm::length(normal_0) > 0.0001f) normal_0 = glm::normalize(normal_0);
      else normal_0 = glm::vec3(0.0f, 0.0f, 1.0f);

      if (glm::length(normal_1) > 0.0001f) normal_1 = glm::normalize(normal_1);
      else normal_1 = glm::vec3(0.0f, 0.0f, 1.0f);

      if (glm::length(normal_2) > 0.0001f) normal_2 = glm::normalize(normal_2);
      else normal_2 = glm::vec3(0.0f, 0.0f, 1.0f);

      draw_textured_phong_triangle_screen_zbuffer(mesh,
                                                  face,
                                                  p0,
                                                  p1,
                                                  p2,
                                                  world_v0,
                                                  world_v1,
                                                  world_v2,
                                                  normal_0,
                                                  normal_1,
                                                  normal_2,
                                                  camera.position);
    } else {
      uint32_t color = get_face_debug_color(i);
      draw_filled_triangle_screen(p0, p1, p2, color);
    }
  }
}

void clear_z_buffer() {
  for (int i = 0; i < WIDTH * HEIGHT; i++) {
    g_z_buffer[i] = 1000000000.0f;
  }
}

void draw_z_buffer_visualization() {
  float min_z = 1000000000.0f;
  float max_z = -1000000000.0f;

  for (int i = 0; i < WIDTH * HEIGHT; i++) {
    if (g_z_buffer[i] < 999999999.0f) {
      min_z = std::min(min_z, g_z_buffer[i]);
      max_z = std::max(max_z, g_z_buffer[i]);
    }
  }

  if (min_z > max_z) {
    return;
  }

  float range = max_z - min_z;

  if (std::fabs(range) < 0.00001f) {
    range = 1.0f;
  }

  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      int index = y * WIDTH + x;

      if (g_z_buffer[index] >= 999999999.0f) {
        g_buffer[index] = MFB_RGB(0, 0, 0);
        continue;
      }

      float t = (g_z_buffer[index] - min_z) / range;

      if (t < 0.0f) {
        t = 0.0f;
      }

      if (t > 1.0f) {
        t = 1.0f;
      }

      int gray = (int)(t * 255.0f);

      if (gray < 0) {
        gray = 0;
      }

      if (gray > 255) {
        gray = 255;
      }

      g_buffer[index] = MFB_RGB(gray, gray, gray);
    }
  }
}

void draw_filled_triangle_screen_zbuffer(const glm::vec3& p0,
                                         const glm::vec3& p1,
                                         const glm::vec3& p2,
                                         uint32_t color) {
  float min_xf = std::min(p0.x, std::min(p1.x, p2.x));
  float max_xf = std::max(p0.x, std::max(p1.x, p2.x));

  float min_yf = std::min(p0.y, std::min(p1.y, p2.y));
  float max_yf = std::max(p0.y, std::max(p1.y, p2.y));

  int min_x = (int)std::floor(min_xf);
  int max_x = (int)std::ceil(max_xf);

  int min_y = (int)std::floor(min_yf);
  int max_y = (int)std::ceil(max_yf);

  if (max_x < 0 || max_y < 0 || min_x >= WIDTH || min_y >= HEIGHT) {
    return;
  }

  if (min_x < 0) {
    min_x = 0;
  }

  if (min_y < 0) {
    min_y = 0;
  }

  if (max_x >= WIDTH) {
    max_x = WIDTH - 1;
  }

  if (max_y >= HEIGHT) {
    max_y = HEIGHT - 1;
  }

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      float alpha;
      float beta;
      float gamma;

      bool valid =
          compute_barycentric_weights((float)x + 0.5f,
                                      (float)y + 0.5f,
                                      p0,
                                      p1,
                                      p2,
                                      alpha,
                                      beta,
                                      gamma);

      if (!valid) {
        continue;
      }

      if (alpha >= -0.0001f &&
          beta >= -0.0001f &&
          gamma >= -0.0001f) {
        float z =
            alpha * p0.z +
            beta * p1.z +
            gamma * p2.z;

        int index = y * WIDTH + x;

        if (z < g_z_buffer[index]) {
          g_z_buffer[index] = z;
          put_pixel(x, y, color);
        }
      }
    }
  }
}

void draw_ambient_lit_triangles_projected(const Mesh& mesh,
                                          const glm::mat4& mvp_matrix) {
  if (!mesh.loaded) {
    return;
  }

  glm::vec3 ambient_color = calculate_ambient_lighting();
  uint32_t color = vec3_to_color(ambient_color);

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;

    bool ok0 = project_point_to_screen(mesh.vertices[face.v0],
                                       mvp_matrix,
                                       p0);

    bool ok1 = project_point_to_screen(mesh.vertices[face.v1],
                                       mvp_matrix,
                                       p1);

    bool ok2 = project_point_to_screen(mesh.vertices[face.v2],
                                       mvp_matrix,
                                       p2);

    if (!ok0 || !ok1 || !ok2) {
      continue;
    }

    draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
  }
}

void draw_ambient_lit_triangles(const Mesh& mesh,
                                const MeshBounds& bounds,
                                const glm::mat4& model_matrix) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  glm::vec3 ambient_color = calculate_ambient_lighting();
  uint32_t color = vec3_to_color(ambient_color);

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec4 tv0 = model_matrix * glm::vec4(mesh.vertices[face.v0], 1.0f);
    glm::vec4 tv1 = model_matrix * glm::vec4(mesh.vertices[face.v1], 1.0f);
    glm::vec4 tv2 = model_matrix * glm::vec4(mesh.vertices[face.v2], 1.0f);

    glm::vec3 p0 = normalize_to_viewport(glm::vec3(tv0), bounds);
    glm::vec3 p1 = normalize_to_viewport(glm::vec3(tv1), bounds);
    glm::vec3 p2 = normalize_to_viewport(glm::vec3(tv2), bounds);

    draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
  }
}

void draw_flat_shaded_triangles_projected(const Mesh& mesh,
                                          const glm::mat4& mvp_matrix,
                                          const glm::mat4& lighting_matrix) {
  if (!mesh.loaded) {
    return;
  }

  if (mesh.face_normals.size() != mesh.faces.size()) {
    return;
  }

  glm::mat3 normal_matrix =
      glm::transpose(glm::inverse(glm::mat3(lighting_matrix)));

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 local_v0 = mesh.vertices[face.v0];
    glm::vec3 local_v1 = mesh.vertices[face.v1];
    glm::vec3 local_v2 = mesh.vertices[face.v2];

    glm::vec3 local_center =
        (local_v0 + local_v1 + local_v2) / 3.0f;

    glm::vec3 world_center =
        glm::vec3(lighting_matrix * glm::vec4(local_center, 1.0f));

    glm::vec3 world_normal =
        normal_matrix * mesh.face_normals[i];

    if (glm::length(world_normal) > 0.0001f) {
      world_normal = glm::normalize(world_normal);
    }
    else {
      world_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 final_color =
        calculate_flat_lighting(world_center, world_normal);

    uint32_t color =
        vec3_to_color(final_color);

    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;

    bool ok0 = project_point_to_screen(local_v0,
                                       mvp_matrix,
                                       p0);

    bool ok1 = project_point_to_screen(local_v1,
                                       mvp_matrix,
                                       p1);

    bool ok2 = project_point_to_screen(local_v2,
                                       mvp_matrix,
                                       p2);

    if (!ok0 || !ok1 || !ok2) {
      continue;
    }

    draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
  }
}

void draw_flat_shaded_triangles(const Mesh& mesh,
                                const MeshBounds& bounds,
                                const glm::mat4& screen_matrix,
                                const glm::mat4& lighting_matrix) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  if (mesh.face_normals.size() != mesh.faces.size()) {
    return;
  }

  glm::mat3 normal_matrix =
      glm::transpose(glm::inverse(glm::mat3(lighting_matrix)));

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 local_v0 = mesh.vertices[face.v0];
    glm::vec3 local_v1 = mesh.vertices[face.v1];
    glm::vec3 local_v2 = mesh.vertices[face.v2];

    glm::vec3 local_center =
        (local_v0 + local_v1 + local_v2) / 3.0f;

    glm::vec3 world_center =
        glm::vec3(lighting_matrix * glm::vec4(local_center, 1.0f));

    glm::vec3 world_normal =
        normal_matrix * mesh.face_normals[i];

    if (glm::length(world_normal) > 0.0001f) {
      world_normal = glm::normalize(world_normal);
    }
    else {
      world_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 final_color =
        calculate_flat_lighting(world_center, world_normal);

    uint32_t color =
        vec3_to_color(final_color);

    glm::vec4 tv0 = screen_matrix * glm::vec4(local_v0, 1.0f);
    glm::vec4 tv1 = screen_matrix * glm::vec4(local_v1, 1.0f);
    glm::vec4 tv2 = screen_matrix * glm::vec4(local_v2, 1.0f);

    glm::vec3 p0 = normalize_to_viewport(glm::vec3(tv0), bounds);
    glm::vec3 p1 = normalize_to_viewport(glm::vec3(tv1), bounds);
    glm::vec3 p2 = normalize_to_viewport(glm::vec3(tv2), bounds);

    draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
  }
}

void draw_specular_triangles_projected(const Mesh& mesh,
                                       const glm::mat4& mvp_matrix,
                                       const glm::mat4& lighting_matrix,
                                       const glm::vec3& camera_position) {
  if (!mesh.loaded) {
    return;
  }

  if (mesh.face_normals.size() != mesh.faces.size()) {
    return;
  }

  glm::mat3 normal_matrix =
      glm::transpose(glm::inverse(glm::mat3(lighting_matrix)));

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 local_v0 = mesh.vertices[face.v0];
    glm::vec3 local_v1 = mesh.vertices[face.v1];
    glm::vec3 local_v2 = mesh.vertices[face.v2];

    glm::vec3 local_center =
        (local_v0 + local_v1 + local_v2) / 3.0f;

    glm::vec3 world_center =
        glm::vec3(lighting_matrix * glm::vec4(local_center, 1.0f));

    glm::vec3 world_normal =
        normal_matrix * mesh.face_normals[i];

    if (glm::length(world_normal) > 0.0001f) {
      world_normal = glm::normalize(world_normal);
    }
    else {
      world_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 final_color =
        calculate_specular_lighting(world_center,
                                    world_normal,
                                    camera_position);

    uint32_t color =
        vec3_to_color(final_color);

    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;

    bool ok0 = project_point_to_screen(local_v0, mvp_matrix, p0);
    bool ok1 = project_point_to_screen(local_v1, mvp_matrix, p1);
    bool ok2 = project_point_to_screen(local_v2, mvp_matrix, p2);

    if (!ok0 || !ok1 || !ok2) {
      continue;
    }

    draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
  }
}

void draw_phong_triangle_screen_zbuffer(const glm::vec3& screen_p0,
                                        const glm::vec3& screen_p1,
                                        const glm::vec3& screen_p2,
                                        const glm::vec3& world_p0,
                                        const glm::vec3& world_p1,
                                        const glm::vec3& world_p2,
                                        const glm::vec3& normal_0,
                                        const glm::vec3& normal_1,
                                        const glm::vec3& normal_2,
                                        const glm::vec3& camera_position) {
  float min_xf =
      std::min(screen_p0.x,
      std::min(screen_p1.x, screen_p2.x));

  float max_xf =
      std::max(screen_p0.x,
      std::max(screen_p1.x, screen_p2.x));

  float min_yf =
      std::min(screen_p0.y,
      std::min(screen_p1.y, screen_p2.y));

  float max_yf =
      std::max(screen_p0.y,
      std::max(screen_p1.y, screen_p2.y));

  int min_x = (int)std::floor(min_xf);
  int max_x = (int)std::ceil(max_xf);

  int min_y = (int)std::floor(min_yf);
  int max_y = (int)std::ceil(max_yf);

  if (max_x < 0 || max_y < 0 || min_x >= WIDTH || min_y >= HEIGHT) {
    return;
  }

  if (min_x < 0) {
    min_x = 0;
  }

  if (min_y < 0) {
    min_y = 0;
  }

  if (max_x >= WIDTH) {
    max_x = WIDTH - 1;
  }

  if (max_y >= HEIGHT) {
    max_y = HEIGHT - 1;
  }

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      float alpha;
      float beta;
      float gamma;

      bool valid =
          compute_barycentric_weights((float)x + 0.5f,
                                      (float)y + 0.5f,
                                      screen_p0,
                                      screen_p1,
                                      screen_p2,
                                      alpha,
                                      beta,
                                      gamma);

      if (!valid) {
        continue;
      }

      if (alpha >= -0.0001f &&
          beta >= -0.0001f &&
          gamma >= -0.0001f) {
        float z =
            alpha * screen_p0.z +
            beta * screen_p1.z +
            gamma * screen_p2.z;

        int index = y * WIDTH + x;

        if (z < g_z_buffer[index]) {
          g_z_buffer[index] = z;

          glm::vec3 pixel_position =
              alpha * world_p0 +
              beta * world_p1 +
              gamma * world_p2;

          glm::vec3 pixel_normal =
              alpha * normal_0 +
              beta * normal_1 +
              gamma * normal_2;

          if (glm::length(pixel_normal) > 0.0001f) {
            pixel_normal = glm::normalize(pixel_normal);
          }
          else {
            pixel_normal = glm::vec3(0.0f, 0.0f, 1.0f);
          }

          glm::vec3 final_color =
              calculate_phong_lighting(pixel_position,
                                       pixel_normal,
                                       camera_position);

          put_pixel(x, y, vec3_to_color(final_color));
        }
      }
    }
  }
}

void draw_textured_phong_triangle_screen_zbuffer(const Mesh& mesh,
                                                 const Face& face,
                                                 const glm::vec3& screen_p0,
                                                 const glm::vec3& screen_p1,
                                                 const glm::vec3& screen_p2,
                                                 const glm::vec3& world_p0,
                                                 const glm::vec3& world_p1,
                                                 const glm::vec3& world_p2,
                                                 const glm::vec3& normal_0,
                                                 const glm::vec3& normal_1,
                                                 const glm::vec3& normal_2,
                                                 const glm::vec3& camera_position) {
  float min_xf =
      std::min(screen_p0.x,
      std::min(screen_p1.x, screen_p2.x));

  float max_xf =
      std::max(screen_p0.x,
      std::max(screen_p1.x, screen_p2.x));

  float min_yf =
      std::min(screen_p0.y,
      std::min(screen_p1.y, screen_p2.y));

  float max_yf =
      std::max(screen_p0.y,
      std::max(screen_p1.y, screen_p2.y));

  int min_x = (int)std::floor(min_xf);
  int max_x = (int)std::ceil(max_xf);

  int min_y = (int)std::floor(min_yf);
  int max_y = (int)std::ceil(max_yf);

  if (max_x < 0 || max_y < 0 || min_x >= WIDTH || min_y >= HEIGHT) {
    return;
  }

  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= WIDTH) max_x = WIDTH - 1;
  if (max_y >= HEIGHT) max_y = HEIGHT - 1;

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      float alpha;
      float beta;
      float gamma;

      bool valid =
          compute_barycentric_weights((float)x + 0.5f,
                                      (float)y + 0.5f,
                                      screen_p0,
                                      screen_p1,
                                      screen_p2,
                                      alpha,
                                      beta,
                                      gamma);

      if (!valid) continue;

      if (alpha >= -0.0001f && beta >= -0.0001f && gamma >= -0.0001f) {
        float z = alpha * screen_p0.z + beta * screen_p1.z + gamma * screen_p2.z;

        int index = y * WIDTH + x;

        if (z < g_z_buffer[index]) {
          g_z_buffer[index] = z;

          glm::vec3 pixel_position = alpha * world_p0 + beta * world_p1 + gamma * world_p2;

          glm::vec3 pixel_normal = alpha * normal_0 + beta * normal_1 + gamma * normal_2;

          if (glm::length(pixel_normal) > 0.0001f) pixel_normal = glm::normalize(pixel_normal);
          else pixel_normal = glm::vec3(0.0f, 0.0f, 1.0f);

          glm::vec3 phong = calculate_phong_lighting(pixel_position, pixel_normal, camera_position);

          uint32_t tex_color_u = sample_face_texture(mesh, face, alpha, beta, gamma);

          int tr = (tex_color_u >> 16) & 0xFF;
          int tg = (tex_color_u >> 8) & 0xFF;
          int tb = (tex_color_u) & 0xFF;

          glm::vec3 tex_col = glm::vec3(tr / 255.0f, tg / 255.0f, tb / 255.0f);

          glm::vec3 final_color = tex_col * phong;

          put_pixel(x, y, vec3_to_color(final_color));
        }
      }
    }
  }
}


void draw_phong_shaded_triangles_projected(const Mesh& mesh,
                                           const glm::mat4& mvp_matrix,
                                           const glm::mat4& lighting_matrix,
                                           const glm::vec3& camera_position) {
  if (!mesh.loaded) {
    return;
  }

  if (mesh.vertex_normals.size() != mesh.vertices.size()) {
    return;
  }

  glm::mat3 normal_matrix =
      glm::transpose(glm::inverse(glm::mat3(lighting_matrix)));

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 local_v0 = mesh.vertices[face.v0];
    glm::vec3 local_v1 = mesh.vertices[face.v1];
    glm::vec3 local_v2 = mesh.vertices[face.v2];

    glm::vec3 world_v0 =
        glm::vec3(lighting_matrix * glm::vec4(local_v0, 1.0f));

    glm::vec3 world_v1 =
        glm::vec3(lighting_matrix * glm::vec4(local_v1, 1.0f));

    glm::vec3 world_v2 =
        glm::vec3(lighting_matrix * glm::vec4(local_v2, 1.0f));

    glm::vec3 normal_0 =
        normal_matrix * mesh.vertex_normals[face.v0];

    glm::vec3 normal_1 =
        normal_matrix * mesh.vertex_normals[face.v1];

    glm::vec3 normal_2 =
        normal_matrix * mesh.vertex_normals[face.v2];

    if (glm::length(normal_0) > 0.0001f) {
      normal_0 = glm::normalize(normal_0);
    }

    if (glm::length(normal_1) > 0.0001f) {
      normal_1 = glm::normalize(normal_1);
    }

    if (glm::length(normal_2) > 0.0001f) {
      normal_2 = glm::normalize(normal_2);
    }

    glm::vec3 screen_p0;
    glm::vec3 screen_p1;
    glm::vec3 screen_p2;

    bool ok0 =
        project_point_to_screen(local_v0, mvp_matrix, screen_p0);

    bool ok1 =
        project_point_to_screen(local_v1, mvp_matrix, screen_p1);

    bool ok2 =
        project_point_to_screen(local_v2, mvp_matrix, screen_p2);

    if (!ok0 || !ok1 || !ok2) {
      continue;
    }

    if (show_texture_mapping && mesh.texture.has_texture() && face.vt0 >= 0 && face.vt1 >= 0 && face.vt2 >= 0) {
      draw_textured_phong_triangle_screen_zbuffer(mesh,
                                                  face,
                                                  screen_p0,
                                                  screen_p1,
                                                  screen_p2,
                                                  world_v0,
                                                  world_v1,
                                                  world_v2,
                                                  normal_0,
                                                  normal_1,
                                                  normal_2,
                                                  camera_position);
    } else {
      draw_phong_triangle_screen_zbuffer(screen_p0,
                                         screen_p1,
                                         screen_p2,
                                         world_v0,
                                         world_v1,
                                         world_v2,
                                         normal_0,
                                         normal_1,
                                         normal_2,
                                         camera_position);
    }
  }
}

void draw_specular_triangles(const Mesh& mesh,
                             const MeshBounds& bounds,
                             const glm::mat4& screen_matrix,
                             const glm::mat4& lighting_matrix,
                             const glm::vec3& camera_position) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  if (mesh.face_normals.size() != mesh.faces.size()) {
    return;
  }

  glm::mat3 normal_matrix =
      glm::transpose(glm::inverse(glm::mat3(lighting_matrix)));

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 local_v0 = mesh.vertices[face.v0];
    glm::vec3 local_v1 = mesh.vertices[face.v1];
    glm::vec3 local_v2 = mesh.vertices[face.v2];

    glm::vec3 local_center =
        (local_v0 + local_v1 + local_v2) / 3.0f;

    glm::vec3 world_center =
        glm::vec3(lighting_matrix * glm::vec4(local_center, 1.0f));

    glm::vec3 world_normal =
        normal_matrix * mesh.face_normals[i];

    if (glm::length(world_normal) > 0.0001f) {
      world_normal = glm::normalize(world_normal);
    }
    else {
      world_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 final_color =
        calculate_specular_lighting(world_center,
                                    world_normal,
                                    camera_position);

    uint32_t color =
        vec3_to_color(final_color);

    glm::vec4 tv0 = screen_matrix * glm::vec4(local_v0, 1.0f);
    glm::vec4 tv1 = screen_matrix * glm::vec4(local_v1, 1.0f);
    glm::vec4 tv2 = screen_matrix * glm::vec4(local_v2, 1.0f);

    glm::vec3 p0 = normalize_to_viewport(glm::vec3(tv0), bounds);
    glm::vec3 p1 = normalize_to_viewport(glm::vec3(tv1), bounds);
    glm::vec3 p2 = normalize_to_viewport(glm::vec3(tv2), bounds);

    draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
  }
}

void draw_reflection_debug_vectors_projected(const Mesh& mesh,
                                             const MeshBounds& bounds,
                                             const glm::mat4& lighting_matrix,
                                             const glm::mat4& projection_view_matrix) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  if (mesh.face_normals.size() != mesh.faces.size()) {
    return;
  }

  glm::mat3 normal_matrix =
      glm::transpose(glm::inverse(glm::mat3(lighting_matrix)));

  float max_extent =
      std::max(bounds.size.x,
      std::max(bounds.size.y, bounds.size.z));

  float vector_length = max_extent * 0.35f;

  int debug_face_count = std::min((int)mesh.faces.size(), 5);

  for (int i = 0; i < debug_face_count; i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 local_v0 = mesh.vertices[face.v0];
    glm::vec3 local_v1 = mesh.vertices[face.v1];
    glm::vec3 local_v2 = mesh.vertices[face.v2];

    glm::vec3 local_center =
        (local_v0 + local_v1 + local_v2) / 3.0f;

    glm::vec3 world_center =
        glm::vec3(lighting_matrix * glm::vec4(local_center, 1.0f));

    glm::vec3 world_normal =
        normal_matrix * mesh.face_normals[i];

    if (glm::length(world_normal) > 0.0001f) {
      world_normal = glm::normalize(world_normal);
    }
    else {
      world_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 light_direction =
        g_light.position - world_center;

    if (glm::length(light_direction) > 0.0001f) {
      light_direction = glm::normalize(light_direction);
    }
    else {
      light_direction = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::vec3 reflection_direction =
        calculate_reflection_vector(light_direction, world_normal);

    glm::vec3 light_end =
        world_center + light_direction * vector_length;

    glm::vec3 reflection_end =
        world_center + reflection_direction * vector_length;

    // Yellow = incoming light direction
    draw_projected_3d_line(world_center,
                           light_end,
                           projection_view_matrix,
                           MFB_RGB(255, 255, 0));

    // Magenta = reflection direction
    draw_projected_3d_line(world_center,
                           reflection_end,
                           projection_view_matrix,
                           MFB_RGB(255, 0, 255));
  }
}

void draw_zbuffered_triangles_projected(const Mesh& mesh,
                                        const glm::mat4& mvp_matrix) {
  if (!mesh.loaded) {
    return;
  }

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;

    bool ok0 = project_point_to_screen(mesh.vertices[face.v0],
                                       mvp_matrix,
                                       p0);

    bool ok1 = project_point_to_screen(mesh.vertices[face.v1],
                                       mvp_matrix,
                                       p1);

    bool ok2 = project_point_to_screen(mesh.vertices[face.v2],
                                       mvp_matrix,
                                       p2);

    if (!ok0 || !ok1 || !ok2) {
      continue;
    }

    uint32_t color = get_face_debug_color(i);

    draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
  }
}

void draw_zbuffered_triangles(const Mesh& mesh,
                              const MeshBounds& bounds,
                              const glm::mat4& model_matrix) {
  if (!mesh.loaded || !bounds.valid) {
    return;
  }

  for (int i = 0; i < (int)mesh.faces.size(); i++) {
    const Face& face = mesh.faces[i];

    if (face.v0 < 0 || face.v1 < 0 || face.v2 < 0 ||
        face.v0 >= (int)mesh.vertices.size() ||
        face.v1 >= (int)mesh.vertices.size() ||
        face.v2 >= (int)mesh.vertices.size()) {
      continue;
    }

    glm::vec4 tv0 = model_matrix * glm::vec4(mesh.vertices[face.v0], 1.0f);
    glm::vec4 tv1 = model_matrix * glm::vec4(mesh.vertices[face.v1], 1.0f);
    glm::vec4 tv2 = model_matrix * glm::vec4(mesh.vertices[face.v2], 1.0f);

    glm::vec3 p0 = normalize_to_viewport(glm::vec3(tv0), bounds);
    glm::vec3 p1 = normalize_to_viewport(glm::vec3(tv1), bounds);
    glm::vec3 p2 = normalize_to_viewport(glm::vec3(tv2), bounds);

    // If texture mapping is enabled and the face has UVs, render textured Phong z-buffered triangle
    if (show_texture_mapping && mesh.texture.has_texture() && face.vt0 >= 0 && face.vt1 >= 0 && face.vt2 >= 0) {
      glm::vec3 world_v0 = glm::vec3(tv0);
      glm::vec3 world_v1 = glm::vec3(tv1);
      glm::vec3 world_v2 = glm::vec3(tv2);

      glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(model_matrix)));

      glm::vec3 normal_0 = normal_matrix * mesh.vertex_normals[face.v0];
      glm::vec3 normal_1 = normal_matrix * mesh.vertex_normals[face.v1];
      glm::vec3 normal_2 = normal_matrix * mesh.vertex_normals[face.v2];

      if (glm::length(normal_0) > 0.0001f) normal_0 = glm::normalize(normal_0);
      else normal_0 = glm::vec3(0.0f, 0.0f, 1.0f);

      if (glm::length(normal_1) > 0.0001f) normal_1 = glm::normalize(normal_1);
      else normal_1 = glm::vec3(0.0f, 0.0f, 1.0f);

      if (glm::length(normal_2) > 0.0001f) normal_2 = glm::normalize(normal_2);
      else normal_2 = glm::vec3(0.0f, 0.0f, 1.0f);

      draw_textured_phong_triangle_screen_zbuffer(mesh,
                                                  face,
                                                  p0,
                                                  p1,
                                                  p2,
                                                  world_v0,
                                                  world_v1,
                                                  world_v2,
                                                  normal_0,
                                                  normal_1,
                                                  normal_2,
                                                  camera.position);
    } else {
      uint32_t color = get_face_debug_color(i);
      draw_filled_triangle_screen_zbuffer(p0, p1, p2, color);
    }
  }
}

int main() {
  // Part 0: GLM test
  glm::vec3 p(1.0f, 2.0f, 3.0f);

  glm::mat4 model(1.0f);
  model = glm::translate(model, glm::vec3(10.0f, 0.0f, 0.0f));

  glm::vec4 transformed = model * glm::vec4(p, 1.0f);

  printf("GLM test: %.1f, %.1f, %.1f\n",
       transformed.x,
       transformed.y,
       transformed.z);

    Mesh mesh;

  std::vector<std::string> model_paths = {
    "models/simple.obj",
    "models/cube.obj",
  };

  std::vector<std::string> texture_paths = {
    "textures/checkerboard.bmp",
    "textures/cube.bmp",
  };

  std::vector<std::string> comparison_texture_paths = {
    "textures/checkerboard.bmp",
    "textures/filter_comparison.bmp",
    "textures/filtering_test_32.bmp",
  };

  int current_texture_index = 0;
  int current_model_index = 0;

  if (!load_obj(model_paths[current_model_index], mesh)) {
    printf("Failed to load model: %s\n", model_paths[current_model_index].c_str());
  }

  if (current_model_index < (int)texture_paths.size()) {
    const std::string& texture_path = texture_paths[current_model_index];
    if (!load_texture(texture_path, mesh.texture)) {
      printf("Texture not loaded for model %s, expected path: %s\n",
             model_paths[current_model_index].c_str(),
             texture_path.c_str());
    }
  }

  compute_mesh_normals(mesh);

   // Part 2: Compute bounding box and viewport transform
  MeshBounds bounds = compute_mesh_bounds(mesh);
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

      if (c == 'c' || c == 'C') {
        g_color_shift = (g_color_shift + 40) % 256;
        printf("Color shift changed: %d\n", g_color_shift);
        return;
      }

      if (c == 'p' || c == 'P') {
        g_pattern_mode = !g_pattern_mode;
        printf("Pattern mode changed: %d\n", g_pattern_mode);
        return;
      } 
      if (c == 'r' || c == 'R') {
        reset_all_transforms();
        printf("Keyboard: Reset all transforms\n");
        return;
      }
            // Part 6: Keyboard mode selection
      if (c == '1') {
        keyboard_operation_mode = 1;
        printf("Keyboard operation mode: Move\n");
        return;
      }

      if (c == '2') {
        keyboard_operation_mode = 2;
        printf("Keyboard operation mode: Rotate\n");
        return;
      }

      if (c == '3') {
        keyboard_operation_mode = 3;
        printf("Keyboard operation mode: Scale\n");
        return;
      }

      if (c == '4') {
        keyboard_frame_mode = 1;
        printf("Keyboard frame mode: Local\n");
        return;
      }

      if (c == '5') {
        keyboard_frame_mode = 2;
        printf("Keyboard frame mode: World\n");
        return;
      }

      // Part 6: Keyboard transformation steps
      float translate_step = 0.1f;
      float rotate_step = 5.0f;
      float scale_step = 0.05f;

      // Part 6: Move mode
      if (keyboard_operation_mode == 1) {
        if (keyboard_frame_mode == 1) {
          // Local translation
          if (c == 'j' || c == 'J') {
            local_translate_x -= translate_step;
            printf("Local Translate X = %.2f\n", local_translate_x);
            return;
          }

          if (c == 'l' || c == 'L') {
            local_translate_x += translate_step;
            printf("Local Translate X = %.2f\n", local_translate_x);
            return;
          }

          if (c == 'i' || c == 'I') {
            local_translate_y += translate_step;
            printf("Local Translate Y = %.2f\n", local_translate_y);
            return;
          }

          if (c == 'k' || c == 'K') {
            local_translate_y -= translate_step;
            printf("Local Translate Y = %.2f\n", local_translate_y);
            return;
          }

          if (c == 'u' || c == 'U') {
            local_translate_z -= translate_step;
            printf("Local Translate Z = %.2f\n", local_translate_z);
            return;
          }

          if (c == 'o' || c == 'O') {
            local_translate_z += translate_step;
            printf("Local Translate Z = %.2f\n", local_translate_z);
            return;
          }
        }
        else {
          // World translation
          if (c == 'j' || c == 'J') {
            world_translate_x -= translate_step;
            printf("World Translate X = %.2f\n", world_translate_x);
            return;
          }

          if (c == 'l' || c == 'L') {
            world_translate_x += translate_step;
            printf("World Translate X = %.2f\n", world_translate_x);
            return;
          }

          if (c == 'i' || c == 'I') {
            world_translate_y += translate_step;
            printf("World Translate Y = %.2f\n", world_translate_y);
            return;
          }

          if (c == 'k' || c == 'K') {
            world_translate_y -= translate_step;
            printf("World Translate Y = %.2f\n", world_translate_y);
            return;
          }

          if (c == 'u' || c == 'U') {
            world_translate_z -= translate_step;
            printf("World Translate Z = %.2f\n", world_translate_z);
            return;
          }

          if (c == 'o' || c == 'O') {
            world_translate_z += translate_step;
            printf("World Translate Z = %.2f\n", world_translate_z);
            return;
          }
        }
      }

      // Part 6: Rotate mode
      if (keyboard_operation_mode == 2) {
        if (keyboard_frame_mode == 1) {
          // Local rotation
          if (c == 'i' || c == 'I') {
            local_rotate_x += rotate_step;
            printf("Local Rotate X = %.2f\n", local_rotate_x);
            return;
          }

          if (c == 'k' || c == 'K') {
            local_rotate_x -= rotate_step;
            printf("Local Rotate X = %.2f\n", local_rotate_x);
            return;
          }

          if (c == 'j' || c == 'J') {
            local_rotate_y -= rotate_step;
            printf("Local Rotate Y = %.2f\n", local_rotate_y);
            return;
          }

          if (c == 'l' || c == 'L') {
            local_rotate_y += rotate_step;
            printf("Local Rotate Y = %.2f\n", local_rotate_y);
            return;
          }

          if (c == 'u' || c == 'U') {
            local_rotate_z -= rotate_step;
            printf("Local Rotate Z = %.2f\n", local_rotate_z);
            return;
          }

          if (c == 'o' || c == 'O') {
            local_rotate_z += rotate_step;
            printf("Local Rotate Z = %.2f\n", local_rotate_z);
            return;
          }
        }
        else {
          // World rotation
          if (c == 'i' || c == 'I') {
            world_rotate_x += rotate_step;
            printf("World Rotate X = %.2f\n", world_rotate_x);
            return;
          }

          if (c == 'k' || c == 'K') {
            world_rotate_x -= rotate_step;
            printf("World Rotate X = %.2f\n", world_rotate_x);
            return;
          }

          if (c == 'j' || c == 'J') {
            world_rotate_y -= rotate_step;
            printf("World Rotate Y = %.2f\n", world_rotate_y);
            return;
          }

          if (c == 'l' || c == 'L') {
            world_rotate_y += rotate_step;
            printf("World Rotate Y = %.2f\n", world_rotate_y);
            return;
          }

          if (c == 'u' || c == 'U') {
            world_rotate_z -= rotate_step;
            printf("World Rotate Z = %.2f\n", world_rotate_z);
            return;
          }

          if (c == 'o' || c == 'O') {
            world_rotate_z += rotate_step;
            printf("World Rotate Z = %.2f\n", world_rotate_z);
            return;
          }
        }
      }

      // Part 6: Scale mode
      if (keyboard_operation_mode == 3) {
        bool increase_scale =
            (c == 'i' || c == 'I' ||
             c == 'l' || c == 'L' ||
             c == 'o' || c == 'O');

        bool decrease_scale =
            (c == 'j' || c == 'J' ||
             c == 'k' || c == 'K' ||
             c == 'u' || c == 'U');

        if (increase_scale || decrease_scale) {
          float direction = increase_scale ? 1.0f : -1.0f;

          if (keyboard_frame_mode == 1) {
            local_scale_x += direction * scale_step;
            local_scale_y += direction * scale_step;
            local_scale_z += direction * scale_step;

            if (local_scale_x < 0.1f) local_scale_x = 0.1f;
            if (local_scale_y < 0.1f) local_scale_y = 0.1f;
            if (local_scale_z < 0.1f) local_scale_z = 0.1f;

            if (local_scale_x > 3.0f) local_scale_x = 3.0f;
            if (local_scale_y > 3.0f) local_scale_y = 3.0f;
            if (local_scale_z > 3.0f) local_scale_z = 3.0f;

            printf("Local Scale = %.2f\n", local_scale_x);
            return;
          }
          else {
            world_scale_x += direction * scale_step;
            world_scale_y += direction * scale_step;
            world_scale_z += direction * scale_step;

            if (world_scale_x < 0.1f) world_scale_x = 0.1f;
            if (world_scale_y < 0.1f) world_scale_y = 0.1f;
            if (world_scale_z < 0.1f) world_scale_z = 0.1f;

            if (world_scale_x > 3.0f) world_scale_x = 3.0f;
            if (world_scale_y > 3.0f) world_scale_y = 3.0f;
            if (world_scale_z > 3.0f) world_scale_z = 3.0f;

            printf("World Scale = %.2f\n", world_scale_x);
            return;
          }
        }
      }

      ui_bridge_char_input(w, c);
    },
    window);

  while (mfb_update_events(window) != MFB_STATE_EXIT) {
    // 1. Input
     ui_bridge_input(ctx, window);

    // Fix mouse position when the MiniFB window is resized.
    // The UI is drawn in WIDTH x HEIGHT coordinates, but the mouse
    // position is received in the current window size.
    int window_width = mfb_get_window_width(window);
    int window_height = mfb_get_window_height(window);

    if (window_width > 0 && window_height > 0) {
      ctx->mouse_pos.x =
          (int)((float)ctx->mouse_pos.x * (float)WIDTH / (float)window_width);

      ctx->mouse_pos.y =
          (int)((float)ctx->mouse_pos.y * (float)HEIGHT / (float)window_height);
    }

    int mx = ctx->mouse_pos.x;
    int my = ctx->mouse_pos.y;
    bool left_down = (ctx->mouse_down & MU_MOUSE_LEFT) != 0;

    uint32_t current_line_color =
       MFB_RGB((int)line_r, (int)line_g, (int)line_b);
    if (enable_interactive_lines) {
  // Start drawing when mouse is pressed
       if (left_down && !prev_left_down) {
          is_drawing = true;
          line_start_x = mx;
          line_start_y = my;
          current_mouse_x = mx;
          current_mouse_y = my;

          printf("Start line at: (%d, %d)\n", mx, my);
  }

  // Update preview while dragging
       if (is_drawing && left_down) {
          current_mouse_x = mx;
          current_mouse_y = my;
  }

  // Save the line when mouse is released
       if (!left_down && prev_left_down && is_drawing) {
         saved_lines.push_back({
         line_start_x,
         line_start_y,
         current_mouse_x,
         current_mouse_y,
         current_line_color
    });

        printf("Line saved: (%d,%d) -> (%d,%d)\n",
          line_start_x, line_start_y,
          current_mouse_x, current_mouse_y);

        is_drawing = false;
  }
}  
    else {
     is_drawing = false;
}

    prev_left_down = left_down;

    for (int i = 0; i < WIDTH * HEIGHT; i++) {
      int x = i % WIDTH;
      int y = i / WIDTH;
      if (use_solid_background) {
        g_buffer[i] = MFB_RGB((int)solid_bg_r,
                              (int)solid_bg_g,
                              (int)solid_bg_b);
        continue;
      }      

      // Center of the screen
      int cx = WIDTH / 2;
      int cy = HEIGHT / 2;

      // Distance from the center
      int dx = x - cx;
      int dy = y - cy;
      int dist2 = dx * dx + dy * dy;

      // Pattern changes when pressing P
      int rings;
      int checker;

      int ring_size_int = (int)g_ring_size;
      if (ring_size_int < 50) {
        ring_size_int = 50;
     }

      if (g_pattern_mode) {
        rings = ((x + y + g_color_shift) / 40) % 2;
        checker = ((x - y + g_color_shift) / 60) % 2;
     }
      else {
        rings = (dist2 / ring_size_int) % 2;
        checker = ((x / 40) + (y / 40)) % 2;
     }

      // Color changes when pressing C
      int r_val = (int)((((x * 255) / WIDTH + g_color_shift) % 256) * g_background_intensity);
      int g_val = (int)((((y * 255) / HEIGHT + g_color_shift) % 256) * g_background_intensity);
      int b_val = rings ? (int)g_blue_strength : (int)(g_blue_strength * 0.35f);

      if (r_val > 255) r_val = 255;
      if (g_val > 255) g_val = 255;
      if (b_val > 255) b_val = 255;

      uint8_t r = (uint8_t)r_val;
      uint8_t g = (uint8_t)g_val;
      uint8_t b = (uint8_t)b_val;

      // Invert colors on alternating squares
      if (checker) {
        r = 255 - r;
        g = 255 - g;
      }

      g_buffer[i] = MFB_RGB(r, g, b);
    }
    clear_z_buffer();
    clear_z_buffer();
    // Part 5: Build model transformation matrix from GUI values
    glm::mat4 model_matrix = build_model_matrix();

    // Assignment 3 - Part 2: Build View matrix from camera
    glm::mat4 view_matrix = build_view_matrix(camera);

    // If texture mapping is enabled, hide wireframe overlay so textures are visible
    if (show_texture_mapping) {
      show_mesh_wireframe = 0;
    }

    if (use_perspective_projection) {
      // Assignment 3 - Part 3: Perspective Projection
      glm::mat4 projection_matrix = build_perspective_projection_matrix();

      // Center the mesh around the origin before applying Model/View/Projection.
      // This makes the perspective projection easier to see.
      glm::mat4 center_matrix =
          glm::translate(glm::mat4(1.0f), -bounds.center);

      // Full pipeline: P * V * M * v
      glm::mat4 mvp_matrix =
          projection_matrix * view_matrix * model_matrix * center_matrix;

      glm::mat4 projection_view_matrix =
          projection_matrix * view_matrix;

      if (show_triangle_bounding_boxes) {
        draw_triangle_bounding_boxes_projected(mesh, mvp_matrix);
      }
      if (show_texture_mapping || show_phong_shading) {
        glm::mat4 lighting_matrix =
           model_matrix * center_matrix;

        draw_phong_shaded_triangles_projected(mesh,
                                         mvp_matrix,
                                         lighting_matrix,
                                         camera.position);
     }

      if (show_specular_lighting) {
        glm::mat4 lighting_matrix =
            model_matrix * center_matrix;

        draw_specular_triangles_projected(mesh,
                                          mvp_matrix,
                                          lighting_matrix,
                                          camera.position);
      }
     
      else if (show_flat_shading) {
        glm::mat4 lighting_matrix =
            model_matrix * center_matrix;

        draw_flat_shaded_triangles_projected(mesh,
                                             mvp_matrix,
                                             lighting_matrix);
      }
      else if (show_ambient_lighting) {
        draw_ambient_lit_triangles_projected(mesh, mvp_matrix);
      }
      else if (show_zbuffered_triangles) {
        draw_zbuffered_triangles_projected(mesh, mvp_matrix);
      }
      else if (show_filled_triangles) {
        draw_filled_triangles_projected(mesh, mvp_matrix);
      }

      if (show_reflection_debug_vectors) {
        glm::mat4 lighting_matrix =
            model_matrix * center_matrix;

        draw_reflection_debug_vectors_projected(mesh,
                                                bounds,
                                                lighting_matrix,
                                                projection_view_matrix);
      }

      if (show_mesh_wireframe) {
        draw_mesh_wireframe_projected(mesh,
                                      mvp_matrix,
                                      MFB_RGB(255, 255, 255));
      }

      if (show_bounding_box) {
        draw_bounding_box_debug_projected(bounds, mvp_matrix);
      }

      if (show_local_axes) {
        draw_local_axes_debug_projected(bounds, mvp_matrix);
      }

      if (show_world_axes) {
        draw_world_axes_debug_projected(bounds, projection_view_matrix);
      }

      if (show_face_normals) {
        draw_face_normals_projected(mesh,
                                    bounds,
                                    mvp_matrix,
                                    MFB_RGB(255, 128, 0));
      }

      if (show_vertex_normals) {
        draw_vertex_normals_projected(mesh,
                                      bounds,
                                      mvp_matrix,
                                      MFB_RGB(0, 255, 255));
      }
    }
    else {
      // Orthographic / old viewport mode: V * M * v
      glm::mat4 view_model_matrix = view_matrix * model_matrix;

      if (show_triangle_bounding_boxes) {
        draw_triangle_bounding_boxes(mesh, bounds, view_model_matrix);
      }

      if (show_texture_mapping) {
        draw_filled_triangles(mesh,
                              bounds,
                              view_model_matrix);
      }
      else if (show_specular_lighting) {
        draw_specular_triangles(mesh,
                                bounds,
                                view_model_matrix,
                                model_matrix,
                                camera.position);
      }
      else if (show_flat_shading) {
        draw_flat_shaded_triangles(mesh,
                                   bounds,
                                   view_model_matrix,
                                   model_matrix);
      }
      else if (show_ambient_lighting) {
        draw_ambient_lit_triangles(mesh, bounds, view_model_matrix);
      }
      else if (show_zbuffered_triangles) {
        draw_zbuffered_triangles(mesh, bounds, view_model_matrix);
      }
      else if (show_filled_triangles) {
        draw_filled_triangles(mesh, bounds, view_model_matrix);
      }

      if (show_mesh_wireframe) {
        draw_mesh_wireframe(mesh,
                            bounds,
                            view_model_matrix,
                            MFB_RGB(255, 255, 255));
      }

      if (show_bounding_box) {
        draw_bounding_box_debug(bounds, view_model_matrix);
      }

      if (show_local_axes) {
        draw_local_axes_debug(bounds, view_model_matrix);
      }

      if (show_world_axes) {
        draw_world_axes_debug(bounds, view_matrix);
      }
      if (show_face_normals) {
        draw_face_normals(mesh,
                          bounds,
                          view_model_matrix,
                          MFB_RGB(255, 128, 0));
      }

      if (show_vertex_normals) {
        draw_vertex_normals(mesh,
                            bounds,
                            view_model_matrix,
                            MFB_RGB(0, 255, 255));
      }
    }
    if (show_z_buffer_depth_view) {
      draw_z_buffer_visualization();
    }

    if (enable_interactive_lines) {
  // Draw permanent saved lines
     for (const Line& line : saved_lines) {
        draw_line(line.x0, line.y0, line.x1, line.y1, line.color);
    }

  // Draw preview line while dragging
     if (is_drawing) {
       draw_line(line_start_x, line_start_y,
          current_mouse_x, current_mouse_y,
          current_line_color);
  }
}


    // 3. UI Logic
    static float slider_val = 50.0f;
    static float number_val = 3.14f;
    static int checkbox_a = 0;
    static int checkbox_b = 1;
    static int show_message = 0;
    static char textbox_buf[128] = "edit me";
    static bool quit_requested = false;
    // UI mode: 0 = Main Menu, 1 = Assignments (Navigation), 2 = Final Project
    static int ui_mode = 0;
    mu_begin(ctx);
        // --- Main Menu / Assignments Navigation ---
        if (ui_mode == 0) {
          if (mu_begin_window(ctx, "Main Menu", mu_rect(20, 20, 300, 200))) {
            int mn[] = {-1};
            mu_layout_row(ctx, 1, mn, 0);
            mu_label(ctx, "Select a mode:");

            mu_layout_row(ctx, 1, mn, 0);
            if (mu_button(ctx, "Assignments")) {
              ui_mode = 1;
            }

            mu_layout_row(ctx, 1, mn, 0);
            if (mu_button(ctx, "Final Project")) {
              ui_mode = 2;
              ui_show_texture_mapping = 1; // open final project window
            }

            mu_end_window(ctx);
          }
        }

        if (ui_mode == 1) {
          // Existing Assignments Navigation window (unchanged except Final Project controls removed)
          if (mu_begin_window(ctx, "UI Navigation", mu_rect(20, 20, 300, 520))) {
            int wn[] = {-1};

            mu_layout_row(ctx, 1, wn, 0);
            mu_label(ctx, "Open / close control windows:");

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_widgets ? "Hide Widgets" : "Show Widgets")) {
              ui_show_widgets = !ui_show_widgets;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_mesh_info ? "Hide Mesh Info" : "Show Mesh Info")) {
              ui_show_mesh_info = !ui_show_mesh_info;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_drawing_controls ? "Hide Drawing Controls" : "Show Drawing Controls")) {
              ui_show_drawing_controls = !ui_show_drawing_controls;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_panel_demo ? "Hide Panel Demo" : "Show Panel Demo")) {
              ui_show_panel_demo = !ui_show_panel_demo;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_part1_debug ? "Hide Assignment 3 - Part 1" : "Show Assignment 3 - Part 1")) {
              ui_show_part1_debug = !ui_show_part1_debug;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_local_transform ? "Hide Local Transform" : "Show Local Transform")) {
              ui_show_local_transform = !ui_show_local_transform;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_world_transform ? "Hide World Transform" : "Show World Transform")) {
              ui_show_world_transform = !ui_show_world_transform;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_camera_position ? "Hide Camera Position" : "Show Camera Position")) {
              ui_show_camera_position = !ui_show_camera_position;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_camera_rotation ? "Hide Camera Rotation" : "Show Camera Rotation")) {
              ui_show_camera_rotation = !ui_show_camera_rotation;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_projection_mode ? "Hide Projection Mode" : "Show Projection Mode")) {
              ui_show_projection_mode = !ui_show_projection_mode;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_part4_normals
                               ? "Hide Assignment 3 - Part 4"
                               : "Show Assignment 3 - Part 4")) {
              ui_show_part4_normals = !ui_show_part4_normals;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_triangle_bbox_debug
                               ? "Hide Triangle Bounding Boxes"
                               : "Show Triangle Bounding Boxes")) {
              ui_show_triangle_bbox_debug = !ui_show_triangle_bbox_debug;
            }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, ui_show_lighting_material
                               ? "Hide Lighting and Material"
                               : "Show Lighting and Material")) {
              ui_show_lighting_material = !ui_show_lighting_material;
           }

            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, "Close All Windows")) {
              close_all_ui_windows();
            }
            mu_layout_row(ctx, 1, wn, 0);
            if (mu_button(ctx, "Back to Main Menu")) {
              close_all_ui_windows();
              ui_mode = 0;
            }
            mu_end_window(ctx);
          }
        }
   

    // --- Widgets window ---
    if (ui_show_widgets && mu_begin_window(ctx, "Widgets", mu_rect(340, 20, 520, 1050))) {
      int w1[] = {-1};

      // label / text
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "mu_label: plain static text");
      mu_text(ctx, "mu_text: word-wrapped longer text that will reflow inside "
                   "the window width automatically.");

      // button
      mu_layout_row(ctx, 1, w1, 0);
      if (mu_button(ctx, "Toggle Message")) {
      show_message = !show_message;
    }

      if (show_message) {
      mu_label(ctx, "Hello! The button changed the UI state.");
    }

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
            mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Background Mode:");

      mu_layout_row(ctx, 1, w1, 0);
      if (mu_button(ctx, use_solid_background
                         ? "Use Pattern Background"
                         : "Use Solid Background")) {
        use_solid_background = !use_solid_background;
      }

      if (use_solid_background) {
        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "Solid Background Red:");
        mu_slider(ctx, &solid_bg_r, 0.0f, 255.0f);

        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "Solid Background Green:");
        mu_slider(ctx, &solid_bg_g, 0.0f, 255.0f);

        mu_layout_row(ctx, 1, w1, 0);
        mu_label(ctx, "Solid Background Blue:");
        mu_slider(ctx, &solid_bg_b, 0.0f, 255.0f);
      }
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Background Intensity:");
      mu_slider(ctx, &g_background_intensity, 0.2f, 2.0f);

      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Ring Size:");
      mu_slider(ctx, &g_ring_size, 100.0f, 3000.0f);

      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "Blue Strength:");
      mu_slider(ctx, &g_blue_strength, 0.0f, 255.0f);

      // number
      mu_layout_row(ctx, 1, w1, 0);
      mu_label(ctx, "mu_number (step 0.1):");
      mu_number(ctx, &number_val, 0.1f);

      // header (collapsible section)
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

      mu_layout_row(ctx, 1, w1, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_widgets = 0;
      }

      mu_end_window(ctx);
    }
        // --- Mesh Info window ---
    if (ui_show_mesh_info && mu_begin_window(ctx, "Mesh Info", mu_rect(340, 20, 520, 520))) {
      int wi[] = {-1};

      mu_layout_row(ctx, 1, wi, 0);
      mu_label(ctx, "Assignment 2 Mesh Info");

      // Part 1: OBJ information
      mu_layout_row(ctx, 1, wi, 0);
      mu_label(ctx, "Part 1: OBJ Loader");

      char obj_info[128];

      snprintf(obj_info, sizeof(obj_info),
               "Loaded: %s",
               mesh.loaded ? "yes" : "no");
      mu_label(ctx, obj_info);

      snprintf(obj_info, sizeof(obj_info),
               "Vertices: %zu",
               mesh.vertices.size());
      mu_label(ctx, obj_info);

      snprintf(obj_info, sizeof(obj_info),
               "Faces: %zu",
               mesh.faces.size());
      mu_label(ctx, obj_info);

      // Part 2: Bounding box and viewport transform
      mu_layout_row(ctx, 1, wi, 0);
      mu_label(ctx, "Part 2: Bounds + Viewport");

      char bounds_info[160];

      snprintf(bounds_info, sizeof(bounds_info),
               "Min: %.1f, %.1f, %.1f",
               bounds.min.x,
               bounds.min.y,
               bounds.min.z);
      mu_label(ctx, bounds_info);

      snprintf(bounds_info, sizeof(bounds_info),
               "Max: %.1f, %.1f, %.1f",
               bounds.max.x,
               bounds.max.y,
               bounds.max.z);
      mu_label(ctx, bounds_info);

      snprintf(bounds_info, sizeof(bounds_info),
               "Center: %.1f, %.1f, %.1f",
               bounds.center.x,
               bounds.center.y,
               bounds.center.z);
      mu_label(ctx, bounds_info);

      snprintf(bounds_info, sizeof(bounds_info),
               "Scale: %.1f",
               bounds.scale);
      mu_label(ctx, bounds_info);

      snprintf(bounds_info, sizeof(bounds_info),
               "Translation: %.1f, %.1f, %.1f",
               bounds.viewport_translation.x,
               bounds.viewport_translation.y,
               bounds.viewport_translation.z);
      mu_label(ctx, bounds_info);

      mu_layout_row(ctx, 1, wi, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_mesh_info = 0;
      }

      mu_end_window(ctx);
    }
    if (ui_show_texture_mapping && mu_begin_window(ctx, "Final Project - Texture Mapping", mu_rect(340, 20, 520, 520))) {
      int wt[] = {-1};

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Texture Mapping Controls");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Select Model:");

      int model_buttons[] = {-1, -1};
      mu_layout_row(ctx, 2, model_buttons, 0);

      if (mu_button(ctx, "Pyramid")) {
        current_model_index = 0;

        if (load_obj(model_paths[current_model_index], mesh)) {
          compute_mesh_normals(mesh);
          bounds = compute_mesh_bounds(mesh);

          if (!load_texture(texture_paths[current_model_index], mesh.texture)) {
            printf("Failed to load project texture: %s\n",
                   texture_paths[current_model_index].c_str());
          }

          reset_all_transforms();
        }
      }

      if (mu_button(ctx, "Cube")) {
        current_model_index = 1;

        if (load_obj(model_paths[current_model_index], mesh)) {
          compute_mesh_normals(mesh);
          bounds = compute_mesh_bounds(mesh);

          if (!load_texture(texture_paths[current_model_index], mesh.texture)) {
            printf("Failed to load project texture: %s\n",
                   texture_paths[current_model_index].c_str());
          }

          reset_all_transforms();
        }
      }
      char current_project_model_label[128];
      snprintf(current_project_model_label,
               sizeof(current_project_model_label),
               "Current model: %s",
               current_model_index == 0 ? "Pyramid" : "Cube");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, current_project_model_label);
      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Select Texture:");

      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Checkerboard")) {
        current_texture_index = 0;

        if (!load_texture(comparison_texture_paths[current_texture_index], mesh.texture)) {
          printf("Failed to load texture: %s\n",
                 comparison_texture_paths[current_texture_index].c_str());
        }
      }

      mu_layout_row(ctx, 1, wt, 0);
      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Filter Comparison")) {
        current_texture_index = 1;

        if (!load_texture(comparison_texture_paths[current_texture_index], mesh.texture)) {
          printf("Failed to load texture: %s\n",
                 comparison_texture_paths[current_texture_index].c_str());
        }
      }

      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Filtering Test 32x32")) {
        current_texture_index = 2;

        if (!load_texture(comparison_texture_paths[current_texture_index], mesh.texture)) {
          printf("Failed to load texture: %s\n",
                 comparison_texture_paths[current_texture_index].c_str());
        }
      }

      mu_layout_row(ctx, 1, wt, 0);

      if (current_texture_index == 0) {
        mu_label(ctx, "Current Texture: Checkerboard");
      } else if (current_texture_index == 1) {
        mu_label(ctx, "Current Texture: Filter Comparison");
      } else {
        mu_label(ctx, "Current Texture: Filtering Test 32x32");
      }

      mu_layout_row(ctx, 1, wt, 0);
      mu_checkbox(ctx, "Enable Texture Mapping", &show_texture_mapping);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, mesh.texture.has_texture() ? "Texture loaded: yes" : "Texture loaded: no");

      mu_layout_row(ctx, 1, wt, 0);
      if (mesh.texture.has_texture()) {
        char texture_path_label[256];
        snprintf(texture_path_label, sizeof(texture_path_label), "Path: %s", mesh.texture.path.c_str());
        mu_label(ctx, texture_path_label);

        char texture_size_label[64];
        snprintf(texture_size_label, sizeof(texture_size_label), "Dimensions: %dx%d", mesh.texture.width, mesh.texture.height);
        mu_label(ctx, texture_size_label);
      } else {
        mu_label(ctx, "Path: (none)");
        mu_label(ctx, "Dimensions: N/A");
      }

      char uv_count_label[64];
      snprintf(uv_count_label, sizeof(uv_count_label), "UV coordinates: %zu", mesh.texture_coords.size());
      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, uv_count_label);
      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Texture Filtering:");

      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Nearest Neighbor")) {
        texture_filter_mode = 0;
      }

      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Bilinear Filtering")) {
        texture_filter_mode = 1;
      }

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx,
               texture_filter_mode == 0
                   ? "Current Filtering: Nearest Neighbor"
                   : "Current Filtering: Bilinear");
      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Back to Main Menu")) {
        ui_show_texture_mapping = 0;
        ui_mode = 0;
      }

      mu_end_window(ctx);
    }


        // --- Part 4: Transformation Controls window ---
    // --- Local Transform Controls window ---
    if (ui_show_local_transform && mu_begin_window(ctx, "Local Transform", mu_rect(340, 20, 520, 1050))) {   
      int wt[] = {-1};

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Local Transform Controls");

      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Reset All Transforms")) {
        local_translate_x = 0.0f;
        local_translate_y = 0.0f;
        local_translate_z = 0.0f;

        local_rotate_x = 0.0f;
        local_rotate_y = 0.0f;
        local_rotate_z = 0.0f;

        local_scale_x = 1.0f;
        local_scale_y = 1.0f;
        local_scale_z = 1.0f;

        world_translate_x = 0.0f;
        world_translate_y = 0.0f;
        world_translate_z = 0.0f;

        world_rotate_x = 0.0f;
        world_rotate_y = 0.0f;
        world_rotate_z = 0.0f;

        world_scale_x = 1.0f;
        world_scale_y = 1.0f;
        world_scale_z = 1.0f;
      }

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Local Translation");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &local_translate_x, -2.0f, 2.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &local_translate_y, -2.0f, 2.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &local_translate_z, -2.0f, 2.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Local Rotation");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &local_rotate_x, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &local_rotate_y, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &local_rotate_z, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Local Scale");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &local_scale_x, 0.1f, 3.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &local_scale_y, 0.1f, 3.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &local_scale_z, 0.1f, 3.0f);

      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_local_transform = 0;
      }

      mu_end_window(ctx);
    }

    // --- World Transform Controls window ---
    if (ui_show_world_transform && mu_begin_window(ctx, "World Transform", mu_rect(340, 20, 520, 1050))) {     
      int wt[] = {-1};

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "World Transform Controls");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "World Translation");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &world_translate_x, -2.0f, 2.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &world_translate_y, -2.0f, 2.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &world_translate_z, -2.0f, 2.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "World Rotation");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &world_rotate_x, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &world_rotate_y, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &world_rotate_z, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "World Scale");

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &world_scale_x, 0.1f, 3.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &world_scale_y, 0.1f, 3.0f);

      mu_layout_row(ctx, 1, wt, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &world_scale_z, 0.1f, 3.0f);

      mu_layout_row(ctx, 1, wt, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_world_transform = 0;
      }

      mu_end_window(ctx);
    }

        // --- Assignment 3 Part 2 Camera Position window ---
    if (ui_show_camera_position && mu_begin_window(ctx, "Camera Position", mu_rect(340, 20, 520, 420))) {      
      int wc[] = {-1};

      mu_layout_row(ctx, 1, wc, 0);
      mu_label(ctx, "Assignment 3 - Part 2 Camera Position");

      mu_layout_row(ctx, 1, wc, 0);
      if (mu_button(ctx, "Reset Camera Position")) {
        camera.position = glm::vec3(0.0f, 0.0f, 0.0f);
      }

      mu_layout_row(ctx, 1, wc, 0);
      mu_label(ctx, "Camera Position");

      mu_layout_row(ctx, 1, wc, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &camera.position.x, -5.0f, 5.0f);

      mu_layout_row(ctx, 1, wc, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &camera.position.y, -5.0f, 5.0f);

      mu_layout_row(ctx, 1, wc, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &camera.position.z, -10.0f, 10.0f);

      mu_layout_row(ctx, 1, wc, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_camera_position = 0;
      }
      mu_end_window(ctx);
    }

    // --- Assignment 3 Part 2 Camera Rotation window ---
    if (ui_show_camera_rotation && mu_begin_window(ctx, "Camera Rotation", mu_rect(340, 300, 520, 420))) {
      int wr[] = {-1};

      mu_layout_row(ctx, 1, wr, 0);
      mu_label(ctx, "Assignment 3 - Part 2 Camera Rotation");

      mu_layout_row(ctx, 1, wr, 0);
      if (mu_button(ctx, "Reset Camera Rotation")) {
        camera.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
      }

      mu_layout_row(ctx, 1, wr, 0);
      mu_label(ctx, "Camera Rotation");

      mu_layout_row(ctx, 1, wr, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &camera.rotation.x, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wr, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &camera.rotation.y, -180.0f, 180.0f);

      mu_layout_row(ctx, 1, wr, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &camera.rotation.z, -180.0f, 180.0f);
      mu_layout_row(ctx, 1, wr, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_camera_rotation = 0;
      }      
      mu_end_window(ctx);
    }
     // --- Assignment 3 Part 3 Projection window ---
    if (ui_show_projection_mode && mu_begin_window(ctx, "Projection Mode", mu_rect(340, 20, 520, 450))) {
      int wpj[] = {-1};

      mu_layout_row(ctx, 1, wpj, 0);
      mu_label(ctx, "Assignment 3 - Part 3 Projection");

      mu_layout_row(ctx, 1, wpj, 0);
      mu_label(ctx, use_perspective_projection
                    ? "Current: Perspective"
                    : "Current: Orthographic");

      mu_layout_row(ctx, 1, wpj, 0);
      if (mu_button(ctx, use_perspective_projection
                         ? "Use Orthographic Projection"
                         : "Use Perspective Projection")) {
        use_perspective_projection = !use_perspective_projection;

        // In perspective mode, the camera must be away from the object.
        if (use_perspective_projection && camera.position.z == 0.0f) {
          camera.position.z = 3.0f;
        }
      }

      mu_layout_row(ctx, 1, wpj, 0);
      if (mu_button(ctx, "Set Camera Z = 3")) {
        camera.position.z = 3.0f;
      }

      mu_layout_row(ctx, 1, wpj, 0);
      if (mu_button(ctx, "Set Camera Z = 6")) {
        camera.position.z = 6.0f;
      }

      char camera_z_text[80];
      snprintf(camera_z_text, sizeof(camera_z_text),
               "Camera Z: %.2f", camera.position.z);

      mu_layout_row(ctx, 1, wpj, 0);
      mu_label(ctx, camera_z_text);

      mu_layout_row(ctx, 1, wpj, 0);
      mu_label(ctx, "Perspective FOV:");
      mu_slider(ctx, &perspective_fov, 20.0f, 100.0f);

      mu_layout_row(ctx, 1, wpj, 0);
      mu_label(ctx, "Near Plane:");
      mu_slider(ctx, &perspective_near, 0.1f, 5.0f);

      mu_layout_row(ctx, 1, wpj, 0);
      mu_label(ctx, "Far Plane:");
      mu_slider(ctx, &perspective_far, 2.0f, 50.0f);

      // Keep Far plane always bigger than Near plane
      if (perspective_far <= perspective_near + 0.1f) {
        perspective_far = perspective_near + 0.1f;
      }
      mu_layout_row(ctx, 1, wpj, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_projection_mode = 0;
      }      
      mu_end_window(ctx);
    }   

    // --- Assignment 3 Part 4 Normals window ---
    if (ui_show_part4_normals && mu_begin_window(ctx, "Assignment 3 - Part 4", mu_rect(340, 20, 520, 360))) {
      int wn4[] = {-1};

      mu_layout_row(ctx, 1, wn4, 0);
      mu_label(ctx, "Face Normals and Vertex Normals");

      mu_layout_row(ctx, 1, wn4, 0);
      mu_checkbox(ctx, "Show Face Normals", &show_face_normals);

      mu_layout_row(ctx, 1, wn4, 0);
      mu_checkbox(ctx, "Show Vertex Normals", &show_vertex_normals);

      mu_layout_row(ctx, 1, wn4, 0);
      mu_label(ctx, "Normals Length:");
      mu_slider(ctx, &normals_length, 0.02f, 0.5f);

      char normals_info[160];

      snprintf(normals_info, sizeof(normals_info),
               "Face normals: %zu",
               mesh.face_normals.size());

      mu_layout_row(ctx, 1, wn4, 0);
      mu_label(ctx, normals_info);

      snprintf(normals_info, sizeof(normals_info),
               "Vertex normals: %zu",
               mesh.vertex_normals.size());

      mu_layout_row(ctx, 1, wn4, 0);
      mu_label(ctx, normals_info);

      mu_layout_row(ctx, 1, wn4, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_part4_normals = 0;
      }

      mu_end_window(ctx);
    }

        // --- Drawing Controls window ---
    if (ui_show_drawing_controls && mu_begin_window(ctx, "Drawing Controls", mu_rect(340, 20, 520, 420))) {
      int wd[] = {-1};

      mu_layout_row(ctx, 1, wd, 0);
      mu_label(ctx, "Assignment 1 Drawing Tool");

      mu_checkbox(ctx, "Enable Drawing Mode", &enable_interactive_lines);

      if (mu_button(ctx, "Clear Lines")) {
        saved_lines.clear();
      }

      mu_label(ctx, "Line Red:");
      mu_slider(ctx, &line_r, 0.0f, 255.0f);

      mu_label(ctx, "Line Green:");
      mu_slider(ctx, &line_g, 0.0f, 255.0f);

      mu_label(ctx, "Line Blue:");
      mu_slider(ctx, &line_b, 0.0f, 255.0f);

      mu_layout_row(ctx, 1, wd, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_drawing_controls = 0;
      }

      mu_end_window(ctx);
    }

    // --- Panel window ---
    if (ui_show_panel_demo && mu_begin_window(ctx, "Panel Demo", mu_rect(340, 20, 520, 300))) {
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
      mu_layout_row(ctx, 1, w2, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_panel_demo = 0;
      }
      mu_end_window(ctx);
    }
   
     // --- Assignment 3 Part 1 Debug Controls window ---
    if (ui_show_part1_debug && mu_begin_window(ctx, "Assignment 3 - Part 1", mu_rect(340, 20, 520, 480))) {
      int wa3[] = {-1};

      mu_layout_row(ctx, 1, wa3, 0);
      mu_label(ctx, "Coordinate Frames and Bounding Box");

      mu_layout_row(ctx, 1, wa3, 0);
      mu_checkbox(ctx, "Show Mesh Wireframe", &show_mesh_wireframe);

      mu_layout_row(ctx, 1, wa3, 0);
      mu_checkbox(ctx, "Show Local Axes", &show_local_axes);

      mu_layout_row(ctx, 1, wa3, 0);
      mu_checkbox(ctx, "Show World Axes", &show_world_axes);

      mu_layout_row(ctx, 1, wa3, 0);
      mu_checkbox(ctx, "Show Bounding Box", &show_bounding_box);

      mu_layout_row(ctx, 1, wa3, 0);
      if (mu_button(ctx, "Next Model")) {
        current_model_index =
            (current_model_index + 1) % (int)model_paths.size();

        if (load_obj(model_paths[current_model_index], mesh)) {
          compute_mesh_normals(mesh);
          bounds = compute_mesh_bounds(mesh);
          reset_all_transforms();
        }
      }

      char current_model_text[160];
      snprintf(current_model_text, sizeof(current_model_text),
               "Current model: %s",
               model_paths[current_model_index].c_str());

      mu_layout_row(ctx, 1, wa3, 0);
      mu_label(ctx, current_model_text);
      mu_layout_row(ctx, 1, wa3, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_part1_debug = 0;
      }

      mu_end_window(ctx);
    }
      // --- Triangle Bounding Box Debug window ---
    if (ui_show_triangle_bbox_debug && mu_begin_window(ctx, "Triangle Rasterization Debug", mu_rect(880, 20, 520, 520))) {
      int wbbox[] = {-1};

      mu_layout_row(ctx, 1, wbbox, 0);
      mu_label(ctx, "Triangle Rasterization");

      mu_layout_row(ctx, 1, wbbox, 0);
      mu_checkbox(ctx, "Show Triangle Bounding Boxes",
                  &show_triangle_bounding_boxes);

      mu_layout_row(ctx, 1, wbbox, 0);
      mu_checkbox(ctx, "Show Filled Triangles",
                  &show_filled_triangles);

      mu_layout_row(ctx, 1, wbbox, 0);
      mu_checkbox(ctx, "Show Z-Buffered Triangles",
                  &show_zbuffered_triangles);

      mu_layout_row(ctx, 1, wbbox, 0);
      mu_checkbox(ctx, "Show Z-Buffer Depth View",
                  &show_z_buffer_depth_view);            

      mu_layout_row(ctx, 1, wbbox, 0);
      mu_checkbox(ctx, "Show Mesh Wireframe", &show_mesh_wireframe);

      mu_layout_row(ctx, 1, wbbox, 0);
      if (mu_button(ctx, "Bounding Boxes Only")) {
        show_triangle_bounding_boxes = 1;
        show_mesh_wireframe = 0;
        show_local_axes = 0;
        show_world_axes = 0;
        show_bounding_box = 0;
      }

      mu_layout_row(ctx, 1, wbbox, 0);
      if (mu_button(ctx, "Filled Triangles Only")) {
        show_triangle_bounding_boxes = 0;
        show_filled_triangles = 1;
        show_mesh_wireframe = 0;
        show_local_axes = 0;
        show_world_axes = 0;
        show_bounding_box = 0;
      }

      mu_layout_row(ctx, 1, wbbox, 0);
      if (mu_button(ctx, "Bounding Boxes + Wireframe")) {
        show_triangle_bounding_boxes = 1;
        show_mesh_wireframe = 1;
        show_local_axes = 0;
        show_world_axes = 0;
        show_bounding_box = 0;
      }

            mu_layout_row(ctx, 1, wbbox, 0);
      if (mu_button(ctx, "Z-Buffered Triangles Only")) {
        show_triangle_bounding_boxes = 0;
        show_filled_triangles = 0;
        show_zbuffered_triangles = 1;
        show_z_buffer_depth_view = 0;
        show_mesh_wireframe = 0;
        show_local_axes = 0;
        show_world_axes = 0;
        show_bounding_box = 0;
      }

      mu_layout_row(ctx, 1, wbbox, 0);
      if (mu_button(ctx, "Z-Buffered Triangles + Wireframe")) {
        show_triangle_bounding_boxes = 0;
        show_filled_triangles = 0;
        show_zbuffered_triangles = 1;
        show_z_buffer_depth_view = 0;
        show_mesh_wireframe = 1;
        show_local_axes = 0;
        show_world_axes = 0;
        show_bounding_box = 0;
      }

      mu_layout_row(ctx, 1, wbbox, 0);
      if (mu_button(ctx, "Z-Buffer Depth View")) {
        show_triangle_bounding_boxes = 0;
        show_filled_triangles = 0;
        show_zbuffered_triangles = 1;
        show_z_buffer_depth_view = 1;
        show_mesh_wireframe = 0;
        show_local_axes = 0;
        show_world_axes = 0;
        show_bounding_box = 0;
      }

      mu_layout_row(ctx, 1, wbbox, 0);
      mu_label(ctx, "Bounding boxes are Part 1. Filled triangles are Part 2.");

      mu_layout_row(ctx, 1, wbbox, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_triangle_bbox_debug = 0;
      }

      mu_end_window(ctx);
    }  


        // --- Assignment 5 Part 1: Lighting and Material window ---
    if (ui_show_lighting_material &&
        mu_begin_window(ctx, "Lighting and Material", mu_rect(930, 20, 640, 1080))) {
      int wl[] = {-1};
      int row_position[] = {80, -1};
      int row_color[] = {120, -1};

      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Assignment 5 - Part 1 and Part 2 and part 3");

      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Ambient Lighting Mode")) {
        show_ambient_lighting = 1;
        show_flat_shading = 0;
        show_specular_lighting = 0;
      }

      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Flat Shading Mode")) {
        show_flat_shading = 1;
        show_ambient_lighting = 0;
        show_specular_lighting = 0;
        show_phong_shading = 0;
      }
      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Specular Lighting Mode")) {
        show_specular_lighting = 1;
        show_flat_shading = 0;
        show_ambient_lighting = 0;
        show_phong_shading = 0;
        show_triangle_bounding_boxes = 0;
        show_filled_triangles = 0;
        show_zbuffered_triangles = 0;
        show_z_buffer_depth_view = 0;

        show_mesh_wireframe = 0;
        show_local_axes = 0;
        show_world_axes = 0;
        show_bounding_box = 0;
      }
      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Phong Shading Mode")) {
       show_phong_shading = 1;
       show_specular_lighting = 0;
       show_flat_shading = 0;
       show_ambient_lighting = 0;

       show_triangle_bounding_boxes = 0;
       show_filled_triangles = 0;
       show_zbuffered_triangles = 0;
       show_z_buffer_depth_view = 0;

       show_mesh_wireframe = 0;
       show_local_axes = 0;
       show_world_axes = 0;
       show_bounding_box = 0;
       show_reflection_debug_vectors = 0;
     }

      mu_layout_row(ctx, 1, wl, 0);
      mu_checkbox(ctx, "Show Reflection Debug Vectors",
                  &show_reflection_debug_vectors);

      if (show_flat_shading) {
        show_ambient_lighting = 0;
      }
      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Light Position");

      mu_layout_row(ctx, 2, row_position, 0);
      mu_label(ctx, "X:");
      mu_slider(ctx, &g_light.position.x, -5.0f, 5.0f);

      mu_layout_row(ctx, 2, row_position, 0);
      mu_label(ctx, "Y:");
      mu_slider(ctx, &g_light.position.y, -5.0f, 5.0f);

      mu_layout_row(ctx, 2, row_position, 0);
      mu_label(ctx, "Z:");
      mu_slider(ctx, &g_light.position.z, -5.0f, 5.0f);

      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Light Ambient Color");

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Ambient R:");
      mu_slider(ctx, &g_light.ambient.r, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Ambient G:");
      mu_slider(ctx, &g_light.ambient.g, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Ambient B:");
      mu_slider(ctx, &g_light.ambient.b, 0.0f, 1.0f);

      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Material Ambient Color");

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material R:");
      mu_slider(ctx, &g_material.ambient.r, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material G:");
      mu_slider(ctx, &g_material.ambient.g, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material B:");
      mu_slider(ctx, &g_material.ambient.b, 0.0f, 1.0f);

      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Light Diffuse Color");

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Diffuse R:");
      mu_slider(ctx, &g_light.diffuse.r, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Diffuse G:");
      mu_slider(ctx, &g_light.diffuse.g, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Diffuse B:");
      mu_slider(ctx, &g_light.diffuse.b, 0.0f, 1.0f);

      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Material Diffuse Color");

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material Diffuse R:");
      mu_slider(ctx, &g_material.diffuse.r, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material Diffuse G:");
      mu_slider(ctx, &g_material.diffuse.g, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material Diffuse B:");
      mu_slider(ctx, &g_material.diffuse.b, 0.0f, 1.0f);

      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Light Specular Color");

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Specular R:");
      mu_slider(ctx, &g_light.specular.r, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Specular G:");
      mu_slider(ctx, &g_light.specular.g, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Specular B:");
      mu_slider(ctx, &g_light.specular.b, 0.0f, 1.0f);

      mu_layout_row(ctx, 1, wl, 0);
      mu_label(ctx, "Material Specular Color");

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material Specular R:");
      mu_slider(ctx, &g_material.specular.r, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material Specular G:");
      mu_slider(ctx, &g_material.specular.g, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Material Specular B:");
      mu_slider(ctx, &g_material.specular.b, 0.0f, 1.0f);

      mu_layout_row(ctx, 2, row_color, 0);
      mu_label(ctx, "Shininess:");
      mu_slider(ctx, &g_material.shininess, 1.0f, 128.0f);      

      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Ambient Red Material")) {
        g_light.ambient = glm::vec3(0.5f, 0.5f, 0.5f);
        g_material.ambient = glm::vec3(0.9f, 0.2f, 0.2f);
      }

      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Ambient Green Material")) {
        g_light.ambient = glm::vec3(0.5f, 0.5f, 0.5f);
        g_material.ambient = glm::vec3(0.2f, 0.9f, 0.2f);
      }

      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Ambient Blue Material")) {
        g_light.ambient = glm::vec3(0.5f, 0.5f, 0.5f);
        g_material.ambient = glm::vec3(0.2f, 0.2f, 0.9f);
      }

      if (mu_button(ctx, "Reset Lighting Values")) {
        g_light.position = glm::vec3(0.0f, 0.0f, 3.0f);

        g_light.ambient = glm::vec3(0.4f, 0.4f, 0.4f);
        g_material.ambient = glm::vec3(0.8f, 0.2f, 0.2f);

        g_light.diffuse = glm::vec3(0.8f, 0.8f, 0.8f);
        g_material.diffuse = glm::vec3(0.8f, 0.2f, 0.2f);

        g_light.specular = glm::vec3(1.0f, 1.0f, 1.0f);
        g_material.specular = glm::vec3(1.0f, 1.0f, 1.0f);
        g_material.shininess = 32.0f;
      }

      mu_layout_row(ctx, 1, wl, 0);
      if (mu_button(ctx, "Close Window")) {
        ui_show_lighting_material = 0;
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

    // Cap FPS (optional, minifb has built-in sync)
    mfb_wait_sync(window);
  }

  mfb_close(window);
  free(ctx);
  return 0;
}