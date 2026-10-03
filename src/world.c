#include "world.h"
#include "gfx.h"
#include "figure.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define i_type IntV, int
#include <stc/vec.h>


int g_launches, g_wakes, g_secret, g_wardLoop;

// ---------------------------------------------------------------- helpers
static void add_box(Level *L, Vector3 c, Vector3 h, TexId tex, Color tint, float scale, int flags) {
    Box b = { c, h, tex, tint, scale, (uint8_t)flags };
    Boxes_push(&L->boxes, b);
    if (flags & (F_NOCOLLIDE | F_EMIT)) return;
    b3BodyDef bd = b3DefaultBodyDef();
    bd.position = b3v(c);
    b3BodyId body = b3CreateBody(L->phys, &bd);
    b3ShapeDef sd = b3DefaultShapeDef();
    b3BoxHull hull = b3MakeBoxHull(h.x, h.y, h.z);
    b3ShapeId s = b3CreateHullShape(body, &sd, &hull.base);
    b3Shape_SetUserData(s, (void *)(uintptr_t)Boxes_size(&L->boxes)); // index + 1
}

// text scrawled on a wall. axis 0 = wall normal along x, 2 = along z; dir is which way the writing faces
static void add_decal(Level *L, const char *txt, Vector3 p, int axis, int dir, float hh, Color col) {
    int id = gfx_text_tex(txt, col);
    hh *= 1.5f;
    float hw = hh * gfx_text_aspect(id);
    Vector3 c = p, h;
    if (axis == 2) { c.z += dir * 0.03f; h = (Vector3){ hw, hh, 0.001f }; }
    else           { c.x += dir * 0.03f; h = (Vector3){ 0.001f, hh, hw }; }
    Box b = { c, h, (TexId)id, WHITE, (float)dir, F_DECAL | F_NOCOLLIDE };
    Boxes_push(&L->boxes, b);
}
static const Color BLOOD = { 150, 22, 18, 255 };

static void add_prop(Level *L, Vector3 c, Vector3 h, TexId tex, Color tint, float density, float gscale) {
    b3BodyDef bd = b3DefaultBodyDef();
    bd.type = b3_dynamicBody;
    bd.position = b3v(c);
    bd.gravityScale = gscale;
    bd.linearDamping = 0.15f;
    bd.angularDamping = 0.3f;
    b3BodyId body = b3CreateBody(L->phys, &bd);
    b3ShapeDef sd = b3DefaultShapeDef();
    sd.density = density;
    b3BoxHull hull = b3MakeBoxHull(h.x, h.y, h.z);
    b3CreateHullShape(body, &sd, &hull.base);
    Prop p = { body, h, tex, tint };
    Props_push(&L->props, p);
}

static const Color CHALK = { 196, 190, 176, 255 }, SOOT = { 30, 24, 22, 255 };

