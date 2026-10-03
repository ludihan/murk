#include "common.h"
#include "gfx.h"
#include "world.h"
#include "player.h"
#include "audio.h"
#include "figure.h"
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
static float freezeT, nextFreeze = 70, phantomT = 15, stepGap, titleT;
static int phantomLeft;
static float sens = 0.085f;   // degrees per mouse count; [ and ] change it
static float nextEvent = 30, eyesOpenT;
static bool gardenCaught;

// eyes that hang in the fog at the edge of sight and are gone when you face them
typedef struct { Vector3 pos; float life, seenT; bool on; } Lurker;
static Lurker lurk[3];
static float lurkTimer = 8;
static char msg[160];
static float msgT;

static struct { bool on; float t; WorldId to; bool wake, loaded; } tr;

// the last time you were in a dream, you walked a path through it. next time, someone else walks it.
#define GHOST_MAX 1600
static Vector3 ghostPath[W_COUNT][GHOST_MAX], rec[GHOST_MAX];
static int ghostN[W_COUNT], recN;
static float recT;
static struct { bool on, noticed; float t; Vector3 pos; float yaw; } ghost;

// the pages. most of them were written for someone else, and they tell you the rules anyway
static const char *NOTES[NOTE_COUNT] = {
    [NOTE_VIGIL] = "PARISH NOTICE.\nThe vigil continues in shifts. Kneel when the bell is rung.\nDo not speak to him if he speaks. Do not let him see your faces.\nWhatever he asks for, do not wake him.",
    [NOTE_DAY9] = "Day 9.\nHe sleeps through the day now. Father A. says that is good.\nHe says the sleep is where the door is, and that every door\nin the house must be left open.",
    [NOTE_DOORS] = "I counted the doors in his room tonight. There were four.\nWhen I came back up with the candles there were five.\nI did not tell Father A. I think he already knows.",
    [NOTE_GARDEN] = "He talked in his sleep again. He described a garden.\nHe said the flowers were all looking at him and that he\ndid not want to pick any of them.\nFather A. wrote down every word.",
    [NOTE_DRAINS] = "Do not go down to the drains after dark.\nBrother M. went down to see what was drinking from the water.\nWe hear him sometimes when we are quiet. Tapping.",
    [NOTE_CLIMB] = "It climbs the way you do. It has learned your hands.\nWhen you climb, it climbs. When you stop, it stops.\nDo not stop for long. The water is coming up behind it.",
    [NOTE_EYE] = "There is an eye above the steps.\nIt does not see what does not move. When it opens, be still,\nor be behind the old stones. It closes again. It always closes again.",
    [NOTE_FLOWERS] = "The flowers are its eyes and the gardeners are its hands.\nA flower that sees you will call them to where you stood.\nKeep low. Keep to where the petals are closed.",
    [NOTE_CANDLES] = "Light the three black candles down here and the grate\nover the lamp will open.\nThe one who lives in the drains is blind. It hunts by the sound of you. Walk softly. Kneel if it is close. Do not run.",
    [NOTE_FAMILY] = "To the family.\nThank you for your son. He will sleep for as long as we need him to.\nYou may visit on Sundays. Please do not bring anything that rings.\n\n- the Congregation of the Lower Church",
    [NOTE_AWAKE] = "If you are reading this then you are awake.\nYou are not supposed to be awake.\nLie back down.",
    [NOTE_BELL] = "When the bell tolls, kneel with the others.\nThe priest counts the heads.\nHe must not count one that is standing.",
};
static int reading = -1;   // the note on screen, or -1
static const char *useHint;
// the shaft: something on the wall below you that climbs when you climb, and only while you aren't looking at it
static struct { bool on, seen; float y, lastPY, scrapeT; Vector3 pos, n; } climber;
static struct { int phase; float t, noticed, lift; bool warned; } gaze;

static void use_thing(Use *u);

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
    static const char *T[] = { "MURK (Not Responding)", "MURK - NEMA", "MURK - do not wake him", "MURK - he is still asleep", "MURK - 1 player" };
    SetWindowTitle(T[GetRandomValue(0, 4)]);
    titleT = 3.0f;
}

static float frand_(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }
static void say(const char *s, float secs) { snprintf(msg, sizeof msg, "%s", s); msgT = secs; }

