#include "figure.h"
#include "gfx.h"
#include <math.h>
#include <rlgl.h>

// a local frame: origin, right, up, forward. up may be scaled to squash a body flat against a wall
typedef struct { Vector3 o, r, u, f; } Frame;
static Vector3 W(const Frame *F, float x, float y, float z) {
    return Vector3Add(F->o, Vector3Add(Vector3Scale(F->r, x), Vector3Add(Vector3Scale(F->u, y), Vector3Scale(F->f, z))));
}
static Vector3 add3(Vector3 a, Vector3 b, Vector3 c) { return Vector3Add(a, Vector3Add(b, c)); }
static Color tint_or(const Fig *f, Color def) { return f->tint.a ? f->tint : def; }
static Color scale_c(Color c, float k) { return (Color){ (unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), 255 }; }

// head axes: turned partly toward what it is looking at, then tilted the wrong way
static void head_axes(const Fig *f, const Frame *F, Vector3 hc, Vector3 *hr, Vector3 *hu, Vector3 *hf) {
    Vector3 fw = Vector3Normalize(F->f), up = Vector3Normalize(F->u);
    if (f->look > 0) {
        Vector3 to = Vector3Subtract(f->lookAt, hc);
        if (Vector3Length(to) > 0.01f) fw = Vector3Normalize(Vector3Lerp(fw, Vector3Normalize(to), f->look));
    }
    Vector3 r = Vector3CrossProduct(fw, up);
    if (Vector3Length(r) < 0.01f) r = F->r;
    r = Vector3Normalize(r);
    Vector3 u = Vector3CrossProduct(r, fw);
    *hf = fw;
    *hr = Vector3Add(Vector3Scale(r, cosf(f->tilt)), Vector3Scale(u, sinf(f->tilt)));
    *hu = Vector3Add(Vector3Scale(u, cosf(f->tilt)), Vector3Scale(r, -sinf(f->tilt)));
}

static void fingers(Vector3 wrist, Vector3 dir, Vector3 side, float len, int n, Color c) {
    for (int k = 0; k < n; k++) {
        float o = (k - (n - 1) * 0.5f) * 0.035f;
        Vector3 tip = add3(wrist, Vector3Scale(dir, len * (k == 1 ? 1.0f : 0.85f)), Vector3Scale(side, o * 1.6f));
        gfx_limb(Vector3Add(wrist, Vector3Scale(side, o)), tip, 0.012f, 0.006f, TEX_SKIN, c);
    }
}

static void seated(const Fig *f, const Frame *F) {
    Color robe = tint_or(f, (Color){ 30, 26, 28, 255 }), skin = { 150, 138, 136, 255 };
    float top = 1.38f;
    for (int s = -1; s <= 1; s += 2) {   // thighs out over the seat, shins straight down
        gfx_limb(W(F, s * 0.13f, 0.52f, -0.05f), W(F, s * 0.14f, 0.52f, 0.38f), 0.1f, 0.09f, TEX_CLOTH, robe);
        gfx_limb(W(F, s * 0.14f, 0.52f, 0.38f), W(F, s * 0.14f, 0.03f, 0.42f), 0.08f, 0.07f, TEX_CLOTH, robe);
    }
    gfx_limb(W(F, 0, 0.45f, -0.06f), W(F, 0, top, 0), 0.26f, 0.18f, TEX_CLOTH, robe);
    gfx_limb(W(F, -0.24f, top, 0.02f), W(F, 0.24f, top, 0.02f), 0.11f, 0.11f, TEX_CLOTH, robe);
    gfx_limb(W(F, 0, top + 0.02f, 0.02f), W(F, 0, top + 0.2f, 0.12f), 0.08f, 0.07f, TEX_CLOTH, robe);
    Vector3 hc = W(F, 0, top + 0.28f, 0.13f), hr, hu, hf;
    head_axes(f, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.19f), Vector3Scale(hu, 0.24f), Vector3Scale(hf, 0.21f), TEX_CLOTH, robe);
    gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.12f), Vector3Scale(hu, -0.03f)), Vector3Scale(hr, 0.12f), Vector3Scale(hu, 0.15f), Vector3Scale(hf, 0.1f), TEX_CONCRETE, (Color){ 5, 4, 4, 255 });
    for (int s = -1; s <= 1; s += 2) {   // forearms laid on the table
        Vector3 sh = W(F, s * 0.28f, top - 0.04f, 0.03f), el = W(F, s * 0.3f, top - 0.45f, 0.18f), wr = W(F, s * 0.2f, 0.84f, 0.55f);
        gfx_limb(sh, el, 0.09f, 0.11f, TEX_CLOTH, robe);
        gfx_limb(el, wr, 0.035f, 0.028f, TEX_SKIN, skin);
        fingers(wr, Vector3Normalize(F->f), F->r, 0.22f, 4, skin);
    }
}

static void cocoon(const Fig *f, const Frame *F) {
    Color wrap = tint_or(f, (Color){ 170, 150, 140, 255 });
    float sway = sinf(f->t * 0.7f) * 0.12f, twist = sinf(f->t * 0.43f) * 0.3f;
    Vector3 top = f->pos, c = W(F, sway, -1.4f, 0);
    gfx_limb(top, W(F, sway * 0.5f, -0.5f, 0), 0.015f, 0.015f, TEX_SKIN, (Color){ 80, 30, 30, 255 });
    Vector3 ax = Vector3Scale(F->r, cosf(twist) * 0.32f), az = Vector3Scale(F->f, 0.3f);
    ax = Vector3Add(ax, Vector3Scale(F->f, sinf(twist) * 0.32f));
    gfx_ellipsoid(c, ax, (Vector3){ 0, 0.9f, 0 }, az, TEX_SKIN, wrap);
    // a face pushing out through the wrapping, mouth open
    Vector3 fc = Vector3Add(W(F, sway, -1.05f, 0), Vector3Scale(Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, ax)), 0.27f));
    gfx_ellipsoid(fc, Vector3Scale(F->r, 0.12f), (Vector3){ 0, 0.15f, 0 }, Vector3Scale(F->f, 0.08f), TEX_SKIN, wrap);
    Vector3 mo = Vector3Add(fc, Vector3Scale(Vector3Normalize(Vector3Subtract(fc, c)), 0.06f));
    gfx_ellipsoid((Vector3){ mo.x, mo.y - 0.05f, mo.z }, Vector3Scale(F->r, 0.04f), (Vector3){ 0, 0.07f, 0 }, Vector3Scale(F->f, 0.03f), TEX_CONCRETE, (Color){ 6, 2, 2, 255 });
}

static void tall(const Fig *f, const Frame *F) {
    Color suit = tint_or(f, (Color){ 16, 15, 18, 255 }), skin = { 220, 216, 210, 255 };
    for (int s = -1; s <= 1; s += 2) {
        gfx_limb(W(F, s * 0.09f, 1.25f, 0), W(F, s * 0.1f, 0.62f, 0.03f), 0.045f, 0.035f, TEX_CLOTH, suit);
        gfx_limb(W(F, s * 0.1f, 0.62f, 0.03f), W(F, s * 0.1f, 0.02f, 0), 0.035f, 0.03f, TEX_CLOTH, suit);
    }
    gfx_limb(W(F, 0, 1.22f, 0), W(F, 0, 2.05f, 0.02f), 0.08f, 0.11f, TEX_CLOTH, suit);
    gfx_limb(W(F, -0.16f, 2.04f, 0.02f), W(F, 0.16f, 2.04f, 0.02f), 0.05f, 0.05f, TEX_CLOTH, suit);
    for (int s = -1; s <= 1; s += 2) {   // arms that hang past the knees
        Vector3 sh = W(F, s * 0.18f, 2.02f, 0.02f), el = W(F, s * 0.22f, 1.35f, 0.04f), wr = W(F, s * 0.2f, 0.7f, 0.06f);
        gfx_limb(sh, el, 0.035f, 0.03f, TEX_CLOTH, suit);
        gfx_limb(el, wr, 0.03f, 0.025f, TEX_CLOTH, suit);
        fingers(wr, Vector3Negate(F->u), F->r, 0.22f, 3, skin);
    }
    gfx_limb(W(F, 0, 2.06f, 0.02f), W(F, 0, 2.22f, 0.03f), 0.03f, 0.03f, TEX_SKIN, skin);
    Vector3 hc = W(F, 0, 2.36f, 0.03f), hr, hu, hf;
    head_axes(f, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.1f), Vector3Scale(hu, 0.15f), Vector3Scale(hf, 0.11f), TEX_SKIN, skin);   // no face at all
}

