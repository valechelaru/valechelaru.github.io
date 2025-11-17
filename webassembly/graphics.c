// Minimal 3D wireframe maze "game" rendered in software for WebAssembly.
// Movement: WASD to move, mouse to look, Space to jump, Shift to sprint.
#define WIDTH 600u
#define HEIGHT 600u
#define PIXEL_COUNT (WIDTH * HEIGHT)
#define HALF_WIDTH 300.0f
#define HALF_HEIGHT 300.0f
#define SCREEN_GUARD                                                           \
  2 // guard band in pixels to keep faces from popping at screen edges
unsigned int BUFFER[PIXEL_COUNT];
float DEPTH[PIXEL_COUNT];

typedef struct {
  float x;
  float y;
  float z;
} Vec3;

typedef struct {
  Vec3 center;
  float size;
  unsigned int color;
  unsigned int line_color;
  unsigned char visible_faces; // bitmask of which faces/edges to render
} Cube;

typedef struct {
  float minx;
  float minz;
  float maxx;
  float maxz;
  float height;
  unsigned int color;
  unsigned char visible_faces;
} Wall;

// Face bits (match face order in drawCube): front, back, bottom, top, left,
// right.
#define FACE_FRONT (1u << 0)
#define FACE_BACK (1u << 1)
#define FACE_BOTTOM (1u << 2)
#define FACE_TOP (1u << 3)
#define FACE_LEFT (1u << 4)
#define FACE_RIGHT (1u << 5)
#define FACE_ALL 0x3Fu
// Cube topology reused per draw.
static const unsigned char CUBE_EDGES[12][2] = {
    {0, 1}, {1, 3}, {3, 2}, {2, 0}, // bottom
    {4, 5}, {5, 7}, {7, 6}, {6, 4}, // top
    {0, 4}, {1, 5}, {2, 6}, {3, 7}  // sides
};
static const unsigned char EDGE_FACE_BITS[12][2] = {
    {FACE_BACK, FACE_BOTTOM},  // 0: {0,1}
    {FACE_BACK, FACE_RIGHT},   // 1: {1,3}
    {FACE_BACK, FACE_TOP},     // 2: {3,2}
    {FACE_BACK, FACE_LEFT},    // 3: {2,0}
    {FACE_FRONT, FACE_BOTTOM}, // 4: {4,5}
    {FACE_FRONT, FACE_RIGHT},  // 5: {5,7}
    {FACE_FRONT, FACE_TOP},    // 6: {7,6}
    {FACE_FRONT, FACE_LEFT},   // 7: {6,4}
    {FACE_LEFT, FACE_BOTTOM},  // 8: {0,4}
    {FACE_RIGHT, FACE_BOTTOM}, // 9: {1,5}
    {FACE_LEFT, FACE_TOP},     // 10:{2,6}
    {FACE_RIGHT, FACE_TOP},    // 11:{3,7}
};
static const unsigned char FACE_VERTS[6][4] = {
    {4, 5, 7, 6}, // front  (+z)
    {0, 2, 3, 1}, // back   (-z)
    {0, 1, 5, 4}, // bottom (-y)
    {2, 3, 7, 6}, // top    (+y)
    {0, 4, 6, 2}, // left   (-x)
    {1, 3, 7, 5}  // right  (+x)
};
static const Vec3 FACE_NORMALS[6] = {
    {0.0f, 0.0f, 1.0f},  // front
    {0.0f, 0.0f, -1.0f}, // back
    {0.0f, -1.0f, 0.0f}, // bottom
    {0.0f, 1.0f, 0.0f},  // top
    {-1.0f, 0.0f, 0.0f}, // left
    {1.0f, 0.0f, 0.0f}   // right
};

// Input state written from JS.
volatile int INPUT_KEYS = 0;
volatile int INPUT_MOUSE_DX = 0;
volatile int INPUT_MOUSE_DY = 0;

// Camera state.
Vec3 camera_pos = {2.5f, 0.7f, 2.5f};
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;
float player_y = 0.0f;
float player_y_vel = 0.0f;
int player_grounded = 1;
const float player_height = 0.7f;
const float player_radius = 0.3f;
// Cached camera basis so we do not recompute trig per cube.
typedef struct {
  float cy, sy;
  float cp, sp;
  float fwd_x, fwd_z;
  float right_x, right_z;
} ViewBasis;
ViewBasis VIEW = {1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f};