static void load_world(WorldId id, bool wake) {
    reading = -1; useHint = NULL;
    if (levelLoaded) {
        if (recN > 40 && L.id != W_SHAFT && L.id != W_END) { memcpy(ghostPath[L.id], rec, recN * sizeof *rec); ghostN[L.id] = recN; }
        level_free(&L);
    }
    recN = 0; recT = 0;
    memset(&climber, 0, sizeof climber);
    memset(&gaze, 0, sizeof gaze); gaze.t = 7.0f;
    ghost.on = ghostN[id] > 40 && dreams >= 2 && GetRandomValue(0, 2) > 0; ghost.noticed = false; ghost.t = -frand_(4, 9);
    level_build(&L, id, ++dreams);
    levelLoaded = true;
    player_spawn(&P, &L);
    if (id == W_HUB && wake) {
        P.pos = L.wakePos;
        P.yaw = L.wakeYaw;
    }
    nameT = 3.5f;
    blackout = 0; nextBlackout = 14 + GetRandomValue(0, 12); phantomLeft = 0;
    memset(lurk, 0, sizeof lurk); lurkTimer = 6 + GetRandomValue(0, 6);
    lampOn = (P.fx & (1u << FX_LAMP)) ? lampOn : 0;
    if (id == W_HUB && !wake && dreams <= 1) say("WASD walk · SHIFT run · CTRL kneel · E use · hold LMB at rusty walls to grip · R wake up", 12);
    if (id == W_SHAFT) say("hold LMB on the rusty plates. W climbs, A/D shuffle, SPACE lunges. don't let go.", 9);
    if (id == W_DRAINS) say("something down here is listening.", 7);
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


// pan of a point relative to where you are facing: -1 hard left, 1 hard right
static float pan_of(Vector3 p) {
    float yaw = P.yaw * DEG2RAD;
    Vector3 right = { cosf(yaw), 0, sinf(yaw) }, d = Vector3Normalize((Vector3){ p.x - P.pos.x, 0, p.z - P.pos.z });
    return Vector3DotProduct(right, d);
}
// a sound that comes from somewhere: quieter with distance, from the side it is on
static void play_from(Sfx s, Vector3 p, float vol, float pitch) {
    float d = Vector3Distance(p, P.pos);
    audio_play_at(s, vol / (1.0f + d * 0.08f), pitch, pan_of(p));
}
// a sound from a direction you are not looking: behind you, off to one side
static void play_behind(Sfx s, float vol, float pitch) {
    float a = (P.yaw + 180 + frand_(-70, 70)) * DEG2RAD;
    audio_play_at(s, vol, pitch, sinf(a - P.yaw * DEG2RAD));
}

static bool ray_clear(Vector3 a, Vector3 b) {
    b3RayResult r = b3World_CastRayClosest(L.phys, b3v(a), b3v(Vector3Subtract(b, a)), b3DefaultQueryFilter());
    return !r.hit || r.fraction > 0.97f;
}

static const char *WHISPERS[W_COUNT][4] = {
    { "the candles were not lit when you fell asleep.", "someone has been sitting in the chairs.", "they are praying under the floor.", "he is still asleep. good." },
    { "don't look down.", "it climbs when you climb.", "your hands are not yours.", "it was a long way down for him too." },
    { "it can hear your heart.", "walk softly.", "something is drinking from the water.", "the walls are wet. they are always wet." },
    { "the eye was here first.", "there is no floor. there never was.", "you are falling very slowly.", "hold still and it looks past you." },
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
    if (kind == 0 || L.id != W_GARDEN) {
        say(WHISPERS[L.id][GetRandomValue(0, 3)], 4.0f);
        static const Sfx AMB[] = { SFX_KNOCK, SFX_BELL, SFX_CHANT, SFX_PRAYER, SFX_HUM, SFX_CREAK, SFX_SCRAPE };
        Sfx a = AMB[GetRandomValue(0, 6)];
        play_behind(a, a == SFX_HUM || a == SFX_PRAYER ? 0.35f : 0.5f, frand_(0.85f, 1.0f));
    } else if (L.id == W_GARDEN) {
        eyesOpenT = 5.0f; audio_play(SFX_SWELL);
        say("every flower opens its eyes.", 4.0f);
    }
}

// the visitors in the house never walk where you can see. they are just somewhere else when you look again,
// a little closer each time, and in the end they are standing at your back
static Vector3 behind_you(void) {
    Vector3 f = player_forward(&P); f.y = 0; f = Vector3Normalize(f);
    return (Vector3){ P.pos.x - f.x * 1.25f, P.pos.y, P.pos.z - f.z * 1.25f };
}
static void hub_visitors(float dt, Vector3 eye, Vector3 fwd) {
    float nearest = 99;
    for (int i = 0; i < L.watchers.size; i++) {
        Watcher *w = &L.watchers.data[i];
        bool seen = blackout <= 0 && level_seen(&L, eye, fwd, w->pos);
        w->phase += dt;
        float d = Vector3Distance(w->pos, P.pos);
        if (w->state == 1) {           // at your back
            Vector3 b = behind_you();
            if (!seen) {
                if (ray_clear((Vector3){ P.pos.x, P.pos.y + 1.2f, P.pos.z }, (Vector3){ b.x, b.y + 1.2f, b.z })) w->pos = b;
                w->timer += dt;
                if (w->timer > 14) { w->state = 0; w->timer = frand_(6, 10); w->pos = L.spots[GetRandomValue(0, L.nspots - 1)]; }
            } else { w->state = 2; w->timer = 0; }
        } else if (w->state == 2) {    // you turned round, and it is right there. then the light goes
            w->timer += dt;
            if (w->timer > 0.8f) {
                blackout = 2.6f; madness = fminf(1.0f, madness + 0.5f);
                int far = 0; float fd = 0;
                for (int k = 0; k < L.nspots; k++) { float sd = Vector3Distance(L.spots[k], P.pos); if (sd > fd) { fd = sd; far = k; } }
                w->pos = L.spots[far]; w->state = 0; w->timer = frand_(8, 14);
                say("it only wanted to be near you.", 4);
            }
        } else {
            if (seen && !w->seen) audio_play_ex(SFX_SWELL, 0.3f, 0.9f);
            w->timer -= dt;
            if (w->timer <= 0 && !seen) {
                w->timer = frand_(5, 10) * fmaxf(0.45f, 1.0f - dreams * 0.05f);
                int best = -1; float bd = d - 1.0f;
                for (int k = 0; k < L.nspots; k++) {
                    Vector3 sp = L.spots[k];
                    if (Vector3Distance(sp, w->pos) > 12) continue;
                    float ds = Vector3Distance(sp, P.pos);
                    if (ds < bd && ds > 2.5f && !level_seen(&L, eye, fwd, sp)) { bd = ds; best = k; }
                }
                Vector3 b = behind_you();
                if (best >= 0) {
                    w->pos = L.spots[best];
                    if (GetRandomValue(0, 2) == 0) play_from(SFX_CREAK, w->pos, 0.5f, frand_(0.75f, 0.95f));
                } else if (d < 7 && !level_seen(&L, eye, fwd, b) && ray_clear((Vector3){ P.pos.x, P.pos.y + 1.2f, P.pos.z }, (Vector3){ b.x, b.y + 1.2f, b.z })) {
                    w->pos = b; w->state = 1; w->timer = 0;
                }
            }
        }
        w->seen = seen;
        if (d < nearest) nearest = d;
    }
    L.nearest = nearest;
}

static bool update_climber(Vector3 eye, Vector3 fwd) {
    if (L.id != W_SHAFT) return false;
    if (!climber.on) {
        if (!L.sludgeArmed) { climber.lastPY = P.pos.y; return false; }
        climber.on = true; climber.y = P.pos.y - 11.0f; climber.lastPY = P.pos.y;
        play_behind(SFX_SCRAPE, 0.5f, 0.8f);
        say("something has started up the wall after you.", 4);
    }
    // it stays on whichever wall you are nearest, under you
    float W = L.shaftW - 0.06f;
    bool onX = fabsf(P.pos.x) > fabsf(P.pos.z);
    climber.n = onX ? (Vector3){ P.pos.x > 0 ? -1.0f : 1.0f, 0, 0 } : (Vector3){ 0, 0, P.pos.z > 0 ? -1.0f : 1.0f };
    Vector3 p = onX ? (Vector3){ P.pos.x > 0 ? W : -W, climber.y, Clamp(P.pos.z, -W + 0.6f, W - 0.6f) }
                    : (Vector3){ Clamp(P.pos.x, -W + 0.6f, W - 0.6f), climber.y, P.pos.z > 0 ? W : -W };
    Vector3 to = Vector3Subtract(Vector3Add(p, Vector3Scale(climber.n, 0.4f)), eye);
    float d = Vector3Length(to);
    climber.seen = d > 0.01f && Vector3DotProduct(Vector3Scale(to, 1.0f / d), fwd) > 0.55f && d < 30 && ray_clear(eye, Vector3Add(p, Vector3Scale(climber.n, 0.5f)));
    float rise = P.pos.y - climber.lastPY;
    climber.lastPY = P.pos.y;
    if (!climber.seen) {
        float before = climber.y;
        if (rise > 0) climber.y += rise * 1.35f;                 // your hands, a little quicker than yours
        climber.y = fmaxf(climber.y, P.pos.y - 15.0f);            // it never falls far behind
        climber.y = fmaxf(climber.y, L.sludgeY + 0.4f);
        float moved = climber.y - before;
        climber.scrapeT -= moved;
        if (climber.scrapeT <= 0) { climber.scrapeT = 1.6f; play_from(GetRandomValue(0, 2) ? SFX_CLICK : SFX_SCRAPE, p, 0.7f, frand_(0.8f, 1.0f)); }
    }
    p.y = climber.y;
    climber.pos = p;
    return Vector3Distance(Vector3Add(p, (Vector3){ 0, 0.8f, 0 }), (Vector3){ P.pos.x, P.pos.y + 0.6f, P.pos.z }) < 1.4f;
}

// the eye above the steps keeps a slow rhythm: shut, opening, open, closing. while it is open it sees
// anything that moves out in the open. be still, or put stone between you and it
static bool update_gaze(float dt) {
    static const Vector3 ED = { -0.2962f, 0.5f, 0.8138f };   // toward the eye: azimuth 200, elevation 30
    gaze.t -= dt;
    if (gaze.phase == 0 && gaze.t <= 0) { gaze.phase = 1; gaze.t = 2.2f; audio_play_ex(SFX_SWELL, 0.7f, 0.8f); }
    else if (gaze.phase == 1 && gaze.t <= 0) {
        gaze.phase = 2; gaze.t = frand_(4.5f, 7.0f);
        if (!gaze.warned) { gaze.warned = true; say("be still.", 3); }
    }
    else if (gaze.phase == 2 && gaze.t <= 0) { gaze.phase = 3; gaze.t = 1.2f; }
    else if (gaze.phase == 3 && gaze.t <= 0) { gaze.phase = 0; gaze.t = frand_(9, 15) * fmaxf(0.55f, 1.0f - dreams * 0.04f); }
    float open = gaze.phase == 0 ? 0.04f : gaze.phase == 1 ? 0.04f + 0.96f * (1.0f - gaze.t / 2.2f) : gaze.phase == 2 ? 1.0f : 0.04f + 0.96f * (gaze.t / 1.2f);
    L.sky.eyeAmt = open;
    if (gaze.lift > 0) {   // it has you. up, off the steps, into it
        gaze.lift += dt;
        P.vel.y = fmaxf(P.vel.y, 10.0f);
        madness = 1;
        return gaze.lift > 2.0f;
    }
    if (gaze.phase == 2) {
        Vector3 chest = { P.pos.x, P.pos.y + 1.1f - P.crouch * 0.5f, P.pos.z };
        b3RayResult r = b3World_CastRayClosest(L.phys, b3v(chest), b3v(Vector3Scale(ED, 40.0f)), b3DefaultQueryFilter());
        bool covered = r.hit;
        bool moving = P.speedMeter > 0.6f || !P.grounded || P.gripping;
        if (!covered && moving) gaze.noticed += dt * (P.crouch > 0.5f ? 0.45f : 0.9f);
        else gaze.noticed = fmaxf(0, gaze.noticed - dt * 0.25f);
        if (gaze.noticed >= 1.0f) { gaze.lift = 0.001f; say("it saw you.", 4); audio_play_ex(SFX_CHANT, 0.8f, 0.7f); }
    } else gaze.noticed = fmaxf(0, gaze.noticed - dt * 0.4f);
    return false;
}

// the orchard: the flowers are its eyes and the gardeners are its hands. the gardeners are blind; a flower
// that sees you standing calls them to where you were. in the long grass, kneeling, the flowers can't see you
static bool flower_open(int i, float t) { return eyesOpenT > 0 || sinf(t * 0.4f + i * 3.1f) > 0.2f; }
static bool flower_has_eye(const Bloom *b, int i) { return b->yaw <= 0.5f && i % 4 == 0; }
static void garden_update(float dt, Vector3 eye) {
    static float spotted;
    bool seen = false; Vector3 from = { 0 };
    float range = P.crouch > 0.5f ? (P.speedMeter > 0.5f ? 3.5f : 0.0f) : 11.0f;
    for (int i = 0; i < L.blooms.size && range > 0; i++) {
        const Bloom *b = &L.blooms.data[i];
        if (!flower_has_eye(b, i) || !flower_open(i, L.t)) continue;
        if (Vector3Distance(b->pos, eye) > range || !ray_clear(b->pos, eye)) continue;
        seen = true; from = b->pos; break;
    }
    if (spotted < 0) spotted = fminf(0.0f, spotted + dt);   // it has just called; give them a moment
    else spotted = seen ? spotted + dt : fmaxf(0.0f, spotted - dt);
    if (spotted > 0.8f) {   // it calls them: the two nearest come to where you were standing
        spotted = -3.0f;
        play_from(SFX_PICKUP, from, 0.5f, 0.55f);
        play_from(SFX_HUM, from, 0.35f, 0.8f);
        for (int k = 0; k < 2; k++) {
            Watcher *best = NULL; float bd = 1e9f;
            for (int i = 0; i < L.watchers.size; i++) {
                Watcher *w = &L.watchers.data[i];
                float d = Vector3Distance(w->pos, P.pos);
                if (w->state != 1 && d < bd) { bd = d; best = w; }
            }
            if (best) { best->state = 1; best->goal = (Vector3){ P.pos.x, 0, P.pos.z }; best->timer = 7; }
        }
    }
    float nearest = 99;
    int dn = dreams > 8 ? 8 : dreams;
    for (int i = 0; i < L.watchers.size; i++) {
        Watcher *w = &L.watchers.data[i];
        w->phase += dt;
        float d = Vector3Distance((Vector3){ w->pos.x, 0, w->pos.z }, (Vector3){ P.pos.x, 0, P.pos.z });
        if (d < nearest) nearest = d;
        if (d < 3.2f && (P.noise > 0.15f || P.crouch < 0.5f)) { w->state = 2; w->goal = (Vector3){ P.pos.x, 0, P.pos.z }; w->timer = 3; }   // close enough to feel you
        if (d < 1.1f) { gardenCaught = true; say("it only wanted to hold you.", 4); }
        Vector3 to = Vector3Subtract(w->goal, w->pos); to.y = 0;
        float len = Vector3Length(to), sp = w->state == 0 ? 0.7f : w->state == 1 ? 1.8f + 0.1f * dn : 2.5f + 0.1f * dn;
        if (len < 0.5f) {
            if (w->state > 0) { w->timer -= dt; if (w->timer <= 0) w->state = 0; }
            if (w->state == 0) {
                w->goal = (Vector3){ Clamp(w->pos.x + frand_(-14, 14), -46, 46), 0, Clamp(w->pos.z + frand_(-14, 14), -46, 46) };
                if (fabsf(w->goal.x) < 16 && fabsf(w->goal.z) < 16) w->goal.x = w->goal.x < 0 ? -17 : 17;   // not into the pond
            }
        } else {
            float step = fminf(len, sp * dt);
            w->pos = Vector3Add(w->pos, Vector3Scale(to, step / len));
            w->stride += step * 2.2f;
        }
    }
    L.nearest = nearest;
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
            if (GetRandomValue(0, 2) == 0) play_behind(GetRandomValue(0, 1) ? SFX_KNOCK : SFX_CREAK, 0.4f, frand_(0.8f, 1.0f));
            else { phantomLeft = GetRandomValue(3, 6); stepGap = 0; }
        }
    }
    if (phantomLeft > 0) {
        stepGap -= dt;
        if (stepGap <= 0) { play_behind(SFX_STEP, 0.3f, 0.6f); stepGap = 0.62f; phantomLeft--; }
    }
    // ---- blackouts: the lights just stop. in the drains, things keep walking.
    bool haunted = L.id == W_DRAINS || (L.id == W_HUB && L.watchers.size > 0);
    if (blackout > 0) {
        blackout -= dt;
    } else if (haunted && !tr.on) {
        nextBlackout -= dt;
        if (nextBlackout <= 0) {
            blackout = frand_(0.9f, 2.0f);
            nextBlackout = fmaxf(7.0f, 26.0f - dreams * 1.2f) * frand_(0.6f, 1.4f);
            audio_play(SFX_KNOCK);
        }
    }
    if (L.id == W_HUB) hub_visitors(dt, eye, fwd);
    if (L.id == W_GARDEN) garden_update(dt, eye);
    director(dt);
    // ---- your own path, walked by someone else
    recT += dt;
    if (recT >= 0.25f && recN < GHOST_MAX) { recT = 0; rec[recN++] = P.pos; }
    if (ghost.on) {
        const Vector3 *gp = ghostPath[L.id];
        int n = ghostN[L.id];
        if (!ghost.noticed) ghost.t += dt;
        if (ghost.t >= 0) {
            float fi = ghost.t / 0.25f;
            int i = (int)fi;
            if (i >= n - 1) ghost.on = false;
            else {
                Vector3 np = Vector3Lerp(gp[i], gp[i + 1], fi - i);
                Vector3 mv = Vector3Subtract(gp[i + 1], gp[i]);
                if (!ghost.noticed && Vector3Length((Vector3){ mv.x, 0, mv.z }) > 0.02f) ghost.yaw = atan2f(mv.x, -mv.z);
                ghost.pos = np;
                bool seen = level_seen(&L, eye, fwd, np);
                if (!ghost.noticed && Vector3Distance(np, P.pos) < 4.5f) { ghost.noticed = true; ghost.yaw = atan2f(P.pos.x - np.x, -(P.pos.z - np.z)); }
                else if (ghost.noticed && !seen) ghost.on = false;   // look away and it was never there
            }
        }
    }
    // ---- the ones that kneel and stand still: they turn their heads when you aren't looking
    for (int i = 0; i < L.effigies.size; i++) {
        Effigy *e = &L.effigies.data[i];
        e->seen = level_seen(&L, eye, fwd, e->pos);
        if (!e->seen && Vector3Distance(e->pos, P.pos) < 10) e->look = fminf(1.0f, e->look + dt * 0.6f);
    }
    // ---- lurkers
    if (L.id != W_END && L.id != W_SHAFT) {
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
                    Vector3 p = { eye.x + sinf(a) * d, eye.y + 0.4f, eye.z - cosf(a) * d };
                    if (!ray_clear(eye, p)) continue;
                    b3RayResult fl = b3World_CastRayClosest(L.phys, b3v(p), (b3Vec3){ 0, -6, 0 }, b3DefaultQueryFilter());
                    if (fl.hit) p.y = fl.point.y + 2.0f;
                    lurk[i] = (Lurker){ p, frand_(5, 9), 0, true };
                    if (GetRandomValue(0, 2) == 0) play_from(SFX_CREAK, p, 0.5f, frand_(0.7f, 0.9f));
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
    float light = L.light * flicker(time, L.id == W_HUB ? 0.35f + 0.05f * (dreams > 8 ? 8 : dreams) : 0.15f) * (lampOn > 0.5f ? 1.2f : 1.0f);
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
        if (Vector3Distance(eye, w->pos) > cull + 2) continue;
        Fig f = { L.id == W_DRAINS ? FIG_CRAWLER : L.creepers ? FIG_GARDENER : FIG_PENITENT, w->pos,
                  atan2f(P.pos.x - w->pos.x, -(P.pos.z - w->pos.z)), w->stride, eye, 0.7f, sinf(i * 1.7f) * 0.35f, w->phase, { 0, 0, 1 }, { 0 } };
        figure_draw(&f);
    }
    for (int i = 0; i < L.uses.size; i++) {
        const Use *u = &L.uses.data[i];
        if (Vector3Distance(eye, u->pos) > cull) continue;
        if (u->kind == USE_NOTE) {
            gfx_box((Vector3){ u->pos.x, u->pos.y + 0.004f, u->pos.z }, (Vector3){ 0.13f, 0.004f, 0.18f }, TEX_SKIN, u->done ? (Color){ 150, 140, 120, 255 } : (Color){ 235, 225, 196, 255 }, 1.0f);
            for (int k = 0; k < 5; k++)   // lines of handwriting
                gfx_box((Vector3){ u->pos.x - 0.01f * (k & 1), u->pos.y + 0.009f, u->pos.z - 0.12f + k * 0.055f }, (Vector3){ 0.09f - 0.02f * (k == 4), 0.001f, 0.006f }, TEX_CONCRETE, (Color){ 40, 30, 28, 255 }, 1.0f);
        } else if (u->kind == USE_LILY && !u->done) {   // a white lily, the only white thing out here
            gfx_limb(u->pos, (Vector3){ u->pos.x, u->pos.y + 0.7f, u->pos.z }, 0.015f, 0.01f, TEX_GRASS, (Color){ 80, 120, 90, 255 });
            Vector3 c = { u->pos.x, u->pos.y + 0.74f, u->pos.z };
            for (int k = 0; k < 6; k++) {
                float a = k * 1.0472f + i;
                Vector3 d = { sinf(a), 0.55f, cosf(a) }, pp = { cosf(a), 0, -sinf(a) };
                gfx_ellipsoid(Vector3Add(c, Vector3Scale(d, 0.09f)), Vector3Scale(d, 0.1f), Vector3Scale(pp, 0.035f), (Vector3){ 0, 0.01f, 0 }, TEX_SKIN, (Color){ 250, 248, 240, 255 });
            }
        } else if (u->kind == USE_CANDLE) {
            gfx_box((Vector3){ u->pos.x, u->pos.y + 0.2f, u->pos.z }, (Vector3){ 0.05f, 0.2f, 0.05f }, TEX_SKIN, (Color){ 36, 30, 30, 255 }, 1.0f);
            gfx_box((Vector3){ u->pos.x, u->pos.y + 0.003f, u->pos.z }, (Vector3){ 0.25f, 0.003f, 0.25f }, TEX_SLUDGE, (Color){ 40, 34, 30, 255 }, 1.0f);
        }
    }
    for (int i = 0; i < L.effigies.size; i++) {
        const Effigy *e = &L.effigies.data[i];
        if (Vector3Distance(eye, e->pos) > cull + 2) continue;
        Fig f = { (FigKind)e->kind, e->pos, e->yaw, 0, eye, e->look * 0.9f, e->tilt, time + i, { 0, 0, 1 }, { 0 } };
        figure_draw(&f);
    }
    if (climber.on) {
        Fig f = { FIG_CLIMBER, climber.pos, 0, climber.y * 3.0f, eye, 0.8f, 0.6f, time, climber.n, { 0 } };
        figure_draw(&f);
    }
    if (ghost.on && ghost.t >= 0) {
        Fig f = { FIG_PENITENT, ghost.pos, ghost.yaw, ghost.t * 3, eye, ghost.noticed ? 1.0f : 0.0f, 0.0f, time, { 0, 0, 1 }, (Color){ 18, 16, 18, 255 } };
        figure_draw(&f);
    }
    // lurkers: someone standing very still at the edge of the fog
    for (int i = 0; i < 3; i++) {
        const Lurker *k = &lurk[i];
        if (!k->on) continue;
        Fig f = { FIG_PENITENT, { k->pos.x, k->pos.y - 2.0f, k->pos.z }, atan2f(eye.x - k->pos.x, -(eye.z - k->pos.z)), 0, eye, 0.9f, 0.5f * sinf(i * 2.3f), time, { 0, 0, 1 }, (Color){ 12, 10, 12, 255 } };
        figure_draw(&f);
    }
    for (int i = 0; i < L.blooms.size; i++) {   // some of the flowers have an eye in them, and it is turned toward you
        const Bloom *b = &L.blooms.data[i];
        if (!flower_has_eye(b, i) || !flower_open(i, L.t)) continue;
        if (Vector3Distance(b->pos, eye) > 30) continue;
        float s = b->size;
        Vector3 to = Vector3Normalize(Vector3Subtract(eye, b->pos)), r = Vector3Normalize(Vector3CrossProduct(to, (Vector3){ 0, 1, 0 })), u = Vector3CrossProduct(r, to);
        Vector3 ec = Vector3Add(b->pos, Vector3Scale(to, s * 0.12f));
        gfx_ellipsoid(ec, Vector3Scale(r, s * 0.2f), Vector3Scale(u, s * 0.2f), Vector3Scale(to, s * 0.14f), TEX_SKIN, (Color){ 250, 244, 236, 255 });
        gfx_ellipsoid(Vector3Add(ec, Vector3Scale(to, s * 0.13f)), Vector3Scale(r, s * 0.07f), Vector3Scale(u, s * 0.09f), Vector3Scale(to, s * 0.02f), TEX_CONCRETE, (Color){ 4, 2, 3, 255 });
    }
    for (int i = 0; i < L.pickups.size; i++) {   // a grate over anything you have not earned yet
        const Pickup *pk = &L.pickups.data[i];
        if (pk->taken || !pk->locked) continue;
        for (int k = 0; k < 10; k++) {
            float a = k * 0.6283f;
            gfx_box((Vector3){ pk->pos.x + sinf(a) * 0.55f, pk->pos.y - 0.1f, pk->pos.z + cosf(a) * 0.55f }, (Vector3){ 0.025f, 1.2f, 0.025f }, TEX_RUST, (Color){ 120, 100, 90, 255 }, 1.0f);
        }
        gfx_box((Vector3){ pk->pos.x, pk->pos.y + 1.1f, pk->pos.z }, (Vector3){ 0.6f, 0.03f, 0.6f }, TEX_RUST, (Color){ 120, 100, 90, 255 }, 1.0f);
    }
    if (L.sludge) gfx_slab(L.sludgeY, 6.0f, TEX_SLUDGE, (Color){ 140, 160, 90, 255 }, time * 12.0f);
    if (L.water) gfx_slab(L.waterY, L.waterHalf, TEX_WATER, (Color){ 50, 56, 64, 255 }, time * 3.0f);

    gfx_set_emit(true);
    for (int i = 0; i < L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (!(b->flags & F_EMIT)) continue;
        float k = ((L.id == W_HUB || L.id == W_DRAINS) && b->h.y < 0.2f) ? flicker(time + i, 0.4f) : 1.0f;
        gfx_glow(b->c, b->h, scale_col(b->tint, k));
    }
    for (int i = 0; i < L.pickups.size; i++) {
        const Pickup *pk = &L.pickups.data[i];
        if (pk->taken) continue;
        Vector3 c = { pk->pos.x, pk->pos.y + sinf(time * 2.0f) * 0.12f, pk->pos.z };
        Quaternion q = QuaternionMultiply(QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, time * 1.4f), QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, 0.6f));
        gfx_box_rot(c, (Vector3){ 0.22f, 0.22f, 0.22f }, q, TEX_CONCRETE, fx_color(pk->fx), 100.0f);
    }
    for (int i = 0; i < L.motes.size; i++) {
        const Mote *m = &L.motes.data[i];
        float a = sinf(fminf(m->life, 1.0f) * 3.14159f);
        float sz = L.moteGlow ? 0.03f : 0.012f;
        if (!L.moteGlow) gfx_glow(m->pos, (Vector3){ sz, sz, sz }, scale_col(L.moteCol, a));
    }
    // the flowers that watch
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
    }
    for (int i = 0; i < L.uses.size; i++) {
        const Use *u = &L.uses.data[i];
        if (u->kind == USE_CANDLE && u->done) gfx_glow((Vector3){ u->pos.x, u->pos.y + 0.46f + sinf(time * 13 + i) * 0.006f, u->pos.z }, (Vector3){ 0.02f, 0.05f, 0.02f }, (Color){ 255, 160, 70, 255 });
    }
    // soft glow around everything bright
    gfx_begin_glow();
    for (int i = 0; i < L.uses.size; i++) {
        const Use *u = &L.uses.data[i];
        if (u->kind == USE_CANDLE && u->done) gfx_halo((Vector3){ u->pos.x, u->pos.y + 0.47f, u->pos.z }, 1.6f + sinf(time * 9 + i) * 0.1f, (Color){ 255, 150, 70, 255 }, 0.5f);
        if (u->kind == USE_LILY && !u->done) gfx_halo((Vector3){ u->pos.x, u->pos.y + 0.78f, u->pos.z }, 0.9f, (Color){ 220, 225, 235, 255 }, 0.3f);
    }
    for (int i = 0; i < L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (!(b->flags & F_EMIT)) continue;
        float mx = fmaxf(b->h.x, fmaxf(b->h.y, b->h.z));
        if (mx > 1.6f || b->h.y > 3.0f) continue;
        if (dist_to_box(eye, b) > cull) continue;
        float k = ((L.id == W_HUB || L.id == W_DRAINS) && b->h.y < 0.2f) ? flicker(time + i, 0.4f) : 1.0f;
        Vector3 ho = Vector3Add(b->c, Vector3Scale(Vector3Normalize(Vector3Subtract(eye, b->c)), 0.45f));   // pull it off the wall it hangs on
        if (mx < 0.06f) gfx_halo(ho, 0.45f, b->tint, 0.28f * k);   // a candle flame: a small warm smudge, not a bloom
        else gfx_halo(ho, 0.9f + mx * 2.6f, b->tint, 0.55f * k);
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
    if (L.moteGlow) for (int i = 0; i < L.motes.size; i++) {
        const Mote *m = &L.motes.data[i];
        gfx_halo(m->pos, 0.22f, L.moteCol, 0.5f * sinf(fminf(m->life, 1.0f) * 3.14159f));
    }
    gfx_end_glow();
    gfx_set_emit(false);
    gfx_end_scene();
}