static void mother(const Fig *f, const Frame *F) {
    Color gown = tint_or(f, (Color){ 168, 160, 146, 255 }), skin = { 196, 188, 182, 255 }, hair = { 10, 8, 8, 255 };
    gfx_limb(W(F, 0, 0.02f, 0), W(F, 0, 1.5f, 0), 0.34f, 0.15f, TEX_CLOTH, gown);
    for (int s = -1; s <= 1; s += 2) gfx_limb(W(F, s * 0.1f, 0.03f, 0.18f), W(F, s * 0.12f, 0.02f, 0.44f), 0.045f, 0.015f, TEX_SKIN, skin);   // bare feet, toes too long
    gfx_limb(W(F, -0.2f, 1.5f, 0), W(F, 0.2f, 1.5f, 0), 0.07f, 0.07f, TEX_CLOTH, gown);
    // the neck goes up and then over, much too far
    Vector3 nk = W(F, 0.12f, 1.74f, 0.04f), hc = W(F, 0.27f, 1.8f, 0.07f), hr, hu, hf;
    gfx_limb(W(F, 0, 1.52f, 0), nk, 0.05f, 0.045f, TEX_SKIN, skin);
    gfx_limb(nk, hc, 0.045f, 0.04f, TEX_SKIN, skin);
    head_axes(f, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.1f), Vector3Scale(hu, 0.13f), Vector3Scale(hf, 0.11f), TEX_SKIN, skin);
    gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.09f), Vector3Scale(hu, -0.05f)), Vector3Scale(hr, 0.03f), Vector3Scale(hu, 0.06f), Vector3Scale(hf, 0.03f), TEX_CONCRETE, (Color){ 4, 2, 2, 255 });
    // hair: it hangs straight down from wherever her head is, over her face
    for (int k = 0; k < 18; k++) {
        float a = k * 0.349f;
        Vector3 p0 = add3(hc, Vector3Scale(hu, 0.07f), Vector3Add(Vector3Scale(hr, cosf(a) * 0.09f), Vector3Scale(hf, sinf(a) * 0.1f)));
        float len = 0.7f + 0.25f * ((k * 7) % 5) / 4.0f, sw = sinf(f->t * 0.9f + k) * 0.02f;
        Vector3 mid = { p0.x + sw, p0.y - len * 0.5f, p0.z }, end = { p0.x + sw * 2 + cosf(a) * 0.03f, p0.y - len, p0.z + sinf(a) * 0.03f };
        gfx_limb(p0, mid, 0.022f, 0.016f, TEX_CLOTH, hair);
        gfx_limb(mid, end, 0.016f, 0.004f, TEX_CLOTH, hair);
    }
    for (int s = -1; s <= 1; s += 2) {   // arms down past her knees
        Vector3 sh = W(F, s * 0.22f, 1.48f, 0), el = W(F, s * 0.27f, 1.0f, 0.08f), wr = W(F, s * 0.25f, 0.52f, 0.16f);
        gfx_limb(sh, el, 0.035f, 0.03f, TEX_SKIN, skin);
        gfx_limb(el, wr, 0.03f, 0.022f, TEX_SKIN, skin);
        fingers(wr, Vector3Negate(F->u), F->r, 0.27f, 4, skin);
    }
}

static void hand(const Fig *f, const Frame *F) {
    Color skin = tint_or(f, (Color){ 176, 168, 160, 255 });
    gfx_limb(W(F, 0, -1.5f, 0), W(F, 0, 0, 0), 0.2f, 0.18f, TEX_SKIN, skin);
    gfx_limb(W(F, 0, 0, 0), W(F, 0, 2.2f, 0.1f), 0.18f, 0.14f, TEX_SKIN, skin);
    Vector3 palm = W(F, 0, 2.55f, 0.12f);
    gfx_ellipsoid(palm, Vector3Scale(F->r, 0.26f), Vector3Scale(F->u, 0.32f), Vector3Scale(F->f, 0.1f), TEX_SKIN, skin);
    float close = 0.5f + 0.5f * sinf(f->t * 0.45f);
    for (int k = 0; k < 5; k++) {   // four fingers and a thumb, curling in toward whatever is in front of it
        bool thumb = k == 4;
        Vector3 base = thumb ? W(F, -0.26f, 2.4f, 0.16f) : W(F, -0.18f + k * 0.12f, 2.85f, 0.12f);
        Vector3 up = thumb ? Vector3Normalize(Vector3Add(Vector3Scale(F->r, -0.7f), F->u)) : Vector3Normalize(F->u);
        float a = 0.25f + close * (0.6f + 0.1f * k) + 0.06f * sinf(f->t * 1.7f + k);
        static const float LEN[3] = { 0.42f, 0.3f, 0.22f };
        Vector3 p = base;
        for (int s = 0; s < 3; s++) {
            float ang = a * (s + 1);
            Vector3 dir = Vector3Normalize(Vector3Add(Vector3Scale(up, cosf(ang)), Vector3Scale(Vector3Normalize(F->f), sinf(ang))));
            Vector3 q = Vector3Add(p, Vector3Scale(dir, LEN[s] * (thumb ? 0.8f : 1.0f)));
            gfx_limb(p, q, 0.06f - s * 0.012f, 0.05f - s * 0.014f, TEX_SKIN, skin);
            p = q;
        }
    }
}
static void face(const Fig *f, const Frame *F) {
    Color skin = tint_or(f, (Color){ 196, 184, 176, 255 });
    Vector3 c = f->pos, n = Vector3Normalize(F->f);
    gfx_ellipsoid(c, Vector3Scale(F->r, 0.34f), Vector3Scale(F->u, 0.46f), Vector3Scale(n, 0.16f), TEX_SKIN, skin);
    Vector3 to = Vector3Subtract(f->lookAt, c);
    float ex = Clamp(Vector3DotProduct(to, Vector3Normalize(F->r)) * 0.02f, -0.03f, 0.03f), ey = Clamp(Vector3DotProduct(to, Vector3Normalize(F->u)) * 0.02f, -0.025f, 0.025f);
    for (int s = -1; s <= 1; s += 2) {   // the sockets, and in them the eyes, following you
        Vector3 sc = W(F, s * 0.12f, 0.1f, 0.12f);
        gfx_ellipsoid(sc, Vector3Scale(F->r, 0.075f), Vector3Scale(F->u, 0.055f), Vector3Scale(n, 0.04f), TEX_CONCRETE, (Color){ 6, 3, 3, 255 });
        gfx_ellipsoid(add3(sc, Vector3Scale(n, 0.035f), Vector3Add(Vector3Scale(Vector3Normalize(F->r), ex), Vector3Scale(Vector3Normalize(F->u), ey))), Vector3Scale(F->r, 0.022f), Vector3Scale(F->u, 0.022f), Vector3Scale(n, 0.01f), TEX_SKIN, (Color){ 230, 226, 214, 255 });
    }
    float open = 0.4f + 0.6f * fabsf(sinf(f->t * 0.9f));
    gfx_ellipsoid(W(F, 0, -0.2f, 0.12f), Vector3Scale(F->r, 0.09f), Vector3Scale(F->u, 0.03f + 0.08f * open), Vector3Scale(n, 0.04f), TEX_CONCRETE, (Color){ 6, 2, 2, 255 });
}

