#include "common.h"
#include "gfx.h"
#include "world.h"
#include "player.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
// browsers have no save file: use localStorage
EM_JS(int, web_load_int, (const char *key), {
    var v = localStorage.getItem(UTF8ToString(key));
    return v ? parseInt(v, 10) : 0;
});
EM_JS(void, web_save_int, (const char *key, int v), { localStorage.setItem(UTF8ToString(key), String(v)); });
#endif

typedef enum { S_TITLE, S_PLAY, S_ENDING } State;

static State state = S_TITLE;
static Level L;
static Player P;
static bool levelLoaded;
static int dreams;
static float madness, tension, flash, lampOn, nameT, stepDist, endT;
static float blackout, nextBlackout = 25, glitch;
static float freezeT, nextFreeze = 70, pullT, pullYaw, phantomT = 15, stepGap, titleT;
static int phantomLeft;
static float sens = 0.085f;   // degrees per mouse count; [ and ] change it
static float stareT, nextEvent = 30, eyesOpenT, eyeBoost;
static bool gardenCaught;

// eyes that hang in the fog at the edge of sight and are gone when you face them
typedef struct { Vector3 pos; float life, seenT; bool on; } Lurker;
static Lurker lurk[3];
static float lurkTimer = 8;
static char msg[160];
static float msgT;

static struct { bool on; float t; WorldId to; bool wake, loaded; } tr;

static const char *FX_NAME[FX_COUNT] = { "LAMP", "GLOVES", "BOOTS", "FEATHER" };
static const char *FX_DESC[FX_COUNT] = {
    "press F to toggle it. the dark pulls back a little.",
    "your grip drains slower. you climb faster.",
    "you can jump once more in the air.",
    "hold SPACE in the air. you fall like something that was never heavy.",
};

// the game keeps a tiny file about you
static void save_memory(void) {
#ifdef __EMSCRIPTEN__
    web_save_int("murk.launches", g_launches); web_save_int("murk.wakes", g_wakes); web_save_int("murk.sens", (int)(sens * 10000));
#else
    FILE *f = fopen("murk.sav", "w");
    if (f) { fprintf(f, "%d %d %d\n", g_launches, g_wakes, (int)(sens * 10000)); fclose(f); }
#endif
}
static void load_memory(void) {
#ifdef __EMSCRIPTEN__
    g_launches = web_load_int("murk.launches"); g_wakes = web_load_int("murk.wakes");
    { int sv = web_load_int("murk.sens"); if (sv >= 100 && sv <= 5000) sens = sv / 10000.0f; }
#else
    FILE *f = fopen("murk.sav", "r");
    if (f) {
        int sv = 0, n = fscanf(f, "%d %d %d", &g_launches, &g_wakes, &sv);
        if (n < 2) g_launches = g_wakes = 0;
        if (n == 3 && sv >= 100 && sv <= 5000) sens = sv / 10000.0f;
        fclose(f);
    }
#endif
    g_launches++;
    save_memory();
}

static void haunt_title(void) {
    static const char *T[] = { "MURK - don't turn around", "MURK - it can see your desktop", "MURK - it's in the room", "MURK - wake up", "MURK - not responding" };
    SetWindowTitle(T[GetRandomValue(0, 4)]);
    titleT = 3.0f;
}

static void say(const char *s, float secs) { snprintf(msg, sizeof msg, "%s", s); msgT = secs; }

static void load_world(WorldId id, bool wake) {
    if (levelLoaded) level_free(&L);
    level_build(&L, id, ++dreams);
    levelLoaded = true;
    player_spawn(&P, &L);
    if (id == W_HUB && wake) {
        P.pos = (Vector3){ -3.9f, 0.05f, -4.4f };
        P.yaw = 250;
    }
    nameT = 3.5f;
    blackout = 0; nextBlackout = 14 + GetRandomValue(0, 12); pullT = 0; phantomLeft = 0;
    memset(lurk, 0, sizeof lurk); lurkTimer = 6 + GetRandomValue(0, 6);
    lampOn = (P.fx & (1u << FX_LAMP)) ? lampOn : 0;
    if (id == W_HUB && !wake && dreams <= 1) say("WASD walk · SHIFT run · SPACE jump · hold LMB at rusty walls to grip · R wake up", 12);
    if (id == W_SHAFT) say("hold LMB on the rusty plates. W climbs, A/D shuffle, SPACE lunges. don't let go.", 9);
    if (id == W_DRAINS) say("don't let them see you stop. don't stop seeing them.", 7);
    if (id == W_VOID) say("there is nothing underneath.", 6);
}

static void go(WorldId to, bool wake) {
    if (tr.on) return;
    tr.on = true; tr.t = 0; tr.to = to; tr.wake = wake; tr.loaded = false;
    audio_play(wake ? SFX_WAKE : SFX_DOOR);
    if (wake) { flash = 1; g_wakes++; save_memory(); }
}

static void new_game(void) {
    memset(&P, 0, sizeof P);
    dreams = 0; madness = 0; tension = 0; lampOn = 0;
    load_world(W_HUB, false);
    state = S_PLAY;
    DisableCursor();
}

static float frand_(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }

static bool ray_clear(Vector3 a, Vector3 b) {
    b3RayResult r = b3World_CastRayClosest(L.phys, b3v(a), b3v(Vector3Subtract(b, a)), b3DefaultQueryFilter());
    return !r.hit || r.fraction > 0.97f;
}

static void hub_reposition(Watcher *w) {
    float a = frand_(0, 6.2831f);
    float r = L.id == W_GARDEN ? 30.0f : 6.5f;
    Vector3 p = { sinf(a) * r, 0, cosf(a) * r };
    if (L.id == W_GARDEN) { p.x = Clamp(p.x + P.pos.x, -46, 46); p.z = Clamp(p.z + P.pos.z, -46, 46); }
    w->pos = p;
}

static const char *WHISPERS[W_COUNT][4] = {
    { "did you hear that?", "someone is breathing in the room.", "the ceiling is lower than it was.", "you left the door open." },
    { "don't look down.", "it's getting closer to your heels.", "your hands are not yours.", "up. only up." },
    { "something is dragging.", "they know the way better than you.", "count the steps behind you.", "the walls are wet." },
    { "the eye has noticed you.", "there is no floor. there never was.", "you are falling very slowly.", "everything here is looking." },
    { "the flowers were people.", "someone planted you here.", "it is so quiet. why is it so quiet.", "don't pick anything." },
    { "stay.", "stay.", "stay.", "stay." },
};