// Atmosphere and lighting tweaks.
const unsigned int FOG_COLOR = 0xff111827;
const float FOG_NEAR = 6.0f;
const float FOG_FAR = 24.0f;
const float FOG_INV_RANGE = 1.0f / (FOG_FAR - FOG_NEAR);
const Vec3 LIGHT_DIR = {0.45f, 0.85f, 0.35f}; // roughly normalized
// View frustum (symmetric ~90 deg FOV because projection divides by z).
const float NEAR_PLANE = 0.08f;
const float FAR_PLANE = 60.0f;
const float FRUSTUM_GUARD =
    1.08f; // Loosen culling to keep faces alive at screen edges.
const float DEPTH_CLEAR = -1e30f;

// Constants and helpers (no stdlib).
const float PI = 3.14159265f;
const float TWO_PI = 6.28318530f;

float wrap_angle(float a) {
  while (a > PI)
    a -= TWO_PI;
  while (a < -PI)
    a += TWO_PI;
  return a;
}

// Polynomial approximations with range reduction to keep stability when
// rotating many times.
static float sin_poly(float x) {
  float x2 = x * x;
  float x3 = x2 * x;
  float x5 = x3 * x2;
  float x7 = x5 * x2;
  return x - (x3 * 0.16666667f) + (x5 * 0.008333333f) - (x7 * 0.0001984127f);
}

float approx_sin(float x) {
  // Wrap to [-pi, pi]
  while (x > PI)
    x -= TWO_PI;
  while (x < -PI)
    x += TWO_PI;

  int sign = 1;
  if (x < 0.0f) {
    sign = -1;
    x = -x;
  }
  if (x > (PI * 0.5f)) {
    // Use symmetry: sin(pi - x) for x in (pi/2, pi]
    x = PI - x;
  }
  return (float)sign * sin_poly(x);
}

float approx_cos(float x) {
  // cos(x) = sin(pi/2 - x) with same range reduction
  return approx_sin((PI * 0.5f) - x);
}

static float clamp01(float v) {
  if (v < 0.0f)
    return 0.0f;
  if (v > 1.0f)
    return 1.0f;
  return v;
}

// Convert from 0xAARRGGBB to in-memory RGBA (little-endian: R,G,B,A bytes).
static unsigned int argb_to_rgba(unsigned int argb) {
  unsigned int a = (argb >> 24) & 0xffu;
  unsigned int r = (argb >> 16) & 0xffu;
  unsigned int g = (argb >> 8) & 0xffu;
  unsigned int b = (argb) & 0xffu;
  return r | (g << 8) | (b << 16) | (a << 24);
}

unsigned int scale_color(unsigned int color, float factor) {
  factor = clamp01(factor);
  unsigned int a = (color >> 24) & 0xffu;
  unsigned int r = (color >> 16) & 0xffu;
  unsigned int g = (color >> 8) & 0xffu;
  unsigned int b = (color) & 0xffu;

  r = (unsigned int)(r * factor);
  if (r > 255u)
    r = 255u;
  g = (unsigned int)(g * factor);
  if (g > 255u)
    g = 255u;
  b = (unsigned int)(b * factor);
  if (b > 255u)
    b = 255u;
  return r | (g << 8) | (b << 16) | (a << 24);
}

unsigned int lerp_color(unsigned int c0, unsigned int c1, float t) {
  t = clamp01(t);
  unsigned int a0 = (c0 >> 24) & 0xffu;
  unsigned int r0 = (c0 >> 16) & 0xffu;
  unsigned int g0 = (c0 >> 8) & 0xffu;
  unsigned int b0 = (c0) & 0xffu;

  unsigned int a1 = (c1 >> 24) & 0xffu;
  unsigned int r1 = (c1 >> 16) & 0xffu;
  unsigned int g1 = (c1 >> 8) & 0xffu;
  unsigned int b1 = (c1) & 0xffu;

  unsigned int a = (unsigned int)(a0 + (a1 - a0) * t);
  unsigned int r = (unsigned int)(r0 + (r1 - r0) * t);
  unsigned int g = (unsigned int)(g0 + (g1 - g0) * t);
  unsigned int b = (unsigned int)(b0 + (b1 - b0) * t);
  return r | (g << 8) | (b << 16) | (a << 24);
}

float fog_factor(float depth) {
  return clamp01((depth - FOG_NEAR) * FOG_INV_RANGE);
}