static void sheet(const Fig *f, const Frame *F) {
    Color cl = tint_or(f, (Color){ 210, 214, 216, 255 }), skin = { 160, 170, 180, 255 };
    gfx_limb(W(F, 0, 0.12f, 0), W(F, 0, 1.55f, 0), 0.38f, 0.2f, TEX_CLOTH, cl);
    gfx_limb(W(F, -0.22f, 1.5f, 0), W(F, 0.22f, 1.5f, 0), 0.11f, 0.11f, TEX_CLOTH, cl);
    Vector3 hc = W(F, 0.04f, 1.78f, 0.04f), hr, hu, hf;
    head_axes(f, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.13f), Vector3Scale(hu, 0.16f), Vector3Scale(hf, 0.14f), TEX_CLOTH, cl);
    gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.11f), Vector3Scale(hu, -0.04f)), Vector3Scale(hr, 0.05f), Vector3Scale(hu, 0.06f), Vector3Scale(hf, 0.04f), TEX_CLOTH, scale_c(cl, 0.75f));   // the shape of a mouth, open, pressing on the sheet
    for (int s = -1; s <= 1; s += 2) gfx_limb(W(F, s * 0.12f, 0.12f, 0.05f), W(F, s * 0.13f, 0.02f, 0.3f), 0.05f, 0.03f, TEX_SKIN, skin);   // grey feet
    gfx_box(W(F, 0.13f, 0.06f, 0.36f), (Vector3){ 0.03f, 0.04f, 0.005f }, TEX_PAPER, (Color){ 220, 210, 170, 255 }, 1.0f);   // the tag
}
static void puppet(const Fig *f, const Frame *F) {
    Color wood = tint_or(f, (Color){ 200, 180, 160, 255 }), cloth = { 110, 20, 26, 255 };
    float j = sinf(f->t * 6.0f), k = cosf(f->t * 4.3f);   // jerked about on its strings
    Vector3 hip = W(F, 0, 1.05f + 0.05f * j, 0), neck = W(F, 0.03f * k, 1.62f, 0);
    gfx_limb(hip, neck, 0.16f, 0.13f, TEX_CLOTH, cloth);
    for (int s = -1; s <= 1; s += 2) {
        Vector3 knee = W(F, s * 0.12f, 0.55f + 0.1f * (s > 0 ? j : -j), 0.12f), foot = W(F, s * 0.12f, 0.05f + 0.06f * (s > 0 ? j : -j), 0.04f);
        gfx_limb(W(F, s * 0.1f, 1.0f, 0), knee, 0.05f, 0.045f, TEX_WOOD, wood);
        gfx_limb(knee, foot, 0.045f, 0.035f, TEX_WOOD, wood);
        gfx_ellipsoid(knee, Vector3Scale(F->r, 0.06f), Vector3Scale(F->u, 0.06f), Vector3Scale(F->f, 0.06f), TEX_WOOD, wood);
        Vector3 sh = W(F, s * 0.2f, 1.58f, 0), el = W(F, s * 0.34f, 1.3f + 0.15f * (s > 0 ? k : -k), 0.1f), hd = W(F, s * 0.3f, 1.55f + 0.25f * (s > 0 ? k : -k), 0.25f);
        gfx_limb(sh, el, 0.04f, 0.035f, TEX_WOOD, wood);
        gfx_limb(el, hd, 0.035f, 0.03f, TEX_WOOD, wood);
        gfx_limb(hd, (Vector3){ hd.x, hd.y + 6.0f, hd.z }, 0.004f, 0.004f, TEX_CONCRETE, (Color){ 200, 200, 200, 255 });   // the strings, up into the dark
    }
    Fig g = *f; g.tilt += 0.7f + 0.3f * j;   // the head hangs to one side
    Vector3 hc = W(F, 0.05f, 1.86f, 0.04f), hr, hu, hf;
    head_axes(&g, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.13f), Vector3Scale(hu, 0.17f), Vector3Scale(hf, 0.14f), TEX_SKIN, (Color){ 236, 226, 214, 255 });
    for (int s = -1; s <= 1; s += 2) gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.12f), Vector3Add(Vector3Scale(hr, s * 0.05f), Vector3Scale(hu, 0.03f))), Vector3Scale(hr, 0.025f), Vector3Scale(hu, 0.03f), Vector3Scale(hf, 0.015f), TEX_CONCRETE, (Color){ 10, 6, 6, 255 });
    gfx_limb(add3(hc, Vector3Scale(hf, 0.13f), Vector3Add(Vector3Scale(hu, -0.06f), Vector3Scale(hr, -0.05f))), add3(hc, Vector3Scale(hf, 0.13f), Vector3Add(Vector3Scale(hu, -0.06f), Vector3Scale(hr, 0.05f))), 0.008f, 0.008f, TEX_FLESH, (Color){ 120, 20, 20, 255 });   // a painted smile
    gfx_limb(hc, (Vector3){ hc.x, hc.y + 6.0f, hc.z }, 0.004f, 0.004f, TEX_CONCRETE, (Color){ 200, 200, 200, 255 });
}

static void penitent(const Fig *f, const Frame *F, bool kneel) {
    Color robe = tint_or(f, (Color){ 30, 26, 28, 255 }), skin = { 150, 138, 136, 255 };
    float sway = sinf(f->t * 0.7f) * 0.02f, top = kneel ? 1.12f : 1.72f;
    if (kneel) gfx_limb(W(F, 0, 0.02f, -0.12f), W(F, sway, top, 0), 0.48f, 0.2f, TEX_CLOTH, robe);
    else gfx_limb(W(F, 0, 0.02f, 0), W(F, sway, top, 0), 0.44f, 0.19f, TEX_CLOTH, robe);
    gfx_limb(W(F, -0.25f + sway, top, 0.03f), W(F, 0.25f + sway, top, 0.03f), 0.12f, 0.12f, TEX_CLOTH, robe);
    gfx_limb(W(F, sway, top + 0.02f, 0.02f), W(F, sway, top + 0.22f, 0.14f), 0.08f, 0.07f, TEX_CLOTH, robe);
    Vector3 hc = W(F, sway, top + 0.3f, 0.15f), hr, hu, hf;
    head_axes(f, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.2f), Vector3Scale(hu, 0.25f), Vector3Scale(hf, 0.22f), TEX_CLOTH, robe);
    // where the face should be there is only a hole
    gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.13f), Vector3Scale(hu, -0.03f)), Vector3Scale(hr, 0.13f), Vector3Scale(hu, 0.16f), Vector3Scale(hf, 0.11f), TEX_CONCRETE, (Color){ 5, 4, 4, 255 });
    if (Vector3Distance(hc, f->lookAt) < 5.0f) {   // close enough, and there is a face in there after all
        Vector3 fc = add3(hc, Vector3Scale(hf, 0.215f), Vector3Scale(hu, -0.04f));
        gfx_ellipsoid(fc, Vector3Scale(hr, 0.085f), Vector3Scale(hu, 0.14f), Vector3Scale(hf, 0.05f), TEX_SKIN, (Color){ 214, 208, 200, 255 });
        for (int s = -1; s <= 1; s += 2)
            gfx_ellipsoid(add3(fc, Vector3Scale(hf, 0.035f), Vector3Add(Vector3Scale(hr, s * 0.035f), Vector3Scale(hu, 0.035f))), Vector3Scale(hr, 0.022f), Vector3Scale(hu, 0.028f), Vector3Scale(hf, 0.02f), TEX_CONCRETE, (Color){ 3, 2, 2, 255 });
        gfx_ellipsoid(add3(fc, Vector3Scale(hf, 0.035f), Vector3Scale(hu, -0.065f)), Vector3Scale(hr, 0.022f), Vector3Scale(hu, 0.07f), Vector3Scale(hf, 0.02f), TEX_CONCRETE, (Color){ 3, 2, 2, 255 });
    }
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh = W(F, s * 0.29f + sway, top - 0.03f, 0.03f), el, wr, fd;
        if (kneel) { el = W(F, s * 0.3f, top - 0.42f, 0.2f); wr = W(F, s * 0.05f, top - 0.28f, 0.34f); fd = Vector3Normalize(Vector3Add(F->u, Vector3Scale(F->f, 0.2f))); }
        else {
            float hang = sinf(f->t * 0.5f + s) * 0.02f;
            el = W(F, s * 0.33f, top - 0.5f, 0.08f + sway); wr = W(F, s * 0.31f, top - 0.95f, 0.12f + hang); fd = Vector3Negate(F->u);
        }
        gfx_limb(sh, el, 0.1f, 0.12f, TEX_CLOTH, robe);
        gfx_limb(el, wr, 0.035f, 0.028f, TEX_SKIN, skin);
        fingers(wr, fd, F->r, kneel ? 0.2f : 0.3f, 3, skin);
    }
    if (!kneel) {   // a cross on a cord, hung upside down
        Color iron = { 120, 110, 96, 255 };
        gfx_limb(W(F, 0, top - 0.25f, 0.215f), W(F, 0, top - 0.6f, 0.25f), 0.014f, 0.014f, TEX_RUST, iron);
        gfx_limb(W(F, -0.08f, top - 0.5f, 0.24f), W(F, 0.08f, top - 0.5f, 0.24f), 0.012f, 0.012f, TEX_RUST, iron);
    }
}