// something happens every half minute or so, so no dream ever just sits there
static void director(float dt) {
    if (tr.on || L.id == W_END) return;
    nextEvent -= dt;
    if (nextEvent > 0) return;
    nextEvent = frand_(22, 45);
    int kind = GetRandomValue(0, 2);
    if (kind == 0 || (L.id != W_GARDEN && L.id != W_VOID)) {
        say(WHISPERS[L.id][GetRandomValue(0, 3)], 4.0f);
        audio_play_ex(SFX_KNOCK, 0.25f, frand_(0.5f, 0.8f)); glitch = 0.45f;
    } else if (L.id == W_GARDEN) {
        eyesOpenT = 5.0f; audio_play(SFX_STINGER); glitch = 0.6f;
        say("every flower opens its eyes.", 4.0f);
    } else {
        eyeBoost = 5.0f; audio_play(SFX_STINGER); glitch = 0.7f; madness = fminf(1.0f, madness + 0.3f);
        say("the eye opens all the way.", 4.0f);
    }
}

static void scare_update(float dt) {
    Vector3 eye = player_eye(&P), fwd = player_forward(&P);
    // ---- the game hitches: everything stops and the sound drops out, then it all lurches back
    if (dreams >= 2 && !tr.on) {
        nextFreeze -= dt;
        if (nextFreeze <= 0) { freezeT = frand_(0.5f, 1.1f); nextFreeze = frand_(50, 110); haunt_title(); }
    }
    // ---- phantom footsteps and knocks while you stand still in a haunted place
    if (dreams >= 2 && L.id != W_END && P.speedMeter < 0.5f && !tr.on) {
        phantomT -= dt;
        if (phantomT <= 0) {
            phantomT = frand_(9, 22);
            if (GetRandomValue(0, 2) == 0) audio_play_ex(SFX_KNOCK, 0.35f, frand_(0.6f, 0.9f));
            else { phantomLeft = GetRandomValue(3, 6); stepGap = 0; }
        }
    }
    if (phantomLeft > 0) {
        stepGap -= dt;
        if (stepGap <= 0) { audio_play_ex(SFX_STEP, 0.3f, 0.6f); stepGap = 0.5f; phantomLeft--; }
    }
    // ---- blackouts: the lights just stop. in the drains, things keep walking.
    bool creeping = L.id == W_HUB || L.creepers;
    bool haunted = (L.id == W_DRAINS) || (creeping && L.watchers.size > 0);
    if (blackout > 0) {
        blackout -= dt;
        if (blackout <= 0 && creeping) {
            for (int i = 0; i < L.watchers.size; i++) {
                Watcher *w = &L.watchers.data[i];
                Vector3 d = { P.pos.x - w->pos.x, 0, P.pos.z - w->pos.z };
                float len = Vector3Length(d);
                if (len > 2.6f) { float nl = fmaxf(2.4f, len * 0.55f); w->pos = (Vector3){ P.pos.x - d.x / len * nl, 0, P.pos.z - d.z / len * nl }; }
            }
            audio_play(SFX_STINGER); glitch = 0.7f; haunt_title();
            // and the game turns your head to look at it
            float bd = 1e9f; Vector3 bp = P.pos;
            for (int i = 0; i < L.watchers.size; i++) { float d = Vector3Distance(L.watchers.data[i].pos, P.pos); if (d < bd) { bd = d; bp = L.watchers.data[i].pos; } }
            if (bd < 1e8f) { pullYaw = atan2f(bp.x - P.pos.x, -(bp.z - P.pos.z)) * RAD2DEG; pullT = 0.7f; }
        }
    } else if (haunted && !tr.on) {
        nextBlackout -= dt;
        if (nextBlackout <= 0) {
            blackout = frand_(0.9f, 2.0f);
            nextBlackout = fmaxf(7.0f, 26.0f - dreams * 1.2f) * frand_(0.6f, 1.4f);
            audio_play(SFX_KNOCK); glitch = 0.6f;
        }
    }
    // ---- hub visitors and gardeners creep whenever you aren't looking
    if (creeping) {
        float nearest = 99;
        for (int i = 0; i < L.watchers.size; i++) {
            Watcher *w = &L.watchers.data[i];
            bool seen = blackout <= 0 && level_seen(&L, eye, fwd, w->pos);
            if (seen && !w->seen) { audio_play(SFX_STINGER); glitch = 0.8f; }
            w->seen = seen;
            w->phase += dt;
            Vector3 d = { P.pos.x - w->pos.x, 0, P.pos.z - w->pos.z };
            float len = Vector3Length(d);
            if (len < nearest) nearest = len;
            float sp = L.id == W_GARDEN ? 1.1f + 0.1f * (dreams > 8 ? 8 : dreams) : 0.55f;
            if (!seen && len > 0.01f) w->pos = Vector3Add(w->pos, Vector3Scale(d, (blackout > 0 ? sp * 3.3f : sp) * dt / len));
            if (len < 1.1f && L.id == W_GARDEN) { gardenCaught = true; say("it only wanted to hold you.", 4); }
            else if (len < 1.1f) {
                audio_play(SFX_SCREAM); glitch = 1; flash = 1;
                hub_reposition(w); blackout = 1.4f; madness = 1;
                say("it was behind you the whole time.", 4);
            }
        }
        L.nearest = nearest;
    }
    director(dt);
    // ---- lurkers
    if (L.id != W_END) {
        for (int i = 0; i < 3; i++) {
            Lurker *k = &lurk[i];
            if (!k->on) continue;
            k->life -= dt;
            float dist = Vector3Distance(k->pos, eye);
            bool vis = Vector3DotProduct(Vector3Normalize(Vector3Subtract(k->pos, eye)), fwd) > 0.93f && ray_clear(eye, k->pos);
            k->seenT = vis ? k->seenT + dt : fmaxf(0, k->seenT - dt);
            if (k->seenT > 0.6f || k->life <= 0 || dist < 3.5f) k->on = false;
        }
        lurkTimer -= dt;
        if (lurkTimer <= 0 && !tr.on) {
            lurkTimer = frand_(5, 12) / (1.0f + 0.15f * (dreams > 10 ? 10 : dreams));
            for (int i = 0; i < 3; i++) if (!lurk[i].on) {
                for (int tries = 0; tries < 8; tries++) {
                    float a = (P.yaw + (GetRandomValue(0, 1) ? 1 : -1) * frand_(35, 120)) * DEG2RAD;
                    float d = (L.id == W_HUB) ? frand_(5, 9) : frand_(9, 18);
                    Vector3 p = { eye.x + sinf(a) * d, frand_(1.6f, 2.5f), eye.z - cosf(a) * d };
                    if (L.id == W_VOID) p.y = eye.y + frand_(-1, 2);
                    if (!ray_clear(eye, p)) continue;
                    lurk[i] = (Lurker){ p, frand_(5, 9), 0, true };
                    break;
                }
                break;
            }
        }
    }
}