float face_brightness(Vec3 normal) {
  float dot =
      normal.x * LIGHT_DIR.x + normal.y * LIGHT_DIR.y + normal.z * LIGHT_DIR.z;
  if (dot < 0.0f)
    dot = 0.0f;
  // Keep some ambient term so faces are never completely dark.
  return 0.35f + dot * 0.65f;
}

void setInput(int keyMask, int mouseDx, int mouseDy) {
  INPUT_KEYS = keyMask;
  INPUT_MOUSE_DX = mouseDx;
  INPUT_MOUSE_DY = mouseDy;
}

void clearBuffer(unsigned int color) {
  unsigned int i;
  unsigned int fill = argb_to_rgba(color);
  for (i = 0; i < PIXEL_COUNT; i++) {
    BUFFER[i] = fill;
    DEPTH[i] = DEPTH_CLEAR;
  }
}

void plot(int x, int y, unsigned int color) {
  if ((unsigned int)x >= WIDTH || (unsigned int)y >= HEIGHT)
    return;
  BUFFER[(unsigned int)y * WIDTH + (unsigned int)x] = color;
}

void drawFilledTriangle(int x0, int y0, float z0, int x1, int y1, float z1,
                        int x2, int y2, float z2, unsigned int color) {
  int minx = x0;
  if (x1 < minx)
    minx = x1;
  if (x2 < minx)
    minx = x2;
  int maxx = x0;
  if (x1 > maxx)
    maxx = x1;
  if (x2 > maxx)
    maxx = x2;
  int miny = y0;
  if (y1 < miny)
    miny = y1;
  if (y2 < miny)
    miny = y2;
  int maxy = y0;
  if (y1 > maxy)
    maxy = y1;
  if (y2 > maxy)
    maxy = y2;

  if (maxx < 0 || maxy < 0 || minx >= (int)WIDTH || miny >= (int)HEIGHT)
    return;
  if (minx < 0)
    minx = 0;
  if (miny < 0)
    miny = 0;
  if (maxx >= (int)WIDTH)
    maxx = (int)WIDTH - 1;
  if (maxy >= (int)HEIGHT)
    maxy = (int)HEIGHT - 1;

  // Flip the orientation so edge functions (based on vertices 1->2, 2->0, 0->1)
  // have the same sign as the triangle area. This keeps barycentric weights
  // positive.
  float area = (float)((x0 - x1) * (y2 - y1) - (y0 - y1) * (x2 - x1));
  if (area > -1e-6f && area < 1e-6f)
    return;

  float invArea = 1.0f / area;

  // Perspective-correct depth: interpolate 1/z linearly in screen space.
  float invZ0 = 1.0f / z0;
  float invZ1 = 1.0f / z1;
  float invZ2 = 1.0f / z2;

  // Edge deltas for incremental evaluation.
  float stepW0x = (float)(y2 - y1);
  float stepW0y = -(float)(x2 - x1);
  float stepW1x = (float)(y0 - y2);
  float stepW1y = -(float)(x0 - x2);
  float stepW2x = (float)(y1 - y0);
  float stepW2y = -(float)(x1 - x0);

  // Sample at pixel centers (add 0.5) to keep coverage consistent.
  float startX = (float)minx + 0.5f;
  float startY = (float)miny + 0.5f;
  float w0_row = (startX - (float)x1) * (float)(y2 - y1) -
                 (startY - (float)y1) * (float)(x2 - x1);
  float w1_row = (startX - (float)x2) * (float)(y0 - y2) -
                 (startY - (float)y2) * (float)(x0 - x2);
  float w2_row = (startX - (float)x0) * (float)(y1 - y0) -
                 (startY - (float)y0) * (float)(x1 - x0);

  int y;
  for (y = miny; y <= maxy; y++) {
    float w0 = w0_row;
    float w1 = w1_row;
    float w2 = w2_row;
    int x;
    for (x = minx; x <= maxx; x++) {
      if (w0 * area >= 0.0f && w1 * area >= 0.0f && w2 * area >= 0.0f) {
        float bw0 = w0 * invArea;
        float bw1 = w1 * invArea;
        float bw2 = w2 * invArea;
        float invZ = bw0 * invZ0 + bw1 * invZ1 + bw2 * invZ2;
        if (invZ > 0.0f) {
          unsigned int idx = (unsigned int)y * WIDTH + (unsigned int)x;
          float current = DEPTH[idx];
          // Compare inverse depth so nearer fragments (larger 1/z) win.
          if (invZ > current) {
            DEPTH[idx] = invZ;
            BUFFER[idx] = color;
          }
        }
      }
      w0 += stepW0x;
      w1 += stepW1x;
      w2 += stepW2x;
    }
    w0_row += stepW0y;
    w1_row += stepW1y;
    w2_row += stepW2y;
  }
}