// a man who has not eaten for a very long time, on his hands and knees. he holds his head craned right back so that
// he can look up at you, his hair hangs to the floor, and he is smiling with every tooth he has. his eyes are white
// all the way across, and they are the last thing to go into the dark
static void crawler(const Fig *f, const Frame *F) {
    Color pale = tint_or(f, (Color){ 150, 148, 138, 255 }), bone = scale_c(pale, 1.15f), hair = { 9, 8, 9, 255 };
    float ph = f->stride;
    float jerk = fmodf(f->t * 1.7f + f->stride * 0.13f, 4.3f) < 0.12f ? 1.0f : 0.0f;   // now and then all of him twitches at once
    float tr = sinf(f->t * 47.0f) * 0.006f;                                           // and he never stops trembling
    Vector3 hips = W(F, tr, 0.64f, -0.44f), mid = W(F, 0, 0.8f, -0.06f), sh = W(F, -tr, 0.74f + 0.05f * jerk, 0.34f);
    gfx_limb(hips, mid, 0.13f, 0.1f, TEX_SKIN, pale);    // nothing at the waist at all
    gfx_limb(mid, sh, 0.1f, 0.15f, TEX_SKIN, pale);
    gfx_ellipsoid(W(F, 0, 0.74f, 0.18f), Vector3Scale(F->r, 0.19f), Vector3Scale(F->u, 0.14f), Vector3Scale(F->f, 0.23f), TEX_SKIN, pale);
    for (int k = 0; k < 8; k++) {   // every knob of the spine
        Vector3 p = Vector3Lerp(hips, sh, (k + 0.5f) / 8.0f);
        p = Vector3Add(p, Vector3Scale(F->u, 0.11f + 0.05f * sinf((k + 0.5f) / 8.0f * 3.14159f)));
        gfx_ellipsoid(p, Vector3Scale(F->r, 0.03f), Vector3Scale(F->u, 0.035f), Vector3Scale(F->f, 0.035f), TEX_SKIN, bone);
    }
    for (int s = -1; s <= 1; s += 2) {
        for (int k = 0; k < 5; k++) {   // ribs you could count from across the room
            float z = 0.0f + k * 0.08f;
            Vector3 a = W(F, s * 0.04f, 0.86f, z), b = W(F, s * 0.19f, 0.74f, z + 0.03f), c = W(F, s * 0.15f, 0.6f, z + 0.05f);
            gfx_limb(a, b, 0.016f, 0.014f, TEX_SKIN, bone);
            gfx_limb(b, c, 0.014f, 0.01f, TEX_SKIN, bone);
        }
        gfx_ellipsoid(W(F, s * 0.11f, 0.86f + 0.03f * jerk, 0.3f), Vector3Scale(F->r, 0.08f), Vector3Scale(F->u, 0.035f), Vector3Scale(F->f, 0.1f), TEX_SKIN, bone);   // shoulder blades, like folded wings
        // legs: knees on the floor, feet trailing behind, thin as broom handles
        float lp = ph + (s < 0 ? 0.0f : 3.14159f), lift = fmaxf(0.0f, sinf(lp)) * 0.1f, sw = cosf(lp) * 0.14f;
        Vector3 hip = W(F, s * 0.13f, 0.6f, -0.46f), knee = W(F, s * 0.17f, 0.07f + lift, -0.28f + sw), ankle = W(F, s * 0.15f, 0.08f + lift * 0.5f, -0.82f + sw), toe = W(F, s * 0.15f, 0.02f, -0.94f + sw);
        gfx_limb(hip, knee, 0.075f, 0.05f, TEX_SKIN, pale);
        gfx_ellipsoid(knee, Vector3Scale(F->r, 0.05f), Vector3Scale(F->u, 0.05f), Vector3Scale(F->f, 0.055f), TEX_SKIN, bone);
        gfx_limb(knee, ankle, 0.045f, 0.03f, TEX_SKIN, pale);
        gfx_limb(ankle, toe, 0.035f, 0.02f, TEX_SKIN, pale);
        // arms: a man's arms, only much too long, and the hands spread flat
        float ap = lp + 3.14159f, alift = fmaxf(0.0f, sinf(ap)) * 0.15f, asw = cosf(ap) * 0.17f;
        Vector3 s0 = W(F, s * 0.21f, 0.76f, 0.36f), el = W(F, s * 0.33f, 0.42f + alift, 0.44f + asw * 0.5f), wr = W(F, s * 0.25f, 0.03f + alift * 0.7f, 0.66f + asw);
        gfx_limb(s0, el, 0.055f, 0.04f, TEX_SKIN, pale);
        gfx_ellipsoid(el, Vector3Scale(F->r, 0.04f), Vector3Scale(F->u, 0.04f), Vector3Scale(F->f, 0.04f), TEX_SKIN, bone);
        gfx_limb(el, wr, 0.038f, 0.026f, TEX_SKIN, pale);
        for (int k = 0; k < 5; k++) {
            float a = (k - 2) * 0.32f;
            Vector3 d = Vector3Normalize(Vector3Add(Vector3Scale(Vector3Normalize(F->f), cosf(a)), Vector3Scale(Vector3Normalize(F->r), sinf(a) * s)));
            Vector3 k1 = Vector3Add(wr, Vector3Scale(d, 0.1f)), k2 = Vector3Add(k1, Vector3Add(Vector3Scale(d, 0.09f), Vector3Scale(F->u, -0.02f)));
            gfx_limb(wr, k1, 0.012f, 0.01f, TEX_SKIN, pale);
            gfx_limb(k1, k2, 0.01f, 0.004f, TEX_SKIN, pale);
        }
    }
    // the neck goes up and back, and his face is turned all the way up to yours
    Vector3 nk = W(F, 0, 0.94f, 0.5f), hc = W(F, 0.02f * jerk, 1.05f, 0.6f);
    gfx_limb(sh, nk, 0.06f, 0.05f, TEX_SKIN, pale);
    gfx_limb(nk, hc, 0.05f, 0.045f, TEX_SKIN, pale);
    Fig g = *f; g.look = fmaxf(f->look, 0.85f); g.tilt += jerk * 0.8f + sinf(f->t * 0.6f) * 0.15f;
    Frame hF = *F; hF.u = Vector3Normalize(F->u);
    Vector3 hr, hu, hf;
    head_axes(&g, &hF, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.095f), Vector3Scale(hu, 0.14f), Vector3Scale(hf, 0.11f), TEX_SKIN, pale);
    for (int s = -1; s <= 1; s += 2) {   // cheeks fallen in, and the sockets deep
        gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.07f), Vector3Add(Vector3Scale(hr, s * 0.06f), Vector3Scale(hu, -0.04f))), Vector3Scale(hr, 0.03f), Vector3Scale(hu, 0.05f), Vector3Scale(hf, 0.04f), TEX_SKIN, scale_c(pale, 0.55f));
        gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.084f), Vector3Add(Vector3Scale(hr, s * 0.04f), Vector3Scale(hu, s < 0 ? 0.028f : 0.038f))), Vector3Scale(hr, s < 0 ? 0.03f : 0.024f), Vector3Scale(hu, s < 0 ? 0.027f : 0.019f), Vector3Scale(hf, 0.025f), TEX_SKIN, scale_c(pale, 0.38f));
    }
    // the jaw: hanging open much further than a jaw goes, as if it had come unhooked, with the teeth all wrong
    float gape = 0.05f + 0.025f * fmaxf(0.0f, sinf(f->t * 1.7f)) + 0.05f * f->look;
    Vector3 mc = add3(hc, Vector3Scale(hf, 0.085f), Vector3Scale(hu, -0.075f - gape * 0.6f));
    gfx_ellipsoid(mc, Vector3Scale(hr, 0.045f), Vector3Scale(hu, gape), Vector3Scale(hf, 0.03f), TEX_CONCRETE, (Color){ 10, 2, 4, 255 });
    gfx_ellipsoid(add3(mc, Vector3Scale(hu, -gape - 0.01f), Vector3Scale(hf, -0.01f)), Vector3Scale(hr, 0.05f), Vector3Scale(hu, 0.025f), Vector3Scale(hf, 0.05f), TEX_SKIN, pale);   // the chin, a long way down
    static const float TL[7] = { 0.03f, 0.018f, 0.04f, 0.012f, 0.035f, 0.022f, 0.028f };
    for (int k = 0; k < 7; k++) {
        float x = (k - 3) / 3.0f;
        Vector3 tp = add3(mc, Vector3Scale(hr, x * 0.038f), Vector3Add(Vector3Scale(hu, gape * 0.9f), Vector3Scale(hf, 0.022f)));
        gfx_limb(tp, add3(tp, Vector3Scale(hu, -TL[k]), Vector3Scale(hr, x * 0.006f)), 0.006f, 0.002f, TEX_SKIN, (Color){ 196, 180, 130, 255 });
        if (k % 2) { Vector3 bp = add3(mc, Vector3Scale(hr, x * 0.034f), Vector3Add(Vector3Scale(hu, -gape * 0.9f), Vector3Scale(hf, 0.02f))); gfx_limb(bp, Vector3Add(bp, Vector3Scale(hu, TL[6 - k] * 0.7f)), 0.006f, 0.002f, TEX_SKIN, (Color){ 180, 164, 120, 255 }); }
    }
    gfx_set_emit(true);   // two points of white in the sockets, lit from inside, not quite the same size
    for (int s = -1; s <= 1; s += 2)
        gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.11f), Vector3Add(Vector3Scale(hr, s * 0.04f), Vector3Scale(hu, s < 0 ? 0.026f : 0.038f))), Vector3Scale(hr, s < 0 ? 0.009f : 0.007f), Vector3Scale(hu, s < 0 ? 0.007f : 0.005f), Vector3Scale(hf, 0.006f), TEX_SKIN, (Color){ 240, 240, 230, 255 });
    gfx_set_emit(false);
    // hair: thin, black, wet, in strings. it hangs straight down from his scalp, some of it across his face
    for (int k = 0; k < 16; k++) {
        float a = 0.9f + k * (4.48f / 15.0f);   // round the sides and back, not over the face
        Vector3 p0 = add3(hc, Vector3Scale(hu, 0.08f), Vector3Add(Vector3Scale(hr, sinf(a) * 0.09f), Vector3Scale(hf, cosf(a) * 0.09f)));
        float len = 0.3f + 0.45f * ((k * 7) % 5) / 4.0f, sw = sinf(f->t * 1.1f + k) * 0.012f;
        Vector3 m = { p0.x + sw, p0.y - len * 0.5f, p0.z }, e = { p0.x + sw * 2, p0.y - len, p0.z };
        gfx_limb(p0, m, 0.009f, 0.007f, TEX_CLOTH, hair);
        gfx_limb(m, e, 0.007f, 0.002f, TEX_CLOTH, hair);
    }
}