static float flicker(float t, float amount) {
    float s = sinf(t * 37.0f) * sinf(t * 11.3f + 1.7f) * sinf(t * 5.1f);
    float v = 1.0f - amount * 0.12f * sinf(t * 53.0f);
    if (s > 0.80f) v *= 1.0f - amount * 0.75f;
    return v;
}

// ---------------------------------------------------------------- drawing
static float dist_to_box(Vector3 p, const Box *b) {
    float dx = fmaxf(fabsf(p.x - b->c.x) - b->h.x, 0), dy = fmaxf(fabsf(p.y - b->c.y) - b->h.y, 0), dz = fmaxf(fabsf(p.z - b->c.z) - b->h.z, 0);
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static Color fx_color(EffectId f) {
    return f == FX_LAMP ? (Color){ 255, 225, 120, 255 } : f == FX_GLOVES ? (Color){ 255, 140, 60, 255 } : f == FX_BOOTS ? (Color){ 190, 140, 255, 255 } : (Color){ 255, 150, 220, 255 };
}

static Color scale_col(Color c, float k) {
    return (Color){ (unsigned char)fminf(255, c.r * k), (unsigned char)fminf(255, c.g * k), (unsigned char)fminf(255, c.b * k), 255 };
}

static void draw_scene(Camera3D cam, float time) {
    float dens = L.fogDensity * (lampOn > 0.5f ? 0.55f : 1.0f);
    float light = L.light * flicker(time, L.id == W_HUB ? 1.0f + 0.12f * (dreams > 8 ? 8 : dreams) : 0.25f) * (lampOn > 0.5f ? 1.2f : 1.0f);
    if (blackout > 0) { light *= 0.04f; dens *= 1.8f; }
    gfx_begin_scene(cam, L.fog, dens, light, time);
    gfx_sky(&L.sky, time, P.pos);
    float cull = 3.2f / dens + 6;
    Vector3 eye = cam.position;
    for (int i = 0; i < (int)L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (b->flags & F_EMIT) continue;
        if (dist_to_box(eye, b) > cull) continue;
        if (b->flags & F_DECAL) gfx_decal(b->c, b->h, (int)b->tex, b->scale);
        else gfx_box(b->c, b->h, b->tex, b->tint, b->scale);
    }
    for (int i = 0; i < L.props.size; i++) {
        const Prop *pr = &L.props.data[i];
        b3Vec3 pos = b3Body_GetPosition(pr->body);
        b3Quat q = b3Body_GetRotation(pr->body);
        gfx_box_rot(rv(pos), pr->h, (Quaternion){ q.v.x, q.v.y, q.v.z, q.s }, pr->tex, pr->tint, 1.0f);
    }
    for (int i = 0; i < L.watchers.size; i++) {
        const Watcher *w = &L.watchers.data[i];
        if (L.creepers) {   // a gardener: pale, too tall, and its head is a flower
            float sway = sinf(w->phase * 1.1f) * 0.06f;
            gfx_box((Vector3){ w->pos.x + sway, 1.5f, w->pos.z }, (Vector3){ 0.13f, 1.5f, 0.1f }, TEX_FLESH, (Color){ 215, 200, 210, 255 }, 1.0f);
            gfx_box((Vector3){ w->pos.x - 0.25f + sway, 1.8f, w->pos.z }, (Vector3){ 0.04f, 1.1f, 0.04f }, TEX_FLESH, (Color){ 200, 185, 195, 255 }, 1.0f);
            gfx_box((Vector3){ w->pos.x + 0.25f + sway, 1.8f, w->pos.z }, (Vector3){ 0.04f, 1.1f, 0.04f }, TEX_FLESH, (Color){ 200, 185, 195, 255 }, 1.0f);
            continue;
        }
        Vector3 c = { w->pos.x + sinf(w->phase * 1.3f) * 0.04f, 1.45f, w->pos.z };
        gfx_box(c, (Vector3){ 0.2f, 1.45f, 0.2f }, TEX_CONCRETE, (Color){ 14, 14, 16, 255 }, 1.0f);
        gfx_box((Vector3){ c.x, 3.05f, c.z }, (Vector3){ 0.16f, 0.2f, 0.16f }, TEX_CONCRETE, (Color){ 20, 20, 22, 255 }, 1.0f);
    }
    if (L.sludge) gfx_slab(L.sludgeY, 6.0f, TEX_SLUDGE, (Color){ 140, 160, 90, 255 }, time * 12.0f);
    if (L.water) gfx_slab(L.waterY, L.waterHalf, TEX_WATER, (Color){ 150, 190, 255, 255 }, time * 3.0f);

    gfx_set_emit(true);
    for (int i = 0; i < L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (!(b->flags & F_EMIT)) continue;
        float k = ((L.id == W_HUB || L.id == W_DRAINS) && b->h.y < 0.2f) ? flicker(time, 1.0f) : 1.0f;
        gfx_glow(b->c, b->h, scale_col(b->tint, k));
    }
    for (int i = 0; i < L.pickups.size; i++) {
        const Pickup *pk = &L.pickups.data[i];
        if (pk->taken) continue;
        Vector3 c = { pk->pos.x, pk->pos.y + sinf(time * 2.0f) * 0.12f, pk->pos.z };
        Quaternion q = QuaternionMultiply(QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, time * 1.4f), QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, 0.6f));
        gfx_box_rot(c, (Vector3){ 0.22f, 0.22f, 0.22f }, q, TEX_CONCRETE, fx_color(pk->fx), 100.0f);
    }
    for (int i = 0; i < L.watchers.size; i++) {
        const Watcher *w = &L.watchers.data[i];
        Vector3 to = { P.pos.x - w->pos.x, 0, P.pos.z - w->pos.z };
        to = Vector3Normalize(to);
        Vector3 perp = { -to.z, 0, to.x };
        Vector3 e = { w->pos.x + to.x * 0.22f, 2.5f, w->pos.z + to.z * 0.22f };
        gfx_glow(Vector3Add(e, Vector3Scale(perp, 0.07f)), (Vector3){ 0.03f, 0.05f, 0.03f }, (Color){ 235, 230, 200, 255 });
        gfx_glow(Vector3Subtract(e, Vector3Scale(perp, 0.07f)), (Vector3){ 0.03f, 0.05f, 0.03f }, (Color){ 235, 230, 200, 255 });
    }
    for (int i = 0; i < 3; i++) {
        const Lurker *k = &lurk[i];
        if (!k->on || sinf(time * 2.7f + i * 5.0f) > 0.96f) continue;     // they blink
        Vector3 to = Vector3Normalize((Vector3){ cam.position.x - k->pos.x, 0, cam.position.z - k->pos.z });
        Vector3 perp = { -to.z, 0, to.x };
        Color c = (i % 2) ? (Color){ 230, 40, 30, 255 } : (Color){ 235, 235, 205, 255 };
        gfx_glow(Vector3Add(k->pos, Vector3Scale(perp, 0.11f)), (Vector3){ 0.035f, 0.02f, 0.035f }, c);
        gfx_glow(Vector3Subtract(k->pos, Vector3Scale(perp, 0.11f)), (Vector3){ 0.035f, 0.02f, 0.035f }, c);
    }
    for (int i = 0; i < L.motes.size; i++) {
        const Mote *m = &L.motes.data[i];
        float a = sinf(fminf(m->life, 1.0f) * 3.14159f);
        float sz = L.moteGlow ? 0.03f : 0.012f;
        if (!L.moteGlow) gfx_glow(m->pos, (Vector3){ sz, sz, sz }, scale_col(L.moteCol, a));
    }
    // gardeners' heads and the flowers that watch
    for (int i = 0; i < L.watchers.size && L.creepers; i++) {
        const Watcher *w = &L.watchers.data[i];
        Vector3 hc = { w->pos.x, 3.35f, w->pos.z };
        Vector3 to = Vector3Normalize((Vector3){ P.pos.x - w->pos.x, 0, P.pos.z - w->pos.z });
        float pulse = 0.9f + 0.1f * sinf(time * 3 + i);
        gfx_glow(hc, (Vector3){ 0.55f * pulse, 0.03f, 0.16f }, (Color){ 255, 120, 190, 255 });
        gfx_glow(hc, (Vector3){ 0.16f, 0.03f, 0.55f * pulse }, (Color){ 255, 120, 190, 255 });
        gfx_glow(hc, (Vector3){ 0.3f, 0.035f, 0.3f }, (Color){ 255, 220, 150, 255 });
        Vector3 ec = Vector3Add(hc, (Vector3){ to.x * 0.22f, 0.02f, to.z * 0.22f });
        gfx_glow(ec, (Vector3){ 0.13f, 0.13f, 0.13f }, (Color){ 240, 236, 220, 255 });
        gfx_glow(Vector3Add(ec, Vector3Scale(to, 0.1f)), (Vector3){ 0.05f, 0.07f, 0.05f }, (Color){ 4, 2, 4, 255 });
    }
    for (int i = 0; i < L.blooms.size; i++) {
        const Bloom *b = &L.blooms.data[i];
        float d = Vector3Distance(b->pos, eye);
        if (d > 55) continue;
        if (b->yaw > 0.5f) { gfx_glow(b->pos, (Vector3){ b->size, b->size, b->size }, b->col); continue; }
        float s = b->size, br = 0.85f + 0.15f * sinf(time * 1.5f + i);
        Color pc = scale_col(b->col, br);
        gfx_glow(b->pos, (Vector3){ s, 0.025f, s * 0.38f }, pc);
        gfx_glow(b->pos, (Vector3){ s * 0.38f, 0.025f, s }, pc);
        gfx_glow(b->pos, (Vector3){ s * 0.27f, 0.04f, s * 0.27f }, (Color){ 255, 230, 160, 255 });
        bool open = eyesOpenT > 0 || (d < 22 && sinf(time * 0.9f + i * 3.1f) > -0.85f);
        if (open) {   // an eye on the flower, turned toward you
            Vector3 to = Vector3Normalize((Vector3){ cam.position.x - b->pos.x, 0.25f, cam.position.z - b->pos.z });
            Vector3 ec = Vector3Add(b->pos, Vector3Scale(to, s * 0.3f));
            gfx_glow(ec, (Vector3){ s * 0.2f, s * 0.2f, s * 0.2f }, (Color){ 240, 236, 220, 255 });
            gfx_glow(Vector3Add(ec, Vector3Scale(to, s * 0.14f)), (Vector3){ s * 0.09f, s * 0.12f, s * 0.09f }, (Color){ 4, 2, 4, 255 });
        }
    }
    // soft glow around everything bright
    gfx_begin_glow();
    for (int i = 0; i < L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (!(b->flags & F_EMIT)) continue;
        float mx = fmaxf(b->h.x, fmaxf(b->h.y, b->h.z));
        if (mx > 1.6f || b->h.y > 3.0f) continue;
        if (dist_to_box(eye, b) > cull) continue;
        float k = ((L.id == W_HUB || L.id == W_DRAINS) && b->h.y < 0.2f) ? flicker(time, 1.0f) : 1.0f;
        Vector3 ho = Vector3Add(b->c, Vector3Scale(Vector3Normalize(Vector3Subtract(eye, b->c)), 0.45f));   // pull it off the wall it hangs on
        gfx_halo(ho, 0.9f + mx * 2.6f, b->tint, 0.55f * k);
    }
    for (int i = 0; i < L.pickups.size; i++) {
        const Pickup *pk = &L.pickups.data[i];
        if (!pk->taken) gfx_halo(pk->pos, 2.6f + sinf(time * 3) * 0.3f, fx_color(pk->fx), 0.9f);
    }
    for (int i = 0; i < L.blooms.size; i++) {
        const Bloom *b = &L.blooms.data[i];
        if (Vector3Distance(b->pos, eye) > 45) continue;
        gfx_halo(b->pos, b->yaw > 0.5f ? 1.0f : b->size * 3.5f, b->col, b->yaw > 0.5f ? 0.55f : 0.4f);
    }
    for (int i = 0; i < L.watchers.size; i++) {
        const Watcher *w = &L.watchers.data[i];
        if (L.creepers) gfx_halo((Vector3){ w->pos.x, 3.35f, w->pos.z }, 2.4f, (Color){ 255, 90, 160, 255 }, 0.6f);
        else gfx_halo((Vector3){ w->pos.x, 2.5f, w->pos.z }, 0.6f, (Color){ 255, 230, 200, 255 }, 0.4f);
    }
    for (int i = 0; i < 3; i++) {
        const Lurker *k = &lurk[i];
        if (!k->on) continue;
        gfx_halo(k->pos, 0.8f, (i % 2) ? (Color){ 255, 50, 30, 255 } : (Color){ 235, 235, 205, 255 }, 0.5f);
    }
    if (L.moteGlow) for (int i = 0; i < L.motes.size; i++) {
        const Mote *m = &L.motes.data[i];
        gfx_halo(m->pos, 0.22f, L.moteCol, 0.5f * sinf(fminf(m->life, 1.0f) * 3.14159f));
    }
    gfx_end_glow();
    gfx_set_emit(false);
    gfx_end_scene();
}

