#include "world.h"
#include "gfx.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define i_type IntV, int
#include <stc/vec.h>


int g_launches, g_wakes;
char g_user[64] = "YOU";

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
    L->spawn = (Vector3){ 0, 0.05f, 2.0f };
    L->spawnYaw = 0;
    L->killY = -50;
    const float S = 8, H = 4.2f;
    room(L, S, S, H, TEX_TILE, (Color){ 200, 205, 180, 255 }, TEX_WOOD, (Color){ 190, 170, 150, 255 }, TEX_CONCRETE, (Color){ 140, 140, 130, 255 });

    // doors: slab glows in a dark frame, set into the wall faces
    struct { Vector3 p; int axis; Color c; WorldId to; int needs; const char *lbl; } d[4] = {
        { { 0, 1.3f, -S + 0.15f }, 0, { 230, 120, 40, 255 },  W_SHAFT,  0, "THE SHAFT" },
        { { S - 0.15f, 1.3f, 0 }, 1, { 90, 220, 90, 255 },   W_DRAINS, 0, "THE DRAINS" },
        { { -S + 0.15f, 1.3f, 0 }, 1, { 170, 90, 255, 255 }, W_VOID,   0, "THE STEPS" },
        { { 0, 1.3f, S - 0.15f }, 0, { 255, 245, 235, 255 }, W_END,    7, "THE WAY OUT" },
    };
    for (int i = 0; i < 4; i++) {
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

    // furniture: a bed that nobody wants, a desk with a TV that never turns off
    add_box(L, (Vector3){ -5.8f, 0.35f, -5.2f }, (Vector3){ 1.0f, 0.35f, 1.9f }, TEX_WOOD, (Color){ 150, 120, 100, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ -5.8f, 0.78f, -5.0f }, (Vector3){ 0.92f, 0.1f, 1.7f }, TEX_TILE, (Color){ 150, 130, 120, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ -5.8f, 0.95f, -6.6f }, (Vector3){ 0.6f, 0.12f, 0.3f }, TEX_CONCRETE, (Color){ 200, 190, 170, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 5.6f, 0.45f, 5.6f }, (Vector3){ 1.2f, 0.45f, 0.6f }, TEX_WOOD, (Color){ 140, 110, 90, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 5.6f, 1.2f, 5.6f }, (Vector3){ 0.5f, 0.35f, 0.4f }, TEX_CONCRETE, (Color){ 50, 50, 46, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 5.6f, 1.2f, 5.2f }, (Vector3){ 0.4f, 0.27f, 0.02f }, TEX_STATIC, (Color){ 150, 170, 160, 255 }, 1.0f, F_NOCOLLIDE);
    // hanging bulb
    add_box(L, (Vector3){ 0, 3.7f, 0 }, (Vector3){ 0.01f, 0.5f, 0.01f }, TEX_CONCRETE, (Color){ 20, 20, 20, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 0, 3.15f, 0 }, (Vector3){ 0.1f, 0.14f, 0.1f }, TEX_CONCRETE, (Color){ 255, 220, 140, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    // stains on the floor
    for (int i = 0; i < 10; i++)
        add_box(L, (Vector3){ frand(-7, 7), 0.01f, frand(-7, 7) }, (Vector3){ frand(0.4f, 1.4f), 0.005f, frand(0.4f, 1.4f) }, TEX_SLUDGE, (Color){ 120, 90, 90, 255 }, 2.0f, F_NOCOLLIDE);
    // things written on the walls
    char buf[96];
    snprintf(buf, sizeof buf, "%s IS STILL ASLEEP", g_user);
    add_decal(L, buf, (Vector3){ S, 2.3f, -4.5f }, 0, -1, 0.26f, BLOOD);
    if (g_launches > 1) add_decal(L, "YOU CAME BACK", (Vector3){ -S, 2.1f, 4.2f }, 0, 1, 0.24f, BLOOD);
    if (g_wakes > 0) {
        snprintf(buf, sizeof buf, "WOKEN %d TIMES", g_wakes);
        add_decal(L, buf, (Vector3){ -5.0f, 2.4f, -S }, 2, 1, 0.22f, BLOOD);
    }
    if (seed >= 3) add_decal(L, "IT FOLLOWED YOU", (Vector3){ -4.6f, 2.0f, S }, 2, -1, 0.26f, BLOOD);
    if (seed >= 5) add_decal(L, "DON'T LOOK BEHIND YOU", (Vector3){ 4.2f, 2.5f, S }, 2, -1, 0.22f, BLOOD);
    // visitors: they only move when you aren't looking, and there are more each time you wake
    int visitors = seed >= 3 ? (seed - 1) / 2 : 0;
    if (visitors > 3) visitors = 3;
    for (int i = 0; i < visitors; i++) {
        float a = frand(0, 6.2831f);
        Watcher w = { { sinf(a) * 6.5f, 0, cosf(a) * 6.5f }, frand(0, 6), false };
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
static Vector3 wall_pt(int w, float s, float y, float inset) {
    const float W = 6.0f;
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

#define SHAFT_LEVELS 11
static void build_shaft(Level *L) {
    L->name = "THE SHAFT";
    L->fog = (Color){ 26, 14, 8, 255 };
    L->fogDensity = 0.035f;
    L->light = 0.95f;
    L->killY = -60;
    L->sludge = true;
    L->sludgeY = -3;
    L->sludgeSpeed = 0.35f;
    const float W = 6.0f, STEP = 4.5f, TOP = STEP * SHAFT_LEVELS + 60;
    // floor + four tall walls (smooth slimy concrete: you can't grip these)
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ W + 1, 0.5f, W + 1 }, TEX_TILE, (Color){ 150, 150, 130, 255 }, 2.0f, 0);
    Color wc = (Color){ 120, 112, 100, 255 };
    add_box(L, (Vector3){ 0, TOP / 2 - 10, -W - 0.5f }, (Vector3){ W + 1, TOP / 2 + 10, 0.5f }, TEX_CONCRETE, wc, 2.0f, 0);
    add_box(L, (Vector3){ 0, TOP / 2 - 10, W + 0.5f }, (Vector3){ W + 1, TOP / 2 + 10, 0.5f }, TEX_CONCRETE, wc, 2.0f, 0);
    add_box(L, (Vector3){ -W - 0.5f, TOP / 2 - 10, 0 }, (Vector3){ 0.5f, TOP / 2 + 10, W + 1 }, TEX_CONCRETE, wc, 2.0f, 0);
    add_box(L, (Vector3){ W + 0.5f, TOP / 2 - 10, 0 }, (Vector3){ 0.5f, TOP / 2 + 10, W + 1 }, TEX_CONCRETE, wc, 2.0f, 0);
    // below the floor is a pit the sludge fills
    add_box(L, (Vector3){ 0, TOP + 0.5f, 0 }, (Vector3){ W + 1, 0.5f, W + 1 }, TEX_CONCRETE, (Color){ 60, 60, 60, 255 }, 3.0f, 0);

    Color rust = (Color){ 235, 205, 180, 255 };
    for (int k = 1; k <= SHAFT_LEVELS; k++) {
        float yk = STEP * k, yp = STEP * (k - 1);
        int w = k % 4;
        // shelf on the right half of wall w
        wall_box(L, w, 6.5f, 12.0f, yk - 0.4f, yk, 2.6f, TEX_CONCRETE, (Color){ 150, 140, 120, 255 }, 0);
        wall_box(L, w, 6.5f, 12.0f, yk - 0.4f, yk + 0.02f, 0.2f, TEX_RUST, (Color){ 110, 100, 90, 255 }, F_NOCOLLIDE);
        // grip strip on the left of wall w, up from the previous shelf
        float y0 = yp + 0.6f, y1 = yk + 3.0f;
        if (k % 2 == 0) {
            // a slick band in the middle: lunge across it
            float m = (y0 + y1) / 2;
            { static const int ax[4] = { 2, 0, 2, 0 }, dr[4] = { 1, -1, -1, 1 };
              Vector3 wp = wall_pt(w, 3.25f, m, 0.0f);
              add_decal(L, "LET GO", wp, ax[w], dr[w], 0.22f, BLOOD); }
            wall_box(L, w, 0.0f, 6.5f, y0, m - 0.7f, 0.25f, TEX_RUST, rust, F_GRIP);
            wall_box(L, w, 0.0f, 6.5f, m + 0.7f, y1, 0.25f, TEX_RUST, rust, F_GRIP);
        } else {
            wall_box(L, w, 0.0f, 6.5f, y0, y1, 0.25f, TEX_RUST, rust, F_GRIP);
        }
    }
    float top = STEP * SHAFT_LEVELS;
    // the very top: a landing with the effect on a plinth
    int w = (SHAFT_LEVELS + 1) % 4;
    wall_box(L, w, 0.0f, 12.0f, top + STEP - 0.4f, top + STEP, 4.0f, TEX_FLESH, (Color){ 190, 160, 160, 255 }, 0);
    Vector3 pp = wall_pt(w, 6.0f, top + STEP + 0.5f, 1.8f);
    Pickup pk = { pp, FX_GLOVES, false };
    Pickups_push(&L->pickups, pk);
    // the shelf the top strip lands on needs a strip to the landing too
    wall_box(L, w, 0.0f, 6.5f, top + 0.6f, top + STEP + 3.0f, 0.25f, TEX_RUST, rust, F_GRIP);
    // spawn facing the first grip strip (wall 1, +X)
    L->spawn = (Vector3){ 4.0f, 0.05f, -3.0f };
    L->spawnYaw = 90;
}

// ---------------------------------------------------------------- DRAINS: a maze with things in it
static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };

static void build_drains(Level *L, int seed) {
    L->name = "THE DRAINS";
    L->fog = (Color){ 6, 12, 8, 255 };
    L->fogDensity = 0.115f;
    L->light = 0.85f;
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

    // geometry
    float tot = N * C;
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ tot / 2 + 1, 0.5f, tot / 2 + 1 }, TEX_TILE, (Color){ 120, 150, 120, 255 }, 2.0f, 0);
    add_box(L, (Vector3){ 0, H + 0.5f, 0 }, (Vector3){ tot / 2 + 1, 0.5f, tot / 2 + 1 }, TEX_CONCRETE, (Color){ 90, 100, 90, 255 }, 3.0f, 0);
    Color wc = (Color){ 130, 150, 130, 255 };
    float wt = 0.35f;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        Vector3 c = CELLC(i);
        // south and east walls, plus north/west on the rim
        if (!(L->open[i] & 4) || y == N - 1) add_box(L, (Vector3){ c.x, H / 2, c.z + C / 2 }, (Vector3){ C / 2 + wt, H / 2, wt }, TEX_CONCRETE, wc, 2.0f, 0);
        if (!(L->open[i] & 2) || x == N - 1) add_box(L, (Vector3){ c.x + C / 2, H / 2, c.z }, (Vector3){ wt, H / 2, C / 2 + wt }, TEX_CONCRETE, wc, 2.0f, 0);
        if (y == 0) add_box(L, (Vector3){ c.x, H / 2, c.z - C / 2 }, (Vector3){ C / 2 + wt, H / 2, wt }, TEX_CONCRETE, wc, 2.0f, 0);
        if (x == 0) add_box(L, (Vector3){ c.x - C / 2, H / 2, c.z }, (Vector3){ wt, H / 2, C / 2 + wt }, TEX_CONCRETE, wc, 2.0f, 0);
        // clutter
        int r = GetRandomValue(0, 9);
        if (i != 0 && i != best) {
            if (r < 2) add_box(L, (Vector3){ c.x + frand(-1.5f, 1.5f), H / 2, c.z + frand(-1.5f, 1.5f) }, (Vector3){ 0.35f, H / 2, 0.35f }, TEX_RUST, (Color){ 120, 110, 100, 255 }, 1.5f, 0);
            else if (r < 5) add_box(L, (Vector3){ c.x + frand(-1.2f, 1.2f), 0.012f, c.z + frand(-1.2f, 1.2f) }, (Vector3){ frand(0.8f, 2.2f), 0.01f, frand(0.8f, 2.2f) }, TEX_SLUDGE, (Color){ 100, 120, 100, 255 }, 3.0f, F_NOCOLLIDE);
            else if (r < 7) add_prop(L, (Vector3){ c.x + frand(-1.5f, 1.5f), 0.4f, c.z + frand(-1.5f, 1.5f) }, (Vector3){ 0.4f, 0.4f, 0.4f }, TEX_WOOD, (Color){ 120, 130, 110, 255 }, 200, 1);
        }
        // ceiling pipes
        if (GetRandomValue(0, 3) == 0)
            add_box(L, (Vector3){ c.x, H - 0.3f, c.z }, (Vector3){ C / 2, 0.16f, 0.16f }, TEX_RUST, (Color){ 120, 120, 110, 255 }, 1.0f, F_NOCOLLIDE);
    }
    {   // dead ends get a message
        static const char *DEAD[] = { "IT WAS HERE", "STAY", "DON'T STOP", "TOO LATE", "BEHIND YOU", "IT HEARS YOU" };
        int placed = 0;
        for (int i = 1; i < N * N && placed < 7; i++) {
            int o = L->open[i], cnt = (o & 1) + ((o >> 1) & 1) + ((o >> 2) & 1) + ((o >> 3) & 1);
            if (cnt != 1 || i == best) continue;
            int d = -1;
            for (int k = 0; k < 4; k++) if (!(o & (1 << k))) { d = k; if (GetRandomValue(0, 1)) break; }
            Vector3 c = CELLC(i);
            const char *txt = placed == 0 ? g_user : DEAD[GetRandomValue(0, 5)];
            Vector3 wp = c; wp.y = 1.7f;
            static const int ax[4] = { 2, 0, 2, 0 }, dr[4] = { 1, -1, -1, 1 };
            if (d == 0) wp.z -= C / 2 - wt; else if (d == 1) wp.x += C / 2 - wt; else if (d == 2) wp.z += C / 2 - wt; else wp.x -= C / 2 - wt;
            add_decal(L, txt, wp, ax[d], dr[d], 0.4f, BLOOD);
            placed++;
        }
    }
    L->spawn = CELLC(0); L->spawn.y = 0.05f;
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
        Watcher w = { CELLC(i), frand(0, 6), false };
        Watchers_push(&L->watchers, w);
        placed++;
    }
    free(far); free(seen); IntV_drop(&q); IntV_drop(&stack);
    #undef CELLC
}