// the cat. tilt says what it is doing (0 walking, 1 sitting, 2 asleep); look is how frightened it is
static void cat(const Fig *f, const Frame *F) {
    Color fur = tint_or(f, (Color){ 22, 20, 24, 255 });
    float fear = f->look, mode = f->tilt;
    if (mode >= 2) {   // curled up in a ball, nose under its tail
        gfx_ellipsoid(W(F, 0, 0.09f, 0), Vector3Scale(F->r, 0.17f), Vector3Scale(F->u, 0.09f + 0.006f * sinf(f->t * 1.6f)), Vector3Scale(F->f, 0.15f), TEX_CLOTH, fur);
        gfx_ellipsoid(W(F, 0.1f, 0.1f, 0.1f), Vector3Scale(F->r, 0.07f), Vector3Scale(F->u, 0.06f), Vector3Scale(F->f, 0.07f), TEX_CLOTH, fur);
        for (int s = -1; s <= 1; s += 2) gfx_limb(W(F, 0.1f + s * 0.035f, 0.14f, 0.1f), W(F, 0.12f + s * 0.05f, 0.2f, 0.11f), 0.02f, 0.002f, TEX_CLOTH, fur);
        gfx_limb(W(F, -0.15f, 0.05f, -0.05f), W(F, 0.05f, 0.04f, 0.17f), 0.035f, 0.025f, TEX_CLOTH, fur);
        return;
    }
    float sit = mode >= 1 ? 1.0f : 0.0f, arch = fear * 0.09f, puff = 1.0f + fear * 0.5f;
    Vector3 hip = W(F, 0, 0.2f + arch * 0.5f - sit * 0.1f, -0.15f), chest = W(F, 0, 0.22f + arch * 0.4f + sit * 0.08f, 0.12f);
    Vector3 back = Vector3Add(Vector3Lerp(hip, chest, 0.5f), Vector3Scale(F->u, 0.03f + arch));
    gfx_limb(hip, back, 0.085f * puff, 0.08f * puff, TEX_CLOTH, fur);
    gfx_limb(back, chest, 0.08f * puff, 0.075f * puff, TEX_CLOTH, fur);
    for (int s = -1; s <= 1; s += 2) {
        float lp = f->stride + (s < 0 ? 0.0f : 3.14159f), sw = sit ? 0 : sinf(lp) * 0.06f, lift = sit ? 0 : fmaxf(0, cosf(lp)) * 0.03f;
        gfx_limb(W(F, s * 0.05f, 0.18f, 0.12f), W(F, s * 0.05f, 0.01f + lift, 0.13f + sw), 0.025f, 0.018f, TEX_CLOTH, fur);   // front legs
        if (sit) gfx_ellipsoid(W(F, s * 0.06f, 0.06f, -0.12f), Vector3Scale(F->r, 0.05f), Vector3Scale(F->u, 0.06f), Vector3Scale(F->f, 0.09f), TEX_CLOTH, fur);   // haunches
        else gfx_limb(W(F, s * 0.05f, 0.18f + arch * 0.5f, -0.16f), W(F, s * 0.05f, 0.01f + lift, -0.16f - sw), 0.03f, 0.018f, TEX_CLOTH, fur);
    }
    // the tail: low and swinging, or straight up and twice as thick
    float swing = sinf(f->t * (1.2f + fear * 6.0f)) * (0.12f - fear * 0.08f);
    Vector3 t0 = W(F, 0, 0.2f - sit * 0.12f, -0.2f), t1 = W(F, swing, 0.28f + fear * 0.15f - sit * 0.15f, -0.34f + fear * 0.1f + sit * 0.05f), t2 = W(F, swing * 2, 0.36f + fear * 0.28f - sit * 0.3f, -0.36f + fear * 0.12f + sit * 0.22f);
    gfx_limb(t0, t1, 0.022f * puff, 0.02f * puff, TEX_CLOTH, fur);
    gfx_limb(t1, t2, 0.02f * puff, 0.012f * puff, TEX_CLOTH, fur);
    Vector3 hc = W(F, 0, 0.3f + sit * 0.08f + arch * 0.2f - fear * 0.04f, 0.2f), hr, hu, hf;
    head_axes(f, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.06f), Vector3Scale(hu, 0.055f), Vector3Scale(hf, 0.06f), TEX_CLOTH, fur);
    gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.05f), Vector3Scale(hu, -0.015f)), Vector3Scale(hr, 0.03f), Vector3Scale(hu, 0.022f), Vector3Scale(hf, 0.02f), TEX_CLOTH, fur);
    for (int s = -1; s <= 1; s += 2) {   // ears: up, or flat back against the skull
        Vector3 eb = add3(hc, Vector3Scale(hr, s * 0.038f), Vector3Scale(hu, 0.04f));
        Vector3 et = fear > 0.5f ? add3(eb, Vector3Scale(hr, s * 0.05f), Vector3Scale(hf, -0.04f)) : add3(eb, Vector3Scale(hu, 0.055f), Vector3Scale(hr, s * 0.012f));
        gfx_limb(eb, et, 0.022f, 0.002f, TEX_CLOTH, fur);
    }
    gfx_set_emit(true);
    for (int s = -1; s <= 1; s += 2)
        gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.052f), Vector3Add(Vector3Scale(hr, s * 0.025f), Vector3Scale(hu, 0.012f))), Vector3Scale(hr, 0.012f), Vector3Scale(hu, 0.01f + 0.004f * fear), Vector3Scale(hf, 0.006f), TEX_SKIN, (Color){ 150, 230, 90, 255 });
    gfx_set_emit(false);
}