// ---------------------------------------------------------------- the furniture of a ceremony
static void add_candle(Level *L, Vector3 p, float h, bool black) {
    add_box(L, (Vector3){ p.x, p.y + h / 2, p.z }, (Vector3){ 0.035f, h / 2, 0.035f }, TEX_SKIN, black ? (Color){ 40, 34, 34, 255 } : (Color){ 210, 200, 176, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ p.x, p.y + h + 0.045f, p.z }, (Vector3){ 0.014f, 0.04f, 0.014f }, TEX_CONCRETE, (Color){ 255, 168, 80, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
}
// a cross hung upside down on a wall. axis is the wall normal (0 = x, 2 = z)
static void add_cross(Level *L, Vector3 p, float size, int axis, Color c) {
    Vector3 v = axis == 0 ? (Vector3){ 0.03f, size, 0.05f } : (Vector3){ 0.05f, size, 0.03f };
    Vector3 hb = axis == 0 ? (Vector3){ 0.03f, 0.05f, size * 0.42f } : (Vector3){ size * 0.42f, 0.05f, 0.03f };
    add_box(L, p, v, TEX_WOOD, c, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ p.x, p.y - size * 0.45f, p.z }, hb, TEX_WOOD, c, 1.0f, F_NOCOLLIDE);
}
static void add_sigil(Level *L, Vector3 p, float r, Color c, int seed) {
    Box b = { { p.x, p.y + 0.012f, p.z }, { r, 0.001f, r }, (TexId)gfx_sigil_tex(c, seed), WHITE, 1.0f, F_DECAL | F_NOCOLLIDE };
    Boxes_push(&L->boxes, b);
}
// a plain wooden chair, facing yaw (0 = -Z)
static void add_chair(Level *L, Vector3 p, float yaw) {
    Color c = { 120, 96, 76, 255 };
    bool alongZ = fabsf(sinf(yaw)) < 0.7f;
    float fx = sinf(yaw), fz = -cosf(yaw);
    add_box(L, (Vector3){ p.x, p.y + 0.45f, p.z }, (Vector3){ 0.22f, 0.03f, 0.22f }, TEX_WOOD, c, 1.0f, 0);
    for (int i = 0; i < 4; i++) add_box(L, (Vector3){ p.x + (i & 1 ? 0.18f : -0.18f), p.y + 0.21f, p.z + (i & 2 ? 0.18f : -0.18f) }, (Vector3){ 0.02f, 0.21f, 0.02f }, TEX_WOOD, c, 1.0f, F_NOCOLLIDE);
    Vector3 back = { p.x - fx * 0.2f, p.y + 0.78f, p.z - fz * 0.2f };
    add_box(L, back, alongZ ? (Vector3){ 0.22f, 0.32f, 0.025f } : (Vector3){ 0.025f, 0.32f, 0.22f }, TEX_WOOD, c, 1.0f, F_NOCOLLIDE);
}
static void add_note(Level *L, Vector3 p, int id) {
    Use u = { p, USE_NOTE, id, false };
    Uses_push(&L->uses, u);
}
static void add_effigy(Level *L, FigKind k, Vector3 p, float yaw, float tilt) {
    Effigy e = { (int)k, p, yaw, tilt, 0, false, 0 };
    Effigies_push(&L->effigies, e);
}

static Color scale_tint(Color c, float k) { return (Color){ (unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), 255 }; }
static float frand(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }

const Box *level_box_of_shape(const Level *L, b3ShapeId s) {
    uintptr_t i = (uintptr_t)b3Shape_GetUserData(s);
    return (i > 0 && i <= (uintptr_t)Boxes_size(&L->boxes)) ? &L->boxes.data[i - 1] : NULL;
}

static void room(Level *L, float hx, float hz, float height, TexId wall, Color wc, TexId floor, Color fc, TexId ceil, Color cc) {
    float t = 0.5f;
    add_box(L, (Vector3){ 0, -t, 0 }, (Vector3){ hx + t, t, hz + t }, floor, fc, 2.0f, 0);
    add_box(L, (Vector3){ 0, height + t, 0 }, (Vector3){ hx + t, t, hz + t }, ceil, cc, 3.0f, 0);
    add_box(L, (Vector3){ 0, height / 2, -hz - t }, (Vector3){ hx + t * 2, height / 2 + t, t }, wall, wc, 2.0f, 0);
    add_box(L, (Vector3){ 0, height / 2, hz + t }, (Vector3){ hx + t * 2, height / 2 + t, t }, wall, wc, 2.0f, 0);
    add_box(L, (Vector3){ -hx - t, height / 2, 0 }, (Vector3){ t, height / 2 + t, hz + t * 2 }, wall, wc, 2.0f, 0);
    add_box(L, (Vector3){ hx + t, height / 2, 0 }, (Vector3){ t, height / 2 + t, hz + t * 2 }, wall, wc, 2.0f, 0);
}

static void add_portal(Level *L, Vector3 pos, WorldId to, int needs, Color col, const char *label) {
    Portal p = { pos, 1.3f, to, needs, col, label };
    Portals_push(&L->portals, p);
}

// ---------------------------------------------------------------- HUB: the house
// a bedroom, a hallway with a bathroom and a linen cupboard off it, stairs going down a long way, and a parlour
// set out for a service. the doors to the dreams are closed, and light comes round their edges.
#define WT 0.15f   // half thickness of an interior wall
static void wall_x(Level *L, float x, float z0, float z1, float y0, float y1, TexId t, Color c) {
    if (z1 - z0 > 0.01f && y1 - y0 > 0.01f) add_box(L, (Vector3){ x, (y0 + y1) / 2, (z0 + z1) / 2 }, (Vector3){ WT, (y1 - y0) / 2, (z1 - z0) / 2 }, t, c, 2.0f, 0);
}
static void wall_z(Level *L, float z, float x0, float x1, float y0, float y1, TexId t, Color c) {
    if (x1 - x0 > 0.01f && y1 - y0 > 0.01f) add_box(L, (Vector3){ (x0 + x1) / 2, (y0 + y1) / 2, z }, (Vector3){ (x1 - x0) / 2, (y1 - y0) / 2, WT }, t, c, 2.0f, 0);
}
// a wall with a doorway in it from d0 to d1, open up to dtop
static void wall_x_door(Level *L, float x, float z0, float z1, float y0, float y1, float d0, float d1, float dtop, TexId t, Color c) {
    wall_x(L, x, z0, d0, y0, y1, t, c); wall_x(L, x, d1, z1, y0, y1, t, c); wall_x(L, x, d0, d1, dtop, y1, t, c);
}
static void wall_z_door(Level *L, float z, float x0, float x1, float y0, float y1, float d0, float d1, float dtop, TexId t, Color c) {
    wall_z(L, z, x0, d0, y0, y1, t, c); wall_z(L, z, d1, x1, y0, y1, t, c); wall_z(L, z, d0, d1, dtop, y1, t, c);
}
static void slab(Level *L, float x0, float x1, float z0, float z1, float y, float th, TexId t, Color c, float sc) {
    add_box(L, (Vector3){ (x0 + x1) / 2, y - th / 2, (z0 + z1) / 2 }, (Vector3){ (x1 - x0) / 2, th / 2, (z1 - z0) / 2 }, t, c, sc, 0);
}
// a closed door in a wall. p is the middle of the doorway at floor height; n points into the room you stand in
static void add_door(Level *L, Vector3 p, Vector3 n, Color col, WorldId to, int needs, const char *label) {
    bool xn = fabsf(n.x) > 0.5f;   // wall normal along x
    Vector3 slabH = xn ? (Vector3){ 0.05f, 1.15f, 0.6f } : (Vector3){ 0.6f, 1.15f, 0.05f };
    Vector3 sc = { p.x - n.x * 0.05f, p.y + 1.15f, p.z - n.z * 0.05f };
    add_box(L, sc, slabH, TEX_WOOD, (Color){ 70, 52, 42, 255 }, 1.0f, F_NOCOLLIDE);
    // frame
    Vector3 side = xn ? (Vector3){ 0, 0, 1 } : (Vector3){ 1, 0, 0 };
    for (int s = -1; s <= 1; s += 2) {
        Vector3 c = { p.x + side.x * s * 0.68f + n.x * 0.02f, p.y + 1.2f, p.z + side.z * s * 0.68f + n.z * 0.02f };
        add_box(L, c, xn ? (Vector3){ 0.18f, 1.2f, 0.07f } : (Vector3){ 0.07f, 1.2f, 0.18f }, TEX_WOOD, (Color){ 50, 38, 32, 255 }, 1.0f, F_NOCOLLIDE);
    }
    add_box(L, (Vector3){ p.x + n.x * 0.02f, p.y + 2.37f, p.z + n.z * 0.02f }, xn ? (Vector3){ 0.18f, 0.07f, 0.75f } : (Vector3){ 0.75f, 0.07f, 0.18f }, TEX_WOOD, (Color){ 50, 38, 32, 255 }, 1.0f, F_NOCOLLIDE);
    // the light round the edges: a line under the door, a thin slit at the side, and a little of it on the floor
    add_box(L, (Vector3){ p.x + n.x * 0.01f, p.y + 0.012f, p.z + n.z * 0.01f }, xn ? (Vector3){ 0.03f, 0.01f, 0.58f } : (Vector3){ 0.58f, 0.01f, 0.03f }, TEX_CONCRETE, col, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ p.x + side.x * 0.6f, p.y + 1.15f, p.z + side.z * 0.6f }, xn ? (Vector3){ 0.03f, 1.12f, 0.012f } : (Vector3){ 0.012f, 1.12f, 0.03f }, TEX_CONCRETE, col, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ p.x + n.x * 0.45f, p.y + 0.006f, p.z + n.z * 0.45f }, xn ? (Vector3){ 0.4f, 0.003f, 0.55f } : (Vector3){ 0.55f, 0.003f, 0.4f }, TEX_CONCRETE, scale_tint(col, 0.22f), 1.0f, F_EMIT | F_NOCOLLIDE);
    add_portal(L, (Vector3){ p.x + n.x * 0.55f, p.y, p.z + n.z * 0.55f }, to, needs, col, label);
    L->portals.data[L->portals.size - 1].radius = 0.75f;
}
// a painting on a wall: a frame, and a little landscape in it (a sky, a field, a line of trees)
static void add_portrait(Level *L, Vector3 p, Vector3 n, float w, float h) {
    bool xn = fabsf(n.x) > 0.5f;
    add_box(L, (Vector3){ p.x + n.x * 0.03f, p.y, p.z + n.z * 0.03f }, xn ? (Vector3){ 0.03f, h, w } : (Vector3){ w, h, 0.03f }, TEX_WOOD, (Color){ 150, 112, 60, 255 }, 1.0f, F_NOCOLLIDE);
    Vector3 cc = { p.x + n.x * 0.065f, p.y, p.z + n.z * 0.065f };
    float iw = w - 0.06f, ih = h - 0.06f;
    add_box(L, (Vector3){ cc.x, p.y + ih * 0.4f, cc.z }, xn ? (Vector3){ 0.01f, ih * 0.6f, iw } : (Vector3){ iw, ih * 0.6f, 0.01f }, TEX_PAPER, (Color){ 150, 176, 200, 255 }, 3.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ cc.x, p.y - ih * 0.6f, cc.z }, xn ? (Vector3){ 0.01f, ih * 0.4f, iw } : (Vector3){ iw, ih * 0.4f, 0.01f }, TEX_GRASS, (Color){ 230, 220, 140, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ cc.x + n.x * 0.006f, p.y - ih * 0.15f, cc.z + n.z * 0.006f }, xn ? (Vector3){ 0.004f, ih * 0.08f, iw * 0.9f } : (Vector3){ iw * 0.9f, ih * 0.08f, 0.004f }, TEX_GRASS, (Color){ 160, 200, 140, 255 }, 0.5f, F_NOCOLLIDE);
}
static void add_plant(Level *L, Vector3 p) {
    add_box(L, (Vector3){ p.x, p.y + 0.22f, p.z }, (Vector3){ 0.18f, 0.22f, 0.18f }, TEX_CONCRETE, (Color){ 170, 100, 70, 255 }, 1.0f, 0);
    for (int k = 0; k < 5; k++) {
        float a = k * 1.2566f;
        add_box(L, (Vector3){ p.x + sinf(a) * 0.15f, p.y + 0.6f + (k % 2) * 0.2f, p.z + cosf(a) * 0.15f }, (Vector3){ 0.12f, 0.2f, 0.12f }, TEX_GRASS, (Color){ 200, 240, 170, 255 }, 0.5f, F_NOCOLLIDE);
    }
}
static void add_lamp(Level *L, Vector3 p) {   // a standard lamp with a warm shade
    add_box(L, (Vector3){ p.x, p.y + 0.75f, p.z }, (Vector3){ 0.025f, 0.75f, 0.025f }, TEX_WOOD, (Color){ 90, 70, 50, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ p.x, p.y + 1.6f, p.z }, (Vector3){ 0.2f, 0.15f, 0.2f }, TEX_CONCRETE, (Color){ 255, 200, 130, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
}

// the house is the one place nothing follows you into. it is warm, and quiet, and the doors lead out of it
static void build_hub(Level *L, int seed) {
    L->name = "THE HOUSE";
    L->fog = (Color){ 30, 24, 20, 255 };
    L->fogDensity = 0.05f;
    L->light = 0.85f;
    L->gradeLo = (Color){ 126, 124, 136, 255 }; L->gradeHi = (Color){ 150, 132, 112, 255 };
    L->moteCol = (Color){ 190, 170, 130, 255 };
    L->killY = -50;
    SetRandomSeed(seed * 31 + 5);
    const Color PAPER = { 140, 128, 112, 255 }, WOODF = { 150, 130, 112, 255 }, CEIL = { 120, 114, 104, 255 };
    const float BH = 3.0f, HH = 2.7f, PF = -3.0f, PH = 4.6f;

    // ---- the bedroom: x -4..4, z -3.5..3.5
    slab(L, -4, 4, -3.5f, 3.5f, 0, 1, TEX_WOOD, WOODF, 2.0f);
    slab(L, -4, 4, -3.5f, 3.5f, BH + 1, 1, TEX_PAPER, CEIL, 3.0f);
    wall_x(L, -4, -3.6f, 3.6f, 0, BH, TEX_PAPER, PAPER);
    wall_z(L, -3.5f, -4, 4, 0, BH, TEX_PAPER, PAPER);
    wall_z(L, 3.5f, -4, 4, 0, BH, TEX_PAPER, PAPER);
    wall_x_door(L, 4, -3.6f, 3.6f, 0, BH, -0.6f, 0.6f, 2.25f, TEX_PAPER, PAPER);
    int bedSide = GetRandomValue(0, 1);
    float bz = bedSide ? -2.2f : 2.2f, bx = -2.9f;
    add_box(L, (Vector3){ bx, 0.35f, bz }, (Vector3){ 0.95f, 0.35f, 1.2f }, TEX_WOOD, (Color){ 160, 126, 100, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ bx, 0.78f, bz }, (Vector3){ 0.88f, 0.1f, 1.1f }, TEX_CLOTH, (Color){ 220, 200, 170, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ bx + 0.35f, 0.9f, bz }, (Vector3){ 0.5f, 0.04f, 1.12f }, TEX_PAPER, (Color){ 200, 120, 110, 255 }, 1.0f, F_NOCOLLIDE);   // a quilt
    add_box(L, (Vector3){ -3.75f, 0.9f, bz }, (Vector3){ 0.12f, 0.9f, 1.0f }, TEX_WOOD, (Color){ 130, 100, 74, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ -3.35f, 0.95f, bz }, (Vector3){ 0.25f, 0.1f, 0.5f }, TEX_CLOTH, (Color){ 240, 234, 220, 255 }, 1.0f, F_NOCOLLIDE);
    L->wakePos = (Vector3){ -1.5f, 0.05f, bz }; L->wakeYaw = 90;
    {   // a desk by the bed with a reading lamp and a few books
        float dz = -bz * 1.25f;
        add_box(L, (Vector3){ -1.0f, 0.45f, dz }, (Vector3){ 1.0f, 0.45f, 0.45f }, TEX_WOOD, (Color){ 150, 116, 90, 255 }, 1.0f, 0);
        add_box(L, (Vector3){ -1.5f, 1.0f, dz }, (Vector3){ 0.03f, 0.1f, 0.03f }, TEX_RUST, (Color){ 120, 100, 70, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ -1.5f, 1.16f, dz }, (Vector3){ 0.14f, 0.08f, 0.14f }, TEX_CONCRETE, (Color){ 255, 210, 150, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        static const Color BOOK[4] = { { 140, 60, 50, 255 }, { 60, 90, 120, 255 }, { 90, 110, 60, 255 }, { 150, 120, 60, 255 } };
        for (int i = 0; i < 4; i++) add_box(L, (Vector3){ -0.3f + i * 0.09f, 1.03f, dz }, (Vector3){ 0.04f, 0.13f, 0.12f }, TEX_PAPER, BOOK[i], 1.0f, F_NOCOLLIDE);
    }
    add_box(L, (Vector3){ 3.2f, 1.1f, -bz * 1.3f }, (Vector3){ 0.6f, 1.1f, 0.35f }, TEX_WOOD, (Color){ 120, 92, 70, 255 }, 1.0f, 0);   // a wardrobe
    // a window, and moonlight coming through it onto the floor
    float wz = bz < 0 ? 3.5f - WT : -3.5f + WT, wn = bz < 0 ? -1.0f : 1.0f;
    add_box(L, (Vector3){ 1.6f, 1.8f, wz + wn * 0.02f }, (Vector3){ 0.85f, 0.75f, 0.04f }, TEX_WOOD, (Color){ 220, 210, 190, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 1.6f, 1.8f, wz + wn * 0.05f }, (Vector3){ 0.72f, 0.62f, 0.01f }, TEX_CONCRETE, (Color){ 60, 80, 120, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ 1.6f, 1.8f, wz + wn * 0.07f }, (Vector3){ 0.03f, 0.64f, 0.01f }, TEX_WOOD, (Color){ 220, 210, 190, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 1.6f, 1.8f, wz + wn * 0.07f }, (Vector3){ 0.74f, 0.03f, 0.01f }, TEX_WOOD, (Color){ 220, 210, 190, 255 }, 1.0f, F_NOCOLLIDE);
    for (int s = -1; s <= 1; s += 2) add_box(L, (Vector3){ 1.6f + s * 1.0f, 1.7f, wz + wn * 0.08f }, (Vector3){ 0.2f, 0.95f, 0.02f }, TEX_CLOTH, (Color){ 120, 150, 140, 255 }, 1.0f, F_NOCOLLIDE);   // curtains
    add_box(L, (Vector3){ 1.6f, 0.01f, wz + wn * 1.4f }, (Vector3){ 0.7f, 0.004f, 1.2f }, TEX_CONCRETE, (Color){ 40, 50, 70, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ 0, BH - 0.4f, 0 }, (Vector3){ 0.01f, 0.4f, 0.01f }, TEX_CONCRETE, (Color){ 20, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 0, BH - 0.86f, 0 }, (Vector3){ 0.14f, 0.1f, 0.14f }, TEX_CONCRETE, (Color){ 255, 214, 150, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ 0.4f, 0.008f, 0 }, (Vector3){ 1.8f, 0.004f, 2.2f }, TEX_MOSAIC, (Color){ 90, 66, 60, 255 }, 1.4f, F_NOCOLLIDE);
    add_plant(L, (Vector3){ 3.4f, 0, bz * 1.3f });
    add_portrait(L, (Vector3){ -4 + WT, 1.9f, -bz }, (Vector3){ 1, 0, 0 }, 0.5f, 0.4f);

    // ---- the hallway: x 4..20, z -1.1..1.1
    slab(L, 4, 20, -1.1f, 1.1f, 0, 1, TEX_WOOD, (Color){ 150, 126, 104, 255 }, 2.0f);
    slab(L, 4, 20, -1.1f, 1.1f, HH + 1, 1, TEX_PAPER, CEIL, 3.0f);
    add_box(L, (Vector3){ 12, 0.006f, 0 }, (Vector3){ 8, 0.004f, 0.55f }, TEX_MOSAIC, (Color){ 84, 56, 50, 255 }, 1.0f, F_NOCOLLIDE);
    wall_z_door(L, -1.1f, 4, 20, 0, HH, 9.4f, 10.6f, 2.25f, TEX_PAPER, PAPER);
    wall_z_door(L, 1.1f, 4, 20, 0, HH, 14.4f, 15.6f, 2.25f, TEX_PAPER, PAPER);
    wall_x(L, 4, -1.2f, 1.2f, BH, BH + 0.01f, TEX_PAPER, PAPER);
    for (int i = 0; i < 3; i++) {
        float x = 6.5f + i * 5.5f;
        add_box(L, (Vector3){ x, HH - 0.2f, 0 }, (Vector3){ 0.01f, 0.2f, 0.01f }, TEX_CONCRETE, (Color){ 20, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ x, HH - 0.45f, 0 }, (Vector3){ 0.1f, 0.09f, 0.1f }, TEX_CONCRETE, (Color){ 250, 210, 150, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    }
    add_portrait(L, (Vector3){ 6.0f, 1.6f, -1.1f + WT }, (Vector3){ 0, 0, 1 }, 0.35f, 0.45f);
    add_portrait(L, (Vector3){ 12.5f, 1.7f, -1.1f + WT }, (Vector3){ 0, 0, 1 }, 0.5f, 0.35f);
    add_portrait(L, (Vector3){ 7.5f, 1.6f, 1.1f - WT }, (Vector3){ 0, 0, -1 }, 0.3f, 0.4f);
    add_portrait(L, (Vector3){ 11.5f, 1.6f, 1.1f - WT }, (Vector3){ 0, 0, -1 }, 0.3f, 0.4f);
    add_portrait(L, (Vector3){ 18.0f, 1.65f, 1.1f - WT }, (Vector3){ 0, 0, -1 }, 0.42f, 0.5f);
    add_box(L, (Vector3){ 17.0f, 1.05f, -0.82f }, (Vector3){ 0.28f, 1.05f, 0.14f }, TEX_WOOD, (Color){ 120, 84, 56, 255 }, 1.0f, 0);   // a long-case clock
    add_box(L, (Vector3){ 17.0f, 1.72f, -0.67f }, (Vector3){ 0.18f, 0.18f, 0.01f }, TEX_SKIN, (Color){ 230, 220, 190, 255 }, 1.0f, F_NOCOLLIDE);
    add_plant(L, (Vector3){ 19.3f, 0, 0.7f });

    // ---- the bathroom: x 8..12, z -5..-1.1
    const Color BATH = { 120, 140, 150, 255 };
    slab(L, 8, 12, -5, -1.1f, 0, 1, TEX_MOSAIC, (Color){ 170, 170, 180, 255 }, 0.8f);
    slab(L, 8, 12, -5, -1.1f, HH + 1, 1, TEX_PAPER, CEIL, 3.0f);
    wall_x(L, 8, -5.1f, -1.1f, 0, HH, TEX_PAPER, BATH);
    wall_z(L, -5, 8, 12, 0, HH, TEX_PAPER, BATH);
    wall_x_door(L, 12, -5.1f, -1.1f, 0, HH, -3.7f, -2.5f, 2.3f, TEX_PAPER, BATH);
    {
        Color tub = { 230, 228, 220, 255 };
        add_box(L, (Vector3){ 9.4f, 0.3f, -4.79f }, (Vector3){ 1.2f, 0.3f, 0.06f }, TEX_SKIN, tub, 1.0f, 0);
        add_box(L, (Vector3){ 9.4f, 0.3f, -3.95f }, (Vector3){ 1.2f, 0.3f, 0.06f }, TEX_SKIN, tub, 1.0f, 0);
        add_box(L, (Vector3){ 8.26f, 0.3f, -4.4f }, (Vector3){ 0.06f, 0.3f, 0.45f }, TEX_SKIN, tub, 1.0f, 0);
        add_box(L, (Vector3){ 10.54f, 0.3f, -4.4f }, (Vector3){ 0.06f, 0.3f, 0.45f }, TEX_SKIN, tub, 1.0f, 0);
        add_box(L, (Vector3){ 9.4f, 0.45f, -4.4f }, (Vector3){ 1.1f, 0.01f, 0.39f }, TEX_WATER, (Color){ 150, 190, 210, 255 }, 1.0f, 0);
        add_box(L, (Vector3){ 8.3f, 0.9f, -2.0f }, (Vector3){ 0.18f, 0.08f, 0.3f }, TEX_SKIN, tub, 1.0f, 0);
        add_box(L, (Vector3){ 8 + WT + 0.02f, 1.6f, -2.0f }, (Vector3){ 0.02f, 0.4f, 0.32f }, TEX_WATER, (Color){ 150, 170, 190, 255 }, 1.0f, F_NOCOLLIDE);
    }
    add_door(L, (Vector3){ 12 - WT, 0, -3.1f }, (Vector3){ -1, 0, 0 }, (Color){ 120, 200, 120, 255 }, W_BATHS, 0, "THE BATHS");

    // ---- the linen cupboard: x 14..16, z 1.1..3.4
    slab(L, 14, 16, 1.1f, 3.4f, 0, 1, TEX_WOOD, WOODF, 2.0f);
    slab(L, 14, 16, 1.1f, 3.4f, HH + 1, 1, TEX_PAPER, CEIL, 3.0f);
    wall_x(L, 14, 1.1f, 3.4f, 0, HH, TEX_WOOD, (Color){ 150, 120, 90, 255 });
    wall_x(L, 16, 1.1f, 3.4f, 0, HH, TEX_WOOD, (Color){ 150, 120, 90, 255 });
    wall_z(L, 3.4f, 13.9f, 16.1f, 0, HH, TEX_WOOD, (Color){ 150, 120, 90, 255 });
    for (int i = 0; i < 4; i++) {
        add_box(L, (Vector3){ 14.3f, 0.5f + i * 0.5f, 2.0f }, (Vector3){ 0.15f, 0.02f, 0.6f }, TEX_WOOD, (Color){ 150, 120, 90, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ 14.3f, 0.6f + i * 0.5f, 2.0f + frand(-0.3f, 0.3f) }, (Vector3){ 0.13f, 0.08f, 0.2f }, TEX_CLOTH, (Color){ 230, 220, 200, 255 }, 1.0f, F_NOCOLLIDE);
    }
    add_door(L, (Vector3){ 15.0f, 0, 3.4f - WT }, (Vector3){ 0, 0, -1 }, (Color){ 230, 130, 60, 255 }, W_SHAFT, 0, "THE SHAFT");

    // ---- the stairs down to the sitting room
    slab(L, 20, 26, -1.1f, 1.1f, HH + 1, 1, TEX_PAPER, CEIL, 3.0f);
    wall_z(L, -1.1f, 20, 26, PF, HH, TEX_PAPER, PAPER);
    wall_z(L, 1.1f, 20, 26, PF, HH, TEX_PAPER, PAPER);
    for (int k = 0; k < 20; k++) {
        float top = -0.15f * (k + 1);
        if (top <= PF + 0.01f) break;
        add_box(L, (Vector3){ 20 + 0.3f * k + 0.15f, (top + PF) / 2, 0 }, (Vector3){ 0.15f, (top - PF) / 2, 1.1f }, TEX_WOOD, (Color){ 150, 120, 96, 255 }, 1.0f, 0);
    }
    add_box(L, (Vector3){ 23, 1.0f, -0.98f }, (Vector3){ 3, 0.03f, 0.03f }, TEX_WOOD, (Color){ 120, 90, 66, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 23, 1.4f, 0 }, (Vector3){ 0.1f, 0.08f, 0.1f }, TEX_CONCRETE, (Color){ 250, 210, 150, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);

    // ---- the sitting room: x 26..38, z -6..6, three metres down. a fire, armchairs, books
    slab(L, 26, 38, -6, 6, PF, 1, TEX_WOOD, (Color){ 150, 120, 96, 255 }, 2.0f);
    slab(L, 26, 38, -6, 6, PF + PH + 1, 1, TEX_PAPER, CEIL, 3.0f);
    const Color PW = { 136, 124, 106, 255 };
    wall_x_door(L, 26, -6.1f, 6.1f, PF, HH, -1.1f, 1.1f, PF + PH, TEX_PAPER, PW);
    wall_x_door(L, 38, -6.1f, 6.1f, PF, PF + PH, -0.6f, 0.6f, PF + 2.25f, TEX_PAPER, PW);
    wall_z(L, -6, 26, 29.4f, PF, PF + PH, TEX_PAPER, PW); wall_z(L, -6, 30.6f, 34.4f, PF, PF + PH, TEX_PAPER, PW); wall_z(L, -6, 35.6f, 38, PF, PF + PH, TEX_PAPER, PW);
    wall_z(L, -6, 29.4f, 30.6f, PF + 2.25f, PF + PH, TEX_PAPER, PW); wall_z(L, -6, 34.4f, 35.6f, PF + 2.25f, PF + PH, TEX_PAPER, PW);
    wall_z_door(L, 6, 26, 38, PF, PF + PH, 29.4f, 30.6f, PF + 2.25f, TEX_PAPER, PW);
    add_door(L, (Vector3){ 30, PF, -6 + WT }, (Vector3){ 0, 0, 1 }, (Color){ 170, 110, 240, 255 }, W_VOID, 0, "THE STEPS");
    add_door(L, (Vector3){ 35, PF, -6 + WT }, (Vector3){ 0, 0, 1 }, (Color){ 200, 40, 30, 255 }, W_CHAPEL, 0, "THE LOWER CHURCH");
    add_door(L, (Vector3){ 30, PF, 6 - WT }, (Vector3){ 0, 0, -1 }, (Color){ 200, 190, 170, 255 }, W_NURSERY, 0, "THE NURSERY");
    add_door(L, (Vector3){ 38 - WT, PF, 0 }, (Vector3){ -1, 0, 0 }, (Color){ 255, 250, 240, 255 }, W_END, FX_ALL, "THE WAY OUT");
    {   // the fireplace on the west wall, and two armchairs drawn up to it
        float fz = -3.6f;
        add_box(L, (Vector3){ 26.35f, PF + 0.7f, fz }, (Vector3){ 0.25f, 0.7f, 1.1f }, TEX_CONCRETE, (Color){ 150, 110, 90, 255 }, 1.0f, 0);
        add_box(L, (Vector3){ 26.62f, PF + 0.4f, fz }, (Vector3){ 0.02f, 0.35f, 0.6f }, TEX_CONCRETE, (Color){ 10, 8, 6, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ 26.64f, PF + 0.18f, fz }, (Vector3){ 0.03f, 0.12f, 0.45f }, TEX_CONCRETE, (Color){ 255, 140, 50, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        add_box(L, (Vector3){ 27.3f, PF + 0.006f, fz }, (Vector3){ 0.7f, 0.004f, 1.0f }, TEX_CONCRETE, (Color){ 60, 34, 16, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        add_box(L, (Vector3){ 26.45f, PF + 1.45f, fz }, (Vector3){ 0.25f, 0.05f, 1.25f }, TEX_WOOD, (Color){ 120, 84, 56, 255 }, 1.0f, F_NOCOLLIDE);   // mantel
        add_portrait(L, (Vector3){ 26 + WT, PF + 2.3f, fz }, (Vector3){ 1, 0, 0 }, 0.6f, 0.45f);
        for (int s = -1; s <= 1; s += 2) {
            Vector3 c = { 28.6f, PF, fz + s * 1.2f };
            Color up = { 140, 60, 50, 255 };
            add_box(L, (Vector3){ c.x, c.y + 0.25f, c.z }, (Vector3){ 0.42f, 0.25f, 0.42f }, TEX_CLOTH, up, 1.0f, 0);
            add_box(L, (Vector3){ c.x + 0.36f, c.y + 0.75f, c.z }, (Vector3){ 0.08f, 0.5f, 0.42f }, TEX_CLOTH, up, 1.0f, 0);
            for (int t = -1; t <= 1; t += 2) add_box(L, (Vector3){ c.x, c.y + 0.6f, c.z + t * 0.38f }, (Vector3){ 0.42f, 0.1f, 0.06f }, TEX_CLOTH, up, 1.0f, 0);
        }
        add_box(L, (Vector3){ 27.6f, PF + 0.2f, fz }, (Vector3){ 0.3f, 0.2f, 0.4f }, TEX_WOOD, (Color){ 120, 84, 56, 255 }, 1.0f, 0);
    }
    // bookshelves along the south wall, and the piano
    static const Color BOOK[5] = { { 140, 60, 50, 255 }, { 60, 90, 120, 255 }, { 90, 110, 60, 255 }, { 150, 120, 60, 255 }, { 110, 70, 100, 255 } };
    for (int b = 0; b < 2; b++) {
        float x = 33.0f + b * 2.0f;
        add_box(L, (Vector3){ x, PF + 1.1f, 5.65f }, (Vector3){ 0.85f, 1.1f, 0.2f }, TEX_WOOD, (Color){ 110, 78, 54, 255 }, 1.0f, 0);
        for (int r = 0; r < 4; r++) for (int i = 0; i < 9; i++)
            add_box(L, (Vector3){ x - 0.72f + i * 0.17f, PF + 0.35f + r * 0.5f, 5.43f }, (Vector3){ 0.06f, 0.16f + 0.03f * ((i * 7 + r) % 3), 0.02f }, TEX_PAPER, BOOK[(i + r * 2 + b) % 5], 1.0f, F_NOCOLLIDE);
    }
    add_box(L, (Vector3){ 36.4f, PF + 0.65f, -5.3f }, (Vector3){ 1.1f, 0.65f, 0.4f }, TEX_WOOD, (Color){ 60, 40, 32, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 36.4f, PF + 0.74f, -4.88f }, (Vector3){ 0.95f, 0.02f, 0.06f }, TEX_SKIN, (Color){ 240, 234, 220, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 32.5f, PF + 0.006f, 0 }, (Vector3){ 3.0f, 0.004f, 3.6f }, TEX_MOSAIC, (Color){ 90, 64, 58, 255 }, 1.4f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 32.5f, PF + 0.25f, 0 }, (Vector3){ 0.6f, 0.03f, 0.9f }, TEX_WOOD, (Color){ 120, 84, 56, 255 }, 1.0f, 0);   // a low table
    add_box(L, (Vector3){ 32.5f, PF + 0.12f, 0 }, (Vector3){ 0.5f, 0.12f, 0.8f }, TEX_WOOD, (Color){ 100, 70, 50, 255 }, 1.0f, F_NOCOLLIDE);
    add_lamp(L, (Vector3){ 27.6f, PF, -5.3f }); add_lamp(L, (Vector3){ 37.3f, PF, 4.8f }); add_lamp(L, (Vector3){ 37.3f, PF, -3.6f });
    add_plant(L, (Vector3){ 37.4f, PF, 2.0f }); add_plant(L, (Vector3){ 27.0f, PF, 5.3f });
    add_portrait(L, (Vector3){ 32.5f, PF + 2.4f, -6 + WT }, (Vector3){ 0, 0, 1 }, 0.6f, 0.8f);
    add_box(L, (Vector3){ 32, PF + PH - 0.5f, 0 }, (Vector3){ 0.25f, 0.12f, 0.25f }, TEX_CONCRETE, (Color){ 255, 214, 150, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);

    L->spawn = (Vector3){ 0.5f, 0.05f, 0 }; L->spawnYaw = 270;
    for (int i = 0; i < 4; i++) {   // a few wooden toy blocks on the bedroom floor
        float s = frand(0.12f, 0.2f);
        add_prop(L, (Vector3){ frand(1.0f, 3.0f), s + 0.05f + i * 0.02f, frand(-1.5f, 1.5f) }, (Vector3){ s, s, s }, TEX_WOOD, (Color){ 220, 190, 150, 255 }, 250, 1.0f);
    }
}

// ---------------------------------------------------------------- SHAFT: the climb
static float g_W = 6.0f;   // half width of the shaft; differs every dream

static Vector3 wall_pt(int w, float s, float y, float inset) {
    const float W = g_W;
    switch (((w % 4) + 4) % 4) {
    case 0:  return (Vector3){ -W + s, y, -W + inset };
    case 1:  return (Vector3){ W - inset, y, -W + s };
    case 2:  return (Vector3){ W - s, y, W - inset };
    default: return (Vector3){ -W + inset, y, W - s };
    }
}
// box sitting on wall w, covering s in [s0,s1], y in [y0,y1], protruding depth d from the wall surface
static void wall_box(Level *L, int w, float s0, float s1, float y0, float y1, float d, TexId tex, Color tint, int flags) {
    Vector3 c = wall_pt(w, (s0 + s1) / 2, (y0 + y1) / 2, d / 2);
    Vector3 h = (w % 2 == 0) ? (Vector3){ (s1 - s0) / 2, (y1 - y0) / 2, d / 2 } : (Vector3){ d / 2, (y1 - y0) / 2, (s1 - s0) / 2 };
    add_box(L, c, h, tex, tint, 2.0f, flags);
}

static void build_shaft(Level *L, int seed) {
    L->ambient = 0.3f;
    L->name = "THE SHAFT";
    SetRandomSeed(seed * 2654435 + 11);
    // each descent is a different shaft: width, mood, number and spacing of levels
    static const Color FOG[3] = { { 26, 14, 8, 255 }, { 8, 18, 18, 255 }, { 22, 10, 22, 255 } };
    static const Color WALL[3] = { { 120, 112, 100, 255 }, { 90, 120, 118, 255 }, { 130, 100, 122, 255 } };
    int mood = GetRandomValue(0, 2);
    L->fog = FOG[mood];
    L->fogDensity = frand(0.028f, 0.045f);
    L->light = 0.95f;
    L->killY = -60;
    L->gradeLo = (Color){ 126, 118, 138, 255 }; L->gradeHi = (Color){ 160, 130, 104, 255 };
    L->moteCol = (Color){ 190, 120, 70, 255 };
    L->sludge = true;
    L->sludgeY = -3;
    L->sludgeSpeed = frand(0.30f, 0.42f);
    const float W = g_W = frand(5.0f, 7.2f);
    L->shaftW = W;
    const int LEVELS = GetRandomValue(9, 13);
    float yk[24];
    yk[0] = 0;
    for (int k = 1; k <= LEVELS; k++) yk[k] = yk[k - 1] + frand(3.9f, 5.1f);
    const float TOP = yk[LEVELS] + 70;
    const float sMid = W + 0.5f, sEnd = 2 * W;

    // floor + four tall walls (smooth slimy concrete: you can't grip these)
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ W + 1, 0.5f, W + 1 }, TEX_TILE, (Color){ 150, 150, 130, 255 }, 2.0f, 0);
    Color wc = WALL[mood];
    TexId wt = (mood == 2) ? TEX_FLESH : TEX_CONCRETE;
    add_box(L, (Vector3){ 0, TOP / 2 - 10, -W - 0.5f }, (Vector3){ W + 1, TOP / 2 + 10, 0.5f }, wt, wc, 2.0f, 0);
    add_box(L, (Vector3){ 0, TOP / 2 - 10, W + 0.5f }, (Vector3){ W + 1, TOP / 2 + 10, 0.5f }, wt, wc, 2.0f, 0);
    add_box(L, (Vector3){ -W - 0.5f, TOP / 2 - 10, 0 }, (Vector3){ 0.5f, TOP / 2 + 10, W + 1 }, wt, wc, 2.0f, 0);
    add_box(L, (Vector3){ W + 0.5f, TOP / 2 - 10, 0 }, (Vector3){ 0.5f, TOP / 2 + 10, W + 1 }, wt, wc, 2.0f, 0);
    add_box(L, (Vector3){ 0, TOP + 0.5f, 0 }, (Vector3){ W + 1, 0.5f, W + 1 }, TEX_CONCRETE, (Color){ 60, 60, 60, 255 }, 3.0f, 0);

    // pipes running up the walls, so the shaft isn't four flat planes
    for (int w = 0; w < 4; w++) {
        int n = GetRandomValue(1, 3);
        for (int i = 0; i < n; i++) {
            float s = frand(0.6f, 2 * W - 0.6f), r = frand(0.1f, 0.22f);
            Vector3 c = wall_pt(w, s, TOP / 2 - 5, r);
            add_box(L, c, (Vector3){ r, TOP / 2, r }, TEX_RUST, (Color){ 120, 110, 100, 255 }, 1.0f, F_NOCOLLIDE);
        }
    }

    Color rust = (Color){ 235, 205, 180, 255 };
    for (int k = 1; k <= LEVELS; k++) {
        float yp = yk[k - 1], y = yk[k];
        int w = k % 4;
        float depth = frand(2.2f, 3.2f);
        // shelf on the right half of wall w
        wall_box(L, w, sMid, sEnd, y - 0.4f, y, depth, TEX_CONCRETE, (Color){ 150, 140, 120, 255 }, 0);
        wall_box(L, w, sMid, sEnd, y - 0.4f, y + 0.02f, 0.2f, TEX_RUST, (Color){ 110, 100, 90, 255 }, F_NOCOLLIDE);
        // a work lamp on some of them
        if (GetRandomValue(0, 2) == 0)
            wall_box(L, w, sMid + 0.6f, sMid + 1.0f, y + 1.8f, y + 2.1f, 0.3f, TEX_CONCRETE, (Color){ 255, 170, 70, 255 }, F_EMIT | F_NOCOLLIDE);
        // grip strip on the left of wall w, up from the previous shelf, with 0-2 slick bands you have to lunge across
        float y0 = yp + 0.6f, y1 = y + 3.0f;
        int bands = (k >= 2) ? GetRandomValue(0, 2) : 0;
        float cur = y0, span = y1 - y0;
        for (int b = 0; b < bands; b++) {
            float c = y0 + span * (0.25f + 0.5f * (b + frand(0.2f, 0.8f)) / bands), hb = frand(0.55f, 0.78f);
            if (c - hb < cur + 1.0f) continue;
            wall_box(L, w, 0.0f, sMid, cur, c - hb, 0.25f, TEX_RUST, rust, F_GRIP);
            static const int ax[4] = { 2, 0, 2, 0 }, dr[4] = { 1, -1, -1, 1 };
            if (b == 0) add_decal(L, "LET GO", wall_pt(w, sMid / 2, c, 0.0f), ax[w], dr[w], 0.22f, CHALK);
            cur = c + hb;
        }
        wall_box(L, w, 0.0f, sMid, cur, y1, 0.25f, TEX_RUST, rust, F_GRIP);
    }
    float top = yk[LEVELS], STEP = 4.5f;
    // people hung down the middle of the shaft on long ropes, turning slowly
    for (int i = 0; i < 6; i++) {
        float hx = frand(-W + 1.8f, W - 1.8f), hz = frand(-W + 1.8f, W - 1.8f), hy = frand(9, top - 4);
        add_box(L, (Vector3){ hx, (hy + TOP) / 2, hz }, (Vector3){ 0.02f, (TOP - hy) / 2, 0.02f }, TEX_CLOTH, (Color){ 60, 50, 40, 255 }, 1.0f, F_NOCOLLIDE);
        add_effigy(L, FIG_COCOON, (Vector3){ hx, hy, hz }, frand(0, 6.28f), 0);
    }
    // and faces in the walls, here and there, that watch you climb past
    static const Vector3 INW[4] = { { 0, 0, 1 }, { -1, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 } };
    for (int i = 0; i < 10; i++) {
        int fw = GetRandomValue(0, 3);
        Vector3 fp = wall_pt(fw, frand(0.8f, 2 * W - 0.8f), frand(3, top), 0.08f);
        add_effigy(L, FIG_FACE, fp, atan2f(INW[fw].x, -INW[fw].z), 0);
    }
    // the very top: a landing with the effect on a plinth
    int w = (LEVELS + 1) % 4;
    wall_box(L, w, 0.0f, sEnd, top + STEP - 0.4f, top + STEP, 4.0f, TEX_FLESH, (Color){ 190, 160, 160, 255 }, 0);
    {   // at the top, a plain door in the wall, and rain falling somewhere behind it
        static const Vector3 IN[4] = { { 0, 0, 1 }, { -1, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 } };
        Vector3 dp = wall_pt(w, 2 * W - 1.2f, top + STEP, 0.0f);
        add_door(L, dp, IN[w], (Color){ 150, 170, 200, 255 }, W_CITY, 0, "THE CITY");
    }
    Vector3 pp = wall_pt(w, W, top + STEP + 0.5f, 1.8f);
    Pickup pk = { pp, FX_GLOVES, false, false };
    Pickups_push(&L->pickups, pk);
    wall_box(L, w, 0.0f, sMid, top + 0.6f, top + STEP + 3.0f, 0.25f, TEX_RUST, rust, F_GRIP);
    add_decal(L, "HE IS BELOW", wall_pt((w + 1) % 4, W, top + STEP + 2.5f, 0.0f), (((w + 1) % 4) % 2 == 0) ? 2 : 0,
              ((w + 1) % 4) == 0 ? 1 : ((w + 1) % 4) == 1 ? -1 : ((w + 1) % 4) == 2 ? -1 : 1, 0.3f, BLOOD);
    // spawn facing the first grip strip (wall 1, +X)
    L->spawn = (Vector3){ W - 2.0f, 0.05f, -W / 2 };
    add_note(L, (Vector3){ W - 2.6f, 0.01f, -W / 2 + 1.0f }, NOTE_CLIMB);
    L->spawnYaw = 90;
}

// ---------------------------------------------------------------- BATHS: the public baths, underground, long closed
// tiled halls round a deep pool that nobody has drained in years. showers, a corridor of lockers, a slide that goes
// down into the dark. turn three valves and the pool drains, and what lives in it climbs out (main)
static void tiled_room(Level *L, float x0, float x1, float z0, float z1, float h, Color wall) {
    slab(L, x0, x1, z0, z1, 0, 1, TEX_POOL, (Color){ 150, 160, 160, 255 }, 1.0f);
    slab(L, x0, x1, z0, z1, h + 1, 1, TEX_POOL, scale_tint(wall, 0.6f), 2.0f);
    (void)wall;
}
static void build_baths(Level *L, int seed) {
    L->name = "THE BATHS";
    L->ambient = 0.2f;
    SetRandomSeed(seed * 7919 + 13);
    L->fog = (Color){ 6, 14, 16, 255 };
    L->fogDensity = 0.07f;
    L->light = 0.9f;
    L->killY = -50;
    L->gradeLo = (Color){ 112, 132, 136, 255 }; L->gradeHi = (Color){ 136, 142, 132, 255 };
    L->moteCol = (Color){ 150, 180, 180, 255 };
    const Color TW = { 170, 190, 190, 255 }, AQUA = { 90, 200, 210, 255 };
    // ---- the hall and its pool: the walkway round the edge, and the pool four metres deep
    const float HX = 12, HH = 9, PH = 6, D = 4;
    slab(L, -HX, HX, -HX, -PH, 0, 1, TEX_POOL, TW, 1.0f); slab(L, -HX, HX, PH, HX, 0, 1, TEX_POOL, TW, 1.0f);
    slab(L, -HX, -PH, -PH, PH, 0, 1, TEX_POOL, TW, 1.0f); slab(L, PH, HX, -PH, PH, 0, 1, TEX_POOL, TW, 1.0f);
    slab(L, -PH, PH, -PH, PH, -D, 1, TEX_POOL, (Color){ 120, 150, 150, 255 }, 1.0f);
    wall_x(L, -PH - WT, -PH, PH, -D, 0, TEX_POOL, TW); wall_x(L, PH + WT, -PH, PH, -D, 0, TEX_POOL, TW);
    wall_z(L, -PH - WT, -PH, PH, -D, 0, TEX_POOL, TW); wall_z(L, PH + WT, -PH, PH, -D, 0, TEX_POOL, TW);
    for (int i = -5; i <= 5; i += 2) add_box(L, (Vector3){ (float)i, -D + 0.01f, 0 }, (Vector3){ 0.08f, 0.006f, PH - 0.4f }, TEX_CONCRETE, (Color){ 20, 40, 60, 255 }, 1.0f, F_NOCOLLIDE);   // lane lines
    add_box(L, (Vector3){ PH - 0.2f, -D / 2, -2 }, (Vector3){ 0.12f, D / 2 + 0.6f, 0.5f }, TEX_RUST, (Color){ 200, 200, 190, 255 }, 1.0f, F_GRIP);   // the ladder
    slab(L, -HX, HX, -HX, HX, HH + 1, 1, TEX_POOL, scale_tint(TW, 0.6f), 2.0f);
    wall_x_door(L, -HX, -HX, HX, 0, HH, -1.5f, 1.5f, 3.0f, TEX_POOL, TW);
    wall_x_door(L, HX, -HX, HX, 0, HH, -1.5f, 1.5f, 3.0f, TEX_POOL, TW);
    wall_z_door(L, -HX, -HX, HX, 0, HH, -1.5f, 1.5f, 3.0f, TEX_POOL, TW);
    wall_z_door(L, HX, -HX, HX, 0, HH, -1.5f, 1.5f, 3.0f, TEX_POOL, TW);
    for (int i = 0; i < 4; i++) add_box(L, (Vector3){ (i & 1) ? 9.5f : -9.5f, HH / 2, (i & 2) ? 9.5f : -9.5f }, (Vector3){ 0.6f, HH / 2, 0.6f }, TEX_POOL, TW, 1.0f, 0);
    for (int i = 0; i < 6; i++) add_box(L, (Vector3){ -7.5f + i * 3.0f, HH - 0.05f, (i % 2) ? -8.5f : 8.5f }, (Vector3){ 0.5f, 0.03f, 0.5f }, TEX_CONCRETE, AQUA, 1.0f, F_EMIT | F_NOCOLLIDE);
    L->water = true; L->waterY = -0.25f; L->waterHalf = PH; L->waterC = (Vector3){ 0, 0, 0 };
    Pickup pk = { { 0, -D + 1.1f, 0 }, FX_LAMP, false, true };   // at the bottom, under the water
    Pickups_push(&L->pickups, pk);
    add_decal(L, "NO DIVING", (Vector3){ 0, 2.4f, -HX + WT }, 2, 1, 0.22f, (Color){ 40, 60, 70, 255 });
    add_decal(L, "HE IS AT THE BOTTOM", (Vector3){ -HX + WT, 2.0f, -6 }, 0, 1, 0.16f, BLOOD);
    // ---- the way in: a long tiled passage with the floor under a few centimetres of water
    slab(L, -1.5f, 1.5f, HX, 28, 0, 1, TEX_POOL, TW, 1.0f);
    slab(L, -1.5f, 1.5f, HX, 28, 3.2f + 1, 1, TEX_POOL, scale_tint(TW, 0.6f), 2.0f);
    wall_x(L, -1.5f - WT, HX, 28, 0, 3.2f, TEX_POOL, TW); wall_x(L, 1.5f + WT, HX, 28, 0, 3.2f, TEX_POOL, TW); wall_z(L, 28 + WT, -1.6f, 1.6f, 0, 3.2f, TEX_POOL, TW);
    add_box(L, (Vector3){ 0, 0.08f, 20 }, (Vector3){ 1.5f, 0.004f, 8 }, TEX_WATER, (Color){ 40, 70, 80, 255 }, 1.0f, F_NOCOLLIDE);
    for (int i = 0; i < 3; i++) add_box(L, (Vector3){ 0, 3.15f, 15 + i * 5.0f }, (Vector3){ 0.4f, 0.03f, 0.12f }, TEX_CONCRETE, AQUA, 1.0f, F_EMIT | F_NOCOLLIDE);
    L->spawn = (Vector3){ 0, 0.05f, 26 }; L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.0f, 0.02f, 24.5f }, NOTE_CANDLES);
    // ---- north: the showers. stalls, curtains, and someone standing behind one of them
    tiled_room(L, -6, 6, -26, -HX, 3.4f, TW);
    wall_x(L, -6 - WT, -26, -HX, 0, 3.4f, TEX_POOL, TW); wall_x(L, 6 + WT, -26, -HX, 0, 3.4f, TEX_POOL, TW); wall_z(L, -26 - WT, -6.1f, 6.1f, 0, 3.4f, TEX_POOL, TW);
    for (int s = -1; s <= 1; s += 2) for (int i = 0; i < 4; i++) {
        float z = -15 - i * 2.6f, x = s * 4.6f;
        add_box(L, (Vector3){ s * 3.3f, 1.1f, z - 1.3f }, (Vector3){ 1.3f, 1.1f, 0.05f }, TEX_POOL, TW, 1.0f, 0);   // partitions
        add_box(L, (Vector3){ s * 3.3f, 1.2f, z }, (Vector3){ 0.02f, 1.0f, 1.1f }, TEX_CLOTH, (Color){ 150, 170, 160, 255 }, 1.0f, F_NOCOLLIDE);   // the curtain
        add_box(L, (Vector3){ x + s * 1.1f, 2.4f, z }, (Vector3){ 0.08f, 0.05f, 0.08f }, TEX_RUST, (Color){ 160, 160, 150, 255 }, 1.0f, F_NOCOLLIDE);
        if (i == 2 && s == 1) add_effigy(L, FIG_PENITENT, (Vector3){ x, 0, z }, -1.5708f, 0.6f);   // behind this curtain
    }
    add_box(L, (Vector3){ 0, 3.35f, -19 }, (Vector3){ 0.3f, 0.03f, 0.3f }, TEX_CONCRETE, AQUA, 1.0f, F_EMIT | F_NOCOLLIDE);
    Use v1 = { { 0, 1.2f, -25.6f }, USE_VALVE, 0, false }; Uses_push(&L->uses, v1);
    // ---- east: the locker room, a long corridor of them
    tiled_room(L, HX, 32, -3, 3, 3.0f, TW);
    wall_z(L, -3 - WT, HX, 32, 0, 3.0f, TEX_POOL, TW); wall_z(L, 3 + WT, HX, 32, 0, 3.0f, TEX_POOL, TW); wall_x(L, 32 + WT, -3.1f, 3.1f, 0, 3.0f, TEX_POOL, TW);
    for (int s = -1; s <= 1; s += 2) for (int i = 0; i < 16; i++) {
        float x = 13.4f + i * 1.15f;
        Color lc = scale_tint((Color){ 90, 110, 120, 255 }, frand(0.7f, 1.0f));
        add_box(L, (Vector3){ x, 1.1f, s * 2.6f }, (Vector3){ 0.55f, 1.1f, 0.4f }, TEX_RUST, lc, 1.0f, 0);
        add_box(L, (Vector3){ x, 1.6f, s * 2.19f }, (Vector3){ 0.3f, 0.12f, 0.01f }, TEX_CONCRETE, (Color){ 10, 10, 12, 255 }, 1.0f, F_NOCOLLIDE);   // vents
        if (i == 9 && s < 0) add_box(L, (Vector3){ x + 0.4f, 1.1f, -1.9f }, (Vector3){ 0.03f, 1.05f, 0.5f }, TEX_RUST, lc, 1.0f, F_NOCOLLIDE);   // one door hanging open
    }
    add_box(L, (Vector3){ 22, 0.4f, 0 }, (Vector3){ 6, 0.06f, 0.35f }, TEX_WOOD, (Color){ 120, 100, 80, 255 }, 1.0f, 0);   // a bench
    add_box(L, (Vector3){ 22, 2.95f, 0 }, (Vector3){ 0.5f, 0.03f, 0.2f }, TEX_CONCRETE, AQUA, 1.0f, F_EMIT | F_NOCOLLIDE);
    Use v2 = { { 31.6f, 1.2f, 0 }, USE_VALVE, 1, false }; Uses_push(&L->uses, v2);
    // ---- west: the slide hall. stairs up to a platform, and a tube from it going down into the dark
    tiled_room(L, -30, -HX, -9, 9, 14, TW);
    wall_z(L, -9 - WT, -30, -HX, 0, 14, TEX_POOL, TW); wall_z(L, 9 + WT, -30, -HX, 0, 14, TEX_POOL, TW); wall_x(L, -30 - WT, -9.1f, 9.1f, 0, 14, TEX_POOL, TW);
    for (int k = 0; k < 10; k++) add_box(L, (Vector3){ -16 - k * 0.8f, 0.25f + k * 0.5f, -6.5f }, (Vector3){ 0.4f, 0.25f + k * 0.5f, 1.2f }, TEX_POOL, TW, 1.0f, 0);
    add_box(L, (Vector3){ -26.5f, 2.6f, -5.5f }, (Vector3){ 3.0f, 2.6f, 2.2f }, TEX_POOL, TW, 1.0f, 0);   // the platform, top at 5.2
    add_box(L, (Vector3){ -28.6f, 7.0f, -5.5f }, (Vector3){ 1.2f, 1.6f, 1.2f }, TEX_CONCRETE, (Color){ 200, 80, 60, 255 }, 1.0f, F_NOCOLLIDE);   // the mouth of the tube
    add_box(L, (Vector3){ -28.6f, 6.6f, -4.28f }, (Vector3){ 0.9f, 1.1f, 0.02f }, TEX_CONCRETE, (Color){ 2, 2, 2, 255 }, 1.0f, F_NOCOLLIDE);
    add_portal(L, (Vector3){ -28.6f, 5.2f, -4.0f }, W_WARD, 0, (Color){ 200, 220, 210, 255 }, "THE SLIDE");
    L->portals.data[L->portals.size - 1].radius = 0.8f;
    for (int k = 0; k < 8; k++) {   // the tube itself, coiling down out of sight
        float a = k * 0.7f;
        add_box(L, (Vector3){ -22 + sinf(a) * 3, 11 - k * 1.2f, cosf(a) * 3 }, (Vector3){ 0.8f, 0.8f, 0.8f }, TEX_CONCRETE, (Color){ 180, 70, 50, 255 }, 1.0f, F_NOCOLLIDE);
    }
    add_box(L, (Vector3){ -21, 0.1f, 3 }, (Vector3){ 3.5f, 0.004f, 3.5f }, TEX_WATER, (Color){ 40, 70, 80, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ -21, 13.9f, 0 }, (Vector3){ 0.5f, 0.03f, 0.5f }, TEX_CONCRETE, AQUA, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_decal(L, "DOWN THE SLIDE", (Vector3){ -30 + WT, 6.8f, -2 }, 0, 1, 0.16f, BLOOD);
    Use v3 = { { -24.2f, 6.2f, -7.4f }, USE_VALVE, 2, false }; Uses_push(&L->uses, v3);
    add_note(L, (Vector3){ -24.5f, 5.22f, -4.6f }, NOTE_DAY9);
}

// ---------------------------------------------------------------- VOID: platforms in nothing
// support of an axis aligned footprint along a unit direction: how far its edge is from its center
static float support(float hx, float hz, float dx, float dz) { return hx * fabsf(dx) + hz * fabsf(dz); }

static void build_void(Level *L, int seed) {
    L->ambient = 0.75f;
    L->name = "THE STEPS";
    L->fog = (Color){ 18, 6, 14, 255 };
    L->fogDensity = 0.028f;
    L->light = 1.1f;
    L->sky = (Sky){ true, { 2, 0, 3, 255 }, { 18, 6, 14, 255 }, { 18, 6, 14, 255 }, 0.25f, false, { 0, 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0, 0 }, 0.0f, true, 0.7f };
    L->gradeLo = (Color){ 120, 112, 128, 255 }; L->gradeHi = (Color){ 158, 120, 116, 255 };
    L->moteCol = (Color){ 140, 60, 50, 255 }; L->moteGlow = true;
    L->killY = -35;
    SetRandomSeed(seed * 104729 + 7);
    const Color tint = { 150, 130, 170, 255 };
    Vector3 p = { 0, 0, 0 };           // center of the top surface of the platform we're leaving
    float phx = 3.0f, phz = 3.0f;      // its half extents
    float yaw = 0;
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ phx, 0.5f, phz }, TEX_CONCRETE, tint, 2.0f, 0);
    L->spawn = (Vector3){ 0, 0.05f, 0 };
    L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.3f, 0.01f, -1.4f }, NOTE_EYE);
    add_box(L, (Vector3){ 0, 2.5f, 5.0f }, (Vector3){ 1.3f, 2.5f, 0.12f }, TEX_CONCRETE, (Color){ 40, 36, 44, 255 }, 2.0f, F_NOCOLLIDE);
    add_decal(L, "HE FELL TOO", (Vector3){ 0, 3.0f, 4.88f }, 2, -1, 0.3f, CHALK);
    const int COUNT = 30;
    int last = -1;
    for (int i = 1; i <= COUNT; i++) {
        yaw += frand(-0.55f, 0.55f) + 0.07f;
        float dx = sinf(yaw), dz = -cosf(yaw);
        float gap = frand(1.4f, 3.0f);
        int type = (i == COUNT) ? 0 : GetRandomValue(0, 9);
        if (type == last && type != 0) type = 0;
        last = type;
        TexId tx = (i % 5 == 0) ? TEX_FLESH : (i % 3 == 0) ? TEX_RUST : TEX_CONCRETE;
        float nhx, nhz;
        Vector3 c;
        switch (type) {
        case 4: case 5: {   // a beam: long, narrow, dead straight
            bool alongX = fabsf(dx) > fabsf(dz);
            float hl = frand(3.0f, 5.0f), hw = 0.55f;
            nhx = alongX ? hl : hw; nhz = alongX ? hw : hl;
            float d = support(phx, phz, dx, dz) + support(nhx, nhz, dx, dz) + gap;
            c = (Vector3){ p.x + dx * d, p.y + frand(-0.2f, 0.6f), p.z + dz * d };
            add_box(L, (Vector3){ c.x, c.y - 0.4f, c.z }, (Vector3){ nhx, 0.4f, nhz }, TEX_RUST, (Color){ 140, 120, 150, 255 }, 2.0f, 0);
            break;
        }
        case 6: {           // stepping stones: three little islands
            float cx = p.x, cz = p.z, cy = p.y;
            float hx0 = phx, hz0 = phz;
            for (int k = 0; k < 3; k++) {
                float hs = 0.95f, d = support(hx0, hz0, dx, dz) + support(hs, hs, dx, dz) + frand(1.3f, 2.2f);
                cx += dx * d + frand(-0.5f, 0.5f); cz += dz * d + frand(-0.5f, 0.5f); cy += frand(-0.3f, 0.5f);
                add_box(L, (Vector3){ cx, cy - 0.4f, cz }, (Vector3){ hs, 0.4f, hs }, TEX_FLESH, tint, 1.5f, 0);
                hx0 = hz0 = hs;
            }
            c = (Vector3){ cx, cy, cz }; nhx = nhz = 0.95f;
            break;
        }
        case 7: {           // a ruin: pillars and a lintel to pass under
            float nh = 2.6f;
            nhx = nhz = nh;
            float d = support(phx, phz, dx, dz) + support(nh, nh, dx, dz) + gap;
            c = (Vector3){ p.x + dx * d, p.y + frand(0.0f, 0.8f), p.z + dz * d };
            add_box(L, (Vector3){ c.x, c.y - 0.5f, c.z }, (Vector3){ nh, 0.5f, nh }, TEX_CONCRETE, tint, 2.0f, 0);
            add_box(L, (Vector3){ c.x - nh + 0.5f, c.y + 1.8f, c.z }, (Vector3){ 0.4f, 1.8f, 0.4f }, TEX_CONCRETE, (Color){ 120, 105, 135, 255 }, 1.5f, 0);
            add_box(L, (Vector3){ c.x + nh - 0.5f, c.y + 1.8f, c.z }, (Vector3){ 0.4f, 1.8f, 0.4f }, TEX_CONCRETE, (Color){ 120, 105, 135, 255 }, 1.5f, 0);
            add_box(L, (Vector3){ c.x, c.y + 3.8f, c.z }, (Vector3){ nh, 0.25f, 0.5f }, TEX_CONCRETE, (Color){ 120, 105, 135, 255 }, 1.5f, 0);
            break;
        }
        case 8: {           // a tower: a landing, then a column you grip your way up, then a tiny summit
            float nh = 2.0f;
            float d = support(phx, phz, dx, dz) + support(nh, nh, dx, dz) + gap;
            Vector3 land = { p.x + dx * d, p.y + frand(-0.2f, 0.5f), p.z + dz * d };
            add_box(L, (Vector3){ land.x, land.y - 0.5f, land.z }, (Vector3){ nh, 0.5f, nh }, TEX_CONCRETE, tint, 2.0f, 0);
            float hgt = frand(5.0f, 7.5f), ch = 1.0f;
            // the column sits flush with the far edge of the landing
            Vector3 cc = { land.x + dx * (nh + ch), land.y + hgt / 2, land.z + dz * (nh + ch) };
            add_box(L, cc, (Vector3){ ch, hgt / 2, ch }, TEX_CONCRETE, (Color){ 110, 100, 130, 255 }, 2.0f, 0);
            // grip plates on the face toward the landing and the two sides
            Vector3 n = { -dx, 0, -dz };
            bool faceX = fabsf(dx) > fabsf(dz);
            float sgn = faceX ? (dx > 0 ? 1 : -1) : (dz > 0 ? 1 : -1);
            if (faceX) {
                add_box(L, (Vector3){ cc.x - sgn * (ch + 0.125f), land.y + 0.5f + (hgt - 0.7f) / 2, cc.z }, (Vector3){ 0.125f, (hgt - 0.7f) / 2, ch }, TEX_RUST, (Color){ 235, 205, 180, 255 }, 2.0f, F_GRIP);
                add_box(L, (Vector3){ cc.x, land.y + 0.5f + (hgt - 0.7f) / 2, cc.z - ch - 0.125f }, (Vector3){ ch, (hgt - 0.7f) / 2, 0.125f }, TEX_RUST, (Color){ 235, 205, 180, 255 }, 2.0f, F_GRIP);
                add_box(L, (Vector3){ cc.x, land.y + 0.5f + (hgt - 0.7f) / 2, cc.z + ch + 0.125f }, (Vector3){ ch, (hgt - 0.7f) / 2, 0.125f }, TEX_RUST, (Color){ 235, 205, 180, 255 }, 2.0f, F_GRIP);
            } else {
                add_box(L, (Vector3){ cc.x, land.y + 0.5f + (hgt - 0.7f) / 2, cc.z - sgn * (ch + 0.125f) }, (Vector3){ ch, (hgt - 0.7f) / 2, 0.125f }, TEX_RUST, (Color){ 235, 205, 180, 255 }, 2.0f, F_GRIP);
                add_box(L, (Vector3){ cc.x - ch - 0.125f, land.y + 0.5f + (hgt - 0.7f) / 2, cc.z }, (Vector3){ 0.125f, (hgt - 0.7f) / 2, ch }, TEX_RUST, (Color){ 235, 205, 180, 255 }, 2.0f, F_GRIP);
                add_box(L, (Vector3){ cc.x + ch + 0.125f, land.y + 0.5f + (hgt - 0.7f) / 2, cc.z }, (Vector3){ 0.125f, (hgt - 0.7f) / 2, ch }, TEX_RUST, (Color){ 235, 205, 180, 255 }, 2.0f, F_GRIP);
            }
            (void)n;
            c = (Vector3){ cc.x, land.y + hgt, cc.z }; nhx = nhz = ch;
            break;
        }
        default: {          // an island
            float nh = (i == COUNT) ? 3.0f : frand(1.3f, 2.4f);
            nhx = nhz = nh;
            float d = support(phx, phz, dx, dz) + support(nh, nh, dx, dz) + gap;
            c = (Vector3){ p.x + dx * d, p.y + frand(-0.3f, 1.0f), p.z + dz * d };
            add_box(L, (Vector3){ c.x, c.y - 0.5f, c.z }, (Vector3){ nh, 0.5f, nh }, tx, tint, 2.0f, 0);
            if (i != COUNT && GetRandomValue(0, 99) < 65) {   // a standing stone on the side facing the eye: shelter
                float ex = -0.342f, ez = 0.94f;
                add_box(L, (Vector3){ c.x + ex * nh * 0.5f, c.y + 1.4f, c.z + ez * nh * 0.5f }, (Vector3){ 0.45f, 1.4f, 0.45f }, TEX_CONCRETE, (Color){ 110, 96, 120, 255 }, 1.5f, 0);
            }
            {   // a glowing seam around the rim: you can see where the edge is
                static const Color TR[3] = { { 120, 20, 20, 255 }, { 90, 14, 18, 255 }, { 140, 40, 30, 255 } };
                Color tc = TR[i % 3];
                add_box(L, (Vector3){ c.x, c.y + 0.015f, c.z - nh + 0.08f }, (Vector3){ nh, 0.015f, 0.06f }, TEX_CONCRETE, tc, 1.0f, F_EMIT | F_NOCOLLIDE);
                add_box(L, (Vector3){ c.x, c.y + 0.015f, c.z + nh - 0.08f }, (Vector3){ nh, 0.015f, 0.06f }, TEX_CONCRETE, tc, 1.0f, F_EMIT | F_NOCOLLIDE);
                add_box(L, (Vector3){ c.x - nh + 0.08f, c.y + 0.015f, c.z }, (Vector3){ 0.06f, 0.015f, nh }, TEX_CONCRETE, tc, 1.0f, F_EMIT | F_NOCOLLIDE);
                add_box(L, (Vector3){ c.x + nh - 0.08f, c.y + 0.015f, c.z }, (Vector3){ 0.06f, 0.015f, nh }, TEX_CONCRETE, tc, 1.0f, F_EMIT | F_NOCOLLIDE);
            }
            break;
        }
        }
        // spires hanging under it, unreachable decoration
        add_box(L, (Vector3){ c.x, c.y - 4.0f, c.z }, (Vector3){ fmaxf(nhx, nhz) * 0.25f, 3.2f, fmaxf(nhx, nhz) * 0.25f }, TEX_CONCRETE, (Color){ 90, 70, 110, 255 }, 2.0f, F_NOCOLLIDE);
        p = c; phx = nhx; phz = nhz;
        // and every few steps, an arm the size of a tower rising out of the nothing beside you
        if (i % 4 == 2) {
            float side = GetRandomValue(0, 1) ? 1.0f : -1.0f, off = frand(6, 10);
            Vector3 hp = { c.x - dz * off * side, c.y - 10.0f, c.z + dx * off * side };
            add_effigy(L, FIG_HAND, hp, atan2f(c.x - hp.x, -(c.z - hp.z)), 0);
            L->effigies.data[L->effigies.size - 1].scale = frand(3.6f, 5.0f);
        }
        // drifting rubble, simulated but weightless
        if (i % 2 == 0)
            add_prop(L, (Vector3){ p.x + frand(-6, 6), p.y + frand(1, 6), p.z + frand(-6, 6) }, (Vector3){ frand(0.3f, 0.9f), frand(0.3f, 0.9f), frand(0.3f, 0.9f) },
                     TEX_FLESH, (Color){ 170, 140, 170, 255 }, 40, 0.0f);
        if (i == 12) {   // a door standing on its own at the edge, with grey light round it
            float px = -dz, pz = dx, r = fminf(phx, phz) - 0.15f;
            Vector3 side = fabsf(px) > fabsf(pz) ? (Vector3){ px > 0 ? 1.0f : -1.0f, 0, 0 } : (Vector3){ 0, 0, pz > 0 ? 1.0f : -1.0f };
            Vector3 dp = { p.x + side.x * r, p.y, p.z + side.z * r };
            add_box(L, (Vector3){ dp.x + side.x * 0.1f, p.y + 1.25f, dp.z + side.z * 0.1f }, fabsf(side.x) > 0.5f ? (Vector3){ 0.1f, 1.25f, 0.8f } : (Vector3){ 0.8f, 1.25f, 0.1f }, TEX_CONCRETE, (Color){ 40, 36, 44, 255 }, 2.0f, F_NOCOLLIDE);
            add_door(L, dp, (Vector3){ -side.x, 0, -side.z }, (Color){ 170, 170, 170, 255 }, W_STATIC, 0, "THE STATIC SEA");
        }
    }
    for (int i = 0; i < L->props.size; i++) {
        b3Body_SetAngularVelocity(L->props.data[i].body, (b3Vec3){ frand(-.6f, .6f), frand(-.6f, .6f), frand(-.6f, .6f) });
        b3Body_SetLinearVelocity(L->props.data[i].body, (b3Vec3){ frand(-.3f, .3f), frand(-.3f, .3f), frand(-.3f, .3f) });
    }
    Vector3 pc = { p.x, p.y + 1.1f, p.z };
    Pickup pk = { pc, FX_BOOTS, false, false };
    Pickups_push(&L->pickups, pk);
    add_box(L, (Vector3){ p.x, p.y + 20, p.z }, (Vector3){ 0.04f, 20, 0.04f }, TEX_CONCRETE, (Color){ 200, 160, 255, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
}

// ---------------------------------------------------------------- NURSERY: his old room, and you no bigger than a toy
// a child's bedroom at night, built eight times too big. a crib like a building with something huge asleep in it,
// a dresser you can get under, a dollhouse your size. his mother comes in to look at him (main) and she is
// sixteen metres tall, and she can see anything that is out on the open floor
static void build_nursery(Level *L, int seed) {
    L->name = "THE NURSERY";
    L->ambient = 0.2f;
    SetRandomSeed(seed * 6007 + 3);
    L->fog = (Color){ 10, 10, 18, 255 };
    L->fogDensity = 0.022f;
    L->light = 0.9f;
    L->killY = -20;
    L->gradeLo = (Color){ 120, 122, 150, 255 }; L->gradeHi = (Color){ 150, 128, 128, 255 };
    L->moteCol = (Color){ 150, 150, 170, 255 };
    const float X = 24, Z = 20, H = 24;
    const Color PAPER = { 110, 104, 130, 255 };
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ X + 1, 0.5f, Z + 1 }, TEX_WOOD, (Color){ 120, 96, 80, 255 }, 8.0f, 0);
    add_box(L, (Vector3){ 0, H + 0.5f, 0 }, (Vector3){ X + 1, 0.5f, Z + 1 }, TEX_PAPER, (Color){ 90, 88, 100, 255 }, 16.0f, 0);
    add_box(L, (Vector3){ -X - 0.5f, H / 2, 0 }, (Vector3){ 0.5f, H / 2, Z + 1 }, TEX_PAPER, PAPER, 16.0f, 0);
    add_box(L, (Vector3){ X + 0.5f, H / 2, 0 }, (Vector3){ 0.5f, H / 2, Z + 1 }, TEX_PAPER, PAPER, 16.0f, 0);
    add_box(L, (Vector3){ 0, H / 2, Z + 0.5f }, (Vector3){ X + 1, H / 2, 0.5f }, TEX_PAPER, PAPER, 16.0f, 0);
    // the north wall has the door in it, ajar, with the landing light coming round it
    add_box(L, (Vector3){ -14, H / 2, -Z - 0.5f }, (Vector3){ 10, H / 2, 0.5f }, TEX_PAPER, PAPER, 16.0f, 0);
    add_box(L, (Vector3){ 14, H / 2, -Z - 0.5f }, (Vector3){ 10, H / 2, 0.5f }, TEX_PAPER, PAPER, 16.0f, 0);
    add_box(L, (Vector3){ 0, 18.5f, -Z - 0.5f }, (Vector3){ 4, 5.5f, 0.5f }, TEX_PAPER, PAPER, 16.0f, 0);
    add_box(L, (Vector3){ -1.2f, 6.5f, -Z - 0.3f }, (Vector3){ 2.8f, 6.5f, 0.15f }, TEX_WOOD, (Color){ 150, 140, 130, 255 }, 6.0f, 0);   // the door, nearly shut
    add_box(L, (Vector3){ 2.6f, 6.5f, -Z - 0.6f }, (Vector3){ 0.6f, 6.5f, 0.05f }, TEX_CONCRETE, (Color){ 230, 190, 130, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ 2.6f, 0.01f, -Z + 2.0f }, (Vector3){ 0.9f, 0.006f, 2.2f }, TEX_CONCRETE, (Color){ 70, 54, 36, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    L->bed = (Vector3){ 2.6f, 0, -Z - 3.0f };   // where she comes in from
    // a tall window full of moon, and the moon lying across the floor
    add_box(L, (Vector3){ -X + 0.05f, 13, 0 }, (Vector3){ 0.05f, 4.5f, 6 }, TEX_CONCRETE, (Color){ 40, 56, 90, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ -X + 0.1f, 13, 0 }, (Vector3){ 0.06f, 4.6f, 0.25f }, TEX_WOOD, (Color){ 60, 56, 60, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ -X + 0.1f, 13, 0 }, (Vector3){ 0.06f, 0.25f, 6.1f }, TEX_WOOD, (Color){ 60, 56, 60, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ -X + 9, 0.01f, 2 }, (Vector3){ 5, 0.006f, 5 }, TEX_CONCRETE, (Color){ 14, 20, 34, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    // the night light, low on the south wall: the only warm thing
    add_box(L, (Vector3){ 10, 1.6f, Z - 0.15f }, (Vector3){ 0.5f, 0.45f, 0.12f }, TEX_CONCRETE, (Color){ 255, 180, 110, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ 10, 0.01f, Z - 3 }, (Vector3){ 3, 0.006f, 3 }, TEX_CONCRETE, (Color){ 46, 30, 18, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ 2, 0.012f, 2 }, (Vector3){ 12, 0.006f, 9 }, TEX_MOSAIC, (Color){ 40, 32, 42, 255 }, 6.0f, F_NOCOLLIDE);
    // the crib: up on legs you can walk under, bars, and a sheet hanging down the front that you can climb
    const float cx0 = -20, cx1 = -8, cz0 = -17, cz1 = -9, base = 2.6f, mat = 4.6f, top = 8.0f;
    const Color CRIB = { 190, 184, 176, 255 };
    for (int i = 0; i < 4; i++) add_box(L, (Vector3){ (i & 1) ? cx1 - 0.3f : cx0 + 0.3f, base / 2, (i & 2) ? cz1 - 0.3f : cz0 + 0.3f }, (Vector3){ 0.3f, base / 2, 0.3f }, TEX_WOOD, CRIB, 2.0f, 0);
    add_box(L, (Vector3){ (cx0 + cx1) / 2, (base + mat) / 2, (cz0 + cz1) / 2 }, (Vector3){ (cx1 - cx0) / 2, (mat - base) / 2, (cz1 - cz0) / 2 }, TEX_WOOD, CRIB, 2.0f, 0);
    add_box(L, (Vector3){ (cx0 + cx1) / 2, mat + 0.25f, (cz0 + cz1) / 2 }, (Vector3){ (cx1 - cx0) / 2 - 0.3f, 0.25f, (cz1 - cz0) / 2 - 0.3f }, TEX_CLOTH, (Color){ 200, 196, 210, 255 }, 3.0f, 0);
    for (float x = cx0; x <= cx1 + 0.01f; x += 0.9f) for (int s = 0; s < 2; s++)
        add_box(L, (Vector3){ x, (mat + top) / 2, s ? cz1 : cz0 }, (Vector3){ 0.09f, (top - mat) / 2, 0.09f }, TEX_WOOD, CRIB, 1.0f, 0);
    for (float z = cz0; z <= cz1 + 0.01f; z += 0.9f) for (int s = 0; s < 2; s++)
        add_box(L, (Vector3){ s ? cx1 : cx0, (mat + top) / 2, z }, (Vector3){ 0.09f, (top - mat) / 2, 0.09f }, TEX_WOOD, CRIB, 1.0f, 0);
    add_box(L, (Vector3){ (cx0 + cx1) / 2, top + 0.12f, cz0 }, (Vector3){ (cx1 - cx0) / 2 + 0.15f, 0.12f, 0.15f }, TEX_WOOD, CRIB, 2.0f, 0);
    add_box(L, (Vector3){ (cx0 + cx1) / 2, top + 0.12f, cz1 }, (Vector3){ (cx1 - cx0) / 2 + 0.15f, 0.12f, 0.15f }, TEX_WOOD, CRIB, 2.0f, 0);
    add_box(L, (Vector3){ cx0, top + 0.12f, (cz0 + cz1) / 2 }, (Vector3){ 0.15f, 0.12f, (cz1 - cz0) / 2 }, TEX_WOOD, CRIB, 2.0f, 0);
    add_box(L, (Vector3){ cx1, top + 0.12f, (cz0 + cz1) / 2 }, (Vector3){ 0.15f, 0.12f, (cz1 - cz0) / 2 }, TEX_WOOD, CRIB, 2.0f, 0);
    add_box(L, (Vector3){ -15, (0.3f + top + 0.2f) / 2, cz1 + 0.26f }, (Vector3){ 1.1f, (top + 0.2f - 0.3f) / 2, 0.06f }, TEX_CLOTH, (Color){ 210, 200, 210, 255 }, 2.0f, F_GRIP);   // the sheet
    // and in it, asleep, something enormous
    add_effigy(L, FIG_SLEEPER, (Vector3){ -15.5f, mat + 0.5f, -13 }, 1.5708f, 0);
    L->effigies.data[L->effigies.size - 1].scale = 4.2f;
    Pickup pk = { { -10.3f, mat + 1.6f, -13 }, FX_FEATHER, false, false };   // on his pillow
    Pickups_push(&L->pickups, pk);
    // the dresser: up on legs, room to stand under it
    const Color DR = { 120, 90, 80, 255 };
    for (int i = 0; i < 4; i++) add_box(L, (Vector3){ (i & 1) ? 21.6f : 8.4f, 1.15f, (i & 2) ? -13.4f : -19.2f }, (Vector3){ 0.4f, 1.15f, 0.4f }, TEX_WOOD, DR, 2.0f, 0);
    add_box(L, (Vector3){ 15, 6.6f, -16.3f }, (Vector3){ 7, 4.3f, 3.3f }, TEX_WOOD, DR, 4.0f, 0);
    for (int r = 0; r < 3; r++) {
        add_box(L, (Vector3){ 15, 3.8f + r * 2.8f, -12.95f }, (Vector3){ 6.5f, 1.2f, 0.06f }, TEX_WOOD, scale_tint(DR, 1.2f), 3.0f, F_NOCOLLIDE);
        for (int s = -1; s <= 1; s += 2) add_box(L, (Vector3){ 15 + s * 3, 3.8f + r * 2.8f, -12.8f }, (Vector3){ 0.25f, 0.25f, 0.12f }, TEX_RUST, (Color){ 180, 150, 90, 255 }, 1.0f, F_NOCOLLIDE);
    }
    // blocks, scattered and stacked: something to get behind
    static const Color BLK[4] = { { 150, 60, 60, 255 }, { 60, 90, 140, 255 }, { 170, 150, 70, 255 }, { 70, 120, 80, 255 } };
    for (int i = 0, tries = 0; i < 14 && tries < 200; tries++) {
        float s = frand(0.9f, 1.6f), x = frand(-X + 3, X - 3), z = frand(-Z + 3, Z - 3);
        if (x < -6 && z < -7) continue;
        if (x > 6 && z < -11) continue;
        if (fabsf(x - 6) < 3 && fabsf(z - 14) < 3) continue;
        add_box(L, (Vector3){ x, s, z }, (Vector3){ s, s, s }, TEX_WOOD, BLK[i % 4], 2.0f, 0);
        if (i % 3 == 0) add_box(L, (Vector3){ x + frand(-0.3f, 0.3f), 2 * s + s * 0.7f, z + frand(-0.3f, 0.3f) }, (Vector3){ s * 0.7f, s * 0.7f, s * 0.7f }, TEX_WOOD, BLK[(i + 1) % 4], 2.0f, 0);
        i++;
    }
    // the toy chest, the lid not quite down
    add_box(L, (Vector3){ 17, 2.4f, 10.5f }, (Vector3){ 4, 2.4f, 2.6f }, TEX_WOOD, (Color){ 140, 100, 70, 255 }, 3.0f, 0);
    add_box(L, (Vector3){ 17, 4.95f, 10.3f }, (Vector3){ 4.1f, 0.15f, 2.7f }, TEX_WOOD, (Color){ 120, 84, 60, 255 }, 3.0f, 0);
    add_box(L, (Vector3){ 17, 4.82f, 13.05f }, (Vector3){ 3.9f, 0.03f, 0.02f }, TEX_CONCRETE, (Color){ 4, 3, 3, 255 }, 1.0f, F_NOCOLLIDE);
    // the dollhouse: a house exactly your size, with a little door
    const Color DH = { 170, 150, 140, 255 };
    add_box(L, (Vector3){ -18.5f, 3.2f, 12 }, (Vector3){ 3.5f, 3.2f, 4 }, TEX_PAPER, DH, 2.0f, 0);
    add_box(L, (Vector3){ -18.5f, 6.8f, 12 }, (Vector3){ 3.9f, 0.4f, 4.4f }, TEX_WOOD, (Color){ 120, 60, 50, 255 }, 2.0f, 0);
    for (int i = 0; i < 2; i++) add_box(L, (Vector3){ -14.95f, 4.3f, 10 + i * 4.0f }, (Vector3){ 0.03f, 0.5f, 0.45f }, TEX_CONCRETE, (Color){ 255, 190, 110, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_door(L, (Vector3){ -15 + WT, 0, 12 }, (Vector3){ 1, 0, 0 }, (Color){ 255, 170, 90, 255 }, W_DINNER, 0, "THE DINNER");
    // a rocking chair in the corner, and a doll somebody dropped
    L->heart = (Vector3){ 16, 0, 14 };   // the chair (main rocks it)
    add_effigy(L, FIG_SLEEPER, (Vector3){ 6, 0.5f, 3 }, 0.6f, 0);   // the doll, face down
    L->effigies.data[L->effigies.size - 1].scale = 1.6f;
    L->spawn = (Vector3){ 6, 0.05f, 15 }; L->spawnYaw = 0;
    add_note(L, (Vector3){ 7.3f, 0.01f, 14.0f }, NOTE_GIANT);
    add_note(L, (Vector3){ -12.5f, 0.01f, 10.0f }, NOTE_NURSERY);
}

// ---------------------------------------------------------------- WARD: the corridor
// one corridor with a dogleg in it, built twice end to end. walking off the end of the first copy into the start
// of the second puts you back at the start of the first (main does that), one lap on. every lap it is worse
static void ward_copy(Level *L, float ox, float oz, int k, bool first) {
    const float H = 2.8f;
    SetRandomSeed(1000 + k * 7);   // a lap looks the same from either copy, so the seam can't be seen
    Color wall = k >= 3 ? (Color){ 170, 120, 116, 255 } : (Color){ 176, 186, 170, 255 }, floor = { 140, 150, 140, 255 };
    #define WX(x) ((x) + ox)
    #define WZ(z) ((z) + oz)
    // A: x -1.2..1.2, z 8..-14.   B: the dogleg, z -14..-16.4, x -1.2..9.2.   C: x 6.8..9.2, z -16.4..-28
    slab(L, WX(-1.2f), WX(1.2f), WZ(-16.4f), WZ(8), 0, 1, TEX_TILE, floor, 1.0f);
    slab(L, WX(1.2f), WX(9.2f), WZ(-16.4f), WZ(-14), 0, 1, TEX_TILE, floor, 1.0f);
    slab(L, WX(6.8f), WX(9.2f), WZ(-28), WZ(-16.4f), 0, 1, TEX_TILE, floor, 1.0f);
    slab(L, WX(-1.2f), WX(1.2f), WZ(-16.4f), WZ(8), H + 1, 1, TEX_CONCRETE, (Color){ 120, 124, 116, 255 }, 3.0f);
    slab(L, WX(1.2f), WX(9.2f), WZ(-16.4f), WZ(-14), H + 1, 1, TEX_CONCRETE, (Color){ 120, 124, 116, 255 }, 3.0f);
    slab(L, WX(6.8f), WX(9.2f), WZ(-28), WZ(-16.4f), H + 1, 1, TEX_CONCRETE, (Color){ 120, 124, 116, 255 }, 3.0f);
    if (k >= 3) wall_x_door(L, WX(-1.2f), WZ(-16.4f), WZ(8), 0, H, WZ(-6.6f), WZ(-5.4f), 2.2f, TEX_TILE, wall);   // room 6 stands open
    else wall_x(L, WX(-1.2f), WZ(-16.4f), WZ(8), 0, H, TEX_TILE, wall);
    wall_x(L, WX(1.2f), WZ(-14), WZ(8), 0, H, TEX_TILE, wall);
    wall_z(L, WZ(-14), WX(1.2f), WX(9.3f), 0, H, TEX_TILE, wall);
    if (k >= 1 && first) {   // from the second lap, a lift door in the dogleg, going down to the mortuary
        wall_z_door(L, WZ(-16.4f), WX(-1.2f), WX(6.8f), 0, H, WX(3.4f), WX(4.6f), 2.25f, TEX_TILE, wall);
        add_door(L, (Vector3){ WX(4), 0, WZ(-16.4f) + WT }, (Vector3){ 0, 0, 1 }, (Color){ 170, 200, 230, 255 }, W_MORGUE, 0, "MORTUARY");
    } else wall_z(L, WZ(-16.4f), WX(-1.2f), WX(6.8f), 0, H, TEX_TILE, wall);
    wall_x(L, WX(6.8f), WZ(-28), WZ(-16.4f), 0, H, TEX_TILE, wall);
    if (k >= 4 && first) wall_x_door(L, WX(9.2f), WZ(-28), WZ(-14), 0, H, WZ(-25.6f), WZ(-24.4f), 2.25f, TEX_TILE, wall);
    else wall_x(L, WX(9.2f), WZ(-28), WZ(-14), 0, H, TEX_TILE, wall);
    if (first) {   // the way you came in is shut behind you
        wall_z(L, WZ(8), WX(-1.3f), WX(1.3f), 0, H, TEX_TILE, wall);
        add_box(L, (Vector3){ WX(0), 1.15f, WZ(8) - 0.17f }, (Vector3){ 0.6f, 1.15f, 0.03f }, TEX_WOOD, (Color){ 90, 96, 90, 255 }, 1.0f, F_NOCOLLIDE);
    } else wall_z(L, WZ(-28), WX(6.7f), WX(9.3f), 0, H, TEX_TILE, wall);   // far beyond anything you can see
    // tubes in the ceiling: pale green, then fewer of them, then red
    Color tube = k >= 3 ? (Color){ 200, 50, 40, 255 } : k == 2 ? (Color){ 210, 200, 150, 255 } : (Color){ 200, 225, 205, 255 };
    for (int i = 0; i < 6; i++) {
        if ((k >= 2 && i % 2 == 1) || k >= 4) continue;   // on the last lap there is no light at all
        add_box(L, (Vector3){ WX(0), H - 0.03f, WZ(6 - i * 4.0f) }, (Vector3){ 0.08f, 0.02f, 0.6f }, TEX_CONCRETE, tube, 1.0f, F_EMIT | F_NOCOLLIDE);
    }
    if (k < 4) add_box(L, (Vector3){ WX(4.0f), H - 0.03f, WZ(-15.2f) }, (Vector3){ 0.6f, 0.02f, 0.08f }, TEX_CONCRETE, tube, 1.0f, F_EMIT | F_NOCOLLIDE);
    if (k < 4) add_box(L, (Vector3){ WX(8.0f), H - 0.03f, WZ(-22) }, (Vector3){ 0.08f, 0.02f, 0.6f }, TEX_CONCRETE, tube, 1.0f, F_EMIT | F_NOCOLLIDE);
    // a gurney with a radio on it
    add_box(L, (Vector3){ WX(0.75f), 0.75f, WZ(-3) }, (Vector3){ 0.32f, 0.04f, 0.95f }, TEX_SKIN, (Color){ 200, 196, 186, 255 }, 1.0f, 0);
    for (int i = 0; i < 4; i++) add_box(L, (Vector3){ WX(0.75f + (i & 1 ? 0.27f : -0.27f)), 0.37f, WZ(-3 + (i & 2 ? 0.85f : -0.85f)) }, (Vector3){ 0.02f, 0.37f, 0.02f }, TEX_RUST, (Color){ 130, 130, 126, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ WX(0.75f), 0.88f, WZ(-3.4f) }, (Vector3){ 0.14f, 0.09f, 0.07f }, TEX_WOOD, (Color){ 100, 70, 50, 255 }, 1.0f, F_NOCOLLIDE);
    // room six
    add_box(L, (Vector3){ WX(-1.2f) - 0.17f, 2.35f, WZ(-6) }, (Vector3){ 0.02f, 0.12f, 0.3f }, TEX_SKIN, (Color){ 220, 216, 200, 255 }, 1.0f, F_NOCOLLIDE);
    add_decal(L, "6", (Vector3){ WX(-1.2f) + 0.15f, 2.35f, WZ(-6) }, 0, 1, 0.1f, SOOT);
    if (k < 3) add_box(L, (Vector3){ WX(-1.2f) + 0.17f, 1.1f, WZ(-6) }, (Vector3){ 0.03f, 1.1f, 0.6f }, TEX_WOOD, (Color){ 140, 150, 140, 255 }, 1.0f, F_NOCOLLIDE);
    else {
        slab(L, WX(-4.6f), WX(-1.2f), WZ(-8), WZ(-4), 0, 1, TEX_TILE, floor, 1.0f);
        slab(L, WX(-4.6f), WX(-1.2f), WZ(-8), WZ(-4), H + 1, 1, TEX_CONCRETE, (Color){ 120, 124, 116, 255 }, 3.0f);
        wall_x(L, WX(-4.6f), WZ(-8), WZ(-4), 0, H, TEX_TILE, wall); wall_z(L, WZ(-8), WX(-4.6f), WX(-1.2f), 0, H, TEX_TILE, wall); wall_z(L, WZ(-4), WX(-4.6f), WX(-1.2f), 0, H, TEX_TILE, wall);
        add_box(L, (Vector3){ WX(-3.6f), 0.35f, WZ(-6) }, (Vector3){ 0.9f, 0.35f, 0.5f }, TEX_SKIN, (Color){ 190, 186, 176, 255 }, 1.0f, 0);
        add_effigy(L, FIG_SLEEPER, (Vector3){ WX(-3.6f), 0.72f, WZ(-6) }, -1.5708f, 0);
        add_effigy(L, FIG_KNEELER, (Vector3){ WX(-3.4f), 0, WZ(-7.2f) }, 0.0f, 0.3f);   // his mother, at the bedside
        add_box(L, (Vector3){ WX(-3.6f), H - 0.03f, WZ(-6) }, (Vector3){ 0.3f, 0.02f, 0.08f }, TEX_CONCRETE, tube, 1.0f, F_EMIT | F_NOCOLLIDE);
        if (first) add_note(L, (Vector3){ WX(-2.4f), 0.01f, WZ(-5.2f) }, NOTE_WARD);
    }
    if (k >= 1) {   // a wheelchair, facing the wall
        Color ch = { 110, 110, 106, 255 };
        add_box(L, (Vector3){ WX(8.7f), 0.5f, WZ(-20.5f) }, (Vector3){ 0.24f, 0.03f, 0.24f }, TEX_CLOTH, (Color){ 60, 60, 66, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ WX(8.95f), 0.8f, WZ(-20.5f) }, (Vector3){ 0.02f, 0.3f, 0.24f }, TEX_CLOTH, (Color){ 60, 60, 66, 255 }, 1.0f, F_NOCOLLIDE);
        for (int s = -1; s <= 1; s += 2) add_box(L, (Vector3){ WX(8.7f), 0.33f, WZ(-20.5f) + s * 0.28f }, (Vector3){ 0.3f, 0.3f, 0.015f }, TEX_RUST, ch, 1.0f, F_NOCOLLIDE);
    }
    if (k == 2) add_effigy(L, FIG_MOTHER, (Vector3){ WX(8.0f), 0, WZ(-26.5f) }, 0.0f, 1.2f);   // a woman at the far end, with her back to you
    if (k >= 3) {   // the floor is wet with it now, and there are hands on the walls
        add_box(L, (Vector3){ WX(0), 0.012f, WZ(-4) }, (Vector3){ 1.15f, 0.006f, 11.9f }, TEX_SLUDGE, (Color){ 110, 14, 12, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ WX(8), 0.012f, WZ(-22) }, (Vector3){ 1.15f, 0.006f, 5.9f }, TEX_SLUDGE, (Color){ 110, 14, 12, 255 }, 1.0f, F_NOCOLLIDE);
        for (int i = 0; i < 26; i++) {
            float z = frand(-13, 7), y = frand(0.4f, 2.3f);
            int sd = GetRandomValue(0, 1) ? 1 : -1;
            add_box(L, (Vector3){ WX(sd * 1.04f), y, WZ(z) }, (Vector3){ 0.004f, 0.09f, 0.07f }, TEX_FLESH, (Color){ 90, 10, 10, 255 }, 1.0f, F_NOCOLLIDE);
            add_box(L, (Vector3){ WX(sd * 1.04f), y + 0.12f, WZ(z) }, (Vector3){ 0.004f, 0.05f, 0.012f }, TEX_FLESH, (Color){ 90, 10, 10, 255 }, 1.0f, F_NOCOLLIDE);
        }
    }
    if (k >= 4 && first) add_decal(L, "DON'T LOOK BACK", (Vector3){ WX(1.2f) - 0.16f, 1.7f, WZ(3) }, 0, -1, 0.16f, BLOOD);
    for (int i = 0; i < k * 3; i++) add_box(L, (Vector3){ WX(frand(-0.8f, 0.8f)), 0.008f, WZ(frand(-12, 6)) }, (Vector3){ frand(0.1f, 0.4f), 0.004f, frand(0.1f, 0.5f) }, TEX_SLUDGE, (Color){ 90, 20, 18, 255 }, 1.0f, F_NOCOLLIDE);
    if (k >= 2) add_decal(L, "SHE VISITS ON SUNDAYS", (Vector3){ WX(4.0f), 1.7f, WZ(-14) - 0.16f }, 2, -1, 0.12f, BLOOD);
    if (k >= 3) add_decal(L, "HE WILL NOT WAKE", (Vector3){ WX(1.2f) - 0.16f, 1.6f, WZ(-9) }, 0, -1, 0.14f, BLOOD);
    if (k >= 4 && first) {
        add_door(L, (Vector3){ WX(9.2f) - WT, 0, WZ(-25) }, (Vector3){ -1, 0, 0 }, (Color){ 200, 40, 40, 255 }, W_WOMB, 0, "DOWN");
        add_decal(L, "COME DOWN", (Vector3){ WX(6.8f) + 0.16f, 1.8f, WZ(-25) }, 0, 1, 0.14f, BLOOD);
    }
    #undef WX
    #undef WZ
}
static void build_ward(Level *L, int seed) {
    int k = g_wardLoop;
    static const float AMB[5] = { 0.5f, 0.38f, 0.24f, 0.14f, 0.02f };
    L->ambient = AMB[k > 4 ? 4 : k];
    L->name = k == 0 ? "THE WARD" : NULL;
    (void)seed;
    L->fog = k >= 3 ? (Color){ 26, 8, 8, 255 } : (Color){ 14, 18, 16, 255 };
    L->fogDensity = 0.085f + 0.01f * k;
    L->light = 0.9f - 0.08f * k;
    L->killY = -50;
    L->gradeLo = (Color){ 120, 132, 126, 255 }; L->gradeHi = (Color){ 140, 140, 124, 255 };
    L->moteCol = (Color){ 160, 170, 160, 255 };
    ward_copy(L, 0, 0, k, true);
    ward_copy(L, 8, -36, k + 1, false);
    L->spawn = (Vector3){ 0, 0.05f, 6.0f }; L->spawnYaw = 0;
}

// ---------------------------------------------------------------- STATIC: the static sea
// a plain of television snow under no sky at all, that goes on forever because it repeats. televisions stand about
// on it, all tuned to the same nothing. touch a screen and you are somewhere else. the grey man walks here (main)
static void add_tv(Level *L, Vector3 p, int face, float s) {
    static const Vector3 N[4] = { { 0, 0, -1 }, { 1, 0, 0 }, { 0, 0, 1 }, { -1, 0, 0 } };
    Vector3 n = N[face & 3];
    bool xn = fabsf(n.x) > 0.5f;
    Vector3 c = { p.x, p.y + 0.42f * s, p.z };
    add_box(L, c, (Vector3){ 0.52f * s, 0.42f * s, 0.48f * s }, TEX_CONCRETE, (Color){ 46, 42, 40, 255 }, 1.0f, F_NOCOLLIDE);
    Vector3 sc = { c.x + n.x * 0.49f * s, c.y + 0.02f * s, c.z + n.z * 0.49f * s };
    add_box(L, sc, xn ? (Vector3){ 0.01f, 0.3f * s, 0.38f * s } : (Vector3){ 0.38f * s, 0.3f * s, 0.01f }, TEX_STATIC, (Color){ 200, 205, 210, 255 }, 0.7f * s, F_NOCOLLIDE | F_SCREEN);
    Use u = { { sc.x + n.x * 0.3f, sc.y, sc.z + n.z * 0.3f }, USE_LINK, 0, false };
    Uses_push(&L->uses, u);
}
static void build_static(Level *L, int seed) {
    L->ambient = 0.5f;
    L->name = "THE STATIC SEA";
    SetRandomSeed(seed * 3301 + 29);
    L->fog = (Color){ 6, 6, 7, 255 };
    L->fogDensity = 0.055f;
    L->light = 0.9f;
    L->killY = -50;
    L->wrap = 120;
    L->gradeLo = (Color){ 128, 128, 132, 255 }; L->gradeHi = (Color){ 128, 128, 126, 255 };
    L->moteCol = (Color){ 120, 120, 120, 255 };
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ 100, 0.5f, 100 }, TEX_STATIC, (Color){ 70, 70, 72, 255 }, 4.0f, 0);
    // televisions: alone, in twos and threes, stacked
    for (int i = 0, tries = 0; i < 26 && tries < 400; tries++) {
        Vector3 p = { frand(-58, 58), 0, frand(-58, 58) };
        if (fabsf(p.x) < 4 && fabsf(p.z) < 4) continue;
        int kind = GetRandomValue(0, 4);
        add_tv(L, p, GetRandomValue(0, 3), frand(0.8f, 1.4f));
        if (kind == 1) add_tv(L, (Vector3){ p.x, 0.85f, p.z }, GetRandomValue(0, 3), 0.9f);
        if (kind == 2) { add_tv(L, (Vector3){ p.x + 1.3f, 0, p.z + 0.2f }, GetRandomValue(0, 3), 1.0f); add_tv(L, (Vector3){ p.x - 1.2f, 0, p.z - 0.3f }, GetRandomValue(0, 3), 0.9f); }
        i++;
    }
    // poles carrying wires to nowhere
    for (int i = 0; i < 10; i++) {
        float x = -55 + i * 12.0f, z = 18.0f + sinf(i * 0.7f) * 3.0f;
        add_box(L, (Vector3){ x, 4.5f, z }, (Vector3){ 0.12f, 4.5f, 0.12f }, TEX_WOOD, (Color){ 60, 56, 52, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ x, 8.4f, z }, (Vector3){ 0.9f, 0.06f, 0.08f }, TEX_WOOD, (Color){ 60, 56, 52, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ x + 6, 8.35f, z }, (Vector3){ 6, 0.01f, 0.01f }, TEX_CONCRETE, (Color){ 20, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
    }
    {   // one street lamp, and someone standing under it
        Vector3 lp = { frand(-30, 30), 0, frand(-40, -15) };
        add_box(L, (Vector3){ lp.x, 2.2f, lp.z }, (Vector3){ 0.06f, 2.2f, 0.06f }, TEX_RUST, (Color){ 60, 60, 60, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ lp.x, 4.45f, lp.z + 0.3f }, (Vector3){ 0.14f, 0.06f, 0.2f }, TEX_CONCRETE, (Color){ 230, 220, 190, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        add_box(L, (Vector3){ lp.x, 0.01f, lp.z + 0.4f }, (Vector3){ 1.4f, 0.004f, 1.4f }, TEX_CONCRETE, (Color){ 60, 58, 50, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        add_effigy(L, FIG_PENITENT, (Vector3){ lp.x + 0.3f, 0, lp.z + 0.6f }, frand(0, 6.28f), 0.2f);
    }
    // a door frame standing in the snow, with nothing on either side of it
    add_box(L, (Vector3){ 9, 1.2f, -9 }, (Vector3){ 0.08f, 1.2f, 0.08f }, TEX_WOOD, (Color){ 120, 110, 100, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 10.4f, 1.2f, -9 }, (Vector3){ 0.08f, 1.2f, 0.08f }, TEX_WOOD, (Color){ 120, 110, 100, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 9.7f, 2.4f, -9 }, (Vector3){ 0.78f, 0.08f, 0.08f }, TEX_WOOD, (Color){ 120, 110, 100, 255 }, 1.0f, F_NOCOLLIDE);
    L->spawn = (Vector3){ 0, 0.05f, 0 }; L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.2f, 0.01f, -1.6f }, NOTE_STATIC);
    add_note(L, (Vector3){ 9.7f, 0.01f, -8.4f }, NOTE_DOORS);
}

// ---------------------------------------------------------------- DINNER: the table
// a dining table that does not end, lit by candles, with the family seated all along it. one chair is empty
static void build_dinner(Level *L, int seed) {
    L->ambient = 0.3f;
    L->name = "THE DINNER";
    SetRandomSeed(seed * 787 + 41);
    const float Wr = 64;
    L->fog = (Color){ 10, 4, 3, 255 };
    L->fogDensity = 0.07f;
    L->light = 0.85f;
    L->killY = -50;
    L->wrap = Wr;
    L->gradeLo = (Color){ 124, 116, 120, 255 }; L->gradeHi = (Color){ 160, 126, 104, 255 };
    L->moteCol = (Color){ 140, 100, 70, 255 };
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ 100, 0.5f, 100 }, TEX_MOSAIC, (Color){ 60, 18, 20, 255 }, 1.0f, 0);
    // the table runs the whole length of the dream, so it has no end
    add_box(L, (Vector3){ 0, 0.78f, 0 }, (Vector3){ 1.0f, 0.04f, Wr / 2 }, TEX_WOOD, (Color){ 90, 50, 36, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 0, 0.81f, 0 }, (Vector3){ 0.85f, 0.005f, Wr / 2 }, TEX_CLOTH, (Color){ 200, 190, 170, 255 }, 1.0f, F_NOCOLLIDE);
    for (float z = -Wr / 2 + 1; z < Wr / 2; z += 4) for (int s = -1; s <= 1; s += 2)
        add_box(L, (Vector3){ s * 0.85f, 0.38f, z }, (Vector3){ 0.05f, 0.38f, 0.05f }, TEX_WOOD, (Color){ 70, 40, 30, 255 }, 1.0f, F_NOCOLLIDE);
    for (float z = -Wr / 2 + 3; z < Wr / 2; z += 6) {   // candelabras
        add_box(L, (Vector3){ 0, 0.95f, z }, (Vector3){ 0.03f, 0.14f, 0.03f }, TEX_RUST, (Color){ 160, 130, 70, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ 0, 1.08f, z }, (Vector3){ 0.2f, 0.015f, 0.015f }, TEX_RUST, (Color){ 160, 130, 70, 255 }, 1.0f, F_NOCOLLIDE);
        for (int k = -1; k <= 1; k++) add_candle(L, (Vector3){ k * 0.2f, 1.09f, z }, 0.18f, false);
    }
    // the family, seated. every place is laid, and nobody has touched anything
    int empty = GetRandomValue(0, 19);
    for (int i = 0; i < 20; i++) {
        float z = -Wr / 2 + 1.6f + i * 3.2f;
        for (int s = -1; s <= 1; s += 2) {
            float x = s * 1.45f;
            add_chair(L, (Vector3){ x, 0, z }, s < 0 ? 1.5708f : -1.5708f);
            add_box(L, (Vector3){ s * 0.5f, 0.82f, z }, (Vector3){ 0.18f, 0.008f, 0.18f }, TEX_SKIN, (Color){ 230, 226, 216, 255 }, 1.0f, F_NOCOLLIDE);
            add_box(L, (Vector3){ s * 0.5f, 0.834f, z }, (Vector3){ 0.09f, 0.008f, 0.07f }, TEX_FLESH, (Color){ 90, 30, 30, 255 }, 1.0f, F_NOCOLLIDE);   // on every plate, something dark
            if (i == empty && s > 0) {
                Use u = { { x, 0.6f, z }, USE_SIT, 0, false };
                Uses_push(&L->uses, u);
                add_note(L, (Vector3){ s * 0.5f, 0.85f, z + 0.35f }, NOTE_FAMILY);
                continue;
            }
            add_effigy(L, FIG_SEATED, (Vector3){ x - s * 0.05f, 0, z }, s < 0 ? 1.5708f : -1.5708f, frand(-0.2f, 0.2f));
        }
    }
    L->spawn = (Vector3){ 3.5f, 0.05f, -Wr / 2 + 1.6f + empty * 3.2f + 6 }; L->spawnYaw = 270;
}

// ---------------------------------------------------------------- BELOW: underneath everything
// one way down, west and deeper: a ribbed passage, a chamber of wrapped bodies hanging, a long tunnel (something comes
// out of one of the bodies and you run), a hall of arms, a passage of mouths, and at the bottom the heart, and him
static void flesh_tunnel(Level *L, float x0, float x1, float y0, float y1, float z, float w, float h) {   // runs along x, stepping down from y0 (east end, x1) to y1 (west end, x0)
    const Color FL = { 170, 90, 96, 255 }, BONE = { 200, 180, 150, 255 };
    int steps = (int)fmaxf(1, fabsf(y0 - y1) / 0.15f);
    float len = x1 - x0, sl = len / steps;
    for (int k = 0; k < steps; k++) {
        float y = y0 - (y0 - y1) * (k + 1) / steps;
        add_box(L, (Vector3){ x1 - sl * (k + 0.5f), y - 0.5f, z }, (Vector3){ sl / 2, 0.5f, w / 2 + 0.3f }, TEX_SLUDGE, (Color){ 120, 40, 40, 255 }, 2.0f, 0);
    }
    float top = fmaxf(y0, y1) + h, bot = fminf(y0, y1) - 0.2f;
    add_box(L, (Vector3){ (x0 + x1) / 2, top + 0.5f, z }, (Vector3){ len / 2, 0.5f, w / 2 + 0.6f }, TEX_FLESH, FL, 2.0f, 0);
    add_box(L, (Vector3){ (x0 + x1) / 2, (top + bot) / 2, z - w / 2 - 0.3f }, (Vector3){ len / 2, (top - bot) / 2, 0.3f }, TEX_FLESH, FL, 2.0f, 0);
    add_box(L, (Vector3){ (x0 + x1) / 2, (top + bot) / 2, z + w / 2 + 0.3f }, (Vector3){ len / 2, (top - bot) / 2, 0.3f }, TEX_FLESH, FL, 2.0f, 0);
    for (float x = x1 - 1.0f; x > x0; x -= 1.6f) {   // ribs
        float fy = y0 - (y0 - y1) * (x1 - x) / len;
        add_box(L, (Vector3){ x, fy + h - 0.15f, z }, (Vector3){ 0.1f, 0.1f, w / 2 }, TEX_SKIN, BONE, 1.0f, F_NOCOLLIDE);
        for (int s = -1; s <= 1; s += 2) add_box(L, (Vector3){ x, fy + h / 2, z + s * (w / 2 - 0.05f) }, (Vector3){ 0.1f, h / 2, 0.06f }, TEX_SKIN, BONE, 1.0f, F_NOCOLLIDE);
    }
}
static void flesh_room(Level *L, float x0, float x1, float z0, float z1, float y, float h, float doorE, float doorW) {   // doorways in the east and west walls at z = door (NAN for none)
    const Color FL = { 170, 90, 96, 255 };
    add_box(L, (Vector3){ (x0 + x1) / 2, y - 0.5f, (z0 + z1) / 2 }, (Vector3){ (x1 - x0) / 2 + 0.6f, 0.5f, (z1 - z0) / 2 + 0.6f }, TEX_SLUDGE, (Color){ 120, 40, 40, 255 }, 2.0f, 0);
    add_box(L, (Vector3){ (x0 + x1) / 2, y + h + 0.5f, (z0 + z1) / 2 }, (Vector3){ (x1 - x0) / 2 + 0.6f, 0.5f, (z1 - z0) / 2 + 0.6f }, TEX_FLESH, FL, 3.0f, 0);
    wall_z(L, z0 - WT, x0 - 0.3f, x1 + 0.3f, y, y + h, TEX_FLESH, FL);
    wall_z(L, z1 + WT, x0 - 0.3f, x1 + 0.3f, y, y + h, TEX_FLESH, FL);
    if (isnan(doorE)) wall_x(L, x1 + WT, z0, z1, y, y + h, TEX_FLESH, FL); else wall_x_door(L, x1 + WT, z0, z1, y, y + h, doorE - 1.5f, doorE + 1.5f, y + 3.2f, TEX_FLESH, FL);
    if (isnan(doorW)) wall_x(L, x0 - WT, z0, z1, y, y + h, TEX_FLESH, FL); else wall_x_door(L, x0 - WT, z0, z1, y, y + h, doorW - 1.5f, doorW + 1.5f, y + 3.2f, TEX_FLESH, FL);
}
static void build_womb(Level *L, int seed) {
    L->name = "BELOW";
    L->ambient = 0.22f;
    SetRandomSeed(seed * 9973 + 3);
    L->fog = (Color){ 26, 4, 6, 255 };
    L->fogDensity = 0.07f;
    L->light = 0.9f;
    L->killY = -50;
    L->gradeLo = (Color){ 136, 112, 116, 255 }; L->gradeHi = (Color){ 168, 118, 108, 255 };
    L->moteCol = (Color){ 160, 60, 60, 255 };
    const float Z = -29;
    // the way in: a long ribbed throat going down, the far end of it closed behind you
    flesh_tunnel(L, 9, 30, 0, -2, Z, 3, 3.2f);
    add_box(L, (Vector3){ 30.4f, 1.2f, Z }, (Vector3){ 0.3f, 2.5f, 1.8f }, TEX_FLESH, (Color){ 150, 70, 76, 255 }, 2.0f, 0);
    // the chamber of the hung: tall, and full of them
    flesh_room(L, -9, 9, Z - 9, Z + 9, -2, 9, Z, Z);
    for (int i = 0; i < 14; i++) add_effigy(L, FIG_COCOON, (Vector3){ frand(-7.5f, 7.5f), 7, Z + frand(-7.5f, 7.5f) }, frand(0, 6.28f), 0);
    for (int i = 0; i < 6; i++) add_box(L, (Vector3){ frand(-7, 7), 6.9f, Z + frand(-7, 7) }, (Vector3){ 0.35f, 0.03f, 0.35f }, TEX_CONCRETE, (Color){ 150, 30, 30, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    // the long tunnel west, where you will be running
    flesh_tunnel(L, -40, -9, -2, -4, Z, 3, 3.4f);
    // the hall of arms
    flesh_room(L, -60, -40, Z - 4, Z + 4, -4, 5, Z, Z);
    for (int i = 0; i < 4; i++) add_box(L, (Vector3){ -43 - i * 5.0f, 0.95f, Z }, (Vector3){ 0.3f, 0.03f, 0.3f }, TEX_CONCRETE, (Color){ 150, 30, 30, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_decal(L, "DON'T STAND STILL", (Vector3){ -40 - WT - 0.02f, -1.4f, Z + 3 }, 0, -1, 0.14f, BLOOD);
    // the passage of mouths
    flesh_tunnel(L, -78, -60, -4, -6, Z, 3, 3.2f);
    // the bottom: the heart, the bed, him, and a door home in the far wall
    flesh_room(L, -98, -78, Z - 10, Z + 10, -6, 10, Z, NAN);
    L->heart = (Vector3){ -92, -4.2f, Z - 4 };
    add_box(L, (Vector3){ -88, -5.7f, Z + 3 }, (Vector3){ 1.2f, 0.3f, 1.6f }, TEX_FLESH, (Color){ 190, 120, 120, 255 }, 1.0f, 0);
    add_effigy(L, FIG_SLEEPER, (Vector3){ -88, -5.38f, Z + 3 }, 3.14159f, 0);
    Use face = { { -88, -5.1f, Z + 1.9f }, USE_FACE, 0, false };
    Uses_push(&L->uses, face);
    add_door(L, (Vector3){ -98 + WT, -6, Z + 6 }, (Vector3){ 1, 0, 0 }, (Color){ 255, 214, 150, 255 }, W_HUB, 0, "HOME");
    L->spawn = (Vector3){ 28, 0.05f, Z }; L->spawnYaw = 270;
    add_note(L, (Vector3){ 26.6f, 0.02f, Z + 0.8f }, NOTE_WOMB);
    add_note(L, (Vector3){ -84, -5.98f, Z - 2 }, NOTE_AWAKE);
}

// ---------------------------------------------------------------- CITY: the empty city
// a grid of blocks in the rain that goes on forever. nobody lives here any more and the lights are left on.
// a tall man stands at the ends of streets (main). there is a hospital, an underground, and a telephone
static void build_city(Level *L, int seed) {
    L->ambient = 0.55f;
    L->name = "THE CITY";
    SetRandomSeed(seed * 5581 + 13);
    const float Wr = 96, B = 8;   // the dream repeats every 96 m; blocks are 16 m with 8 m streets between
    L->fog = (Color){ 48, 54, 64, 255 };   // pale enough that anything standing in the street is a silhouette against it
    L->fogDensity = 0.04f;
    L->light = 0.85f;
    L->killY = -50;
    L->wrap = Wr;
    L->rain = true;
    L->gradeLo = (Color){ 116, 126, 146, 255 }; L->gradeHi = (Color){ 136, 132, 128, 255 };
    L->moteCol = (Color){ 120, 140, 170, 255 };
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ 100, 0.5f, 100 }, TEX_CONCRETE, (Color){ 46, 48, 54, 255 }, 3.0f, 0);
    int hospital = GetRandomValue(0, 15), subway = (hospital + 5 + GetRandomValue(0, 5)) % 16, phone = GetRandomValue(0, 15), theatre = (hospital + 2) % 16;
    if (theatre == subway) theatre = (theatre + 1) % 16;
    for (int bi = 0; bi < 16; bi++) {
        float cx = -36 + (bi % 4) * 24.0f, cz = -36 + (bi / 4) * 24.0f;
        add_box(L, (Vector3){ cx, 0.07f, cz }, (Vector3){ B + 1.2f, 0.07f, B + 1.2f }, TEX_CONCRETE, (Color){ 80, 80, 84, 255 }, 1.5f, F_NOCOLLIDE);   // pavement
        // one to four buildings to a block, all different heights
        int split = GetRandomValue(0, 2);
        for (int k = 0; k < (split == 0 ? 1 : split == 1 ? 2 : 4); k++) {
            float hx = split == 0 ? B : split == 1 ? B : B / 2, hz = split == 0 ? B : split == 1 ? B / 2 : B / 2;
            float ox = split == 2 ? ((k & 1) ? B / 2 : -B / 2) : 0, oz = split == 1 ? (k ? B / 2 : -B / 2) : split == 2 ? ((k & 2) ? B / 2 : -B / 2) : 0;
            float h = frand(8, 34);
            Color tint = scale_tint((Color){ 150, 146, 150, 255 }, frand(0.7f, 1.05f));
            add_box(L, (Vector3){ cx + ox, h / 2, cz + oz }, (Vector3){ hx - 0.2f, h / 2, hz - 0.2f }, TEX_FACADE, tint, 4.0f, 0);
            // a few windows still lit
            for (int wdw = 0; wdw < 6; wdw++) {
                int side = GetRandomValue(0, 3);
                float wy = 2.0f + 4.0f * GetRandomValue(0, (int)(h / 4) - 1), along = frand(-hx + 1.5f, hx - 1.5f);
                Vector3 c = { cx + ox, wy, cz + oz };
                Vector3 hh;
                if (side == 0) { c.z -= hz - 0.15f; c.x += along; hh = (Vector3){ 0.5f, 0.6f, 0.02f }; }
                else if (side == 1) { c.z += hz - 0.15f; c.x += along; hh = (Vector3){ 0.5f, 0.6f, 0.02f }; }
                else if (side == 2) { c.x -= hx - 0.15f; c.z += along; hh = (Vector3){ 0.02f, 0.6f, 0.5f }; }
                else { c.x += hx - 0.15f; c.z += along; hh = (Vector3){ 0.02f, 0.6f, 0.5f }; }
                if (wy > h - 1) continue;
                add_box(L, c, hh, TEX_CONCRETE, GetRandomValue(0, 3) ? (Color){ 220, 170, 100, 255 } : (Color){ 110, 140, 200, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
            }
        }
        // street lamps on two corners
        for (int k = 0; k < 2; k++) {
            float lx = cx + (k ? B + 0.8f : -B - 0.8f), lz = cz + (k ? -B - 0.8f : B + 0.8f);
            add_box(L, (Vector3){ lx, 2.6f, lz }, (Vector3){ 0.07f, 2.6f, 0.07f }, TEX_RUST, (Color){ 50, 52, 56, 255 }, 1.0f, F_NOCOLLIDE);
            if (GetRandomValue(0, 3)) add_box(L, (Vector3){ lx, 5.2f, lz }, (Vector3){ 0.18f, 0.08f, 0.18f }, TEX_CONCRETE, (Color){ 230, 200, 150, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        }
        if (GetRandomValue(0, 2) == 0) {   // a car nobody came back for
            float x = cx + B + 4.0f, z = cz + frand(-5, 5);
            Color car = scale_tint((Color){ 120, 60, 50, 255 }, frand(0.4f, 1.0f));
            add_box(L, (Vector3){ x, 0.55f, z }, (Vector3){ 0.9f, 0.4f, 2.1f }, TEX_RUST, car, 1.0f, 0);
            add_box(L, (Vector3){ x, 1.2f, z + 0.2f }, (Vector3){ 0.8f, 0.3f, 1.1f }, TEX_WATER, (Color){ 40, 46, 56, 255 }, 1.0f, 0);
        }
        Vector3 face = { cx, 0, cz - B + 0.2f };   // the north face of the block, on the pavement
        if (bi == hospital) {
            add_door(L, (Vector3){ face.x, 0.14f, face.z - 0.2f + WT }, (Vector3){ 0, 0, -1 }, (Color){ 190, 220, 200, 255 }, W_WARD, 0, "ST. AGATHA'S");
            add_decal(L, "ST AGATHA'S HOSPITAL", (Vector3){ face.x, 3.2f, face.z - 0.4f }, 2, -1, 0.3f, (Color){ 200, 210, 200, 255 });
            add_box(L, (Vector3){ face.x, 3.2f, face.z - 0.6f }, (Vector3){ 1.6f, 0.03f, 0.4f }, TEX_CONCRETE, (Color){ 180, 220, 190, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        }
        if (bi == subway) {   // steps going down under the road, behind railings, and grey light coming up
            Vector3 s = { cx + B + 4.0f, 0, cz };
            for (int k = -1; k <= 1; k += 2) add_box(L, (Vector3){ s.x + k * 1.4f, 0.55f, s.z }, (Vector3){ 0.04f, 0.55f, 1.6f }, TEX_RUST, (Color){ 40, 60, 50, 255 }, 1.0f, 0);
            add_box(L, (Vector3){ s.x, 0.55f, s.z + 1.6f }, (Vector3){ 1.4f, 0.55f, 0.04f }, TEX_RUST, (Color){ 40, 60, 50, 255 }, 1.0f, 0);
            add_box(L, (Vector3){ s.x, 0.012f, s.z - 0.2f }, (Vector3){ 1.3f, 0.01f, 1.4f }, TEX_CONCRETE, (Color){ 120, 120, 124, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
            add_decal(L, "UNDERGROUND", (Vector3){ s.x, 1.6f, s.z + 1.64f }, 2, -1, 0.18f, (Color){ 220, 220, 220, 255 });
            add_portal(L, (Vector3){ s.x, 0, s.z - 0.6f }, W_STATIC, 0, (Color){ 150, 150, 150, 255 }, "UNDERGROUND");
            L->portals.data[L->portals.size - 1].radius = 0.9f;
        }
        if (bi == theatre) {   // a theatre, its marquee half lit: tonight, the sleeper
            add_door(L, (Vector3){ face.x, 0.14f, face.z - 0.2f + WT }, (Vector3){ 0, 0, -1 }, (Color){ 230, 170, 100, 255 }, W_THEATRE, 0, "THE THEATRE");
            add_box(L, (Vector3){ face.x, 3.4f, face.z - 1.2f }, (Vector3){ 3.5f, 0.5f, 1.0f }, TEX_CONCRETE, (Color){ 30, 26, 24, 255 }, 1.0f, F_NOCOLLIDE);
            add_decal(L, "TONIGHT  THE SLEEPER", (Vector3){ face.x, 3.4f, face.z - 2.22f }, 2, -1, 0.22f, (Color){ 240, 220, 170, 255 });
            for (int k = 0; k < 14; k++) if (GetRandomValue(0, 3)) add_box(L, (Vector3){ face.x - 3.3f + k * 0.5f, 3.95f, face.z - 2.2f }, (Vector3){ 0.07f, 0.07f, 0.07f }, TEX_CONCRETE, (Color){ 255, 210, 140, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
        }
        if (bi == phone) {   // a telephone box on the corner. sometimes it rings
            Vector3 p = { cx - B - 1.4f, 0, cz - B - 1.4f };
            add_box(L, (Vector3){ p.x, 1.2f, p.z }, (Vector3){ 0.5f, 1.2f, 0.5f }, TEX_RUST, (Color){ 120, 30, 26, 255 }, 1.0f, F_NOCOLLIDE);
            add_box(L, (Vector3){ p.x, 2.25f, p.z }, (Vector3){ 0.45f, 0.1f, 0.45f }, TEX_CONCRETE, (Color){ 240, 230, 200, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
            Use u = { { p.x + 0.6f, 1.2f, p.z + 0.6f }, USE_LINK, 1, false };
            Uses_push(&L->uses, u);
            L->bed = p;   // where the ringing comes from
        }
    }
    // a few people standing in the street with their faces to the wall. you never get close to one
    for (int i = 0; i < 3; i++) {
        int bi = GetRandomValue(0, 15);
        float cx = -36 + (bi % 4) * 24.0f, cz = -36 + (bi / 4) * 24.0f;
        add_effigy(L, FIG_PENITENT, (Vector3){ cx + frand(-4, 4), 0.14f, cz + B + 0.5f }, 3.14159f, frand(-0.3f, 0.3f));
    }
    L->spawn = (Vector3){ 0, 0.05f, 4 }; L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.0f, 0.02f, 2.6f }, NOTE_CITY);
}

// ---------------------------------------------------------------- MORGUE: down the lift from the ward
// cold steel and tile. drawers in the walls that open by themselves, tables with sheeted bodies that are sitting
// up when you look round, and one that gets down when the lights fail (main)
static void build_morgue(Level *L, int seed) {
    L->name = "THE MORTUARY";
    L->ambient = 0.18f;
    SetRandomSeed(seed * 1499 + 7);
    L->fog = (Color){ 6, 10, 14, 255 };
    L->fogDensity = 0.08f;
    L->light = 0.9f;
    L->killY = -50;
    L->gradeLo = (Color){ 120, 130, 146, 255 }; L->gradeHi = (Color){ 132, 138, 140, 255 };
    L->moteCol = (Color){ 160, 170, 190, 255 };
    const float H = 4.0f;
    const Color STEEL = { 150, 160, 170, 255 }, TW = { 150, 170, 176, 255 };
    slab(L, -8, 8, -21, 5, 0, 1, TEX_POOL, TW, 1.0f);
    slab(L, -8, 8, -21, 5, H + 1, 1, TEX_POOL, scale_tint(TW, 0.5f), 2.0f);
    wall_z(L, 5 + WT, -8.1f, 8.1f, 0, H, TEX_POOL, TW);
    wall_z(L, -21 - WT, -8.1f, 8.1f, 0, H, TEX_POOL, TW);
    for (int s = -1; s <= 1; s += 2) {   // the walls are all drawers
        wall_x(L, s * (8 + WT), -21, 5, 0, H, TEX_POOL, TW);
        for (int r = 0; r < 3; r++) for (float z = -20.2f; z < 4.5f; z += 1.0f) {
            add_box(L, (Vector3){ s * 7.95f, 0.6f + r * 0.9f, z }, (Vector3){ 0.05f, 0.4f, 0.45f }, TEX_CONCRETE, STEEL, 1.0f, F_NOCOLLIDE);
            add_box(L, (Vector3){ s * 7.88f, 0.6f + r * 0.9f, z }, (Vector3){ 0.02f, 0.04f, 0.18f }, TEX_CONCRETE, (Color){ 90, 90, 96, 255 }, 1.0f, F_NOCOLLIDE);
        }
    }
    // tables, two rows of them, with somebody on each under a sheet
    for (int k = 0; k < 10; k++) {
        float x = (k & 1) ? 3.0f : -3.0f, z = -18.0f + (k / 2) * 4.0f;
        add_box(L, (Vector3){ x, 0.9f, z }, (Vector3){ 0.55f, 0.04f, 1.1f }, TEX_CONCRETE, STEEL, 1.0f, 0);
        add_box(L, (Vector3){ x, 0.45f, z }, (Vector3){ 0.08f, 0.45f, 0.08f }, TEX_CONCRETE, STEEL, 1.0f, 0);
        add_effigy(L, FIG_SLEEPER, (Vector3){ x, 0.94f, z }, 0, 0);
        add_box(L, (Vector3){ x, H - 0.04f, z }, (Vector3){ 0.5f, 0.02f, 0.12f }, TEX_CONCRETE, (Color){ 170, 190, 210, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    }
    add_decal(L, "DRAWER 6 - DO NOT OPEN", (Vector3){ -8 + WT + 0.02f, 3.2f, -6 }, 0, 1, 0.12f, SOOT);
    // a drain in the floor at the far end, and water running into it from somewhere
    add_box(L, (Vector3){ 0, 0.01f, -19.5f }, (Vector3){ 0.6f, 0.006f, 0.6f }, TEX_RUST, (Color){ 30, 30, 34, 255 }, 1.0f, F_NOCOLLIDE);
    add_portal(L, (Vector3){ 0, 0, -19.5f }, W_BATHS, 0, (Color){ 120, 180, 190, 255 }, "THE DRAIN");
    L->portals.data[L->portals.size - 1].radius = 0.6f;
    add_door(L, (Vector3){ 0, 0, 5 - WT }, (Vector3){ 0, 0, -1 }, (Color){ 190, 220, 200, 255 }, W_WARD, 0, "THE LIFT");
    L->spawn = (Vector3){ 0, 0.05f, 3.0f }; L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.0f, 0.02f, 1.8f }, NOTE_MORGUE);
}

// ---------------------------------------------------------------- THEATRE: tonight, the sleeper
// a dark auditorium with an audience of mannequins, all seated, all facing the stage. on the stage, marionettes act
// out a child asleep and a woman bending over him. between scenes the house lights go out (main)
static void build_theatre(Level *L, int seed) {
    L->name = "THE THEATRE";
    L->ambient = 0.14f;
    SetRandomSeed(seed * 2203 + 19);
    L->fog = (Color){ 8, 4, 4, 255 };
    L->fogDensity = 0.05f;
    L->light = 0.9f;
    L->killY = -50;
    L->gradeLo = (Color){ 130, 116, 116, 255 }; L->gradeHi = (Color){ 162, 126, 110, 255 };
    L->moteCol = (Color){ 170, 150, 120, 255 };
    const float H = 14;
    const Color CRIMSON = { 100, 22, 26, 255 }, VEL = { 120, 30, 34, 255 };
    slab(L, -12, 12, -10, 27, 0, 1, TEX_WOOD, (Color){ 90, 60, 50, 255 }, 2.0f);
    slab(L, -12, 12, -10, 27, H + 1, 1, TEX_CLOTH, (Color){ 40, 14, 16, 255 }, 4.0f);
    wall_x(L, -12 - WT, -10, 27, 0, H, TEX_CLOTH, CRIMSON); wall_x(L, 12 + WT, -10, 27, 0, H, TEX_CLOTH, CRIMSON);
    wall_z(L, 27 + WT, -12.1f, 12.1f, 0, H, TEX_CLOTH, CRIMSON); wall_z(L, -10 - WT, -12.1f, 12.1f, 0, H, TEX_CLOTH, CRIMSON);
    add_box(L, (Vector3){ 0, 0.012f, 22 }, (Vector3){ 12, 0.006f, 4 }, TEX_MOSAIC, (Color){ 34, 10, 12, 255 }, 2.0f, F_NOCOLLIDE);
    // rows of seats, and in some of them the audience
    for (int r = 0; r < 9; r++) for (int c = 0; c < 18; c++) {
        float z = 3.0f + r * 2.2f, x = -10.5f + c * 1.0f + (c >= 9 ? 1.4f : 0);
        add_box(L, (Vector3){ x, 0.25f, z }, (Vector3){ 0.4f, 0.25f, 0.35f }, TEX_CLOTH, VEL, 1.0f, 0);
        add_box(L, (Vector3){ x, 0.75f, z + 0.38f }, (Vector3){ 0.4f, 0.5f, 0.06f }, TEX_CLOTH, VEL, 1.0f, 0);
        if (GetRandomValue(0, 99) < 35) add_effigy(L, FIG_SEATED, (Vector3){ x, -0.05f, z + 0.05f }, 0, frand(-0.2f, 0.2f));
    }
    for (int i = 0; i < L->effigies.size; i++) L->effigies.data[i].scale = 0;   // (seated mannequins are their own size)
    // the stage, the arch round it, the curtains drawn back, and a light on the bed
    add_box(L, (Vector3){ 0, 0.6f, -5.5f }, (Vector3){ 10, 0.6f, 4.5f }, TEX_WOOD, (Color){ 110, 80, 60, 255 }, 2.0f, 0);
    for (int s = -1; s <= 1; s += 2) {
        add_box(L, (Vector3){ s * 9.5f, 6, -1.0f }, (Vector3){ 2.5f, 6, 0.3f }, TEX_CLOTH, CRIMSON, 2.0f, 0);
        add_box(L, (Vector3){ s * 6.4f, 6.5f, -1.3f }, (Vector3){ 0.6f, 5.3f, 0.2f }, TEX_CLOTH, (Color){ 150, 30, 34, 255 }, 1.5f, F_NOCOLLIDE);
    }
    add_box(L, (Vector3){ 0, 11, -1.0f }, (Vector3){ 7, 1.0f, 0.3f }, TEX_CLOTH, CRIMSON, 2.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 0, 1.5f, -6 }, (Vector3){ 1.0f, 0.3f, 1.6f }, TEX_WOOD, (Color){ 160, 140, 130, 255 }, 1.0f, 0);   // the bed in the play
    add_effigy(L, FIG_SLEEPER, (Vector3){ 0, 1.8f, -6 }, 0, 0);
    add_box(L, (Vector3){ 0, 1.215f, -5 }, (Vector3){ 2.6f, 0.004f, 2.6f }, TEX_CONCRETE, (Color){ 120, 110, 80, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    for (int i = 0; i < 12; i++) add_box(L, (Vector3){ -9 + i * 1.64f, 1.25f, -1.4f }, (Vector3){ 0.1f, 0.06f, 0.06f }, TEX_CONCRETE, (Color){ 230, 190, 120, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);   // footlights
    add_door(L, (Vector3){ -9.0f, 1.2f, -10 + WT }, (Vector3){ 0, 0, 1 }, (Color){ 255, 170, 90, 255 }, W_DINNER, 0, "BACKSTAGE");
    add_door(L, (Vector3){ 0, 0, 27 - WT }, (Vector3){ 0, 0, -1 }, (Color){ 150, 170, 200, 255 }, W_CITY, 0, "THE STREET");
    L->spawn = (Vector3){ 0, 0.05f, 24.5f }; L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.2f, 0.02f, 23.5f }, NOTE_THEATRE);
}

// ---------------------------------------------------------------- CHAPEL: the lower church
// a nave full of people standing in their pews. when the bell has rung three times they kneel, and the one at
// the altar counts them. the veil is on the altar
static void build_chapel(Level *L, int seed) {
    L->ambient = 0.28f;
    L->name = "THE LOWER CHURCH";
    SetRandomSeed(seed * 4409 + 17);
    L->fog = (Color){ 22, 12, 12, 255 };
    L->fogDensity = 0.05f;
    L->light = 0.8f;
    L->killY = -50;
    L->gradeLo = (Color){ 126, 118, 122, 255 }; L->gradeHi = (Color){ 156, 124, 110, 255 };
    L->moteCol = (Color){ 140, 120, 100, 255 };
    const float X = 6, Z0 = -34, Z1 = 6, H = 9;
    const Color STONE = { 120, 110, 104, 255 };
    slab(L, -X, X, Z0, Z1, 0, 1, TEX_TILE, (Color){ 96, 90, 84, 255 }, 1.5f);
    slab(L, -X, X, Z0, Z1, H + 1, 1, TEX_CONCRETE, (Color){ 60, 56, 54, 255 }, 3.0f);
    wall_x(L, -X, Z0, Z1, 0, H, TEX_CONCRETE, STONE); wall_x(L, X, Z0, Z1, 0, H, TEX_CONCRETE, STONE);
    wall_z(L, Z0, -X, X, 0, H, TEX_CONCRETE, STONE); wall_z(L, Z1, -X, X, 0, H, TEX_CONCRETE, STONE);
    for (int z = 0; z >= -30; z -= 6) for (int s = -1; s <= 1; s += 2)   // columns
        add_box(L, (Vector3){ s * 3.9f, H / 2, (float)z }, (Vector3){ 0.35f, H / 2, 0.35f }, TEX_CONCRETE, (Color){ 100, 92, 88, 255 }, 1.5f, 0);
    // pews, and people standing in them
    int people = 0;
    for (int r = 0; r < 10; r++) {
        float z = -2.0f - r * 2.3f;
        for (int s = -1; s <= 1; s += 2) {
            float cx = s * 3.45f;
            add_box(L, (Vector3){ cx, 0.45f, z }, (Vector3){ 2.0f, 0.04f, 0.28f }, TEX_WOOD, (Color){ 90, 64, 48, 255 }, 1.0f, 0);
            add_box(L, (Vector3){ cx, 0.22f, z }, (Vector3){ 2.0f, 0.22f, 0.04f }, TEX_WOOD, (Color){ 80, 56, 42, 255 }, 1.0f, F_NOCOLLIDE);
            add_box(L, (Vector3){ cx, 0.8f, z + 0.3f }, (Vector3){ 2.0f, 0.38f, 0.04f }, TEX_WOOD, (Color){ 90, 64, 48, 255 }, 1.0f, 0);
            for (int k = 0; k < 4 && people < 34; k++) {
                if (GetRandomValue(0, 99) < 35) continue;
                add_effigy(L, FIG_PENITENT, (Vector3){ cx - 1.5f + k * 1.0f + frand(-0.15f, 0.15f), 0, z - 0.55f }, 0, frand(-0.2f, 0.2f));
                people++;
            }
        }
    }
    // the sanctuary: a step up, the altar, the one who counts, and the cross the wrong way up
    slab(L, -X, X, Z0, -27.5f, 0.4f, 0.4f, TEX_TILE, (Color){ 110, 40, 40, 255 }, 1.0f);
    add_box(L, (Vector3){ 0, 0.4f + 0.55f, -30.5f }, (Vector3){ 1.6f, 0.55f, 0.6f }, TEX_CLOTH, (Color){ 100, 18, 20, 255 }, 1.0f, 0);
    for (int i = 0; i < 7; i++) add_candle(L, (Vector3){ -1.4f + i * 0.47f, 1.5f, -30.3f + frand(-0.2f, 0.2f) }, frand(0.2f, 0.45f), true);
    add_effigy(L, FIG_PRIEST, (Vector3){ 0, 0.4f, -32.0f }, 3.14159f, 0.0f);
    add_cross(L, (Vector3){ 0, 5.2f, Z0 + WT + 0.05f }, 2.2f, 2, (Color){ 50, 34, 30, 255 });
    add_box(L, (Vector3){ 0, 6.0f, Z0 + WT + 0.02f }, (Vector3){ 1.1f, 2.6f, 0.02f }, TEX_EYES, (Color){ 150, 40, 40, 255 }, 0.6f, F_EMIT | F_NOCOLLIDE);   // a window of eyes, lit from behind
    add_sigil(L, (Vector3){ 0, 0.004f, -25.0f }, 1.2f, (Color){ 140, 30, 24, 255 }, 77);
    for (int z = -2; z >= -26; z -= 4) for (int s = -1; s <= 1; s += 2) add_candle(L, (Vector3){ s * 1.0f, 0, (float)z }, frand(0.3f, 0.6f), z % 8 == 0);
    if (seed >= 4) add_decal(L, "IN NOMINE DEI NOSTRI", (Vector3){ -X + WT, 4.0f, -14 }, 0, 1, 0.24f, SOOT);
    add_decal(L, "COUNT THEM", (Vector3){ X - WT, 2.2f, 2.0f }, 0, -1, 0.16f, CHALK);
    Pickup pk = { { 0, 2.3f, -30.5f }, FX_VEIL, false, false };
    Pickups_push(&L->pickups, pk);
    L->spawn = (Vector3){ 0, 0.05f, 4.0f }; L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.3f, 0.01f, 3.4f }, NOTE_BELL);
    add_note(L, (Vector3){ -4.6f, 0.5f, -8.9f }, NOTE_VIGIL);   // left on a pew
    add_door(L, (Vector3){ 4.2f, 0.4f, Z0 + WT }, (Vector3){ 0, 0, 1 }, (Color){ 200, 40, 40, 255 }, W_WOMB, 0, "BELOW");   // behind the altar, a low door
}

// ---------------------------------------------------------------- END: the way out
static void build_end(Level *L) {
    L->ambient = 0.5f;
    L->name = "THE WAY OUT";
    L->fog = (Color){ 70, 60, 52, 255 };
    L->fogDensity = 0.07f;
    L->light = 0.85f;
    L->killY = -50;
    L->gradeLo = (Color){ 134, 128, 126, 255 }; L->gradeHi = (Color){ 140, 130, 122, 255 };
    L->moteCol = (Color){ 230, 225, 210, 255 };
    L->spawn = (Vector3){ 0, 0.05f, 7 };
    L->spawnYaw = 0;
    // a long white hall that ends at a bed
    room(L, 3, 12, 3.2f, TEX_TILE, (Color){ 170, 160, 150, 255 }, TEX_WOOD, (Color){ 220, 210, 200, 255 }, TEX_CONCRETE, (Color){ 230, 230, 225, 255 });
    add_box(L, (Vector3){ 0, 0.35f, -10.5f }, (Vector3){ 1.0f, 0.35f, 1.5f }, TEX_WOOD, (Color){ 220, 200, 190, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 0, 0.78f, -10.3f }, (Vector3){ 0.92f, 0.1f, 1.3f }, TEX_TILE, (Color){ 240, 230, 230, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 0, 1.8f, -11.88f }, (Vector3){ 1.0f, 0.8f, 0.05f }, TEX_CONCRETE, (Color){ 255, 255, 250, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_decal(L, "DON'T LIE DOWN", (Vector3){ 0, 2.6f, -12.0f }, 2, 1, 0.28f, BLOOD);
    L->bed = (Vector3){ 0, 0, -9.0f };
    for (int i = 0; i < 6; i++) for (int s = -1; s <= 1; s += 2) {
        float z = 4.0f - i * 2.2f;
        add_effigy(L, FIG_KNEELER, (Vector3){ s * 2.1f, 0, z }, s < 0 ? 1.5708f : -1.5708f, frand(-0.2f, 0.2f));
        add_candle(L, (Vector3){ s * 2.7f, 0, z + 1.0f }, frand(0.15f, 0.4f), false);
    }
    add_effigy(L, FIG_PRIEST, (Vector3){ 1.6f, 0, -10.6f }, 3.14159f, 0.0f);
    add_cross(L, (Vector3){ 0, 2.0f, -11.75f }, 0.5f, 2, (Color){ 40, 28, 24, 255 });   // against the light
}

// ---------------------------------------------------------------- lifecycle
void level_build(Level *L, WorldId id, int seed) {
    memset(L, 0, sizeof *L);
    L->id = id;
    L->dreamNo = seed;
    L->nearest = 99;
    L->ambient = 1.0f;
    b3WorldDef wd = b3DefaultWorldDef();
    wd.gravity = (b3Vec3){ 0, -20.0f, 0 };
    wd.workerCount = 1;
    L->phys = b3CreateWorld(&wd);
    switch (id) {
    case W_HUB:    build_hub(L, seed); break;
    case W_SHAFT:  build_shaft(L, seed); break;
    case W_BATHS:  build_baths(L, seed); break;
    case W_VOID:   build_void(L, seed); break;
    case W_NURSERY: build_nursery(L, seed); break;
    case W_CHAPEL: build_chapel(L, seed); break;
    case W_WARD:   build_ward(L, seed); break;
    case W_STATIC: build_static(L, seed); break;
    case W_DINNER: build_dinner(L, seed); break;
    case W_WOMB:   build_womb(L, seed); break;
    case W_CITY:   build_city(L, seed); break;
    case W_MORGUE: build_morgue(L, seed); break;
    case W_THEATRE: build_theatre(L, seed); break;
    default:       build_end(L); break;
    }
    // ash motes
    for (int i = 0; i < 140; i++) {
        Mote m = { { 0, 0, 0 }, { frand(-.1f, .1f), frand(-.25f, -.05f), frand(-.1f, .1f) }, frand(0, 1) };
        if (L->moteGlow) m.vel = (Vector3){ frand(-.25f, .25f), frand(0.05f, 0.35f), frand(-.25f, .25f) };
        if (L->rain) m.vel = (Vector3){ 0.6f, frand(-11, -9), 0.2f };
        Motes_push(&L->motes, m);
    }
    if (L->moteCol.a == 0) L->moteCol = (Color){ 150, 140, 120, 255 };
}

void level_free(Level *L) {
    if (b3World_IsValid(L->phys)) b3DestroyWorld(L->phys);
    Boxes_drop(&L->boxes); Pickups_drop(&L->pickups); Portals_drop(&L->portals);
    Watchers_drop(&L->watchers); Motes_drop(&L->motes); Blooms_drop(&L->blooms); Props_drop(&L->props); Effigies_drop(&L->effigies); Uses_drop(&L->uses);
    memset(L, 0, sizeof *L);
}

void level_step(Level *L, float dt, Vector3 player) {
    L->t += dt;
    b3World_Step(L->phys, dt, 4);
    if (L->sludge && L->sludgeArmed) {
        L->sludgeY += L->sludgeSpeed * dt * (1.0f + L->t * 0.004f);
    } else if (L->sludge && player.y > 3.0f) {
        L->sludgeArmed = true;
    }
    for (int i = 0; i < L->motes.size; i++) {
        Mote *m = &L->motes.data[i];
        m->pos = Vector3Add(m->pos, Vector3Scale(m->vel, dt));
        m->life -= dt * 0.15f;
        if (m->life <= 0 || Vector3Distance(m->pos, player) > 12 || (L->rain && m->pos.y < player.y - 0.5f)) {
            m->life = 1;
            m->pos = (Vector3){ player.x + frand(-10, 10), player.y + frand(0, 5), player.z + frand(-10, 10) };
        }
    }
}

// ---------------------------------------------------------------- watchers
bool level_seen(Level *L, Vector3 eye, Vector3 fwd, Vector3 pos) {
    Vector3 chest = { pos.x, pos.y + 1.6f, pos.z };
    Vector3 to = Vector3Subtract(chest, eye);
    float dist = Vector3Length(to);
    if (dist < 0.01f || dist > 45) return false;
    if (Vector3DotProduct(Vector3Scale(to, 1.0f / dist), fwd) < 0.45f) return false;
    b3RayResult r = b3World_CastRayClosest(L->phys, b3v(eye), b3v(to), b3DefaultQueryFilter());
    return !r.hit || r.fraction > 0.97f;
}
