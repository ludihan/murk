#include "world.h"
#include "gfx.h"
#include "figure.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define i_type IntV, int
#include <stc/vec.h>


int g_launches, g_wakes;

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
    add_box(L, (Vector3){ p.x, 0.45f, p.z }, (Vector3){ 0.22f, 0.03f, 0.22f }, TEX_WOOD, c, 1.0f, 0);
    for (int i = 0; i < 4; i++) add_box(L, (Vector3){ p.x + (i & 1 ? 0.18f : -0.18f), 0.21f, p.z + (i & 2 ? 0.18f : -0.18f) }, (Vector3){ 0.02f, 0.21f, 0.02f }, TEX_WOOD, c, 1.0f, F_NOCOLLIDE);
    Vector3 back = { p.x - fx * 0.2f, 0.78f, p.z - fz * 0.2f };
    add_box(L, back, alongZ ? (Vector3){ 0.22f, 0.32f, 0.025f } : (Vector3){ 0.025f, 0.32f, 0.22f }, TEX_WOOD, c, 1.0f, F_NOCOLLIDE);
}
static void add_note(Level *L, Vector3 p, int id) {
    Use u = { p, USE_NOTE, id, false };
    Uses_push(&L->uses, u);
}
static void add_effigy(Level *L, FigKind k, Vector3 p, float yaw, float tilt) {
    Effigy e = { (int)k, p, yaw, tilt, 0, false };
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

// ---------------------------------------------------------------- HUB: the room
static void build_hub(Level *L, int seed) {
    L->name = "THE ROOM";
    L->fog = (Color){ 34, 30, 26, 255 };
    L->fogDensity = 0.06f;
    L->light = 1.0f;
    L->gradeLo = (Color){ 118, 128, 150, 255 }; L->gradeHi = (Color){ 150, 128, 112, 255 };
    L->moteCol = (Color){ 150, 140, 120, 255 };
    L->spawn = (Vector3){ 0, 0.05f, 2.0f };
    L->spawnYaw = 0;
    L->killY = -50;
    const float S = 8, H = 4.2f;
    room(L, S, S, H, seed >= 7 ? TEX_EYES : TEX_TILE, seed >= 7 ? (Color){ 190, 170, 170, 255 } : (Color){ 200, 205, 180, 255 }, TEX_WOOD, (Color){ 190, 170, 150, 255 }, TEX_CONCRETE, (Color){ 140, 140, 130, 255 });

    // doors: slab glows in a dark frame, set into the wall faces
    struct { Vector3 p; int axis; Color c; WorldId to; int needs; const char *lbl; } d[5] = {
        { { 0, 1.3f, -S + 0.15f }, 0, { 230, 120, 40, 255 },  W_SHAFT,  0, "THE SHAFT" },
        { { S - 0.15f, 1.3f, 0 }, 1, { 90, 220, 90, 255 },   W_DRAINS, 0, "THE DRAINS" },
        { { -S + 0.15f, 1.3f, 0 }, 1, { 170, 90, 255, 255 }, W_VOID,   0, "THE STEPS" },
        { { 0, 1.3f, S - 0.15f }, 0, { 255, 245, 235, 255 }, W_END,    FX_ALL, "THE WAY OUT" },
        { { 3.4f, 1.3f, -S + 0.15f }, 0, { 255, 110, 190, 255 }, W_GARDEN, 0, "THE ORCHARD" },
    };
    for (int i = 0; i < 5; i++) {
        Vector3 fh = d[i].axis == 0 ? (Vector3){ 1.35f, 1.5f, 0.12f } : (Vector3){ 0.12f, 1.5f, 1.35f };
        Vector3 sh = d[i].axis == 0 ? (Vector3){ 1.0f, 1.25f, 0.05f } : (Vector3){ 0.05f, 1.25f, 1.0f };
        add_box(L, d[i].p, fh, TEX_CONCRETE, (Color){ 40, 38, 36, 255 }, 1.0f, F_NOCOLLIDE);
        Vector3 in = d[i].p;
        if (d[i].axis == 0) in.z += (d[i].p.z < 0 ? 0.08f : -0.08f); else in.x += (d[i].p.x < 0 ? 0.08f : -0.08f);
        add_box(L, in, sh, TEX_CONCRETE, d[i].c, 1.0f, F_EMIT | F_NOCOLLIDE);
        Vector3 pp = d[i].p;
        if (d[i].axis == 0) pp.z += (pp.z < 0 ? 0.6f : -0.6f); else pp.x += (pp.x < 0 ? 0.6f : -0.6f);
        pp.y = 0;
        add_portal(L, pp, d[i].to, d[i].needs, d[i].c, d[i].lbl);
    }

    // the room is never quite arranged the way you left it
    SetRandomSeed(seed * 31 + 5);
    static const float CX[4] = { -5.8f, 5.8f, -5.8f, 5.8f }, CZ[4] = { -5.4f, -5.4f, 5.4f, 5.4f };
    int bc = GetRandomValue(0, 3), dc = (bc + 1 + GetRandomValue(0, 2)) % 4;
    {   // a bed that nobody wants
        float bx = CX[bc], bz = CZ[bc], sg = bz < 0 ? 1.0f : -1.0f;
        add_box(L, (Vector3){ bx, 0.35f, bz }, (Vector3){ 1.0f, 0.35f, 1.9f }, TEX_WOOD, (Color){ 150, 120, 100, 255 }, 1.0f, 0);
        add_box(L, (Vector3){ bx, 0.78f, bz + 0.2f * sg }, (Vector3){ 0.92f, 0.1f, 1.7f }, TEX_TILE, (Color){ 150, 130, 120, 255 }, 1.0f, 0);
        add_box(L, (Vector3){ bx, 0.95f, bz - 1.4f * sg }, (Vector3){ 0.6f, 0.12f, 0.3f }, TEX_CONCRETE, (Color){ 200, 190, 170, 255 }, 1.0f, F_NOCOLLIDE);
    }
    {   // a desk with a TV that never turns off, screen turned toward the middle of the room
        float dx = CX[dc], dz = CZ[dc], sg = dz < 0 ? 1.0f : -1.0f;
        add_box(L, (Vector3){ dx, 0.45f, dz }, (Vector3){ 1.2f, 0.45f, 0.6f }, TEX_WOOD, (Color){ 140, 110, 90, 255 }, 1.0f, 0);
        add_box(L, (Vector3){ dx, 1.2f, dz }, (Vector3){ 0.5f, 0.35f, 0.4f }, TEX_CONCRETE, (Color){ 50, 50, 46, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ dx, 1.2f, dz + 0.41f * sg }, (Vector3){ 0.4f, 0.27f, 0.02f }, TEX_STATIC, (Color){ 150, 170, 160, 255 }, 1.0f, F_NOCOLLIDE);
        static const int HUBNOTE[] = { NOTE_VIGIL, NOTE_DAY9, NOTE_DOORS, NOTE_GARDEN, NOTE_FAMILY, NOTE_AWAKE };
        add_note(L, (Vector3){ dx + 0.75f, 0.91f, dz + 0.1f * sg }, HUBNOTE[(seed - 1) % 6]);
    }
    // pillars, a half wall: different cover every time
    int npil = GetRandomValue(0, 3);
    for (int i = 0; i < npil; i++) {
        float px = frand(-5.5f, 5.5f), pz = frand(-5.5f, 5.5f);
        if ((fabsf(px) < 2.2f && fabsf(pz) > 3.0f) || (fabsf(pz) < 2.2f && fabsf(px) > 3.0f)) continue;  // keep door approaches clear
        if (fabsf(px) < 1.5f && fabsf(pz - 2.0f) < 1.5f) continue;                                          // and the spawn
        if (fabsf(px - CX[bc]) < 2.4f && fabsf(pz - CZ[bc]) < 2.8f) continue;
        if (fabsf(px - CX[dc]) < 2.4f && fabsf(pz - CZ[dc]) < 2.0f) continue;
        add_box(L, (Vector3){ px, H / 2, pz }, (Vector3){ 0.32f, H / 2, 0.32f }, TEX_CONCRETE, (Color){ 130, 128, 118, 255 }, 1.5f, 0);
    }
    if (GetRandomValue(0, 1)) {
        float sx = GetRandomValue(0, 1) ? 3.8f : -3.8f, sz = GetRandomValue(0, 1) ? 3.4f : -3.4f;
        add_box(L, (Vector3){ sx, 0.55f, sz }, (Vector3){ 1.6f, 0.55f, 0.12f }, TEX_TILE, (Color){ 150, 150, 130, 255 }, 1.0f, 0);
    }
    if (seed == 1) {   // somebody is standing in the corner, facing the wall. next time you wake up they're gone.
        float fx = CX[(bc + 2) % 4] > 0 ? S - 0.6f : -S + 0.6f, fz = CZ[(bc + 2) % 4] > 0 ? S - 0.6f : -S + 0.6f;
        add_effigy(L, FIG_PENITENT, (Vector3){ fx, 0, fz }, atan2f(fx, -fz), 0.0f);
    }
    {   // every time you wake there is more of it: candles around the bed, a cross over it, chalk on the floor, chairs
        float bx = CX[bc], bz = CZ[bc];
        int candles = seed >= 2 ? (seed - 1) * 2 : 0;
        if (candles > 16) candles = 16;
        for (int i = 0; i < candles; i++) {
            float a = i * 6.2831853f / 16.0f + 0.2f;
            Vector3 cp = { bx + sinf(a) * 1.55f, 0, bz + cosf(a) * 2.35f };
            if (fabsf(cp.x) > S - 0.3f || fabsf(cp.z) > S - 0.3f) continue;
            add_candle(L, cp, frand(0.12f, 0.3f), i % 3 == 0);
        }
        if (seed >= 3) add_cross(L, (Vector3){ bx > 0 ? S - 0.06f : -S + 0.06f, 2.0f, bz }, 0.45f, 0, (Color){ 90, 60, 50, 255 });
        if (seed >= 4) add_sigil(L, (Vector3){ 0, 0.004f, 0 }, 2.2f, (Color){ 120, 30, 24, 255 }, seed);
        if (seed >= 6) {   // they sat here and watched you sleep
            float cx0 = bx * 0.25f, cz0 = bz * 0.25f;
            float yaw = atan2f(bx - cx0, -(bz - cz0));
            for (int i = 0; i < 4; i++) {
                Vector3 cp = { cx0 + cosf(yaw) * (i - 1.5f) * 0.75f, 0, cz0 + sinf(yaw) * (i - 1.5f) * 0.75f };
                add_chair(L, cp, yaw);
            }
        }
    }
    // a threadbare rug, and a window onto somewhere that isn't there
    add_box(L, (Vector3){ 0, 0.008f, 0 }, (Vector3){ 2.6f, 0.004f, 3.4f }, TEX_MOSAIC, (Color){ 52, 40, 48, 255 }, 1.4f, F_NOCOLLIDE);
    add_box(L, (Vector3){ -3.6f, 2.4f, -S + 0.1f }, (Vector3){ 0.95f, 0.85f, 0.1f }, TEX_CONCRETE, (Color){ 22, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ -3.6f, 2.4f, -S + 0.23f }, (Vector3){ 0.8f, 0.72f, 0.03f }, TEX_CONCRETE, (Color){ 50, 80, 150, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_box(L, (Vector3){ -3.6f, 2.4f, -S + 0.27f }, (Vector3){ 0.03f, 0.74f, 0.02f }, TEX_CONCRETE, (Color){ 22, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ -3.6f, 2.4f, -S + 0.27f }, (Vector3){ 0.82f, 0.03f, 0.02f }, TEX_CONCRETE, (Color){ 22, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ -3.6f, 0.012f, -S + 1.8f }, (Vector3){ 0.8f, 0.004f, 1.5f }, TEX_CONCRETE, (Color){ 40, 60, 100, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE); // moonlight on the boards
    // hanging bulb
    add_box(L, (Vector3){ 0, 3.7f, 0 }, (Vector3){ 0.01f, 0.5f, 0.01f }, TEX_CONCRETE, (Color){ 20, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 0, 3.15f, 0 }, (Vector3){ 0.1f, 0.14f, 0.1f }, TEX_CONCRETE, (Color){ 255, 220, 140, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    // stains on the floor
    for (int i = 0; i < 10; i++)
        add_box(L, (Vector3){ frand(-7, 7), 0.01f, frand(-7, 7) }, (Vector3){ frand(0.4f, 1.4f), 0.005f, frand(0.4f, 1.4f) }, TEX_SLUDGE, (Color){ 120, 90, 90, 255 }, 2.0f, F_NOCOLLIDE);
    // things written on the walls
    char buf[96];
    add_decal(L, "STILL ASLEEP", (Vector3){ S, 2.3f, -4.5f }, 0, -1, 0.26f, BLOOD);
    if (g_launches > 1) add_decal(L, "YOU CAME BACK", (Vector3){ -S, 2.1f, 4.2f }, 0, 1, 0.24f, BLOOD);
    if (g_wakes > 0) {
        snprintf(buf, sizeof buf, "WOKEN %d TIMES", g_wakes);
        add_decal(L, buf, (Vector3){ -S, 2.4f, -4.0f }, 0, 1, 0.22f, BLOOD);
    }
    if (seed >= 3) add_decal(L, "DO NOT WAKE HIM", (Vector3){ -4.6f, 2.0f, S }, 2, -1, 0.22f, CHALK);
    if (seed >= 5) add_decal(L, "NEMA", (Vector3){ 4.2f, 2.5f, S }, 2, -1, 0.3f, BLOOD);
    if (seed >= 7) add_decal(L, "PATER NOSTER QUI ES IN INFERNIS", (Vector3){ 0, 3.3f, -S }, 2, 1, 0.16f, SOOT);
    // visitors: they only move when you aren't looking, and there are more each time you wake
    int visitors = seed >= 3 ? (seed - 1) / 2 : 0;
    if (visitors > 3) visitors = 3;
    for (int i = 0; i < visitors; i++) {
        float a = frand(0, 6.2831f);
        Watcher w = { { sinf(a) * 6.5f, 0, cosf(a) * 6.5f }, frand(0, 6), 0, false };
        Watchers_push(&L->watchers, w);
    }
    // junk you can shove around (this is what Box3D is for in here)
    for (int i = 0; i < 7; i++) {
        float s = frand(0.3f, 0.55f);
        add_prop(L, (Vector3){ frand(-4, 4), s + 0.05f + i * 0.02f, frand(-5.5f, -1.5f) }, (Vector3){ s, s, s }, i % 2 ? TEX_WOOD : TEX_RUST,
                 (Color){ 170, 160, 150, 255 }, 250, 1.0f);
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
    // the very top: a landing with the effect on a plinth
    int w = (LEVELS + 1) % 4;
    wall_box(L, w, 0.0f, sEnd, top + STEP - 0.4f, top + STEP, 4.0f, TEX_FLESH, (Color){ 190, 160, 160, 255 }, 0);
    Vector3 pp = wall_pt(w, W, top + STEP + 0.5f, 1.8f);
    Pickup pk = { pp, FX_GLOVES, false };
    Pickups_push(&L->pickups, pk);
    wall_box(L, w, 0.0f, sMid, top + 0.6f, top + STEP + 3.0f, 0.25f, TEX_RUST, rust, F_GRIP);
    add_decal(L, "HE IS BELOW", wall_pt((w + 1) % 4, W, top + STEP + 2.5f, 0.0f), (((w + 1) % 4) % 2 == 0) ? 2 : 0,
              ((w + 1) % 4) == 0 ? 1 : ((w + 1) % 4) == 1 ? -1 : ((w + 1) % 4) == 2 ? -1 : 1, 0.3f, BLOOD);
    // spawn facing the first grip strip (wall 1, +X)
    L->spawn = (Vector3){ W - 2.0f, 0.05f, -W / 2 };
    add_note(L, (Vector3){ W - 2.6f, 0.01f, -W / 2 + 1.0f }, NOTE_CLIMB);
    L->spawnYaw = 90;
}

// ---------------------------------------------------------------- DRAINS: a maze with things in it
static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };

static void build_drains(Level *L, int seed) {
    L->name = "THE DRAINS";
    L->fog = (Color){ 6, 12, 8, 255 };
    L->fogDensity = 0.115f;
    L->light = 0.85f;
    L->gradeLo = (Color){ 108, 134, 126, 255 }; L->gradeHi = (Color){ 140, 140, 116, 255 };
    L->moteCol = (Color){ 120, 170, 120, 255 };
    L->killY = -50;
    SetRandomSeed(seed * 7919 + 13);
    const int N = 9;
    const float C = 6.0f, H = 4.0f;
    L->mazeN = N; L->cell = C;
    L->open = calloc(N * N, 1);
    L->dist = calloc(N * N, sizeof(int));
    L->distCell = -1;
    // recursive backtracker
    uint8_t *seen = calloc(N * N, 1);
    IntV stack = { 0 };
    IntV_push(&stack, 0); seen[0] = 1;
    while (IntV_size(&stack) > 0) {
        int cur = stack.data[IntV_size(&stack) - 1];
        int cx = cur % N, cy = cur / N, opts[4], no = 0;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx >= 0 && ny >= 0 && nx < N && ny < N && !seen[ny * N + nx]) opts[no++] = d;
        }
        if (!no) { IntV_pop(&stack); continue; }
        int d = opts[GetRandomValue(0, no - 1)];
        int nxt = (cy + DY[d]) * N + cx + DX[d];
        L->open[cur] |= 1 << d;
        L->open[nxt] |= 1 << ((d + 2) % 4);
        seen[nxt] = 1;
        IntV_push(&stack, nxt);
    }
    // a few extra loops so the watchers can pincer you
    for (int i = 0; i < N * 2; i++) {
        int cx = GetRandomValue(1, N - 2), cy = GetRandomValue(1, N - 2), d = GetRandomValue(0, 3);
        L->open[cy * N + cx] |= 1 << d;
        L->open[(cy + DY[d]) * N + cx + DX[d]] |= 1 << ((d + 2) % 4);
    }
    // BFS from the start for the farthest cell
    int *far = calloc(N * N, sizeof(int));
    for (int i = 0; i < N * N; i++) far[i] = -1;
    IntV q = { 0 }; int head = 0;
    IntV_push(&q, 0); far[0] = 0;
    int best = 0;
    while (head < IntV_size(&q)) {
        int cur = q.data[head++];
        if (far[cur] > far[best]) best = cur;
        for (int d = 0; d < 4; d++) if (L->open[cur] & (1 << d)) {
            int nx = cur % N + DX[d], ny = cur / N + DY[d], n = ny * N + nx;
            if (far[n] < 0) { far[n] = far[cur] + 1; IntV_push(&q, n); }
        }
    }
    float ox = -N * C / 2;
    #define CELLC(i) ((Vector3){ ox + ((i) % N + 0.5f) * C, 0, ox + ((i) / N + 0.5f) * C })

    // geometry. the maze is carved into four kinds of zone, each with its own walls, floor and ceiling height
    static const float ZH[4] = { 4.0f, 3.0f, 5.6f, 2.6f };
    static const TexId ZW[4] = { TEX_CONCRETE, TEX_TILE, TEX_RUST, TEX_FLESH };
    static const TexId ZF[4] = { TEX_TILE, TEX_CONCRETE, TEX_WOOD, TEX_SLUDGE };
    static const Color ZC[4] = { { 130, 150, 130, 255 }, { 150, 150, 125, 255 }, { 120, 110, 100, 255 }, { 170, 120, 120, 255 } };
    #define ZONE(x, y) ((((x) / 3) * 2 + ((y) / 3) * 3 + seed) & 3)
    float tot = N * C;
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ tot / 2 + 1, 0.5f, tot / 2 + 1 }, TEX_CONCRETE, (Color){ 110, 120, 110, 255 }, 2.0f, 0);
    float wt = 0.35f;
    uint8_t *room = calloc(N * N, 1);
    // rooms: knock out the inner walls of a few 2x2 blocks to get open halls
    for (int r = 0; r < 3; r++) {
        int rx = GetRandomValue(1, N - 3), ry = GetRandomValue(1, N - 3);
        if (rx <= 1 && ry <= 1) continue;
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
            int i = (ry + dy) * N + rx + dx;
            room[i] = 1;
            if (dx == 0) { L->open[i] |= 2; L->open[i + 1] |= 8; }
            if (dy == 0) { L->open[i] |= 4; L->open[i + N] |= 1; }
        }
    }
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x, z = ZONE(x, y);
        float hc = ZH[z];
        Vector3 c = CELLC(i);
        float hs = (y + 1 < N) ? fmaxf(hc, ZH[ZONE(x, y + 1)]) : hc, he = (x + 1 < N) ? fmaxf(hc, ZH[ZONE(x + 1, y)]) : hc;
        // ceiling and per-zone floor skin
        add_box(L, (Vector3){ c.x, hc + 0.5f, c.z }, (Vector3){ C / 2 + wt, 0.5f, C / 2 + wt }, z == 3 ? TEX_FLESH : TEX_CONCRETE, scale_tint(ZC[z], 0.75f), 3.0f, 0);
        add_box(L, (Vector3){ c.x, 0.006f, c.z }, (Vector3){ C / 2, 0.006f, C / 2 }, ZF[z], ZC[z], 2.0f, F_NOCOLLIDE);
        // south and east walls, plus north/west on the rim
        if (!(L->open[i] & 4) || y == N - 1) add_box(L, (Vector3){ c.x, hs / 2, c.z + C / 2 }, (Vector3){ C / 2 + wt, hs / 2, wt }, ZW[z], ZC[z], 2.0f, 0);
        if (!(L->open[i] & 2) || x == N - 1) add_box(L, (Vector3){ c.x + C / 2, he / 2, c.z }, (Vector3){ wt, he / 2, C / 2 + wt }, ZW[z], ZC[z], 2.0f, 0);
        if (y == 0) add_box(L, (Vector3){ c.x, hc / 2, c.z - C / 2 }, (Vector3){ C / 2 + wt, hc / 2, wt }, ZW[z], ZC[z], 2.0f, 0);
        if (x == 0) add_box(L, (Vector3){ c.x - C / 2, hc / 2, c.z }, (Vector3){ wt, hc / 2, C / 2 + wt }, ZW[z], ZC[z], 2.0f, 0);
        // clutter
        int r = GetRandomValue(0, 9);
        if (i != 0 && i != best) {
            if (room[i]) {
                if (GetRandomValue(0, 2) == 0) add_box(L, (Vector3){ c.x + frand(-1.8f, 1.8f), hc / 2, c.z + frand(-1.8f, 1.8f) }, (Vector3){ 0.4f, hc / 2, 0.4f }, ZW[z], ZC[z], 1.5f, 0);
                else if (GetRandomValue(0, 2) == 0) add_prop(L, (Vector3){ c.x + frand(-1.5f, 1.5f), 0.4f, c.z + frand(-1.5f, 1.5f) }, (Vector3){ 0.4f, 0.4f, 0.4f }, TEX_WOOD, (Color){ 120, 130, 110, 255 }, 200, 1);
            } else if (r < 2) add_box(L, (Vector3){ c.x + frand(-1.5f, 1.5f), hc / 2, c.z + frand(-1.5f, 1.5f) }, (Vector3){ 0.35f, hc / 2, 0.35f }, TEX_RUST, (Color){ 120, 110, 100, 255 }, 1.5f, 0);
            else if (r < 5) add_box(L, (Vector3){ c.x + frand(-1.2f, 1.2f), 0.012f, c.z + frand(-1.2f, 1.2f) }, (Vector3){ frand(0.8f, 2.2f), 0.01f, frand(0.8f, 2.2f) }, TEX_SLUDGE, (Color){ 100, 120, 100, 255 }, 3.0f, F_NOCOLLIDE);
            else if (r < 7) add_prop(L, (Vector3){ c.x + frand(-1.5f, 1.5f), 0.4f, c.z + frand(-1.5f, 1.5f) }, (Vector3){ 0.4f, 0.4f, 0.4f }, TEX_WOOD, (Color){ 120, 130, 110, 255 }, 200, 1);
        }
        // sick coloured lamps in the ceiling
        if (GetRandomValue(0, 4) == 0) {
            static const Color LC[4] = { { 120, 255, 140, 255 }, { 255, 220, 120, 255 }, { 255, 90, 60, 255 }, { 255, 120, 200, 255 } };
            add_box(L, (Vector3){ c.x + frand(-1.5f, 1.5f), hc - 0.04f, c.z + frand(-1.5f, 1.5f) }, (Vector3){ 0.5f, 0.04f, 0.18f }, TEX_CONCRETE, LC[z], 1.0f, F_EMIT | F_NOCOLLIDE);
        }
        // ceiling pipes
        if (GetRandomValue(0, 3) == 0)
            add_box(L, (Vector3){ c.x, hc - 0.3f, c.z }, (Vector3){ C / 2, 0.16f, 0.16f }, TEX_RUST, (Color){ 120, 120, 110, 255 }, 1.0f, F_NOCOLLIDE);
    }
    free(room);
    {   // dead ends get a message
        static const char *DEAD[] = { "HUSH", "STAY", "NEMA", "HE HEARS", "KNEEL", "NON SERVIAM" };
        int placed = 0;
        for (int i = 1; i < N * N && placed < 7; i++) {
            int o = L->open[i], cnt = (o & 1) + ((o >> 1) & 1) + ((o >> 2) & 1) + ((o >> 3) & 1);
            if (cnt != 1 || i == best) continue;
            int d = -1;
            for (int k = 0; k < 4; k++) if (!(o & (1 << k))) { d = k; if (GetRandomValue(0, 1)) break; }
            Vector3 c = CELLC(i);
            const char *txt = DEAD[GetRandomValue(0, 5)];
            Vector3 wp = c; wp.y = 1.7f;
            static const int ax[4] = { 2, 0, 2, 0 }, dr[4] = { 1, -1, -1, 1 };
            if (d == 0) wp.z -= C / 2 - wt; else if (d == 1) wp.x += C / 2 - wt; else if (d == 2) wp.z += C / 2 - wt; else wp.x -= C / 2 - wt;
            add_decal(L, txt, wp, ax[d], dr[d], 0.4f, BLOOD);
            placed++;
        }
    }
    L->spawn = CELLC(0); L->spawn.y = 0.05f;
    { Vector3 np = CELLC(0); add_note(L, (Vector3){ np.x - 1.6f, 0.02f, np.z + 1.2f }, NOTE_CANDLES); }
    L->spawnYaw = 90;
    Vector3 pc = CELLC(best); pc.y = 1.1f;
    Pickup pk = { pc, FX_LAMP, false };
    Pickups_push(&L->pickups, pk);
    // lamp glow marker so it's findable in the dark: a tall dim beam
    add_box(L, (Vector3){ pc.x, H / 2, pc.z }, (Vector3){ 0.03f, H / 2, 0.03f }, TEX_CONCRETE, (Color){ 255, 230, 120, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    // watchers: start far from you, one near the lamp
    int placed = 0;
    for (int tries = 0; tries < 200 && placed < 4; tries++) {
        int i = GetRandomValue(0, N * N - 1);
        if (far[i] < 6 && i != best) continue;
        Watcher w = { CELLC(i), frand(0, 6), 0, false };
        Watchers_push(&L->watchers, w);
        placed++;
    }
    free(far); free(seen); IntV_drop(&q); IntV_drop(&stack);
    #undef CELLC
}

// ---------------------------------------------------------------- VOID: platforms in nothing
// support of an axis aligned footprint along a unit direction: how far its edge is from its center
static float support(float hx, float hz, float dx, float dz) { return hx * fabsf(dx) + hz * fabsf(dz); }

static void build_void(Level *L, int seed) {
    L->name = "THE STEPS";
    L->fog = (Color){ 52, 20, 70, 255 };
    L->fogDensity = 0.024f;
    L->light = 1.35f;
    L->sky = (Sky){ true, { 6, 2, 22, 255 }, { 52, 20, 70, 255 }, { 52, 20, 70, 255 }, 1.0f, false, { 0, 0, 0, 0 }, { 0, 0, 0 }, { 80, 255, 190, 255 }, 1.0f, true, 0.7f };
    L->gradeLo = (Color){ 116, 118, 160, 255 }; L->gradeHi = (Color){ 160, 124, 138, 255 };
    L->moteCol = (Color){ 200, 150, 255, 255 }; L->moteGlow = true;
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
            {   // a glowing seam around the rim: you can see where the edge is
                static const Color TR[3] = { { 90, 240, 255, 255 }, { 255, 90, 200, 255 }, { 190, 150, 255, 255 } };
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
        // drifting rubble, simulated but weightless
        if (i % 2 == 0)
            add_prop(L, (Vector3){ p.x + frand(-6, 6), p.y + frand(1, 6), p.z + frand(-6, 6) }, (Vector3){ frand(0.3f, 0.9f), frand(0.3f, 0.9f), frand(0.3f, 0.9f) },
                     TEX_FLESH, (Color){ 170, 140, 170, 255 }, 40, 0.0f);
        if (i == 12) {
            add_box(L, (Vector3){ p.x - dz * 3.5f, p.y + 2.0f, p.z + dx * 3.5f }, (Vector3){ 1.0f, 2.0f, 0.1f }, TEX_CONCRETE, (Color){ 40, 36, 44, 255 }, 2.0f, F_NOCOLLIDE);
        }
    }
    for (int i = 0; i < L->props.size; i++) {
        b3Body_SetAngularVelocity(L->props.data[i].body, (b3Vec3){ frand(-.6f, .6f), frand(-.6f, .6f), frand(-.6f, .6f) });
        b3Body_SetLinearVelocity(L->props.data[i].body, (b3Vec3){ frand(-.3f, .3f), frand(-.3f, .3f), frand(-.3f, .3f) });
    }
    Vector3 pc = { p.x, p.y + 1.1f, p.z };
    Pickup pk = { pc, FX_BOOTS, false };
    Pickups_push(&L->pickups, pk);
    add_box(L, (Vector3){ p.x, p.y + 20, p.z }, (Vector3){ 0.04f, 20, 0.04f }, TEX_CONCRETE, (Color){ 200, 160, 255, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
}

// ---------------------------------------------------------------- GARDEN: an orchard under a bad moon
static void flower_at(Level *L, float x, float z, float hgt, Color col, float size) {
    add_box(L, (Vector3){ x, hgt / 2, z }, (Vector3){ 0.05f, hgt / 2, 0.05f }, TEX_GRASS, (Color){ 90, 150, 130, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ x + 0.25f, hgt * 0.35f, z }, (Vector3){ 0.22f, 0.03f, 0.1f }, TEX_GRASS, (Color){ 90, 150, 130, 255 }, 1.0f, F_NOCOLLIDE);   // a leaf
    Bloom b = { { x, hgt + size * 0.4f, z }, col, size, 0 };
    Blooms_push(&L->blooms, b);
}

static void build_garden(Level *L, int seed) {
    L->name = "THE ORCHARD";
    SetRandomSeed(seed * 6007 + 3);
    L->fog = (Color){ 30, 34, 36, 255 };
    L->fogDensity = 0.03f;
    L->light = 1.0f;
    L->killY = -7;
    L->sky = (Sky){ true, { 4, 5, 8, 255 }, { 30, 34, 36, 255 }, { 30, 34, 36, 255 }, 0.35f, true, { 220, 214, 196, 255 }, { -0.35f, 0.42f, -0.84f }, { 0, 0, 0, 0 }, 0.0f, false, 0 };
    L->gradeLo = (Color){ 118, 128, 136, 255 }; L->gradeHi = (Color){ 140, 130, 120, 255 };
    L->moteCol = (Color){ 150, 140, 130, 255 }; L->moteGlow = true;
    L->creepers = true;
    const float E = 50, P = 14;       // half size of the field, half size of the pond
    Color gc = { 92, 84, 76, 255 };
    // the ground, with a square pond cut out of the middle
    add_box(L, (Vector3){ 0, -0.5f, -(E + P) / 2 }, (Vector3){ E + 1, 0.5f, (E - P) / 2 }, TEX_GRASS, gc, 3.0f, 0);
    add_box(L, (Vector3){ 0, -0.5f, (E + P) / 2 }, (Vector3){ E + 1, 0.5f, (E - P) / 2 }, TEX_GRASS, gc, 3.0f, 0);
    add_box(L, (Vector3){ -(E + P) / 2, -0.5f, 0 }, (Vector3){ (E - P) / 2, 0.5f, P }, TEX_GRASS, gc, 3.0f, 0);
    add_box(L, (Vector3){ (E + P) / 2, -0.5f, 0 }, (Vector3){ (E - P) / 2, 0.5f, P }, TEX_GRASS, gc, 3.0f, 0);
    // the edge of the world: low walls of hedge that the fog hides
    add_box(L, (Vector3){ 0, 3, -E - 1 }, (Vector3){ E + 2, 3, 1 }, TEX_GRASS, (Color){ 40, 80, 70, 255 }, 3.0f, 0);
    add_box(L, (Vector3){ 0, 3, E + 1 }, (Vector3){ E + 2, 3, 1 }, TEX_GRASS, (Color){ 40, 80, 70, 255 }, 3.0f, 0);
    add_box(L, (Vector3){ -E - 1, 3, 0 }, (Vector3){ 1, 3, E + 2 }, TEX_GRASS, (Color){ 40, 80, 70, 255 }, 3.0f, 0);
    add_box(L, (Vector3){ E + 1, 3, 0 }, (Vector3){ 1, 3, E + 2 }, TEX_GRASS, (Color){ 40, 80, 70, 255 }, 3.0f, 0);
    L->water = true; L->waterY = -0.8f; L->waterHalf = P; L->waterC = (Vector3){ 0, 0, 0 };
    // an island in the pond with a lone lantern, and two planks to reach it
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ 3.2f, 0.5f, 3.2f }, TEX_MOSAIC, (Color){ 150, 130, 160, 255 }, 1.5f, 0);
    add_box(L, (Vector3){ 0, -0.15f, (P + 3.2f) / 2 }, (Vector3){ 1.0f, 0.15f, (P - 3.2f) / 2 + 0.2f }, TEX_WOOD, (Color){ 150, 130, 160, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 0, -0.15f, -(P + 3.2f) / 2 }, (Vector3){ 1.0f, 0.15f, (P - 3.2f) / 2 + 0.2f }, TEX_WOOD, (Color){ 150, 130, 160, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 0, 1.2f, 0 }, (Vector3){ 0.07f, 1.2f, 0.07f }, TEX_RUST, (Color){ 60, 50, 60, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 0, 2.6f, 0 }, (Vector3){ 0.25f, 0.3f, 0.25f }, TEX_CONCRETE, (Color){ 255, 190, 120, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_decal(L, "SOMEONE LIT THIS FOR YOU", (Vector3){ 0, 1.0f, 3.1f }, 2, 1, 0.2f, (Color){ 255, 190, 120, 255 });
    // a mound with a shrine at the far end, built from shallow steps
    const float MZ = -40;
    float tw = 9.5f, ty = 0;
    for (int i = 0; i < 7; i++) {
        ty += 0.28f;
        add_box(L, (Vector3){ 0, ty / 2, MZ }, (Vector3){ tw, ty / 2, tw }, (i & 1) ? TEX_MOSAIC : TEX_CONCRETE, (Color){ 150, 135, 170, 255 }, 1.5f, 0);
        tw -= 1.1f;
    }
    float top = ty;
    for (int i = 0; i < 4; i++) {
        float a = i * 1.5708f + 0.785f;
        Vector3 pc = { sinf(a) * 2.4f, top + 1.7f, MZ + cosf(a) * 2.4f };
        add_box(L, pc, (Vector3){ 0.25f, 1.7f, 0.25f }, TEX_CONCRETE, (Color){ 190, 180, 200, 255 }, 1.2f, 0);
        add_candle(L, (Vector3){ pc.x, top + 3.4f, pc.z }, 0.25f, true);
    }
    add_box(L, (Vector3){ 0, top + 0.3f, MZ }, (Vector3){ 0.7f, 0.3f, 0.7f }, TEX_FLESH, (Color){ 220, 190, 200, 255 }, 1.0f, 0);
    Pickup pk = { { 0, top + 1.8f, MZ }, FX_FEATHER, false };
    Pickups_push(&L->pickups, pk);
    add_box(L, (Vector3){ 0, 25, MZ }, (Vector3){ 0.05f, 25, 0.05f }, TEX_CONCRETE, (Color){ 200, 170, 140, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_sigil(L, (Vector3){ 0, top, MZ }, 2.0f, CHALK, 41);
    add_decal(L, "TAKE IT. HE WILL KNOW", (Vector3){ 0, 1.2f, MZ + 9.6f }, 2, 1, 0.22f, BLOOD);
    L->spawn = (Vector3){ 0, 0.05f, 42 };
    L->spawnYaw = 0;
    add_note(L, (Vector3){ 1.4f, 0.01f, 40.0f }, NOTE_FLOWERS);
    // fruit trees: tall dark trunks hung with lanterns
    static const Color FRUIT[4] = { { 200, 120, 70, 255 }, { 170, 90, 60, 255 }, { 210, 160, 90, 255 }, { 150, 60, 50, 255 } };
    for (int i = 0; i < 46; i++) {
        float x = frand(-E + 4, E - 4), z = frand(-E + 4, E - 4);
        if (fabsf(x) < P + 2 && fabsf(z) < P + 2) continue;                 // pond
        if (fabsf(x) < 3.0f) continue;                                      // keep a lane open down the middle
        if (z < MZ + 12 && z > MZ - 12 && fabsf(x) < 12) continue;          // mound
        if (z > 36) continue;                                               // spawn clearing
        float h = frand(4.5f, 8.0f);
        add_box(L, (Vector3){ x, h / 2, z }, (Vector3){ 0.32f, h / 2, 0.32f }, TEX_WOOD, (Color){ 120, 90, 130, 255 }, 1.5f, 0);
        add_box(L, (Vector3){ x + 0.9f, h - 1.0f, z }, (Vector3){ 0.9f, 0.1f, 0.1f }, TEX_WOOD, (Color){ 120, 90, 130, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ x, h - 1.8f, z - 0.9f }, (Vector3){ 0.1f, 0.1f, 0.9f }, TEX_WOOD, (Color){ 120, 90, 130, 255 }, 1.0f, F_NOCOLLIDE);
        add_box(L, (Vector3){ x, h + 0.2f, z }, (Vector3){ 1.8f, 0.5f, 1.8f }, TEX_GRASS, (Color){ 100, 150, 130, 255 }, 1.0f, F_NOCOLLIDE);
        for (int k = 0; k < 4; k++) {
            Bloom b = { { x + frand(-1.6f, 1.6f), h - frand(0.4f, 2.2f), z + frand(-1.6f, 1.6f) }, FRUIT[GetRandomValue(0, 3)], 0.12f, 1 };
            Blooms_push(&L->blooms, b);
        }
    }
    // flowers that watch. more of them near the path to the shrine.
    static const Color PET[4] = { { 200, 190, 170, 255 }, { 150, 40, 44, 255 }, { 120, 100, 130, 255 }, { 190, 176, 120, 255 } };
    for (int i = 0; i < 150; i++) {
        float x = frand(-E + 3, E - 3), z = frand(-E + 3, E - 3);
        if (fabsf(x) < P && fabsf(z) < P) continue;
        if (z > 38 && fabsf(x) < 2.5f) continue;
        flower_at(L, x, z, frand(0.8f, 3.4f), PET[GetRandomValue(0, 3)], frand(0.55f, 1.2f));
    }
    // standing stones with writing on them
    static const char *STONE[] = { "WE PLANTED THEM IN ROWS", "EACH ONE HAD A NAME", "THEY GROW WHERE YOU LOOK AWAY", "DON'T COUNT THEM", "ET IN ARCADIA EGO" };
    for (int i = 0; i < 5; i++) {
        float x = (i % 2 ? 1 : -1) * frand(6, 16), z = 36 - i * 13.0f;
        if (fabsf(z) < P + 2 && fabsf(x) < P + 2) continue;
        add_box(L, (Vector3){ x, 1.3f, z }, (Vector3){ 0.9f, 1.3f, 0.2f }, TEX_CONCRETE, (Color){ 150, 140, 160, 255 }, 1.5f, 0);
        add_decal(L, STONE[i], (Vector3){ x, 1.5f, z + 0.2f }, 2, 1, 0.16f, (i % 2) ? BLOOD : CHALK);
    }
    // gardeners
    int n = 3 + (seed / 3 > 3 ? 3 : seed / 3);
    for (int i = 0; i < n; i++) {
        float x = frand(-E + 6, E - 6), z = frand(-E + 6, 10);
        if (fabsf(x) < P + 2 && fabsf(z) < P + 2) { x = (x < 0 ? -1 : 1) * (P + 4); }
        Watcher w = { { x, 0, z }, frand(0, 6), 0, false };
        Watchers_push(&L->watchers, w);
    }
}

// ---------------------------------------------------------------- END: the way out
static void build_end(Level *L) {
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
    b3WorldDef wd = b3DefaultWorldDef();
    wd.gravity = (b3Vec3){ 0, -20.0f, 0 };
    wd.workerCount = 1;
    L->phys = b3CreateWorld(&wd);
    switch (id) {
    case W_HUB:    build_hub(L, seed); break;
    case W_SHAFT:  build_shaft(L, seed); break;
    case W_DRAINS: build_drains(L, seed); break;
    case W_VOID:   build_void(L, seed); break;
    case W_GARDEN: build_garden(L, seed); break;
    default:       build_end(L); break;
    }
    // ash motes
    for (int i = 0; i < 140; i++) {
        Mote m = { { 0, 0, 0 }, { frand(-.1f, .1f), frand(-.25f, -.05f), frand(-.1f, .1f) }, frand(0, 1) };
        if (L->moteGlow) m.vel = (Vector3){ frand(-.25f, .25f), frand(0.05f, 0.35f), frand(-.25f, .25f) };
        Motes_push(&L->motes, m);
    }
    if (L->moteCol.a == 0) L->moteCol = (Color){ 150, 140, 120, 255 };
}

void level_free(Level *L) {
    if (b3World_IsValid(L->phys)) b3DestroyWorld(L->phys);
    Boxes_drop(&L->boxes); Pickups_drop(&L->pickups); Portals_drop(&L->portals);
    Watchers_drop(&L->watchers); Motes_drop(&L->motes); Blooms_drop(&L->blooms); Props_drop(&L->props); Effigies_drop(&L->effigies); Uses_drop(&L->uses);
    free(L->open); free(L->dist);
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
        if (m->life <= 0 || Vector3Distance(m->pos, player) > 12) {
            m->life = 1;
            m->pos = (Vector3){ player.x + frand(-10, 10), player.y + frand(0, 5), player.z + frand(-10, 10) };
        }
    }
}

// ---------------------------------------------------------------- watchers
static int cell_of(const Level *L, Vector3 p) {
    float ox = -L->mazeN * L->cell / 2;
    int x = (int)floorf((p.x - ox) / L->cell), y = (int)floorf((p.z - ox) / L->cell);
    x = x < 0 ? 0 : x >= L->mazeN ? L->mazeN - 1 : x;
    y = y < 0 ? 0 : y >= L->mazeN ? L->mazeN - 1 : y;
    return y * L->mazeN + x;
}

static void refresh_dist(Level *L, int from) {
    int n = L->mazeN;
    for (int i = 0; i < n * n; i++) L->dist[i] = -1;
    IntV q = { 0 }; int head = 0;
    IntV_push(&q, from); L->dist[from] = 0;
    while (head < IntV_size(&q)) {
        int cur = q.data[head++];
        for (int d = 0; d < 4; d++) if (L->open[cur] & (1 << d)) {
            int nx = cur % n + DX[d], ny = cur / n + DY[d], nn = ny * n + nx;
            if (L->dist[nn] < 0) { L->dist[nn] = L->dist[cur] + 1; IntV_push(&q, nn); }
        }
    }
    L->distCell = from;
    IntV_drop(&q);
}

bool level_seen(Level *L, Vector3 eye, Vector3 fwd, Vector3 pos) {
    Vector3 chest = { pos.x, 1.6f, pos.z };
    Vector3 to = Vector3Subtract(chest, eye);
    float dist = Vector3Length(to);
    if (dist < 0.01f || dist > 45) return false;
    if (Vector3DotProduct(Vector3Scale(to, 1.0f / dist), fwd) < 0.45f) return false;
    b3RayResult r = b3World_CastRayClosest(L->phys, b3v(eye), b3v(to), b3DefaultQueryFilter());
    return !r.hit || r.fraction > 0.97f;
}

bool level_watchers(Level *L, Vector3 eye, Vector3 fwd, Vector3 feet, float dt, bool blind) {
    L->nearest = 99;
    if (!L->open) return false;
    int pc = cell_of(L, feet);
    if (pc != L->distCell) refresh_dist(L, pc);
    float ox = -L->mazeN * L->cell / 2;
    float speed = 2.7f + 0.22f * (L->dreamNo > 8 ? 8 : L->dreamNo);
    for (int i = 0; i < L->watchers.size; i++) {
        Watcher *w = &L->watchers.data[i];
        w->phase += dt;
        float dist = Vector3Distance((Vector3){ w->pos.x, 1.6f, w->pos.z }, eye);
        if (dist < L->nearest) L->nearest = dist;
        if (dist < 0.9f) return true;
        // in the dark they don't need you to look away
        bool seen = !blind && level_seen(L, eye, fwd, w->pos);
        if (seen && !w->seen && dist < 22) L->sawWatcher = true;
        w->seen = seen;
        if (seen) continue;
        int wc = cell_of(L, w->pos);
        Vector3 target = feet;
        if (wc != pc) {
            int best = -1, bd = L->dist[wc];
            for (int d = 0; d < 4; d++) if (L->open[wc] & (1 << d)) {
                int nn = (wc / L->mazeN + DY[d]) * L->mazeN + wc % L->mazeN + DX[d];
                if (L->dist[nn] >= 0 && L->dist[nn] < bd) { bd = L->dist[nn]; best = nn; }
            }
            if (best >= 0) target = (Vector3){ ox + (best % L->mazeN + 0.5f) * L->cell, 0, ox + (best / L->mazeN + 0.5f) * L->cell };
        }
        Vector3 dir = Vector3Subtract(target, w->pos); dir.y = 0;
        if (Vector3Length(dir) > 0.05f) {
            float step = (blind ? speed * 1.4f : speed) * dt;
            w->pos = Vector3Add(w->pos, Vector3Scale(Vector3Normalize(dir), step));
            w->stride += step * 5.0f;
        }
    }
    return false;
}