// it is only a child. it is only nearly three metres tall. it counts with its hands over its eyes, and it peeks.
// when it has found you, it takes them away, and there is nothing behind them but two holes and something running out
static void seeker(const Fig *f, const Frame *F) {
    Color jumper = tint_or(f, (Color){ 92, 28, 30, 255 }), shorts = { 60, 60, 66, 255 }, sock = { 190, 186, 176, 255 }, skin = { 206, 192, 180, 255 }, shoe = { 20, 16, 14, 255 };
    bool found = f->look > 0.5f;
    for (int s = -1; s <= 1; s += 2) {
        float lp = f->stride + (s < 0 ? 0 : 3.14159f), sw = sinf(lp) * 0.22f, lift = fmaxf(0, cosf(lp)) * 0.1f;
        Vector3 hip = W(F, s * 0.1f, 1.35f, 0), knee = W(F, s * 0.11f, 0.72f + lift, 0.06f + sw * 0.6f), ank = W(F, s * 0.1f, 0.08f + lift, sw * 0.3f);
        gfx_limb(hip, knee, 0.05f, 0.04f, TEX_SKIN, skin);   // bare knees, scabbed
        gfx_limb(knee, Vector3Lerp(knee, ank, 0.25f), 0.04f, 0.038f, TEX_SKIN, skin);
        gfx_limb(Vector3Lerp(knee, ank, 0.25f), ank, 0.042f, 0.034f, TEX_CLOTH, sock);
        gfx_limb(ank, Vector3Add(ank, Vector3Add(Vector3Scale(F->f, 0.16f), Vector3Scale(F->u, -0.06f))), 0.045f, 0.04f, TEX_CONCRETE, shoe);
    }
    gfx_ellipsoid(W(F, 0, 1.36f, 0), Vector3Scale(F->r, 0.19f), Vector3Scale(F->u, 0.15f), Vector3Scale(F->f, 0.13f), TEX_CLOTH, shorts);
    Vector3 top = W(F, 0, 2.2f, 0.16f);   // hunched over, the way children stand when they count
    gfx_limb(W(F, 0, 1.4f, 0), top, 0.15f, 0.14f, TEX_CLOTH, jumper);
    gfx_box(W(F, 0.07f, 2.0f, 0.2f), (Vector3){ 0.04f, 0.025f, 0.004f }, TEX_PAPER, (Color){ 230, 226, 210, 255 }, 1.0f);   // a name tag
    gfx_limb(top, W(F, 0, 2.36f, 0.24f), 0.045f, 0.04f, TEX_SKIN, skin);
    Vector3 hc = W(F, 0, 2.5f, 0.28f), hr, hu, hf;
    Fig g = *f; g.look = found ? 1.0f : 0.0f;
    head_axes(&g, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.13f), Vector3Scale(hu, 0.15f), Vector3Scale(hf, 0.14f), TEX_SKIN, skin);   // a child's big round head
    gfx_ellipsoid(add3(hc, Vector3Scale(hu, 0.05f), Vector3Scale(hf, -0.02f)), Vector3Scale(hr, 0.14f), Vector3Scale(hu, 0.12f), Vector3Scale(hf, 0.14f), TEX_CLOTH, (Color){ 60, 40, 26, 255 });   // a pudding-basin haircut
    gfx_limb(add3(hc, Vector3Scale(hf, 0.12f), Vector3Add(Vector3Scale(hu, -0.07f), Vector3Scale(hr, -0.03f))), add3(hc, Vector3Scale(hf, 0.12f), Vector3Add(Vector3Scale(hu, -0.065f), Vector3Scale(hr, 0.03f))), 0.006f, 0.006f, TEX_CONCRETE, (Color){ 60, 20, 20, 255 });   // a little smile
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh = W(F, s * 0.2f, 2.16f, 0.16f);
        if (!found) {   // both hands flat over its eyes, and the fingers just far enough apart
            Vector3 el = W(F, s * 0.32f, 1.95f, 0.42f), hand = add3(hc, Vector3Scale(hf, 0.15f), Vector3Add(Vector3Scale(hr, s * 0.05f), Vector3Scale(hu, 0.03f)));
            gfx_limb(sh, el, 0.055f, 0.05f, TEX_CLOTH, jumper);
            gfx_limb(el, hand, 0.045f, 0.035f, TEX_SKIN, skin);
            for (int k = 0; k < 4; k++) gfx_limb(hand, add3(hand, Vector3Scale(hr, -s * 0.03f + k * 0.012f * s), Vector3Scale(hu, 0.03f + k * 0.022f)), 0.012f, 0.008f, TEX_SKIN, skin);
        } else {        // arms down to its shins, and its face
            Vector3 el = W(F, s * 0.28f, 1.45f, 0.18f), wr = W(F, s * 0.27f, 0.62f, 0.22f + sinf(f->t * 3 + s) * 0.04f);
            gfx_limb(sh, el, 0.055f, 0.05f, TEX_CLOTH, jumper);
            gfx_limb(el, wr, 0.04f, 0.032f, TEX_SKIN, skin);
            fingers(wr, Vector3Negate(F->u), F->r, 0.24f, 4, skin);
        }
    }
    if (found) for (int s = -1; s <= 1; s += 2) {
        Vector3 ec = add3(hc, Vector3Scale(hf, 0.12f), Vector3Add(Vector3Scale(hr, s * 0.05f), Vector3Scale(hu, 0.01f)));
        gfx_ellipsoid(ec, Vector3Scale(hr, 0.035f), Vector3Scale(hu, 0.04f), Vector3Scale(hf, 0.03f), TEX_CONCRETE, (Color){ 2, 1, 1, 255 });
        gfx_limb(Vector3Add(ec, Vector3Scale(hf, 0.02f)), add3(ec, Vector3Scale(hf, 0.025f), Vector3Scale(hu, -0.16f)), 0.012f, 0.004f, TEX_CONCRETE, (Color){ 8, 2, 2, 255 });   // and what runs out of them
    }
}