// ---------------------------------------------------------------- VOID: platforms in nothing
static void build_void(Level *L, int seed) {
    L->name = "THE STEPS";
    L->fog = (Color){ 24, 10, 36, 255 };
    L->fogDensity = 0.026f;
    L->light = 1.35f;
    L->killY = -35;
    SetRandomSeed(seed * 104729 + 7);
    Vector3 p = { 0, 0, 0 };
    float half = 3.0f, yaw = 0;
    add_box(L, (Vector3){ 0, -0.5f, 0 }, (Vector3){ half, 0.5f, half }, TEX_CONCRETE, (Color){ 150, 130, 170, 255 }, 2.0f, 0);
    L->spawn = (Vector3){ 0, 0.05f, 0 };
    L->spawnYaw = 0;
    add_box(L, (Vector3){ 0, 2.5f, 5.0f }, (Vector3){ 1.3f, 2.5f, 0.12f }, TEX_CONCRETE, (Color){ 40, 36, 44, 255 }, 2.0f, F_NOCOLLIDE);
    add_decal(L, "TURN BACK", (Vector3){ 0, 3.0f, 4.88f }, 2, -1, 0.3f, BLOOD);
    const int COUNT = 34;
    for (int i = 1; i <= COUNT; i++) {
        float nh = frand(1.3f, 2.4f);
        if (i == COUNT) nh = 3.0f;
        yaw += frand(-0.55f, 0.55f) + 0.07f;
        float gap = frand(1.4f, 3.1f);
        float dist = half + nh + gap;
        float dy = frand(-0.3f, 1.0f);
        p.x += sinf(yaw) * dist; p.z -= cosf(yaw) * dist; p.y += dy;
        TexId tx = (i % 5 == 0) ? TEX_FLESH : (i % 3 == 0) ? TEX_RUST : TEX_CONCRETE;
        add_box(L, (Vector3){ p.x, p.y - 0.5f, p.z }, (Vector3){ nh, 0.5f, nh }, tx, (Color){ 150, 130, 170, 255 }, 2.0f, 0);
        // spires hanging under the islands, unreachable decoration
        add_box(L, (Vector3){ p.x, p.y - 4.0f, p.z }, (Vector3){ nh * 0.25f, 3.2f, nh * 0.25f }, TEX_CONCRETE, (Color){ 90, 70, 110, 255 }, 2.0f, F_NOCOLLIDE);
        half = nh;
        // drifting rubble, simulated but weightless
        if (i % 2 == 0)
            add_prop(L, (Vector3){ p.x + frand(-6, 6), p.y + frand(1, 6), p.z + frand(-6, 6) }, (Vector3){ frand(0.3f, 0.9f), frand(0.3f, 0.9f), frand(0.3f, 0.9f) },
                     TEX_FLESH, (Color){ 170, 140, 170, 255 }, 40, 0.0f);
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

// ---------------------------------------------------------------- END: the way out
static void build_end(Level *L) {
    L->name = "THE WAY OUT";
    L->fog = (Color){ 200, 196, 188, 255 };
    L->fogDensity = 0.05f;
    L->light = 1.2f;
    L->killY = -50;
    L->spawn = (Vector3){ 0, 0.05f, 7 };
    L->spawnYaw = 0;
    // a long white hall that ends at a bed
    room(L, 3, 12, 3.2f, TEX_TILE, (Color){ 230, 230, 225, 255 }, TEX_WOOD, (Color){ 220, 210, 200, 255 }, TEX_CONCRETE, (Color){ 230, 230, 225, 255 });
    add_box(L, (Vector3){ 0, 0.35f, -10.5f }, (Vector3){ 1.0f, 0.35f, 1.5f }, TEX_WOOD, (Color){ 220, 200, 190, 255 }, 1.0f, 0);
    add_box(L, (Vector3){ 0, 0.78f, -10.3f }, (Vector3){ 0.92f, 0.1f, 1.3f }, TEX_TILE, (Color){ 240, 230, 230, 255 }, 1.0f, F_NOCOLLIDE);
    add_box(L, (Vector3){ 0, 1.8f, -11.88f }, (Vector3){ 1.0f, 0.8f, 0.05f }, TEX_CONCRETE, (Color){ 255, 255, 250, 255 }, 1.0f, F_EMIT | F_NOCOLLIDE);
    add_decal(L, "DON'T LIE DOWN", (Vector3){ 0, 2.6f, -12.0f }, 2, 1, 0.28f, BLOOD);
    L->bed = (Vector3){ 0, 0, -9.0f };
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
    case W_SHAFT:  build_shaft(L); break;
    case W_DRAINS: build_drains(L, seed); break;
    case W_VOID:   build_void(L, seed); break;
    default:       build_end(L); break;
    }
    // ash motes
    for (int i = 0; i < 140; i++) {
        Mote m = { { 0, 0, 0 }, { frand(-.1f, .1f), frand(-.25f, -.05f), frand(-.1f, .1f) }, frand(0, 1) };
        Motes_push(&L->motes, m);
    }
}

void level_free(Level *L) {
    if (b3World_IsValid(L->phys)) b3DestroyWorld(L->phys);
    Boxes_drop(&L->boxes); Pickups_drop(&L->pickups); Portals_drop(&L->portals);
    Watchers_drop(&L->watchers); Motes_drop(&L->motes); Props_drop(&L->props);
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
        if (Vector3Length(dir) > 0.05f) w->pos = Vector3Add(w->pos, Vector3Scale(Vector3Normalize(dir), (blind ? speed * 1.4f : speed) * dt));
    }
    return false;
}