// ---------------------------------------------------------------- title: a lit door at the end of the orchard
static void draw_title(float time) {
    static const Sky sky = { true, { 8, 4, 38, 255 }, { 70, 30, 84, 255 }, { 70, 30, 84, 255 }, 1.0f, true, { 255, 236, 230, 255 }, { -0.4f, 0.36f, -0.84f }, { 70, 255, 200, 255 }, 1.0f, false, 0 };
    Camera3D cam = { 0 };
    float yaw = sinf(time * 0.09f) * 0.28f;
    cam.position = (Vector3){ 0, 1.6f, 0 };
    cam.target = (Vector3){ sinf(yaw) * 10, 2.1f + sinf(time * 0.2f) * 0.15f, -cosf(yaw) * 10 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 70;
    cam.projection = CAMERA_PERSPECTIVE;
    gfx_begin_scene(cam, sky.horizon, 0.024f, 1.2f, time);
    gfx_sky(&sky, time, cam.position);
    gfx_box((Vector3){ 0, -0.5f, 0 }, (Vector3){ 90, 0.5f, 90 }, TEX_GRASS, (Color){ 140, 170, 160, 255 }, 3.0f);
    gfx_box((Vector3){ 0, 2.2f, -26 }, (Vector3){ 1.8f, 2.2f, 0.25f }, TEX_CONCRETE, (Color){ 36, 32, 40, 255 }, 1.0f);
    for (int i = 0; i < 6; i++) {
        float z = -4 - i * 3.6f;
        gfx_box((Vector3){ -3.2f, 0.9f, z }, (Vector3){ 0.1f, 0.9f, 0.1f }, TEX_RUST, (Color){ 70, 60, 70, 255 }, 1.0f);
        gfx_box((Vector3){ 3.2f, 0.9f, z - 1.8f }, (Vector3){ 0.1f, 0.9f, 0.1f }, TEX_RUST, (Color){ 70, 60, 70, 255 }, 1.0f);
    }
    static const Color PET[4] = { { 255, 100, 170, 255 }, { 120, 235, 255, 255 }, { 255, 220, 110, 255 }, { 200, 150, 255, 255 } };
    for (int i = 0; i < 70; i++) {
        float a = (i * 2.399f), r = 5 + (i * 37 % 41);
        float x = sinf(a) * r * 0.9f, z = -cosf(a) * r - 2.0f;
        if (fabsf(x) < 1.6f) continue;
        float h = 0.8f + (i * 13 % 25) / 10.0f;
        gfx_box((Vector3){ x, h / 2, z }, (Vector3){ 0.05f, h / 2, 0.05f }, TEX_GRASS, (Color){ 90, 150, 130, 255 }, 1.0f);
    }
    gfx_set_emit(true);
    gfx_glow((Vector3){ 0, 1.9f, -25.7f }, (Vector3){ 1.3f, 1.8f, 0.05f }, (Color){ 255, 238, 215, 255 });
    for (int i = 0; i < 6; i++) {
        gfx_glow((Vector3){ -3.2f, 1.95f, -4 - i * 3.6f }, (Vector3){ 0.14f, 0.14f, 0.14f }, (Color){ 255, 190, 120, 255 });
        gfx_glow((Vector3){ 3.2f, 1.95f, -5.8f - i * 3.6f }, (Vector3){ 0.14f, 0.14f, 0.14f }, (Color){ 255, 190, 120, 255 });
    }
    for (int i = 0; i < 70; i++) {
        float a = (i * 2.399f), r = 5 + (i * 37 % 41);
        float x = sinf(a) * r * 0.9f, z = -cosf(a) * r - 2.0f;
        if (fabsf(x) < 1.6f) continue;
        float h = 0.8f + (i * 13 % 25) / 10.0f, sz = 0.3f + (i % 5) * 0.07f;
        Color c = scale_col(PET[i % 4], 0.85f + 0.15f * sinf(time * 1.5f + i));
        gfx_glow((Vector3){ x, h + 0.1f, z }, (Vector3){ sz, 0.025f, sz * 0.38f }, c);
        gfx_glow((Vector3){ x, h + 0.1f, z }, (Vector3){ sz * 0.38f, 0.025f, sz }, c);
        gfx_glow((Vector3){ x, h + 0.1f, z }, (Vector3){ sz * 0.25f, 0.04f, sz * 0.25f }, (Color){ 255, 230, 160, 255 });
    }
    gfx_begin_glow();
    gfx_halo((Vector3){ 0, 1.9f, -25.0f }, 9.0f, (Color){ 255, 220, 190, 255 }, 0.8f);
    for (int i = 0; i < 6; i++) {
        gfx_halo((Vector3){ -3.2f, 1.95f, -4 - i * 3.6f }, 1.4f, (Color){ 255, 190, 120, 255 }, 0.7f);
        gfx_halo((Vector3){ 3.2f, 1.95f, -5.8f - i * 3.6f }, 1.4f, (Color){ 255, 190, 120, 255 }, 0.7f);
    }
    for (int i = 0; i < 70; i++) {
        float a = (i * 2.399f), r = 5 + (i * 37 % 41);
        float x = sinf(a) * r * 0.9f, z = -cosf(a) * r - 2.0f;
        if (fabsf(x) < 1.6f) continue;
        float h = 0.8f + (i * 13 % 25) / 10.0f;
        gfx_halo((Vector3){ x, h + 0.1f, z }, 1.1f, PET[i % 4], 0.4f);
    }
    for (int i = 0; i < 50; i++) {   // fireflies
        float t = time * 0.2f + i * 1.7f;
        gfx_halo((Vector3){ sinf(t * 1.3f + i) * 9, 0.8f + fmodf(t * 0.5f + i, 4.0f), -4 - fmodf(i * 3.7f, 20) + cosf(t) * 2 }, 0.3f, (Color){ 255, 170, 230, 255 }, 0.6f);
    }
    gfx_end_glow();
    gfx_set_emit(false);
    gfx_end_scene();
}

static void text(const char *s, int x, int y, int size, Color c) {
    DrawText(s, x + 1, y + 1, size, BLACK);
    DrawText(s, x, y, size, c);
}
static void text_c(const char *s, int y, int size, Color c) {
    int w = MeasureText(s, size);
    text(s, (RT_W - w) / 2, y, size, c);
}

static void draw_hud(float time) {
    // crosshair: orange when you can grab, hot when you're holding on
    Color ch = P.gripping ? (Color){ 255, 200, 90, 255 } : P.canGrip ? (Color){ 230, 120, 40, 255 } : (Color){ 160, 160, 150, 150 };
    int cx = RT_W / 2, cy = RT_H / 2;
    DrawRectangle(cx - 1, cy - 1, 2, 2, ch);
    if (P.canGrip || P.gripping) { DrawRectangle(cx - 5, cy, 3, 1, ch); DrawRectangle(cx + 3, cy, 3, 1, ch); }

    // grip meter
    int bw = 70, bx = 8, by = RT_H - 14;
    bool low = P.grip < 0.25f;
    Color fill = low ? ((int)(time * 8) % 2 ? (Color){ 220, 40, 30, 255 } : (Color){ 120, 20, 20, 255 }) : (Color){ 200, 170, 90, 255 };
    DrawRectangle(bx - 1, by - 1, bw + 2, 7, (Color){ 0, 0, 0, 200 });
    DrawRectangle(bx, by, (int)(bw * P.grip), 5, fill);
    text("GRIP", bx, by - 11, 10, (Color){ 190, 180, 160, 255 });

    // collected effects
    int x = 8;
    for (int f = 0; f < FX_COUNT; f++) {
        bool have = P.fx & (1u << f);
        if (!have) continue;
        Color c = fx_color((EffectId)f);
        if (f == FX_LAMP && lampOn < 0.5f) c = scale_col(c, 0.4f);
        text(FX_NAME[f], x, 8, 10, c);
        x += MeasureText(FX_NAME[f], 10) + 8;
    }

    if (nameT > 0 && L.name) {
        unsigned char a = (unsigned char)(fminf(1.0f, nameT) * 255);
        text_c(L.name, RT_H / 2 - 70, 20, (Color){ 220, 210, 190, a });
    }
    if (msgT > 0) text_c(msg, RT_H - 34, 10, (Color){ 210, 200, 180, (unsigned char)(fminf(1.0f, msgT) * 255) });
    if (L.sludge && L.sludgeArmed) {
        float gap = P.pos.y - L.sludgeY;
        if (gap < 10) text_c("IT IS RISING", 22, 10, (Color){ 200, 60, 40, (unsigned char)(120 + 100 * sinf(time * 6)) });
    }
}

static const char *END_LINES[] = {
    "you lie down.",
    "the ceiling is just a ceiling.",
    "the bulb is just a bulb.",
    "somewhere under the floor, something lets go.",
    "",
    "you wake up.",
    "",
    "(or you don't.)",
};

// ---------------------------------------------------------------- dev bot (MURK_BOT=climb|walk|jump)
static const char *bot;
static void bot_input(Input *in, const Player *p, const Level *l, float t) {
    static float lastLog = -1;
    memset(in, 0, sizeof *in);
    if (!strcmp(bot, "climb")) { in->grip = !p->grounded || p->pos.y < 1; if (p->pos.y < 3.3f && p->gripping) in->mz = 1; else if (p->gripping) in->mx = 1; else if (p->grounded && p->pos.y > 4) in->mz = 0; else if (p->grounded) in->mz = 1; }
    else if (!strcmp(bot, "walk")) { in->mz = 1; in->sprint = t > 3; }
    else if (!strcmp(bot, "jump")) { in->mz = 1; in->sprint = true; }
    if (t - lastLog > 0.5f) {
        lastLog = t;
        printf("t=%5.1f pos=(%6.2f %6.2f %6.2f) vel=(%5.2f %5.2f %5.2f) ground=%d grip=%d canGrip=%d stam=%.2f sludge=%.1f\n", t, p->pos.x, p->pos.y, p->pos.z,
               p->vel.x, p->vel.y, p->vel.z, p->grounded, p->gripping, p->canGrip, p->grip, l->sludgeY);
        fflush(stdout);
    }
}

// ---------------------------------------------------------------- main
static int shotWorld = -1, shotFrames, shotFrame;
static char shotPath[256] = "";
static bool quit;
static double prevT, acc, clockT;

static void frame(void) {
    double prev = prevT;
    double clock = clockT;
        double now = GetTime();
        float frameDt = (float)fminf(0.1f, now - prev);
        prevT = now;
        clock += frameDt;
        clockT = clock;
        float time = (float)clock;
        bool frozen = false;
        if (state == S_PLAY && freezeT > 0) {
            freezeT -= frameDt; frozen = true; frameDt = 0; glitch = 0.9f;
            audio_set(1, 0, 0, 0.0f);
            if (freezeT <= 0) { audio_play(SFX_KNOCK); glitch = 1; }
        }
        if (titleT > 0) { titleT -= frameDt; if (titleT <= 0) SetWindowTitle("MURK"); }

        if (state != S_ENDING && (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_EQUAL))) {
            bool up = IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressed(KEY_EQUAL);
            sens = Clamp(sens * (up ? 1.2f : 1.0f / 1.2f), 0.01f, 0.5f);
            save_memory();
            char b[64]; snprintf(b, sizeof b, "mouse sensitivity %.0f%%  ( [ lower   ] higher )", sens / 0.085f * 100.0f);
            say(b, 2.5f);
        }
        if (state == S_TITLE) {
            audio_set(0.8f, 0.0f, 0.0f, 0.6f); audio_music(0.55f, 0.0f);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) new_game();
        } else if (state == S_PLAY) {
            const float DT = 1.0f / 120.0f;
            if (!tr.on || tr.t > 0.6f) {
                Vector2 md = GetMouseDelta();
                if (shotWorld < 0) { P.yaw += md.x * sens; P.pitch -= md.y * sens; }
                if (P.pitch > 88) P.pitch = 88;
                if (P.pitch < -88) P.pitch = -88;
            }
#ifdef __EMSCRIPTEN__
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) DisableCursor();   // browsers drop pointer lock on Esc; click to take it back
#endif
            if (pullT > 0) {   // something takes the camera for a moment
                pullT -= frameDt;
                float diff = fmodf(pullYaw - P.yaw + 540.0f, 360.0f) - 180.0f;
                P.yaw += diff * fminf(1.0f, frameDt * 7.0f);
                P.pitch *= 1.0f - fminf(1.0f, frameDt * 5.0f);
            }
            if (IsKeyPressed(KEY_SPACE)) P.jumpBuf = 0.12f;
            if (IsKeyPressed(KEY_F) && (P.fx & (1u << FX_LAMP))) lampOn = lampOn > 0.5f ? 0.0f : 1.0f;
            if (IsKeyPressed(KEY_R) && L.id != W_HUB) go(W_HUB, true);
            bool wasGrip = P.gripping;

            acc += frameDt;
            while (acc >= DT) {
                acc -= DT;
                if (!tr.on) {
                    Input in = input_read();
                    if (bot) bot_input(&in, &P, &L, (float)clock);
                    player_update(&P, &L, &in, DT);
                }
                level_step(&L, DT, P.pos);
            }
            if (P.gripping && !wasGrip) audio_play(SFX_GRAB);
            if (P.jumped) { audio_play(SFX_JUMP); P.jumped = false; }
            if (P.grounded) {
                stepDist += Vector3Length((Vector3){ P.vel.x, 0, P.vel.z }) * frameDt;
                if (stepDist > 2.0f) { stepDist = 0; audio_play(SFX_STEP); }
            }

            if (!tr.on) {
                // pickups
                for (int i = 0; i < L.pickups.size; i++) {
                    Pickup *pk = &L.pickups.data[i];
                    if (!pk->taken && Vector3Distance(Vector3Add(P.pos, (Vector3){ 0, 1, 0 }), pk->pos) < 1.3f) {
                        pk->taken = true;
                        P.fx |= 1u << pk->fx;
                        flash = 0.0f;
                        char b[160]; snprintf(b, sizeof b, "EFFECT: %s - %s  (R to wake up)", FX_NAME[pk->fx], FX_DESC[pk->fx]);
                        say(b, 10);
                        audio_play(SFX_PICKUP);
                        if (pk->fx == FX_LAMP) lampOn = 1;
                        flash = 0.0f; glitch = 0.3f;
                    }
                }
                // doors
                for (int i = 0; i < L.portals.size; i++) {
                    const Portal *pt = &L.portals.data[i];
                    Vector3 d = Vector3Subtract(P.pos, pt->pos); d.y = 0;
                    if (Vector3Length(d) < pt->radius) {
                        if (pt->needs && (P.fx & (unsigned)pt->needs) != (unsigned)pt->needs) {
                            if (msgT < 0.5f) say("the door won't open. it wants something from every dream.", 3);
                        } else go(pt->to, false);
                    }
                }
                if (L.id == W_END) {
                    Vector3 d = Vector3Subtract(P.pos, L.bed); d.y = 0;
                    if (Vector3Length(d) < 1.4f) { state = S_ENDING; endT = 0; EnableCursor(); audio_play(SFX_WAKE); }
                }
                // hazards
                bool dead = false;
                if (P.pos.y < L.killY) { dead = true; say("you fall for a long time.", 4); }
                if (L.sludge && P.pos.y + 0.6f < L.sludgeY) { dead = true; say("it takes you.", 4); }
                Vector3 fwd = player_forward(&P);
                if (level_watchers(&L, player_eye(&P), fwd, P.pos, frameDt, blackout > 0)) {
                    dead = true; say("it was standing right there.", 4);
                    audio_play(SFX_SCREAM); glitch = 1; haunt_title();
                }
                if (L.sawWatcher) { L.sawWatcher = false; audio_play(SFX_STINGER); glitch = 0.8f; madness = fminf(1, madness + 0.3f); }
                if (P.slipped) { P.slipped = false; audio_play(SFX_KNOCK); glitch = 0.5f; }
                scare_update(frameDt);
                if (gardenCaught) { gardenCaught = false; dead = true; audio_play(SFX_SCREAM); glitch = 1; }
                if (dead) go(W_HUB, true);
            }

            // transition
            if (tr.on) {
                tr.t += frameDt;
                if (tr.t >= 0.6f && !tr.loaded) { load_world(tr.to, tr.wake); tr.loaded = true; }
                if (tr.t >= 1.3f) tr.on = false;
            }

            // the eye in the sky: it grows the longer you look back
            if (L.sky.eye) {
                Vector3 ed = { sinf(200 * DEG2RAD) * cosf(30 * DEG2RAD), sinf(30 * DEG2RAD), -cosf(200 * DEG2RAD) * cosf(30 * DEG2RAD) };
                if (Vector3DotProduct(player_forward(&P), ed) > 0.8f) stareT = fminf(8, stareT + frameDt); else stareT = fmaxf(0, stareT - frameDt * 0.7f);
                if (eyeBoost > 0) eyeBoost -= frameDt;
                L.sky.eyeAmt = fminf(1.0f, 0.5f + stareT * 0.07f + (eyeBoost > 0 ? 0.5f : 0.0f) + 0.02f * (dreams > 10 ? 10 : dreams));
            }
            if (eyesOpenT > 0) eyesOpenT -= frameDt;
            // mood
            float m = 0.04f;
            if (stareT > 1.5f) m += (stareT - 1.5f) * 0.08f;
            if (P.gripping) m += (1.0f - P.grip) * 0.6f;
            if (P.grip < 0.25f) m += (0.25f - P.grip) * 2.0f;
            if (L.sludge && L.sludgeArmed) { float g = P.pos.y - L.sludgeY; if (g < 9) m += (9 - g) / 9.0f * 0.6f; }
            for (int i = 0; i < L.watchers.size; i++) {
                float d = Vector3Distance(L.watchers.data[i].pos, P.pos);
                if (d < 14) m += (14 - d) / 14.0f * 0.5f;
            }
            if (m > 1) m = 1;
            madness += (m - madness) * fminf(1, frameDt * 3);
            tension = madness;
            float tone = L.id == W_HUB ? 1.0f : L.id == W_SHAFT ? 0.75f : L.id == W_DRAINS ? 0.9f : L.id == W_VOID ? 1.4f : L.id == W_GARDEN ? 1.15f : 2.0f;
            float mus = L.id == W_GARDEN ? 0.8f : L.id == W_VOID ? 0.5f : L.id == W_END ? 0.8f : (L.id == W_HUB && dreams >= 4) ? 0.25f : 0.0f;
            audio_music(frozen ? 0.0f : mus, fminf(1.0f, madness * 0.9f + (blackout > 0 ? 0.4f : 0.0f)));
            float whisper = L.nearest < 18 ? 1.0f - L.nearest / 18.0f : 0.0f;
            if (!frozen) audio_set(tone, tension, whisper, tr.on ? 0.3f : 0.9f);
        } else {
            endT += frameDt;
            if (IsKeyPressed(KEY_ENTER) && endT > 8) { if (levelLoaded) { level_free(&L); levelLoaded = false; } state = S_TITLE; }
        }
        if (nameT > 0) nameT -= frameDt;
        if (msgT > 0) msgT -= frameDt;
        if (flash > 0) flash = fmaxf(0, flash - frameDt * 1.2f);
        if (glitch > 0) glitch = fmaxf(0, glitch - frameDt * 1.6f);
        if (levelLoaded && L.id == W_HUB) gfx_update_static(time, dreams >= 3 && fmodf(time, 19.0f) < 0.3f);

        // ---- render
        float fade = 0;
        if (tr.on) fade = tr.t < 0.6f ? tr.t / 0.6f : fmaxf(0, 1.0f - (tr.t - 0.6f) / 0.7f);
        if (state == S_ENDING) fade = fminf(1.0f, endT / 3.0f) * 0.92f;

        BeginTextureMode(gfx_rt);
        if (state == S_TITLE || !levelLoaded) {
            draw_title(time);
            DrawRectangleGradientV(0, 50, RT_W, 130, (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 150 });
            int j = (int)(sinf(time * 40) * 1.2f * (sinf(time * 0.7f) > 0.95f));
            text_c("M U R K", 82 + j, 40, (Color){ 120, 20, 40, 255 });
            text_c("M U R K", 80 + j, 40, (Color){ 235, 220, 195, 255 });
            text_c("a descent into other people's dreams", 128, 10, (Color){ 120, 110, 95, 255 });
            if ((int)(time * 1.5f) % 2) text_c("press ENTER or click", 190, 10, (Color){ 170, 160, 140, 255 });
            const char *memo = g_launches >= 6 ? "it kept your place." : g_launches >= 2 ? "you came back." : "";
            if (msgT > 0) text_c(msg, 172, 10, (Color){ 210, 200, 180, (unsigned char)(fminf(1.0f, msgT) * 255) });
            if (memo[0]) text_c(memo, 156, 10, (Color){ 130, 40, 34, (unsigned char)(150 + 60 * sinf(time * 2.0f)) });
            text_c("WASD · SHIFT · SPACE · hold LMB to grip · F lamp · R wake up · [ ] mouse sensitivity", RT_H - 18, 10, (Color){ 90, 85, 75, 255 });
        } else {
            Vector3 eye = player_eye(&P), fwd = player_forward(&P);
            Camera3D cam = { 0 };
            cam.position = eye;
            cam.target = Vector3Add(eye, fwd);
            cam.up = Vector3RotateByAxisAngle((Vector3){ 0, 1, 0 }, fwd, P.roll * DEG2RAD);
            cam.fovy = 70.0f + fminf(8.0f, P.speedMeter) * 0.7f + madness * 8.0f;
            cam.projection = CAMERA_PERSPECTIVE;
            if (state == S_ENDING) { cam.position = (Vector3){ 0, 1.0f, -6.5f }; cam.target = (Vector3){ 0, 0.8f, -11 }; cam.up = (Vector3){ 0, 1, 0 }; }
            draw_scene(cam, time);
            if (state == S_PLAY) draw_hud(time);
            else {
                int n = (int)(sizeof END_LINES / sizeof *END_LINES);
                for (int i = 0; i < n; i++) {
                    float a = fminf(1.0f, fmaxf(0.0f, (endT - 2.0f - i * 1.1f)));
                    if (a > 0 && END_LINES[i][0]) text_c(END_LINES[i], 60 + i * 16, 10, (Color){ 220, 215, 200, (unsigned char)(a * 255) });
                }
                if (endT > 8) text_c("ENTER", RT_H - 24, 10, (Color){ 120, 115, 105, 255 });
            }
        }
        EndTextureMode();
        {
            Color lo = { 128, 128, 128, 255 }, hi = lo;
            if (state == S_TITLE || !levelLoaded) { lo = (Color){ 112, 124, 158, 255 }; hi = (Color){ 158, 128, 124, 255 }; }
            else if (L.gradeLo.a) { lo = L.gradeLo; hi = L.gradeHi; }
            gfx_grade(lo, hi);
        }
        gfx_present(time, state == S_PLAY ? madness : 0.0f, fade, flash, glitch);

        if (shotWorld >= 0 && ++shotFrame >= shotFrames) { TakeScreenshot(shotPath); quit = true; }
}