// you, as the glass has you: a boy in striped pyjamas with his eyes shut. sleepwalking
static void self_(const Fig *f, const Frame *F) {
    Color pj = tint_or(f, (Color){ 150, 166, 186, 255 }), stripe = scale_c(pj, 0.55f), skin = { 214, 200, 190, 255 };
    bool open = f->look > 0.5f;
    for (int s = -1; s <= 1; s += 2) {
        float lp = f->stride + (s < 0 ? 0 : 3.14159f), sw = sinf(lp) * 0.2f, lift = fmaxf(0, cosf(lp)) * 0.08f;
        Vector3 hip = W(F, s * 0.1f, 0.88f, 0), knee = W(F, s * 0.11f, 0.47f + lift, 0.05f + sw * 0.6f), ank = W(F, s * 0.1f, 0.07f + lift, sw * 0.3f);
        gfx_limb(hip, knee, 0.075f, 0.065f, TEX_CLOTH, pj);
        gfx_limb(knee, ank, 0.065f, 0.055f, TEX_CLOTH, pj);
        gfx_limb(ank, Vector3Add(ank, Vector3Add(Vector3Scale(F->f, 0.15f), Vector3Scale(F->u, -0.05f))), 0.035f, 0.025f, TEX_SKIN, skin);   // bare feet
    }
    gfx_limb(W(F, 0, 0.84f, 0), W(F, 0, 1.42f, 0.02f), 0.17f, 0.16f, TEX_CLOTH, pj);
    for (int k = -2; k <= 2; k++) gfx_limb(W(F, k * 0.06f, 0.86f, 0.15f + 0.02f * (2 - abs(k))), W(F, k * 0.06f, 1.4f, 0.16f + 0.02f * (2 - abs(k))), 0.012f, 0.012f, TEX_CLOTH, stripe);
    gfx_limb(W(F, -0.19f, 1.4f, 0.02f), W(F, 0.19f, 1.4f, 0.02f), 0.08f, 0.08f, TEX_CLOTH, pj);
    for (int s = -1; s <= 1; s += 2) {   // arms hanging, the way sleepwalkers hold them, a little forward
        Vector3 sh = W(F, s * 0.21f, 1.38f, 0.02f), el = W(F, s * 0.25f, 1.08f, 0.14f), wr = W(F, s * 0.23f, 0.82f, 0.3f);
        gfx_limb(sh, el, 0.06f, 0.055f, TEX_CLOTH, pj);
        gfx_limb(el, wr, 0.04f, 0.03f, TEX_SKIN, skin);
        fingers(wr, Vector3Normalize(Vector3Add(Vector3Negate(F->u), Vector3Scale(F->f, 0.6f))), F->r, 0.15f, 4, skin);
    }
    gfx_limb(W(F, 0, 1.44f, 0.02f), W(F, 0, 1.56f, 0.04f), 0.045f, 0.04f, TEX_SKIN, skin);
    Vector3 hc = W(F, 0, 1.68f, 0.05f), hr, hu, hf;
    head_axes(f, F, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.1f), Vector3Scale(hu, 0.125f), Vector3Scale(hf, 0.11f), TEX_SKIN, skin);
    gfx_ellipsoid(add3(hc, Vector3Scale(hu, 0.05f), Vector3Scale(hf, -0.02f)), Vector3Scale(hr, 0.105f), Vector3Scale(hu, 0.09f), Vector3Scale(hf, 0.11f), TEX_CLOTH, (Color){ 40, 30, 24, 255 });
    for (int s = -1; s <= 1; s += 2) {
        Vector3 ec = add3(hc, Vector3Scale(hf, 0.1f), Vector3Add(Vector3Scale(hr, s * 0.04f), Vector3Scale(hu, 0.015f)));
        if (!open) gfx_limb(Vector3Add(ec, Vector3Scale(hr, -0.018f)), Vector3Add(ec, Vector3Scale(hr, 0.018f)), 0.005f, 0.005f, TEX_CONCRETE, (Color){ 40, 24, 24, 255 });
        else {   // open: and there is nothing in them, only black, and a point of light a long way back
            gfx_ellipsoid(ec, Vector3Scale(hr, 0.02f), Vector3Scale(hu, 0.017f), Vector3Scale(hf, 0.012f), TEX_CONCRETE, (Color){ 3, 2, 2, 255 });
            gfx_set_emit(true);
            gfx_ellipsoid(Vector3Add(ec, Vector3Scale(hf, 0.011f)), Vector3Scale(hr, 0.004f), Vector3Scale(hu, 0.004f), Vector3Scale(hf, 0.003f), TEX_SKIN, (Color){ 240, 236, 226, 255 });
            gfx_set_emit(false);
        }
    }
    if (f->look > 0.8f) {   // the jaw lets go, much further than it should
        gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.095f), Vector3Scale(hu, -0.085f)), Vector3Scale(hr, 0.022f), Vector3Scale(hu, 0.055f), Vector3Scale(hf, 0.02f), TEX_CONCRETE, (Color){ 12, 4, 4, 255 });
        gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.07f), Vector3Scale(hu, -0.15f)), Vector3Scale(hr, 0.045f), Vector3Scale(hu, 0.03f), Vector3Scale(hf, 0.05f), TEX_SKIN, skin);
    } else gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.1f), Vector3Scale(hu, -0.06f)), Vector3Scale(hr, 0.03f), Vector3Scale(hu, 0.004f), Vector3Scale(hf, 0.012f), TEX_CONCRETE, (Color){ 30, 10, 10, 255 });
}

static void gardener(const Fig *f, const Frame *F) {
    Color pale = tint_or(f, (Color){ 196, 186, 190, 255 });
    float sway = sinf(f->t * 0.9f) * 0.05f;
    for (int s = -1; s <= 1; s += 2) {
        float sw = sinf(f->stride + (s < 0 ? 0 : 3.14159f)) * 0.25f, lift = fmaxf(0.0f, sinf(f->stride + (s < 0 ? 0 : 3.14159f))) * 0.12f;
        Vector3 hip = W(F, s * 0.12f, 1.95f, 0), knee = W(F, s * 0.14f, 1.0f + lift, 0.08f + sw), foot = W(F, s * 0.13f, 0.03f + lift, sw * 0.5f);
        gfx_limb(hip, knee, 0.06f, 0.045f, TEX_SKIN, pale);
        gfx_limb(knee, foot, 0.045f, 0.03f, TEX_SKIN, pale);
    }
    gfx_limb(W(F, 0, 1.95f, 0), W(F, sway, 2.85f, 0.05f), 0.1f, 0.13f, TEX_SKIN, pale);
    gfx_limb(W(F, -0.2f + sway, 2.82f, 0.05f), W(F, 0.2f + sway, 2.82f, 0.05f), 0.07f, 0.07f, TEX_SKIN, pale);
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh = W(F, s * 0.22f + sway, 2.8f, 0.05f), el = W(F, s * 0.28f, 2.0f, 0.1f), wr = W(F, s * 0.26f, 1.15f, 0.12f + sinf(f->t * 0.6f + s) * 0.05f);
        gfx_limb(sh, el, 0.04f, 0.03f, TEX_SKIN, pale);
        gfx_limb(el, wr, 0.03f, 0.025f, TEX_SKIN, pale);
        fingers(wr, Vector3Negate(F->u), F->r, 0.32f, 3, pale);
    }
    Vector3 hc = W(F, sway * 2, 3.4f, 0.1f), hr, hu, hf;
    gfx_limb(W(F, sway, 2.85f, 0.05f), hc, 0.04f, 0.035f, TEX_GRASS, (Color){ 90, 130, 110, 255 });
    head_axes(f, F, hc, &hr, &hu, &hf);
    Color petal = { 210, 120, 150, 255 };
    for (int k = 0; k < 6; k++) {
        float a = k * 1.0472f + sinf(f->t * 0.8f + k) * 0.06f;
        Vector3 d = Vector3Add(Vector3Scale(hr, cosf(a)), Vector3Scale(hu, sinf(a))), pp = Vector3CrossProduct(d, hf);
        gfx_ellipsoid(add3(hc, Vector3Scale(d, 0.32f), Vector3Scale(hf, -0.03f)), Vector3Scale(d, 0.3f), Vector3Scale(pp, 0.12f), Vector3Scale(hf, 0.025f), TEX_SKIN, petal);
    }
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.17f), Vector3Scale(hu, 0.17f), Vector3Scale(hf, 0.06f), TEX_SKIN, (Color){ 200, 160, 110, 255 });
    gfx_ellipsoid(Vector3Add(hc, Vector3Scale(hf, 0.05f)), Vector3Scale(hr, 0.11f), Vector3Scale(hu, 0.11f), Vector3Scale(hf, 0.06f), TEX_SKIN, (Color){ 255, 245, 235, 255 });
    gfx_ellipsoid(Vector3Add(hc, Vector3Scale(hf, 0.105f)), Vector3Scale(hr, 0.04f), Vector3Scale(hu, 0.055f), Vector3Scale(hf, 0.02f), TEX_CONCRETE, (Color){ 4, 2, 3, 255 });
}