void drawQuad(int a[2], float za, int b[2], float zb, int c[2], float zc,
              int d[2], float zd, unsigned int color) {
  drawFilledTriangle(a[0], a[1], za, b[0], b[1], zb, c[0], c[1], zc, color);
  drawFilledTriangle(a[0], a[1], za, c[0], c[1], zc, d[0], d[1], zd, color);
}

void drawLine(int x0, int y0, int x1, int y1, unsigned int color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int ax = dx < 0 ? -dx : dx;
  int ay = dy < 0 ? -dy : dy;

  int sx = dx >= 0 ? 1 : -1;
  int sy = dy >= 0 ? 1 : -1;

  int err = (ax > ay ? ax : -ay) / 2;
  int e2;

  while (1) {
    plot(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    e2 = err;
    if (e2 > -ax) {
      err -= ay;
      x0 += sx;
    }
    if (e2 < ay) {
      err += ax;
      y0 += sy;
    }
  }
}

void drawLineDepth(int x0, int y0, float z0, int x1, int y1, float z1,
                   unsigned int color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int ax = dx < 0 ? -dx : dx;
  int ay = dy < 0 ? -dy : dy;

  int sx = dx >= 0 ? 1 : -1;
  int sy = dy >= 0 ? 1 : -1;

  int err = (ax > ay ? ax : -ay) / 2;
  int e2;

  int steps = ax > ay ? ax + 1 : ay + 1;
  if (steps < 1)
    steps = 1;
  float invZ0 = 1.0f / z0;
  float invZ1 = 1.0f / z1;
  float dinvZ = (invZ1 - invZ0) / (float)steps;
  float invZ = invZ0;

  while (1) {
    if ((unsigned int)x0 < WIDTH && (unsigned int)y0 < HEIGHT) {
      unsigned int idx = (unsigned int)y0 * WIDTH + (unsigned int)x0;
      if (invZ > 0.0f) {
        float current = DEPTH[idx];
        if (invZ > current) {
          DEPTH[idx] = invZ;
          BUFFER[idx] = color;
        }
      }
    }
    if (x0 == x1 && y0 == y1)
      break;
    e2 = err;
    if (e2 > -ax) {
      err -= ay;
      x0 += sx;
    }
    if (e2 < ay) {
      err += ax;
      y0 += sy;
    }
    invZ += dinvZ;
  }
}

Vec3 rotateY(Vec3 v, float yaw) {
  float cy = approx_cos(yaw);
  float sy = approx_sin(yaw);
  Vec3 out;
  out.x = v.x * cy - v.z * sy;
  out.y = v.y;
  out.z = v.x * sy + v.z * cy;
  return out;
}

Vec3 rotateX(Vec3 v, float pitch) {
  float cp = approx_cos(pitch);
  float sp = approx_sin(pitch);
  Vec3 out;
  out.x = v.x;
  out.y = v.y * cp - v.z * sp;
  out.z = v.y * sp + v.z * cp;
  return out;
}

// Combine yaw and pitch in one shot to avoid recomputing sin/cos per vertex.
static Vec3 rotateYawPitch(Vec3 v, float cy, float sy, float cp, float sp) {
  // Yaw (around Y)
  float rx = v.x * cy - v.z * sy;
  float rz = v.x * sy + v.z * cy;
  // Pitch (around X)
  Vec3 out;
  out.x = rx;
  out.y = v.y * cp - rz * sp;
  out.z = v.y * sp + rz * cp;
  return out;
}

int project(Vec3 p, int *sx, int *sy) {
  if (p.z <= NEAR_PLANE)
    return 0; // Behind or too close.
  float invZ = 1.0f / p.z;
  float xf = (p.x * invZ) * HALF_WIDTH + HALF_WIDTH;
  // Flip y for screen coords so positive world-up moves the scene downwards,
  // matching intuition.
  float yf = (-p.y * invZ) * HALF_HEIGHT + HALF_HEIGHT;
  int xi = (int)xf;
  int yi = (int)yf;
  if (xi < -SCREEN_GUARD || xi >= (int)WIDTH + SCREEN_GUARD)
    return 0;
  if (yi < -SCREEN_GUARD || yi >= (int)HEIGHT + SCREEN_GUARD)
    return 0;
  *sx = xi;
  *sy = yi;
  return 1;
}

void project_no_clip(Vec3 p, int *sx, int *sy) {
  float invZ = 1.0f / p.z;
  float xf = (p.x * invZ) * HALF_WIDTH + HALF_WIDTH;
  float yf = (-p.y * invZ) * HALF_HEIGHT + HALF_HEIGHT;
  *sx = (int)xf;
  *sy = (int)yf;
}

// Generic Sutherland–Hodgman clip against a plane n·p + d >= 0. Returns new
// vertex count.
static int clipPlane(const Vec3 *inPts, int inCount, Vec3 *outPts, float nx,
                     float ny, float nz, float d) {
  if (inCount < 2)
    return 0;
  int outCount = 0;
  int i;
  for (i = 0; i < inCount; i++) {
    Vec3 a = inPts[i];
    Vec3 b = inPts[(i + 1) % inCount];
    float da = a.x * nx + a.y * ny + a.z * nz + d;
    float db = b.x * nx + b.y * ny + b.z * nz + d;
    int aInside = da >= 0.0f;
    int bInside = db >= 0.0f;

    if (aInside && bInside) {
      outPts[outCount++] = b;
    } else if (aInside && !bInside) {
      float t = da / (da - db);
      Vec3 p;
      p.x = a.x + (b.x - a.x) * t;
      p.y = a.y + (b.y - a.y) * t;
      p.z = a.z + (b.z - a.z) * t;
      outPts[outCount++] = p;
    } else if (!aInside && bInside) {
      float t = da / (da - db);
      Vec3 p;
      p.x = a.x + (b.x - a.x) * t;
      p.y = a.y + (b.y - a.y) * t;
      p.z = a.z + (b.z - a.z) * t;
      outPts[outCount++] = p;
      outPts[outCount++] = b;
    }
  }
  return outCount;
}

// Clip a convex polygon against the full view frustum in camera space.
static int clipFrustum(const Vec3 *inPts, int inCount, Vec3 *tmpPtsA,
                       Vec3 *tmpPtsB) {
  // Order: near, far, left, right, top, bottom.
  int count = clipPlane(inPts, inCount, tmpPtsA, 0.0f, 0.0f, 1.0f, -NEAR_PLANE);
  if (count < 3)
    return 0;
  count = clipPlane(tmpPtsA, count, tmpPtsB, 0.0f, 0.0f, -1.0f, FAR_PLANE);
  if (count < 3)
    return 0;
  count = clipPlane(tmpPtsB, count, tmpPtsA, 1.0f, 0.0f, FRUSTUM_GUARD,
                    0.0f); // left: x + guard*z >= 0
  if (count < 3)
    return 0;
  count = clipPlane(tmpPtsA, count, tmpPtsB, -1.0f, 0.0f, FRUSTUM_GUARD,
                    0.0f); // right: -x + guard*z >= 0
  if (count < 3)
    return 0;
  count = clipPlane(tmpPtsB, count, tmpPtsA, 0.0f, -1.0f, FRUSTUM_GUARD,
                    0.0f); // top: -y + guard*z >= 0
  if (count < 3)
    return 0;
  count = clipPlane(tmpPtsA, count, tmpPtsB, 0.0f, 1.0f, FRUSTUM_GUARD,
                    0.0f); // bottom: y + guard*z >= 0
  return count;
}

// Quick reject: if every vertex is outside the same plane, the cube cannot hit
// the screen.
static int cubeInFrustum(const Vec3 *camVerts) {
  int i;
  int outside = 1;
  for (i = 0; i < 8; i++) {
    if (camVerts[i].z >= NEAR_PLANE) {
      outside = 0;
      break;
    }
  }
  if (outside)
    return 0;

  outside = 1;
  for (i = 0; i < 8; i++) {
    if (camVerts[i].z <= FAR_PLANE) {
      outside = 0;
      break;
    }
  }
  if (outside)
    return 0;

  float guard = FRUSTUM_GUARD;

  outside = 1;
  for (i = 0; i < 8; i++) {
    if (camVerts[i].x + guard * camVerts[i].z >= 0.0f) {
      outside = 0;
      break;
    }
  }
  if (outside)
    return 0;

  outside = 1;
  for (i = 0; i < 8; i++) {
    if (-camVerts[i].x + guard * camVerts[i].z >= 0.0f) {
      outside = 0;
      break;
    }
  }
  if (outside)
    return 0;

  outside = 1;
  for (i = 0; i < 8; i++) {
    if (-camVerts[i].y + guard * camVerts[i].z >= 0.0f) {
      outside = 0;
      break;
    }
  }
  if (outside)
    return 0;

  outside = 1;
  for (i = 0; i < 8; i++) {
    if (camVerts[i].y + guard * camVerts[i].z >= 0.0f) {
      outside = 0;
      break;
    }
  }
  if (outside)
    return 0;

  return 1;
}

void drawCube(Cube cube) {
  unsigned char faceMask = cube.visible_faces ? cube.visible_faces : FACE_ALL;
  // Build 8 vertices around center.
  float hs = cube.size * 0.5f;
  float cy = VIEW.cy;
  float sy = -VIEW.sy;
  float cp = VIEW.cp;
  float sp = -VIEW.sp;

  Vec3 verts[8];
  Vec3 camVerts[8];
  int i;
  for (i = 0; i < 8; i++) {
    float x = (i & 1) ? hs : -hs;
    float y = (i & 2) ? hs : -hs;
    float z = (i & 4) ? hs : -hs;
    verts[i].x = cube.center.x + x;
    verts[i].y = cube.center.y + y;
    verts[i].z = cube.center.z + z;
    Vec3 rel;
    rel.x = verts[i].x - camera_pos.x;
    rel.y = verts[i].y - camera_pos.y;
    rel.z = verts[i].z - camera_pos.z;

    camVerts[i] = rotateYawPitch(rel, cy, sy, cp, sp);

    // Projected positions for edges are computed later without culling.
  }

  if (!cubeInFrustum(camVerts)) {
    return;
  }

  const float nearPlane = NEAR_PLANE;

  // Filled faces with backface culling and near-plane clipping.
  for (i = 0; i < 6; i++) {
    if ((faceMask & (1u << i)) == 0)
      continue;
    unsigned char i0 = FACE_VERTS[i][0];
    unsigned char i1 = FACE_VERTS[i][1];
    unsigned char i2 = FACE_VERTS[i][2];
    unsigned char i3 = FACE_VERTS[i][3];

    Vec3 n = FACE_NORMALS[i];

    // Backface cull using world-space normal vs camera position for stability
    // at grazing angles.
    Vec3 faceCenter;
    faceCenter.x = cube.center.x + n.x * hs;
    faceCenter.y = cube.center.y + n.y * hs;
    faceCenter.z = cube.center.z + n.z * hs;
    Vec3 toCam;
    toCam.x = camera_pos.x - faceCenter.x;
    toCam.y = camera_pos.y - faceCenter.y;
    toCam.z = camera_pos.z - faceCenter.z;
    float facing = toCam.x * n.x + toCam.y * n.y + toCam.z * n.z;
    if (facing <= 0.0f)
      continue; // camera is behind the face plane

    // Gather face vertices in camera space.
    Vec3 faceCam[4];
    faceCam[0] = camVerts[i0];
    faceCam[1] = camVerts[i1];
    faceCam[2] = camVerts[i2];
    faceCam[3] = camVerts[i3];

    // Clip against the full frustum to avoid dropping partially visible faces.
    Vec3 tmpA[12];
    Vec3 clipped[12];
    int clippedCount = clipFrustum(faceCam, 4, tmpA, clipped);
    if (clippedCount < 3)
      continue;

    // Project onto screen. No screen clipping; rasterizer clamps bounds.
    int screen[12][2];
    float depths[12];
    int j;
    int minx = (int)WIDTH, miny = (int)HEIGHT;
    int maxx = -1, maxy = -1;
    for (j = 0; j < clippedCount; j++) {
      project_no_clip(clipped[j], &screen[j][0], &screen[j][1]);
      depths[j] = clipped[j].z;
      if (screen[j][0] < minx)
        minx = screen[j][0];
      if (screen[j][0] > maxx)
        maxx = screen[j][0];
      if (screen[j][1] < miny)
        miny = screen[j][1];
      if (screen[j][1] > maxy)
        maxy = screen[j][1];
    }

    // If the whole clipped polygon is off-screen, skip rasterization.
    if (maxx < -SCREEN_GUARD || maxy < -SCREEN_GUARD ||
        minx >= (int)WIDTH + SCREEN_GUARD ||
        miny >= (int)HEIGHT + SCREEN_GUARD) {
      continue;
    }

    // Fog and color.
    float avgDepth = 0.0f;
    for (j = 0; j < clippedCount; j++) {
      avgDepth += depths[j];
    }
    avgDepth /= (float)clippedCount;
    float fog = fog_factor(avgDepth);
    float brightness = face_brightness(n);
    unsigned int baseColor = scale_color(cube.color, brightness);
    unsigned int faceColor = lerp_color(baseColor, FOG_COLOR, fog);

    // Triangle fan to cover the clipped polygon.
    for (j = 1; j < clippedCount - 1; j++) {
      drawFilledTriangle(screen[0][0], screen[0][1], depths[0], screen[j][0],
                         screen[j][1], depths[j], screen[j + 1][0],
                         screen[j + 1][1], depths[j + 1], faceColor);
    }
  }

  // Edge outlines using unclipped projections to keep wireframe visible when
  // close.
  for (i = 0; i < 12; i++) {
    if ((faceMask & EDGE_FACE_BITS[i][0]) == 0 &&
        (faceMask & EDGE_FACE_BITS[i][1]) == 0)
      continue;
    unsigned char a = CUBE_EDGES[i][0];
    unsigned char b = CUBE_EDGES[i][1];
    if (camVerts[a].z <= nearPlane || camVerts[b].z <= nearPlane)
      continue;

    int sa[2], sb[2];
    project_no_clip(camVerts[a], &sa[0], &sa[1]);
    project_no_clip(camVerts[b], &sb[0], &sb[1]);

    if ((sa[0] < -SCREEN_GUARD && sb[0] < -SCREEN_GUARD) ||
        (sa[0] >= (int)WIDTH + SCREEN_GUARD &&
         sb[0] >= (int)WIDTH + SCREEN_GUARD) ||
        (sa[1] < -SCREEN_GUARD && sb[1] < -SCREEN_GUARD) ||
        (sa[1] >= (int)HEIGHT + SCREEN_GUARD &&
         sb[1] >= (int)HEIGHT + SCREEN_GUARD)) {
      continue; // trivially off-screen
    }

    float edgeDepth = (camVerts[a].z + camVerts[b].z) * 0.5f;
    unsigned int edgeColor =
        lerp_color(cube.line_color, FOG_COLOR, fog_factor(edgeDepth));

    drawLineDepth(sa[0], sa[1], camVerts[a].z, sb[0], sb[1], camVerts[b].z,
                  edgeColor);
  }
}

void drawCrosshair() {
  int cx = (int)(WIDTH / 2);
  int cy = (int)(HEIGHT / 2);
  int len = 6;
  unsigned int color = argb_to_rgba(0xffffffff);
  drawLine(cx - len, cy, cx + len, cy, color);
  drawLine(cx, cy - len, cx, cy + len, color);
}

// --------- Maze data ---------
#define MAZE_W 9
#define MAZE_H 9
// # = wall, . = floor
const char MAZE[MAZE_H][MAZE_W + 1] = {
    "#########", "#.....#.#", "#.###.#.#", "#.#...#.#", "#.#.###.#",
    "#.#.#...#", "#.#.#.###", "#...#...#", "#########",
};

#define MAX_WALLS 128
Wall WALLS[MAX_WALLS];
int WALL_COUNT = 0;

void buildMaze() {
  if (WALL_COUNT > 0)
    return;
  int r, c;
  for (r = 0; r < MAZE_H; r++) {
    for (c = 0; c < MAZE_W; c++) {
      if (MAZE[r][c] == '#') {
        // Each cell is 2 units wide.
        float x0 = (float)c * 2.0f;
        float z0 = (float)r * 2.0f;
        if (WALL_COUNT < MAX_WALLS) {
          WALLS[WALL_COUNT].minx = x0;
          WALLS[WALL_COUNT].minz = z0;
          WALLS[WALL_COUNT].maxx = x0 + 2.0f;
          WALLS[WALL_COUNT].maxz = z0 + 2.0f;
          WALLS[WALL_COUNT].height = 1.6f;
          WALLS[WALL_COUNT].color = 0xff475569; // slate gray
          unsigned char mask = FACE_ALL;
          if (r > 0 && MAZE[r - 1][c] == '#')
            mask &= ~FACE_BACK; // neighbor to north
          if (r < MAZE_H - 1 && MAZE[r + 1][c] == '#')
            mask &= ~FACE_FRONT; // neighbor to south
          if (c > 0 && MAZE[r][c - 1] == '#')
            mask &= ~FACE_LEFT; // neighbor to west
          if (c < MAZE_W - 1 && MAZE[r][c + 1] == '#')
            mask &= ~FACE_RIGHT; // neighbor to east
          WALLS[WALL_COUNT].visible_faces = mask;
          WALL_COUNT++;
        }
      }
    }
  }
}

int collides(float x, float z, float y) {
  int i;
  for (i = 0; i < WALL_COUNT; i++) {
    Wall w = WALLS[i];
    if (y > w.height)
      continue;
    if (x > w.minx - player_radius && x < w.maxx + player_radius &&
        z > w.minz - player_radius && z < w.maxz + player_radius) {
      return 1;
    }
  }
  return 0;
}

void updateCamera() {
  float look_sensitivity = 0.0025f;
  float move_speed = 0.08f;
  float sprint_mult = (INPUT_KEYS & 32) ? 1.7f : 1.0f;
  float dt_speed = move_speed * sprint_mult;

  // Mouse right turns right (decrease yaw); mouse up looks up (increase
  // negative movementY -> decrease pitch).
  camera_yaw =
      wrap_angle(camera_yaw - (float)INPUT_MOUSE_DX * look_sensitivity);
  camera_pitch =
      wrap_angle(camera_pitch + (float)INPUT_MOUSE_DY * look_sensitivity);
  if (camera_pitch > 1.4f)
    camera_pitch = 1.4f;
  if (camera_pitch < -1.4f)
    camera_pitch = -1.4f;

  // Cache basis once per frame so we reuse trig for rendering.
  VIEW.cy = approx_cos(camera_yaw);
  VIEW.sy = approx_sin(camera_yaw);
  VIEW.cp = approx_cos(camera_pitch);
  VIEW.sp = approx_sin(camera_pitch);
  VIEW.fwd_x = -VIEW.sy;
  VIEW.fwd_z = VIEW.cy;
  VIEW.right_x = VIEW.cy;
  VIEW.right_z = VIEW.sy;

  float dx = 0.0f;
  float dz = 0.0f;

  if (INPUT_KEYS & 1) { // W
    dx += VIEW.fwd_x * dt_speed;
    dz += VIEW.fwd_z * dt_speed;
  }
  if (INPUT_KEYS & 2) { // S
    dx -= VIEW.fwd_x * dt_speed;
    dz -= VIEW.fwd_z * dt_speed;
  }
  if (INPUT_KEYS & 4) { // A
    dx -= VIEW.right_x * dt_speed;
    dz -= VIEW.right_z * dt_speed;
  }
  if (INPUT_KEYS & 8) { // D
    dx += VIEW.right_x * dt_speed;
    dz += VIEW.right_z * dt_speed;
  }

  float next_x = camera_pos.x + dx;
  float next_z = camera_pos.z + dz;

  // Collision resolve axis by axis.
  if (!collides(next_x, camera_pos.z, player_height + player_y)) {
    camera_pos.x = next_x;
  }
  if (!collides(camera_pos.x, next_z, player_height + player_y)) {
    camera_pos.z = next_z;
  }

  // Jump and gravity.
  const float gravity = -0.015f;
  const float jump_vel = 0.22f;
  if (player_grounded && (INPUT_KEYS & 16)) {
    player_y_vel = jump_vel;
    player_grounded = 0;
  }
  player_y_vel += gravity;
  player_y += player_y_vel;
  if (player_y < 0.0f) {
    player_y = 0.0f;
    player_y_vel = 0.0f;
    player_grounded = 1;
  }
  camera_pos.y = player_height + player_y;

  // Consume mouse deltas.
  INPUT_MOUSE_DX = 0;
  INPUT_MOUSE_DY = 0;
}

void showCanvas() {
  buildMaze();
  updateCamera();
  clearBuffer(0xff111827); // dark background

  int i;
  for (i = 0; i < WALL_COUNT; i++) {
    Wall w = WALLS[i];
    Cube c;
    c.center.x = (w.minx + w.maxx) * 0.5f;
    c.center.y = w.height * 0.5f - 0.1f;
    c.center.z = (w.minz + w.maxz) * 0.5f;
    c.size = (w.maxx - w.minx); // assumes square cell
    c.color = w.color;
    c.line_color = 0xffe2e8f0;
    c.visible_faces = w.visible_faces;
    drawCube(c);
  }

  drawCrosshair();
}