int main(void) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "MURK");
    SetWindowMinSize(480, 270);
#ifdef __EMSCRIPTEN__
    SetExitKey(KEY_NULL);
#else
    SetExitKey(KEY_ESCAPE);
    SetTargetFPS(144);
#endif
    load_memory();
    gfx_init();
    audio_init();

    // dev hook: MURK_SHOT="world,x,y,z,yaw,pitch,fx,frames,path" renders a frame and exits
    float sx = 0, sy = 0, sz = 0, syaw = 0, spit = 0; int sfxmask = 0;
    const char *env = getenv("MURK_SHOT");
    if (env && !strncmp(env, "title", 5)) { shotWorld = 99; shotFrames = 30; snprintf(shotPath, sizeof shotPath, "%s", env + 6); }
    else if (env && sscanf(env, "%d,%f,%f,%f,%f,%f,%d,%d,%255s", &shotWorld, &sx, &sy, &sz, &syaw, &spit, &sfxmask, &shotFrames, shotPath) == 9) {
        new_game();
        P.fx = sfxmask;
        load_world((WorldId)shotWorld, false);
        P.pos = (Vector3){ sx, sy, sz }; P.yaw = syaw; P.pitch = spit;
        if (sfxmask & 1) lampOn = 1;
        EnableCursor();
    } else shotWorld = -1;

    bot = getenv("MURK_BOT");
    prevT = GetTime();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose() && !quit) frame();
#endif
    if (levelLoaded) level_free(&L);
    audio_shutdown();
    gfx_shutdown();
    CloseWindow();
    return 0;
}