// ---------------------------------------------------------------- title: a lit door at the end of the orchard
// it is not quite the same every time you open the game
static void draw_title(float time) {
    static const Sky sky = { true, { 4, 5, 8, 255 }, { 30, 34, 36, 255 }, { 30, 34, 36, 255 }, 0.35f, true, { 220, 214, 196, 255 }, { -0.4f, 0.36f, -0.84f }, { 0, 0, 0, 0 }, 0.0f, false, 0 };
    Camera3D cam = { 0 };
    float yaw = sinf(time * 0.09f) * 0.28f;
    cam.position = (Vector3){ 0, 1.6f, 0 };
    cam.target = (Vector3){ sinf(yaw) * 10, 2.1f + sinf(time * 0.2f) * 0.15f, -cosf(yaw) * 10 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 70;
    cam.projection = CAMERA_PERSPECTIVE;
    bool doorLit = g_launches < 4;
    gfx_begin_scene(cam, sky.horizon, 0.03f, 1.0f, time);
    gfx_sky(&sky, time, cam.position);
    gfx_box((Vector3){ 0, -0.5f, 0 }, (Vector3){ 90, 0.5f, 90 }, TEX_GRASS, (Color){ 90, 104, 98, 255 }, 3.0f);
    gfx_box((Vector3){ 0, 2.2f, -26 }, (Vector3){ 1.8f, 2.2f, 0.25f }, TEX_CONCRETE, (Color){ 36, 32, 40, 255 }, 1.0f);
    for (int i = 0; i < 6; i++) {
        float z = -4 - i * 3.6f;
        gfx_box((Vector3){ -3.2f, 0.9f, z }, (Vector3){ 0.1f, 0.9f, 0.1f }, TEX_RUST, (Color){ 70, 60, 70, 255 }, 1.0f);
        gfx_box((Vector3){ 3.2f, 0.9f, z - 1.8f }, (Vector3){ 0.1f, 0.9f, 0.1f }, TEX_RUST, (Color){ 70, 60, 70, 255 }, 1.0f);
    }
    for (int i = 0; i < 70; i++) {
        float a = (i * 2.399f), r = 5 + (i * 37 % 41);
        float x = sinf(a) * r * 0.9f, z = -cosf(a) * r - 2.0f;
        if (fabsf(x) < 1.6f) continue;
        float h = 0.8f + (i * 13 % 25) / 10.0f;
        gfx_box((Vector3){ x, h / 2, z }, (Vector3){ 0.05f, h / 2, 0.05f }, TEX_GRASS, (Color){ 70, 100, 90, 255 }, 1.0f);
    }
    // the second time, someone is waiting beside the door. later they are on the path, closer. later still, there are more
    if (g_launches >= 2) {
        Fig f = { FIG_PENITENT, { 2.4f, 0, -25.2f }, 0, 0, cam.position, 1.0f, 0.3f, time, { 0, 0, 1 }, (Color){ 16, 14, 16, 255 } };
        if (g_launches >= 4) f.pos = (Vector3){ 1.1f, 0, -13.0f };
        figure_draw(&f);
    }
    if (g_launches >= 6) for (int i = 0; i < 4; i++) {
        Fig f = { FIG_KNEELER, { (i & 1) ? 1.9f : -1.9f, 0, -6.0f - i * 4.0f }, (i & 1) ? -1.5708f : 1.5708f, 0, cam.position, 0.0f, 0.1f, time + i, { 0, 0, 1 }, (Color){ 20, 18, 20, 255 } };
        figure_draw(&f);
    }
    static const Color PET[4] = { { 200, 190, 170, 255 }, { 150, 40, 44, 255 }, { 120, 100, 130, 255 }, { 190, 176, 120, 255 } };
    gfx_set_emit(true);
    if (doorLit) gfx_glow((Vector3){ 0, 1.9f, -25.7f }, (Vector3){ 1.3f, 1.8f, 0.05f }, (Color){ 230, 214, 190, 255 });
    for (int i = 0; i < 6; i++) {
        gfx_glow((Vector3){ -3.2f, 1.95f, -4 - i * 3.6f }, (Vector3){ 0.1f, 0.1f, 0.1f }, (Color){ 230, 150, 80, 255 });
        gfx_glow((Vector3){ 3.2f, 1.95f, -5.8f - i * 3.6f }, (Vector3){ 0.1f, 0.1f, 0.1f }, (Color){ 230, 150, 80, 255 });
    }
    for (int i = 0; i < 70; i++) {
        float a = (i * 2.399f), r = 5 + (i * 37 % 41);
        float x = sinf(a) * r * 0.9f, z = -cosf(a) * r - 2.0f;
        if (fabsf(x) < 1.6f) continue;
        float h = 0.8f + (i * 13 % 25) / 10.0f, sz = 0.3f + (i % 5) * 0.07f;
        Color c = scale_col(PET[i % 4], 0.6f + 0.1f * sinf(time * 1.5f + i));
        gfx_glow((Vector3){ x, h + 0.1f, z }, (Vector3){ sz, 0.025f, sz * 0.38f }, c);
        gfx_glow((Vector3){ x, h + 0.1f, z }, (Vector3){ sz * 0.38f, 0.025f, sz }, c);
    }
    gfx_begin_glow();
    if (doorLit) gfx_halo((Vector3){ 0, 1.9f, -25.0f }, 9.0f, (Color){ 220, 200, 170, 255 }, 0.6f);
    for (int i = 0; i < 6; i++) {
        gfx_halo((Vector3){ -3.2f, 1.95f, -4 - i * 3.6f }, 1.2f, (Color){ 230, 150, 80, 255 }, 0.5f);
        gfx_halo((Vector3){ 3.2f, 1.95f, -5.8f - i * 3.6f }, 1.2f, (Color){ 230, 150, 80, 255 }, 0.5f);
    }
    for (int i = 0; i < 40; i++) {   // ash, drifting down
        float t = time * 0.15f + i * 1.7f;
        gfx_halo((Vector3){ sinf(t * 1.3f + i) * 9, 4.5f - fmodf(t * 0.5f + i, 4.5f), -4 - fmodf(i * 3.7f, 20) + cosf(t) * 2 }, 0.12f, (Color){ 170, 160, 150, 255 }, 0.5f);
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
    if (useHint && reading < 0) text_c(useHint, cy + 10, 10, (Color){ 190, 180, 160, 200 });
    if (reading >= 0) {
        DrawRectangle(0, 0, RT_W, RT_H, (Color){ 0, 0, 0, 170 });
        DrawRectangle(60, 34, RT_W - 120, RT_H - 68, (Color){ 150, 140, 118, 240 });
        DrawRectangleLines(64, 38, RT_W - 128, RT_H - 76, (Color){ 96, 80, 64, 255 });
        // word wrap, keeping the line breaks the writer put in
        const char *t = NOTES[reading];
        int y = 52, maxw = RT_W - 160;
        char line[160] = "";
        while (*t) {
            int n = 0;
            while (t[n] && t[n] != ' ' && t[n] != '\n') n++;
            char tryl[160];
            snprintf(tryl, sizeof tryl, "%s%s%.*s", line, line[0] ? " " : "", n, t);
            if (line[0] && MeasureText(tryl, 10) > maxw) { DrawText(line, 80, y, 10, (Color){ 34, 22, 18, 255 }); y += 13; snprintf(line, sizeof line, "%.*s", n, t); }
            else snprintf(line, sizeof line, "%s", tryl);
            t += n;
            if (*t == '\n') { DrawText(line, 80, y, 10, (Color){ 34, 22, 18, 255 }); y += 13; line[0] = 0; }
            if (*t) t++;
        }
        if (line[0]) DrawText(line, 80, y, 10, (Color){ 34, 22, 18, 255 });
        DrawText("E", RT_W - 82, RT_H - 50, 10, (Color){ 90, 70, 60, 255 });
    }
    if (msgT > 0) text_c(msg, RT_H - 34, 10, (Color){ 210, 200, 180, (unsigned char)(fminf(1.0f, msgT) * 255) });
    if (L.sludge && L.sludgeArmed) {
        float gap = P.pos.y - L.sludgeY;
        if (gap < 10) text_c("IT IS RISING", 22, 10, (Color){ 200, 60, 40, (unsigned char)(120 + 100 * sinf(time * 6)) });
    }
}

static const char *END_LINES[] = {
    "you lie down.",
    "the candles are all lit now.",
    "they are kneeling around the bed. all of them.",
    "someone is holding your hand. it is very cold.",
    "a voice you know says: he is sleeping. don't wake him.",
    "",
    "so you don't.",
    "",
    "NEMA",
};

static void use_thing(Use *u) {
    u->done = true;
    if (u->kind == USE_LILY) {   // you were told not to pick anything
        eyesOpenT = 4.5f;
        audio_play_ex(SFX_SWELL, 0.6f, 1.1f);
        int got = 0, all = 0;
        for (int i = 0; i < L.uses.size; i++) if (L.uses.data[i].kind == USE_LILY) { all++; got += L.uses.data[i].done; }
        if (got < all) say("every flower opens its eyes.", 3);
        else {
            for (int i = 0; i < L.pickups.size; i++) L.pickups.data[i].locked = false;
            audio_play_ex(SFX_BELL, 0.5f, 0.9f);
            say("every flower opens its eyes. up on the mound, something comes loose.", 5);
        }
    }
    if (u->kind == USE_CANDLE) {
        audio_play_ex(SFX_GRAB, 0.4f, 0.6f); play_from(SFX_PRAYER, u->pos, 0.4f, 0.9f);
        P.noise = 1.0f;   // the match is loud. it heard that
        int lit = 0, all = 0;
        for (int i = 0; i < L.uses.size; i++) if (L.uses.data[i].kind == USE_CANDLE) { all++; lit += L.uses.data[i].done; }
        if (lit < all) { char b[64]; snprintf(b, sizeof b, "%d of %d.", lit, all); say(b, 3); }
        else {
            for (int i = 0; i < L.pickups.size; i++) L.pickups.data[i].locked = false;
            audio_play_ex(SFX_BELL, 0.5f, 1.0f); audio_play_ex(SFX_SCRAPE, 0.5f, 0.8f);
            say("far off, iron scrapes on stone. the grate is open.", 5);
        }
    }
}

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
            freezeT -= frameDt; frozen = true; frameDt = 0; glitch = 0.35f;
            audio_set(1, 0, 0, 0.0f);
            if (freezeT <= 0) { audio_play(SFX_KNOCK); glitch = 0.5f; }
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
                if (stepDist > 2.0f) { stepDist = 0; audio_play_ex(SFX_STEP, 0.15f + 0.4f * P.noise, 0.85f + 0.2f * (GetRandomValue(0, 100) / 100.0f)); }
            }

            if (!tr.on) {
                // things you can use: whichever one you are looking at, close enough to touch
                Use *near = NULL;
                {
                    Vector3 eye = player_eye(&P), fwd = player_forward(&P);
                    float best = 0.8f;
                    for (int i = 0; i < L.uses.size; i++) {
                        Use *u = &L.uses.data[i];
                        if (u->done && u->kind != USE_NOTE) continue;
                        Vector3 d = Vector3Subtract(u->pos, eye);
                        float len = Vector3Length(d);
                        if (len > 2.2f) continue;
                        float dot = Vector3DotProduct(Vector3Scale(d, 1.0f / len), fwd);
                        if (dot > best) { best = dot; near = u; }
                    }
                }
                useHint = near ? (near->kind == USE_NOTE ? "E  read" : near->kind == USE_LILY ? "E  pick it" : "E  light it") : NULL;
                if (reading >= 0 && (IsKeyPressed(KEY_E) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || P.speedMeter > 4.5f)) reading = -1;
                else if (near && IsKeyPressed(KEY_E)) {
                    if (near->kind == USE_NOTE) { reading = near->arg; near->done = true; audio_play_ex(SFX_CREAK, 0.25f, 1.6f); }
                    else use_thing(near);
                }
                // pickups
                for (int i = 0; i < L.pickups.size; i++) {
                    Pickup *pk = &L.pickups.data[i];
                    if (!pk->taken && pk->locked && Vector3Distance(Vector3Add(P.pos, (Vector3){ 0, 1, 0 }), pk->pos) < 1.6f) {
                        if (msgT < 0.5f) say(L.id == W_GARDEN ? "it is caged in. somewhere out there, three white lilies." : "there is a grate over it. somewhere, three black candles.", 3);
                        continue;
                    }
                    if (!pk->taken && Vector3Distance(Vector3Add(P.pos, (Vector3){ 0, 1, 0 }), pk->pos) < 1.3f) {
                        pk->taken = true;
                        P.fx |= 1u << pk->fx;
                        flash = 0.0f;
                        char b[160]; snprintf(b, sizeof b, "EFFECT: %s - %s  (R to wake up)", FX_NAME[pk->fx], FX_DESC[pk->fx]);
                        say(b, 10);
                        audio_play(SFX_PICKUP);
                        if (pk->fx == FX_LAMP) lampOn = 1;
                        flash = 0.0f;
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
                if (level_watchers(&L, player_eye(&P), fwd, P.pos, frameDt, P.noise)) {
                    dead = true; say("it heard you.", 4);
                    audio_play_ex(SFX_CLICK, 0.9f, 0.8f); haunt_title();
                }
                if (L.sawWatcher) { L.sawWatcher = false; audio_play_ex(SFX_SWELL, 0.35f, 0.9f); madness = fminf(1, madness + 0.2f); }
                if (L.id == W_DRAINS) {   // you hear them before you see them: wet clicking, faster when they have heard you
                    static float clickT[4];
                    for (int i = 0; i < L.watchers.size && i < 4; i++) {
                        Watcher *w = &L.watchers.data[i];
                        clickT[i] -= frameDt;
                        if (clickT[i] <= 0) {
                            clickT[i] = w->state == 2 ? frand_(0.5f, 0.9f) : w->state == 1 ? frand_(1.0f, 1.8f) : frand_(2.0f, 4.0f);
                            play_from(SFX_CLICK, w->pos, 0.9f, frand_(0.8f, 1.05f));
                            if (w->state > 0 && GetRandomValue(0, 2) == 0) play_from(SFX_THUD, w->pos, 0.4f, frand_(0.9f, 1.1f));
                        }
                    }
                }
                if (P.slipped) { P.slipped = false; audio_play(SFX_KNOCK); }
                if (update_climber(player_eye(&P), fwd)) { dead = true; say("it had your hands.", 4); audio_play_ex(SFX_BREATH, 0.7f, 0.8f); }
                scare_update(frameDt);
                if (gardenCaught) { gardenCaught = false; dead = true; audio_play(SFX_BREATH); }
                if (dead) go(W_HUB, true);
            }

            // transition
            if (tr.on) {
                tr.t += frameDt;
                if (tr.t >= 0.6f && !tr.loaded) { load_world(tr.to, tr.wake); tr.loaded = true; }
                if (tr.t >= 1.3f) tr.on = false;
            }

            if (L.sky.eye && !tr.on && update_gaze(frameDt)) go(W_HUB, true);
            if (eyesOpenT > 0) eyesOpenT -= frameDt;
            // mood
            float m = 0.04f;
            if (gaze.phase == 2) m += 0.25f + gaze.noticed * 0.7f;
            if (P.gripping) m += (1.0f - P.grip) * 0.6f;
            if (P.grip < 0.25f) m += (0.25f - P.grip) * 2.0f;
            if (L.sludge && L.sludgeArmed) { float g = P.pos.y - L.sludgeY; if (g < 9) m += (9 - g) / 9.0f * 0.6f; }
            for (int i = 0; i < L.watchers.size; i++) {
                float d = Vector3Distance(L.watchers.data[i].pos, P.pos);
                if (d < 14) m += (14 - d) / 14.0f * 0.5f;
            }
            if (climber.on) { float d = Vector3Distance(climber.pos, P.pos); if (d < 12) m += (12 - d) / 12.0f * 0.6f; }
            if (m > 1) m = 1;
            madness += (m - madness) * fminf(1, frameDt * 3);
            tension = madness;
            float tone = L.id == W_HUB ? 1.0f : L.id == W_SHAFT ? 0.75f : L.id == W_DRAINS ? 0.9f : L.id == W_VOID ? 1.4f : L.id == W_GARDEN ? 1.15f : 2.0f;
            float mus = L.id == W_GARDEN ? 0.8f : L.id == W_VOID ? 0.5f : L.id == W_END ? 0.8f : (L.id == W_HUB && dreams >= 4) ? 0.25f : 0.0f;
            audio_music(frozen ? 0.0f : mus, fminf(1.0f, madness * 0.9f + (blackout > 0 ? 0.4f : 0.0f)));
            float whisper = L.nearest < 18 ? 1.0f - L.nearest / 18.0f : 0.0f;
            if (!frozen) audio_set(tone, tension, whisper, tr.on ? 0.3f : 0.9f);
            {   // the mass behind the walls gets louder the deeper you go; something breathes when one of them is close
                float choir = L.id == W_HUB ? fminf(1.0f, dreams * 0.12f) : L.id == W_END ? 1.0f : 0.35f;
                float breath = 0, bpan = 0, bd = 1e9f;
                for (int i = 0; i < L.watchers.size; i++) {
                    float d = Vector3Distance(L.watchers.data[i].pos, P.pos);
                    if (d < bd) { bd = d; bpan = pan_of(L.watchers.data[i].pos); }
                }
                if (climber.on) { float d = Vector3Distance(climber.pos, P.pos); if (d < bd) { bd = d; bpan = pan_of(climber.pos); } }
                if (bd < 7) breath = 1.0f - bd / 7.0f;
                audio_atmos(frozen ? 0.0f : choir, breath, bpan);
            }
        } else {
            endT += frameDt;
            if (IsKeyPressed(KEY_ENTER) && endT > 17) { if (levelLoaded) { level_free(&L); levelLoaded = false; } state = S_TITLE; }
        }
        if (nameT > 0) nameT -= frameDt;
        if (msgT > 0) msgT -= frameDt;
        if (flash > 0) flash = fmaxf(0, flash - frameDt * 1.2f);
        if (glitch > 0) glitch = fmaxf(0, glitch - frameDt * 2.5f);
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
            text_c(g_launches >= 3 ? "do not wake him" : "a descent into other people's dreams", 128, 10, (Color){ 120, 110, 95, 255 });
            if ((int)(time * 1.5f) % 2) text_c("press ENTER or click", 190, 10, (Color){ 170, 160, 140, 255 });
            const char *memo = g_launches >= 6 ? "it kept your place." : g_launches >= 2 ? "you came back." : "";
            if (msgT > 0) text_c(msg, 172, 10, (Color){ 210, 200, 180, (unsigned char)(fminf(1.0f, msgT) * 255) });
            if (memo[0]) text_c(memo, 156, 10, (Color){ 130, 40, 34, (unsigned char)(150 + 60 * sinf(time * 2.0f)) });
            text_c("WASD · SHIFT run · CTRL kneel · E use · hold LMB to grip · F lamp · R wake up · [ ] mouse", RT_H - 18, 10, (Color){ 90, 85, 75, 255 });
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
                    float a = fminf(1.0f, fmaxf(0.0f, (endT - 2.0f - i * 1.7f) * 0.6f));
                    Color lc = i == n - 1 ? (Color){ 150, 24, 20, 255 } : (Color){ 220, 215, 200, 255 };
                    lc.a = (unsigned char)(a * 255);
                    if (a > 0 && END_LINES[i][0]) text_c(END_LINES[i], 54 + i * 16, 10, lc);
                }
                if (endT > 17) text_c("ENTER", RT_H - 24, 10, (Color){ 120, 115, 105, 255 });
            }
        }
        EndTextureMode();
        {
            Color lo = { 128, 128, 128, 255 }, hi = lo;
            if (state == S_TITLE || !levelLoaded) { lo = (Color){ 118, 128, 136, 255 }; hi = (Color){ 140, 130, 120, 255 }; }
            else if (L.gradeLo.a) { lo = L.gradeLo; hi = L.gradeHi; }
            gfx_grade(lo, hi);
        }
        gfx_present(time, state == S_PLAY ? madness : 0.0f, fade, flash, glitch);

        if (shotWorld >= 0 && ++shotFrame >= shotFrames) { TakeScreenshot(shotPath); quit = true; }
}

int main(void) {
    // the screenshot hook renders without ever showing a window or making a sound
    bool shot = getenv("MURK_SHOT") != NULL;
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | (shot ? FLAG_WINDOW_HIDDEN : 0));
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
    if (!shot) audio_init();

    // dev hook: MURK_SHOT="world,x,y,z,yaw,pitch,fx,frames,path" renders a frame and exits
    float sx = 0, sy = 0, sz = 0, syaw = 0, spit = 0; int sfxmask = 0;
    const char *env = getenv("MURK_SHOT");
    if (env && !strncmp(env, "title", 5)) { shotWorld = 99; shotFrames = 30; snprintf(shotPath, sizeof shotPath, "%s", env + 6); }
    else if (env && sscanf(env, "%d,%f,%f,%f,%f,%f,%d,%d,%255s", &shotWorld, &sx, &sy, &sz, &syaw, &spit, &sfxmask, &shotFrames, shotPath) == 9) {
        new_game();
        if (getenv("MURK_DREAMS")) dreams = atoi(getenv("MURK_DREAMS"));   // how deep the shot is taken
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
