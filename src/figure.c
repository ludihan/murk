#include "figure.h"
#include "gfx.h"
#include <math.h>

// a local frame: origin, right, up, forward. up may be scaled to squash a body flat against a wall
typedef struct { Vector3 o, r, u, f; } Frame;
static Vector3 W(const Frame *F, float x, float y, float z) {
    return Vector3Add(F->o, Vector3Add(Vector3Scale(F->r, x), Vector3Add(Vector3Scale(F->u, y), Vector3Scale(F->f, z))));
}
static Vector3 add3(Vector3 a, Vector3 b, Vector3 c) { return Vector3Add(a, Vector3Add(b, c)); }
static Color tint_or(const Fig *f, Color def) { return f->tint.a ? f->tint : def; }

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

static void crawler(const Fig *f, const Frame *F) {
    Color pale = tint_or(f, (Color){ 168, 160, 156, 255 });
    float ph = f->stride;
    Vector3 hips = W(F, 0, 0.7f, -0.4f), mid = W(F, 0, 0.92f, -0.02f), sh = W(F, 0, 0.82f, 0.38f);
    gfx_limb(hips, mid, 0.12f, 0.13f, TEX_SKIN, pale);
    gfx_limb(mid, sh, 0.13f, 0.16f, TEX_SKIN, pale);
    for (int k = 0; k < 4; k++) {   // knobs of spine through the skin
        Vector3 p = Vector3Lerp(hips, sh, (k + 0.5f) / 4.0f);
        p = Vector3Add(p, Vector3Scale(F->u, 0.13f + (k == 1 || k == 2 ? 0.08f : 0.0f)));
        gfx_ellipsoid(p, Vector3Scale(F->r, 0.035f), Vector3Scale(F->u, 0.03f), Vector3Scale(F->f, 0.04f), TEX_SKIN, pale);
    }
    for (int s = -1; s <= 1; s += 2) {
        float lp = ph + (s < 0 ? 0.0f : 3.14159f), ap = lp + 3.14159f;
        float ll = fmaxf(0.0f, sinf(lp)) * 0.18f, lsw = cosf(lp) * 0.18f;
        float al = fmaxf(0.0f, sinf(ap)) * 0.22f, asw = cosf(ap) * 0.22f;
        Vector3 hip = W(F, s * 0.14f, 0.68f, -0.42f), knee = W(F, s * 0.3f, 0.32f + ll, -0.12f + lsw), foot = W(F, s * 0.24f, 0.03f + ll * 0.5f, -0.64f + lsw);
        gfx_limb(hip, knee, 0.07f, 0.05f, TEX_SKIN, pale);
        gfx_limb(knee, foot, 0.05f, 0.03f, TEX_SKIN, pale);
        // arms with one joint too many, folded like a spider's
        Vector3 s0 = W(F, s * 0.2f, 0.84f, 0.36f), el = W(F, s * 0.62f, 1.3f + al, 0.38f + asw * 0.5f), el2 = W(F, s * 0.6f, 0.62f + al * 0.6f, 0.86f + asw), hand = W(F, s * 0.36f, 0.03f + al * 0.4f, 1.15f + asw);
        gfx_limb(s0, el, 0.055f, 0.04f, TEX_SKIN, pale);
        gfx_limb(el, el2, 0.04f, 0.032f, TEX_SKIN, pale);
        gfx_limb(el2, hand, 0.032f, 0.022f, TEX_SKIN, pale);
        fingers(hand, Vector3Normalize(F->f), F->r, 0.2f, 4, pale);
    }
    // the head hangs under the shoulders, and now and then it jerks
    float twitch = fmodf(f->t + f->stride * 0.1f, 3.7f) < 0.18f ? sinf(f->t * 40.0f) * 0.5f : 0.0f;
    Vector3 nk = W(F, 0, 0.62f, 0.6f);
    gfx_limb(sh, nk, 0.06f, 0.05f, TEX_SKIN, pale);
    Vector3 hc = W(F, 0, 0.5f, 0.66f);
    Fig g = *f; g.tilt += twitch + 0.4f;
    Frame hfF = *F; hfF.u = Vector3Normalize(F->u);
    Vector3 hr, hu, hf;
    head_axes(&g, &hfF, hc, &hr, &hu, &hf);
    gfx_ellipsoid(hc, Vector3Scale(hr, 0.11f), Vector3Scale(hu, 0.15f), Vector3Scale(hf, 0.12f), TEX_SKIN, pale);
    // the jaw hangs open much further than a jaw should, and the teeth are all the same size
    float gape = 0.16f + 0.04f * sinf(f->t * 2.3f);
    Vector3 mc = add3(hc, Vector3Scale(hf, 0.09f), Vector3Scale(hu, -0.08f));
    gfx_ellipsoid(mc, Vector3Scale(hr, 0.075f), Vector3Scale(hu, gape), Vector3Scale(hf, 0.05f), TEX_CONCRETE, (Color){ 6, 2, 3, 255 });
    for (int k = 0; k < 5; k++) {
        float x = (k - 2) * 0.026f;
        Vector3 tp = add3(mc, Vector3Scale(hr, x), Vector3Add(Vector3Scale(hu, gape * 0.8f), Vector3Scale(hf, 0.04f)));
        gfx_limb(tp, Vector3Add(tp, Vector3Scale(hu, -0.04f)), 0.008f, 0.002f, TEX_SKIN, (Color){ 230, 220, 190, 255 });
        Vector3 bp = add3(mc, Vector3Scale(hr, x), Vector3Add(Vector3Scale(hu, -gape * 0.8f), Vector3Scale(hf, 0.04f)));
        gfx_limb(bp, Vector3Add(bp, Vector3Scale(hu, 0.04f)), 0.008f, 0.002f, TEX_SKIN, (Color){ 230, 220, 190, 255 });
    }
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
    case FIG_COUNT:    break;
    }
}