static void priest(const Fig *f, const Frame *F) {
    Color robe = tint_or(f, (Color){ 44, 14, 16, 255 }), bone = { 200, 188, 160, 255 }, horn = { 70, 60, 52, 255 }, skin = { 120, 100, 96, 255 };
    gfx_limb(W(F, 0, 0.02f, 0), W(F, 0, 1.85f, 0), 0.5f, 0.22f, TEX_CLOTH, robe);
    gfx_limb(W(F, -0.3f, 1.84f, 0), W(F, 0.3f, 1.84f, 0), 0.13f, 0.13f, TEX_CLOTH, robe);
    gfx_limb(W(F, 0, 1.86f, 0), W(F, 0, 2.05f, 0.08f), 0.08f, 0.07f, TEX_CLOTH, robe);
    Vector3 hc = W(F, 0, 2.2f, 0.1f), hr, hu, hf;
    head_axes(f, F, hc, &hr, &hu, &hf);
    hf = Vector3Normalize(Vector3Add(hf, Vector3Scale(hu, -0.35f)));   // the snout points down at you
    gfx_ellipsoid(Vector3Add(hc, Vector3Scale(hf, 0.1f)), Vector3Scale(hr, 0.13f), Vector3Scale(hu, 0.15f), Vector3Scale(hf, 0.27f), TEX_CONCRETE, bone);
    for (int s = -1; s <= 1; s += 2) {
        gfx_ellipsoid(add3(hc, Vector3Scale(hf, 0.12f), Vector3Add(Vector3Scale(hr, s * 0.085f), Vector3Scale(hu, 0.05f))), Vector3Scale(hr, 0.04f), Vector3Scale(hu, 0.035f), Vector3Scale(hf, 0.04f), TEX_CONCRETE, (Color){ 4, 2, 2, 255 });
        Vector3 p0 = add3(hc, Vector3Scale(hu, 0.12f), Vector3Scale(hr, s * 0.08f));
        Vector3 p1 = add3(p0, Vector3Scale(hu, 0.2f), Vector3Add(Vector3Scale(hr, s * 0.12f), Vector3Scale(hf, -0.12f)));
        Vector3 p2 = add3(p1, Vector3Scale(hr, s * 0.22f), Vector3Add(Vector3Scale(hf, -0.15f), Vector3Scale(hu, 0.04f)));
        Vector3 p3 = add3(p2, Vector3Scale(hr, s * 0.08f), Vector3Add(Vector3Scale(hu, -0.17f), Vector3Scale(hf, 0.06f)));
        gfx_limb(p0, p1, 0.05f, 0.04f, TEX_WOOD, horn);
        gfx_limb(p1, p2, 0.04f, 0.022f, TEX_WOOD, horn);
        gfx_limb(p2, p3, 0.022f, 0.006f, TEX_WOOD, horn);
        Vector3 sh = W(F, s * 0.32f, 1.82f, 0), el = W(F, s * 0.58f, 2.1f, 0.15f), wr = W(F, s * 0.52f, 2.55f + sinf(f->t * 0.5f) * 0.05f, 0.22f);
        gfx_limb(sh, el, 0.12f, 0.17f, TEX_CLOTH, robe);
        gfx_limb(el, wr, 0.04f, 0.03f, TEX_SKIN, skin);
        fingers(wr, F->u, F->r, 0.26f, 3, skin);
    }
}

static void sleeper(const Fig *f, const Frame *F) {
    Color sheet = tint_or(f, (Color){ 196, 188, 176, 255 });
    float breathe = 1.0f + 0.04f * sinf(f->t * 1.3f);
    gfx_ellipsoid(W(F, 0, 0.1f, -0.1f), Vector3Scale(F->r, 0.3f), Vector3Scale(F->u, 0.14f * breathe), Vector3Scale(F->f, 0.55f), TEX_CLOTH, sheet);
    gfx_ellipsoid(W(F, 0, 0.08f, -0.85f), Vector3Scale(F->r, 0.24f), Vector3Scale(F->u, 0.1f), Vector3Scale(F->f, 0.35f), TEX_CLOTH, sheet);
    for (int s = -1; s <= 1; s += 2) gfx_ellipsoid(W(F, s * 0.1f, 0.14f, -1.2f), Vector3Scale(F->r, 0.06f), Vector3Scale(F->u, 0.09f), Vector3Scale(F->f, 0.05f), TEX_CLOTH, sheet);
    gfx_ellipsoid(W(F, 0, 0.12f, 0.6f), Vector3Scale(F->r, 0.13f), Vector3Scale(F->u, 0.12f), Vector3Scale(F->f, 0.14f), TEX_CLOTH, sheet);   // the head, covered
}

void figure_draw(const Fig *f) {
    if (f->scale > 0 && f->kind != FIG_TALL) {   // a giant: the whole body scaled, not just stretched
        Fig g = *f;
        g.pos = (Vector3){ 0, 0, 0 };
        g.lookAt = Vector3Scale(Vector3Subtract(f->lookAt, f->pos), 1.0f / f->scale);
        g.scale = 0;
        rlPushMatrix();
        rlTranslatef(f->pos.x, f->pos.y, f->pos.z);
        rlScalef(f->scale, f->scale, f->scale);
        figure_draw(&g);
        rlPopMatrix();
        return;
    }
    Frame F;
    F.o = f->pos;
    if (f->kind == FIG_CLIMBER) {
        F.u = Vector3Scale(Vector3Normalize(f->wallN), 0.45f);   // pressed flat to the wall (or the ceiling)
        F.f = fabsf(f->wallN.y) > 0.9f ? (Vector3){ sinf(f->yaw), 0, -cosf(f->yaw) } : (Vector3){ 0, 1, 0 };
        F.r = Vector3Normalize(Vector3CrossProduct(F.f, f->wallN));
    } else {
        F.f = (Vector3){ sinf(f->yaw), 0, -cosf(f->yaw) };
        F.u = (Vector3){ 0, 1, 0 };
        F.r = (Vector3){ cosf(f->yaw), 0, sinf(f->yaw) };
    }
    if (f->scale > 0) { F.f = Vector3Scale(F.f, f->scale); F.u = Vector3Scale(F.u, f->scale); F.r = Vector3Scale(F.r, f->scale); }
    switch (f->kind) {
    case FIG_PENITENT: penitent(f, &F, false); break;
    case FIG_KNEELER:  penitent(f, &F, true); break;
    case FIG_CRAWLER: case FIG_CLIMBER: crawler(f, &F); break;
    case FIG_GARDENER: gardener(f, &F); break;
    case FIG_PRIEST:   priest(f, &F); break;
    case FIG_SLEEPER:  sleeper(f, &F); break;
    case FIG_SEATED:   seated(f, &F); break;
    case FIG_COCOON:   cocoon(f, &F); break;
    case FIG_TALL:     tall(f, &F); break;
    case FIG_MOTHER:   mother(f, &F); break;
    case FIG_HAND:     hand(f, &F); break;
    case FIG_FACE:     face(f, &F); break;
    case FIG_SHEET:    sheet(f, &F); break;
    case FIG_PUPPET:   puppet(f, &F); break;
    case FIG_CAT:      cat(f, &F); break;
    case FIG_SEEKER:   seeker(f, &F); break;
    case FIG_SELF:     self_(f, &F); break;
    case FIG_COUNT:    break;
    }
}
