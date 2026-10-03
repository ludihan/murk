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
    [NOTE_SLEEPER] = "You keep coming down here. You keep looking for the way out.\nThere is no way out for you. You are what he is dreaming, and we need him to keep dreaming.\nGo up and look at the bed.",
    [NOTE_WARD] = "ROOM 6.\nPatient has not woken in eleven years. Vital signs unremarkable.\nMother visits Sundays and will not leave when asked. She says he is dreaming of a house.\nShe says she can hear it through the wall.",
    [NOTE_STATIC] = "If you see the grey man, walk away from him. Don't run, there is no need.\nHe doesn't hurt anyone. He only wants to stand where you are standing.\nIf he gets there, you will forget something you went a long way to find.",
    [NOTE_WOMB] = "He is not asleep in the house. He was never in the house.\nHe is down here, where it is warm, and the house is what he dreams so that he will not be afraid.\nIf you go on, do not wake him. If you wake him, there will be nothing for you to go back to.",
    [NOTE_CITY] = "Nobody lives here any more. They leave the lights on anyway.\nIf you see the tall one at the end of a street, don't walk toward him, and don't stare.\nHe is never there when you look again. He is only nearer.",
    [NOTE_BELL] = "When the bell tolls, kneel with the others.\nThe priest counts the heads.\nHe must not count one that is standing.",
};
static int reading = -1;   // the note on screen, or -1
static const char *useHint;
// the shaft: something on the wall below you that climbs when you climb, and only while you aren't looking at it
static struct { bool on, seen; float y, lastPY, scrapeT; Vector3 pos, n; } climber;
static float radioT, wardFollow;   // the ward's radio, and how far along the thing that follows you is
static struct { int phase; float t, noticed, lift; bool warned; } gaze;
// the lower church: three tolls, then everyone kneels and he counts them
static struct { int phase, tolls; float t, tollT, stare; bool warned; } mass;
// the city: a very tall man at the far end of a street, never moving. look away and he is gone; the next time
// he is nearer. the last time he is behind you
static struct { bool on, seen, last; float t, seenT, life, dist; Vector3 pos; } tallm;
static float ringT;
// the dinner: the host who moves along the table when you aren't looking, and the chair that was kept for you
static struct { float sitT; Vector3 seat; Vector3 host; float hostStride; bool hostSeen; } dinner;
// the static sea's grey man
static struct { bool on; float t; Vector3 pos; float stride; } grey;


static void use_thing(Use *u);

static const char *FX_NAME[FX_COUNT] = { "LAMP", "GLOVES", "BOOTS", "FEATHER", "VEIL" };
static const char *FX_DESC[FX_COUNT] = {
    "press F to toggle it. the dark pulls back a little.",
    "your grip drains slower. you climb faster.",
    "you can jump once more in the air.",
    "hold SPACE in the air. you fall like something that was never heavy.",
    "kneel, and the things that look for you look past you.",
};

// the game keeps a tiny file about you
static void save_memory(void) {
#ifdef __EMSCRIPTEN__
    web_save_int("murk.launches", g_launches); web_save_int("murk.wakes", g_wakes); web_save_int("murk.sens", (int)(sens * 10000)); web_save_int("murk.s", g_secret);
#else
    FILE *f = fopen("murk.sav", "w");
    if (f) { fprintf(f, "%d %d %d %d\n", g_launches, g_wakes, (int)(sens * 10000), g_secret); fclose(f); }
#endif
}
static void load_memory(void) {
#ifdef __EMSCRIPTEN__
    g_launches = web_load_int("murk.launches"); g_wakes = web_load_int("murk.wakes");
    { int sv = web_load_int("murk.sens"); if (sv >= 100 && sv <= 5000) sens = sv / 10000.0f; }
    g_secret = web_load_int("murk.s") != 0;
#else
    FILE *f = fopen("murk.sav", "r");
    if (f) {
        int sv = 0, n = fscanf(f, "%d %d %d %d", &g_launches, &g_wakes, &sv, &g_secret);
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
// in a dream that repeats, everything is drawn at whichever copy of it is nearest to you
static float wrapf(float d, float w) { return d - w * floorf(d / w + 0.5f); }
static Vector3 wp(Vector3 p, Vector3 eye) {
    if (L.wrap <= 0) return p;
    return (Vector3){ eye.x + wrapf(p.x - eye.x, L.wrap), p.y, eye.z + wrapf(p.z - eye.z, L.wrap) };
}


static void load_world(WorldId id, bool wake) {
    reading = -1; useHint = NULL;
    if (id == W_WARD) { g_wardLoop = 0; radioT = 3.0f; wardFollow = 0; }
    if (levelLoaded) {
        if (recN > 40 && L.id != W_SHAFT && L.id != W_END) { memcpy(ghostPath[L.id], rec, recN * sizeof *rec); ghostN[L.id] = recN; }
        level_free(&L);
    }
    recN = 0; recT = 0;
    memset(&climber, 0, sizeof climber);
    memset(&gaze, 0, sizeof gaze); gaze.t = frand_(22, 30);
    memset(&mass, 0, sizeof mass); mass.t = frand_(18, 24);
    memset(&grey, 0, sizeof grey); grey.t = frand_(40, 70);
    memset(&dinner, 0, sizeof dinner);
    memset(&tallm, 0, sizeof tallm); tallm.t = frand_(50, 80); tallm.dist = 46; ringT = frand_(20, 40);
    ghost.on = id != W_HUB && ghostN[id] > 40 && dreams >= 3 && GetRandomValue(0, 3) == 0; ghost.noticed = false; ghost.t = -frand_(30, 60);
    level_build(&L, id, ++dreams);
    levelLoaded = true;
    player_spawn(&P, &L);
    if (id == W_HUB && wake) {
        P.pos = L.wakePos;
        P.yaw = L.wakeYaw;
    }
    nameT = 3.5f;
    blackout = 0; nextBlackout = frand_(30, 50); phantomLeft = 0; phantomT = frand_(40, 80);
    memset(lurk, 0, sizeof lurk); lurkTimer = frand_(70, 130);
    nextEvent = frand_(50, 80);   // every dream starts quiet
    lampOn = (P.fx & (1u << FX_LAMP)) ? lampOn : 0;
    if (id == W_HUB && !wake && dreams <= 1) say("WASD walk · SHIFT run · CTRL kneel · E use · hold LMB at rusty walls to grip · R wake up", 12);
    if (id == W_SHAFT) say("hold LMB on the rusty plates. W climbs, A/D shuffle, SPACE lunges. don't let go.", 9);
    if (id == W_DRAINS) say("something down here is listening.", 7);
    if (id == W_STATIC) say("nothing is on.", 5);
    if (id == W_CITY) say("it is raining. every light is on, and nobody is home.", 6);
    if (id == W_WOMB) say("it is warm here. something is breathing all around you.", 6);
    if (id == W_DINNER) say("dinner is served. there is a place for you.", 6);
    if (id == W_WARD) say("the lights hum. the corridor goes on ahead of you.", 6);
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
    { "they know all the words.", "someone in the back is weeping.", "he has counted to the same number every week.", "your place in the pew is warm." },
    { "visiting hours are over.", "the radio is tuned to nothing.", "this corridor is longer than it was.", "don't stop walking." },
    { "nothing is on.", "it is so quiet you can hear the screens.", "the grey man doesn't mean any harm.", "every channel is the same channel." },
    { "they have been waiting for you to sit down.", "nobody has touched their food.", "the candles never burn down.", "it is rude to leave the table." },
    { "it is warm here.", "something is breathing all around you.", "this is where he is.", "you were born somewhere like this." },
    { "every window is somebody's.", "the rain doesn't touch you.", "don't look down the long streets.", "he is taller every time." },
    { "stay.", "stay.", "stay.", "stay." },
};
// something happens every half minute or so, so no dream ever just sits there
static void director(float dt) {
    if (tr.on || L.id == W_END) return;
    nextEvent -= dt;
    if (nextEvent > 0) return;
    nextEvent = frand_(70, 120);
    int kind = GetRandomValue(0, 2);
    if (kind == 0 || L.id != W_GARDEN || L.t < 60) {
        if (GetRandomValue(0, 2) == 0) say(WHISPERS[L.id][GetRandomValue(0, 3)], 4.0f);
        static const Sfx AMB[] = { SFX_KNOCK, SFX_BELL, SFX_CHANT, SFX_PRAYER, SFX_HUM, SFX_CREAK, SFX_SCRAPE };
        Sfx a = AMB[GetRandomValue(0, 6)];
        play_behind(a, a == SFX_HUM || a == SFX_PRAYER ? 0.35f : 0.5f, frand_(0.85f, 1.0f));
    } else if (L.id == W_GARDEN) {
        eyesOpenT = 5.0f; audio_play(SFX_SWELL);
        say("every flower opens its eyes.", 4.0f);
    }
}

static bool update_climber(Vector3 eye, Vector3 fwd) {
    if (L.id != W_SHAFT) return false;
    if (!climber.on) {
        if (!L.sludgeArmed || P.pos.y < 12.0f) { climber.lastPY = P.pos.y; return false; }   // a quarter of the way up, far below, something starts
        climber.on = true; climber.y = P.pos.y - 14.0f; climber.lastPY = P.pos.y;
        play_behind(SFX_SCRAPE, 0.4f, 0.7f);
        say("far below you, something has started to climb.", 4);
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
        bool moving = (P.speedMeter > 0.6f || !P.grounded || P.gripping) && !((P.fx & (1u << FX_VEIL)) && P.crouch > 0.5f);
        if (!covered && moving) gaze.noticed += dt * (P.crouch > 0.5f ? 0.45f : 0.9f);
        else gaze.noticed = fmaxf(0, gaze.noticed - dt * 0.25f);
        if (gaze.noticed >= 1.0f) { gaze.lift = 0.001f; say("it saw you.", 4); audio_play_ex(SFX_CHANT, 0.8f, 0.7f); }
    } else gaze.noticed = fmaxf(0, gaze.noticed - dt * 0.4f);
    return false;
}

// the orchard: the flowers are its eyes and the gardeners are its hands. the gardeners are blind; a flower
// that sees you standing calls them to where you were. in the long grass, kneeling, the flowers can't see you
static bool flower_open(int i, float t) { return eyesOpenT > 0 || (t > 25 && sinf(t * 0.4f + i * 3.1f) > 0.2f); }   // they wake up a while after you arrive
static bool flower_has_eye(const Bloom *b, int i) { return b->yaw <= 0.5f && i % 4 == 0; }
static void garden_update(float dt, Vector3 eye) {
    static float spotted;
    bool seen = false; Vector3 from = { 0 };
    bool veil = P.fx & (1u << FX_VEIL);
    float range = P.crouch > 0.5f ? (P.speedMeter > 0.5f && !veil ? 3.5f : 0.0f) : 11.0f;
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

static const Effigy *priest_of(void) {
    for (int i = 0; i < L.effigies.size; i++) if (L.effigies.data[i].kind == FIG_PRIEST) return &L.effigies.data[i];
    return NULL;
}
static bool veil_taken(void) {
    for (int i = 0; i < L.pickups.size; i++) if (L.pickups.data[i].fx == FX_VEIL) return L.pickups.data[i].taken;
    return false;
}
static bool update_mass(float dt, Vector3 eye) {
    if (L.id != W_CHAPEL) return false;
    if (veil_taken()) {   // they all know. the bell does not stop
        mass.tollT -= dt;
        if (mass.tollT <= 0) { mass.tollT = 2.2f; audio_play_ex(SFX_BELL, 0.7f, 0.94f); }
        mass.phase = 0;
        return false;
    }
    mass.t -= dt;
    if (mass.phase == 0 && mass.t <= 0) { mass.phase = 1; mass.tolls = 3; mass.tollT = 0; }
    else if (mass.phase == 1) {
        mass.tollT -= dt;
        if (mass.tollT <= 0 && mass.tolls > 0) {
            audio_play_ex(SFX_BELL, 0.9f, 1.0f); mass.tollT = 1.4f;
            if (--mass.tolls == 0) mass.t = 1.4f;
        } else if (mass.tolls == 0 && mass.tollT <= 0) {
            mass.phase = 2; mass.t = frand_(5.0f, 6.5f);
            audio_play_ex(SFX_CHANT, 0.7f, 0.9f);
            if (!mass.warned) { mass.warned = true; say("they kneel. he is counting.", 3); }
        }
    } else if (mass.phase == 2) {
        const Effigy *pr = priest_of();
        bool seen = false;
        if (pr) {
            Vector3 head = { pr->pos.x, pr->pos.y + 2.2f, pr->pos.z + 0.3f };
            seen = P.crouch < 0.5f && ray_clear(head, eye);
        }
        mass.stare = seen ? mass.stare + dt : fmaxf(0, mass.stare - dt);
        if (mass.stare > 0.6f) return true;
        if (mass.t <= 0) { mass.phase = 0; mass.t = frand_(9, 15) * fmaxf(0.6f, 1.0f - dreams * 0.03f); mass.stare = 0; audio_play_ex(SFX_BELL, 0.6f, 0.8f); }
    }
    return false;
}

// the ward: crossing into the second copy of the corridor puts you back at the start of the first, one lap on
static const char *RADIO[5][2] = {
    { "...visiting hours are from two until four...", "...would the family of the patient in room six..." },
    { "...room six has not woken...", "...his mother visits on Sundays..." },
    { "...eleven years...", "...she brings him flowers from the orchard. they don't die..." },
    { "...she is in the room with him now...", "...don't stop. it walks where you walked..." },
    { "...he has come down to us...", "...the door at the end is open..." },
};
static bool ward_update(float dt, Vector3 eye) {
    if (L.id != W_WARD) return false;
    if (P.pos.z < -30.0f && P.pos.x > 4.0f) {   // another lap
        g_wardLoop++;
        level_free(&L);
        level_build(&L, W_WARD, dreams);
        P.pos.x -= 8.0f; P.pos.z += 36.0f;
        recN = 0; wardFollow = 0; radioT = 2.0f;
        if (g_wardLoop >= 3) play_behind(SFX_KNOCK, 0.4f, 0.8f);
    }
    int k = g_wardLoop > 4 ? 4 : g_wardLoop;
    radioT -= dt;
    if (radioT <= 0) {
        radioT = frand_(18, 30);
        play_from(SFX_PRAYER, (Vector3){ 0.75f, 0.9f, -3.4f }, 0.45f, 0.7f);
        say(RADIO[k][GetRandomValue(0, 1)], 4);
    }
    // the one at the far end is gone before you reach it
    for (int i = 0; i < L.effigies.size; i++) {
        Effigy *e = &L.effigies.data[i];
        if (e->kind == FIG_PENITENT && e->pos.y > -10 && Vector3Distance(e->pos, P.pos) < 7.5f) {
            e->pos.y = -100; blackout = 0.7f; play_behind(SFX_BREATH, 0.4f, 0.9f);
        }
    }
    // on the fourth lap something walks your path, six seconds behind you. stand still and it arrives
    if (g_wardLoop == 3 && recN > 24) {
        wardFollow = fminf(1.0f, wardFollow + dt * 0.5f);
        Vector3 fp = rec[recN - 24];
        if (Vector3Distance(fp, P.pos) < 0.9f && wardFollow >= 1.0f) return true;
    }
    (void)eye;
    return false;
}

// the static sea: the grey man appears far off and walks toward you, never fast. if he reaches you he takes
// one of the things you brought back, and you wake
static bool grey_update(float dt) {
    if (L.id != W_STATIC) return false;
    if (!grey.on) {
        grey.t -= dt;
        if (grey.t > 0) return false;
        float a = (P.yaw + 180 + frand_(-80, 80)) * DEG2RAD;
        grey.pos = (Vector3){ P.pos.x + sinf(a) * 26, 0, P.pos.z - cosf(a) * 26 };
        grey.on = true;
        return false;
    }
    Vector3 d = { wrapf(P.pos.x - grey.pos.x, L.wrap), 0, wrapf(P.pos.z - grey.pos.z, L.wrap) };
    float len = Vector3Length(d);
    if (len > 45) { grey.on = false; grey.t = frand_(30, 50); return false; }   // you lost him. he will find you again
    float step = 1.15f * dt;
    if (len > 0.01f) { grey.pos = Vector3Add(grey.pos, Vector3Scale(d, step / len)); grey.stride += step * 3.0f; }
    grey.pos.x = wrapf(grey.pos.x, L.wrap); grey.pos.z = wrapf(grey.pos.z, L.wrap);
    if (len < 1.1f) {
        int have[FX_COUNT], n = 0;
        for (int f = 0; f < FX_COUNT; f++) if (P.fx & (1u << f)) have[n++] = f;
        if (n) { P.fx &= ~(1u << have[GetRandomValue(0, n - 1)]); if (!(P.fx & (1u << FX_LAMP))) lampOn = 0; }
        return true;
    }
    return false;
}

static bool dinner_update(float dt, Vector3 eye, Vector3 fwd) {
    if (L.id != W_DINNER) return false;
    if (dinner.sitT > 0) {   // every head at the table turns to you. then the meal begins
        float before = dinner.sitT;
        dinner.sitT += dt;
        P.crouch = 0.55f;
        for (int i = 0; i < L.effigies.size; i++) L.effigies.data[i].look = fminf(1.0f, dinner.sitT * 0.6f);
        if (before < 1.6f && dinner.sitT >= 1.6f) audio_play_ex(SFX_BELL, 0.8f, 1.0f);
        if (before < 3.2f && dinner.sitT >= 3.2f) { say("the meal can begin.", 4); audio_play_ex(SFX_CHANT, 0.7f, 0.9f); }
        if (before < 5.0f && dinner.sitT >= 5.0f) go(W_WOMB, false);
        return false;
    }
    // the host walks the length of the table toward you, but only while you aren't looking at him
    if (dinner.host.y == 0 && dinner.host.x == 0) {   // he comes in once you have been at the table a while, from far down it
        if (L.t < 35) return false;
        dinner.host = (Vector3){ -3.0f, 0.001f, wrapf(P.pos.z + 30.0f, L.wrap) };
    }
    Vector3 hp = wp(dinner.host, eye);
    bool seen = level_seen(&L, eye, fwd, hp);
    if (seen && !dinner.hostSeen) audio_play_ex(SFX_SWELL, 0.3f, 0.8f);
    dinner.hostSeen = seen;
    Vector3 d = { P.pos.x - hp.x, 0, P.pos.z - hp.z };
    float len = Vector3Length(d);
    if (!seen && len > 0.01f) {
        float step = fminf(len, (1.6f + 0.08f * (dreams > 8 ? 8 : dreams)) * dt);
        dinner.host.x += d.x / len * step; dinner.host.z += d.z / len * step;
        dinner.host.x = wrapf(dinner.host.x, L.wrap); dinner.host.z = wrapf(dinner.host.z, L.wrap);
        dinner.hostStride += step * 3.0f;
    }
    return len < 1.0f;
}

static bool city_update(float dt, Vector3 eye, Vector3 fwd) {
    if (L.id != W_CITY) return false;
    ringT -= dt;
    if (ringT <= 0) { ringT = frand_(35, 70); if (Vector3Distance(wp(L.bed, eye), P.pos) < 45) play_from(SFX_RING, wp(L.bed, eye), 0.8f, 1.0f); }
    for (int i = 0; i < L.effigies.size; i++) {   // the people in the street are never there when you arrive
        Effigy *e = &L.effigies.data[i];
        if (e->pos.y > -10 && Vector3Distance(wp(e->pos, eye), P.pos) < 11.0f) { e->pos.y = -100; play_behind(SFX_STEP, 0.3f, 0.7f); }
    }
    if (!tallm.on) {
        tallm.t -= dt;
        if (tallm.t > 0) return false;
        if (tallm.last) {   // this time, behind you
            Vector3 f = fwd; f.y = 0; f = Vector3Normalize(f);
            tallm.pos = (Vector3){ P.pos.x - f.x * 2.4f, 0, P.pos.z - f.z * 2.4f };
            tallm.on = true; tallm.seen = false; tallm.seenT = 0; tallm.life = 14;
            play_behind(SFX_BREATH, 0.5f, 0.6f);
            return false;
        }
        float bestErr = 1e9f; Vector3 best = { 0 };
        float sx = roundf(P.pos.x / 24.0f) * 24.0f, sz = roundf(P.pos.z / 24.0f) * 24.0f;
        for (int ix = -2; ix <= 2; ix++) for (int iz = -2; iz <= 2; iz++) {   // street corners, down a street from you
            Vector3 c = { sx + ix * 24.0f, 0, sz + iz * 24.0f };
            if (ix != 0 && iz != 0 && fabsf(c.x - P.pos.x) > 3 && fabsf(c.z - P.pos.z) > 3) continue;
            float d = Vector3Distance(c, P.pos), err = fabsf(d - tallm.dist);
            Vector3 to = Vector3Normalize(Vector3Subtract(c, eye));
            if (Vector3DotProduct(to, fwd) > 0.5f || err > 14 || !ray_clear(eye, (Vector3){ c.x, 3.0f, c.z })) continue;
            if (err < bestErr) { bestErr = err; best = c; }
        }
        if (bestErr > 1e8f) { tallm.t = 2; return false; }
        tallm.pos = best; tallm.on = true; tallm.seen = false; tallm.seenT = 0; tallm.life = 30;
        return false;
    }
    Vector3 hp = wp(tallm.pos, eye), head = { hp.x, 4.0f, hp.z };
    Vector3 to = Vector3Subtract(head, eye);
    float d = Vector3Length(to);
    bool visible = Vector3DotProduct(Vector3Scale(to, 1.0f / d), fwd) > 0.7f && ray_clear(eye, head);
    tallm.life -= dt;
    if (visible) {
        tallm.seenT += dt;
        madness = fminf(1.0f, madness + dt * 0.5f);
        if (!tallm.seen) { tallm.seen = true; audio_play_ex(SFX_SWELL, 0.45f, 0.6f); }
        if (tallm.last && tallm.seenT > 0.7f) return true;
    }
    if ((tallm.seen && (!visible || tallm.seenT > 4.0f)) || tallm.life <= 0) {
        tallm.on = false;
        if (tallm.seen) {
            tallm.dist *= 0.6f;
            if (tallm.dist < 10 && !tallm.last) { tallm.last = true; tallm.t = frand_(18, 28); }
            else if (tallm.last) { tallm.last = false; tallm.dist = 40; tallm.t = frand_(40, 60); }   // you didn't turn round in time to see him. he will start again
            else tallm.t = frand_(22, 40);
        } else tallm.t = frand_(10, 18);
    }
    return false;
}

static void scare_update(float dt) {
    if (L.id == W_HUB) { blackout = 0; L.nearest = 99; return; }   // nothing follows you into the house
    Vector3 eye = player_eye(&P), fwd = player_forward(&P);
    // ---- the game hitches: everything stops and the sound drops out, then it all lurches back
    if (dreams >= 4 && !tr.on) {
        nextFreeze -= dt;
        if (nextFreeze <= 0) { freezeT = frand_(0.5f, 1.1f); nextFreeze = frand_(150, 300); haunt_title(); }
    }
    // ---- phantom footsteps and knocks while you stand still in a haunted place
    if (dreams >= 2 && L.id != W_END && P.speedMeter < 0.5f && !tr.on) {
        phantomT -= dt;
        if (phantomT <= 0) {
            phantomT = frand_(40, 80);
            if (GetRandomValue(0, 2) == 0) play_behind(GetRandomValue(0, 1) ? SFX_KNOCK : SFX_CREAK, 0.4f, frand_(0.8f, 1.0f));
            else { phantomLeft = GetRandomValue(3, 6); stepGap = 0; }
        }
    }
    if (phantomLeft > 0) {
        stepGap -= dt;
        if (stepGap <= 0) { play_behind(SFX_STEP, 0.3f, 0.6f); stepGap = 0.62f; phantomLeft--; }
    }
    // ---- blackouts: the lights just stop. in the drains, things keep walking.
    bool haunted = L.id == W_DRAINS;
    if (blackout > 0) {
        blackout -= dt;
    } else if (haunted && !tr.on) {
        nextBlackout -= dt;
        if (nextBlackout <= 0) {
            blackout = frand_(0.9f, 2.0f);
            nextBlackout = fmaxf(30.0f, 60.0f - dreams * 2.0f) * frand_(0.7f, 1.3f);
            audio_play(SFX_KNOCK);
        }
    }
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
        bool anyLurk = lurk[0].on || lurk[1].on || lurk[2].on;
        if (lurkTimer <= 0 && !tr.on && !anyLurk && dreams >= 3) {
            lurkTimer = frand_(120, 220);
            for (int i = 0; i < 3; i++) if (!lurk[i].on) {
                for (int tries = 0; tries < 8; tries++) {
                    float a = (P.yaw + (GetRandomValue(0, 1) ? 1 : -1) * frand_(35, 120)) * DEG2RAD;
                    float d = frand_(14, 22);
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
    return f == FX_LAMP ? (Color){ 255, 225, 120, 255 } : f == FX_GLOVES ? (Color){ 255, 140, 60, 255 } : f == FX_BOOTS ? (Color){ 190, 140, 255, 255 } : f == FX_FEATHER ? (Color){ 255, 150, 220, 255 } : (Color){ 200, 30, 30, 255 };
}

static Color scale_col(Color c, float k) {
    return (Color){ (unsigned char)fminf(255, c.r * k), (unsigned char)fminf(255, c.g * k), (unsigned char)fminf(255, c.b * k), 255 };
}

static void draw_scene(Camera3D cam, float time) {
    float dens = L.fogDensity * (lampOn > 0.5f ? 0.55f : 1.0f);
    float light = L.light * (L.id == W_HUB ? 1.0f : flicker(time, 0.15f)) * (lampOn > 0.5f ? 1.2f : 1.0f);
    float beat = 0;
    if (L.id == W_WOMB && L.heart.y > -50) {   // the walls swell with the heartbeat
        float hb = fmodf(time * 1.25f, 1.0f);
        beat = expf(-hb * 9.0f) + 0.6f * expf(-fabsf(hb - 0.24f) * 18.0f);
        light *= 0.8f + 0.3f * beat;
    }
    if (blackout > 0) { light *= 0.04f; dens *= 1.8f; }
    gfx_begin_scene(cam, L.fog, dens, light, time);
    gfx_sky(&L.sky, time, P.pos);
    float cull = 3.2f / dens + 6;
    Vector3 eye = cam.position;
    for (int i = 0; i < (int)L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (b->flags & (F_EMIT | F_SCREEN)) continue;
        Box wb = *b;
        if (L.wrap > 0 && b->h.x < L.wrap * 0.25f && b->h.z < L.wrap * 0.25f) wb.c = wp(b->c, eye);
        if (dist_to_box(eye, &wb) > cull) continue;
        if (b->flags & F_DECAL) gfx_decal(wb.c, b->h, (int)b->tex, b->scale);
        else gfx_box(wb.c, b->h, b->tex, b->tint, b->scale);
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
        Fig f = { (L.id == W_DRAINS || L.id == W_WOMB) ? FIG_CRAWLER : L.creepers ? FIG_GARDENER : FIG_PENITENT, w->pos,
                  atan2f(P.pos.x - w->pos.x, -(P.pos.z - w->pos.z)), w->stride, eye, 0.7f, sinf(i * 1.7f) * 0.35f, w->phase, { 0, 0, 1 }, { 0 }, 0 };
        if (w->state < 0) {   // asleep on the ceiling, spread flat
            f.kind = FIG_CLIMBER; f.pos.y = 3.12f; f.wallN = (Vector3){ 0, -1, 0 }; f.yaw = w->phase; f.stride = 0;
        }
        figure_draw(&f);
    }
    for (int i = 0; i < L.uses.size; i++) {
        Use wu = L.uses.data[i];
        const Use *u = &wu;
        wu.pos = wp(wu.pos, eye);
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
        Vector3 ep = wp(e->pos, eye);
        if (Vector3Distance(eye, ep) > cull + 2) continue;
        Fig f = { (FigKind)e->kind, ep, e->yaw, 0, eye, e->look * 0.9f, e->tilt, time + i, { 0, 0, 1 }, { 0 }, 0 };
        if (L.id == W_CHAPEL) {   // the congregation stands, and kneels on the third bell; the priest only looks up to count
            bool taken = veil_taken();
            if (f.kind != FIG_PRIEST) { f.kind = mass.phase == 2 ? FIG_KNEELER : FIG_PENITENT; f.look = taken ? 1.0f : e->look * 0.5f; }
            else f.look = (mass.phase == 2 || taken) ? 1.0f : 0.0f;
        }
        figure_draw(&f);
    }
    if (L.id == W_WARD && g_wardLoop == 3 && recN > 24) {
        Vector3 fp = rec[recN - 24], nx = rec[recN - 23];
        Fig f = { FIG_PENITENT, Vector3Lerp(fp, nx, recT / 0.25f), atan2f(nx.x - fp.x, -(nx.z - fp.z)), (float)recN, eye, 0.6f, 0.3f, time, { 0, 0, 1 }, (Color){ 20, 18, 18, 255 }, 0 };
        figure_draw(&f);
    }
    if (L.id == W_DINNER && dinner.sitT <= 0 && (dinner.host.x != 0 || dinner.host.y != 0)) {   // the host: very tall, and his hands are wet
        Vector3 hp = wp(dinner.host, eye); hp.y = 0;
        Fig f = { FIG_GARDENER, hp, atan2f(eye.x - hp.x, -(eye.z - hp.z)), dinner.hostStride, eye, 1.0f, 0.2f, time, { 0, 0, 1 }, (Color){ 70, 30, 30, 255 }, 0 };
        figure_draw(&f);
    }
    if (L.id == W_CITY && tallm.on) {
        Vector3 hp = wp(tallm.pos, eye);
        Fig f = { FIG_TALL, hp, atan2f(eye.x - hp.x, -(eye.z - hp.z)), 0, eye, 0.0f, 0.15f, time, { 0, 0, 1 }, { 0 }, 2.7f };
        figure_draw(&f);
    }
    if (grey.on) {   // a grey man in a grey coat, walking toward you. he isn't looking at you
        Vector3 gp = wp(grey.pos, eye);
        Fig f = { FIG_PENITENT, gp, atan2f(eye.x - gp.x, -(eye.z - gp.z)), grey.stride, eye, 0.0f, 0.0f, time, { 0, 0, 1 }, (Color){ 104, 104, 104, 255 }, 0 };
        figure_draw(&f);
    }
    if (climber.on) {
        Fig f = { FIG_CLIMBER, climber.pos, 0, climber.y * 3.0f, eye, 0.8f, 0.6f, time, climber.n, { 0 }, 0 };
        figure_draw(&f);
    }
    if (ghost.on && ghost.t >= 0) {
        Fig f = { FIG_PENITENT, ghost.pos, ghost.yaw, ghost.t * 3, eye, ghost.noticed ? 1.0f : 0.0f, 0.0f, time, { 0, 0, 1 }, (Color){ 18, 16, 18, 255 }, 0 };
        figure_draw(&f);
    }
    // lurkers: someone standing very still at the edge of the fog
    for (int i = 0; i < 3; i++) {
        const Lurker *k = &lurk[i];
        if (!k->on) continue;
        Fig f = { FIG_PENITENT, { k->pos.x, k->pos.y - 2.0f, k->pos.z }, atan2f(eye.x - k->pos.x, -(eye.z - k->pos.z)), 0, eye, 0.9f, 0.5f * sinf(i * 2.3f), time, { 0, 0, 1 }, (Color){ 12, 10, 12, 255 }, 0 };
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
    if (L.id == W_WOMB && L.heart.y > -50) {   // a heart the size of a room
        float s = 1.0f + 0.07f * beat;
        gfx_ellipsoid(L.heart, (Vector3){ 1.1f * s, 0, 0.2f }, (Vector3){ 0.1f, 1.4f * s, 0 }, (Vector3){ 0, 0.1f, 0.9f * s }, TEX_FLESH, (Color){ 200, 90, 96, 255 });
        for (int k = 0; k < 5; k++) {
            float a = k * 1.2566f;
            Vector3 o = { L.heart.x + sinf(a) * 0.7f, L.heart.y + 1.0f, L.heart.z + cosf(a) * 0.5f };
            gfx_limb(o, (Vector3){ o.x + sinf(a) * 0.6f, 3.2f, o.z + cosf(a) * 0.6f }, 0.12f * s, 0.18f, TEX_FLESH, (Color){ 150, 60, 80, 255 });
        }
    }
    if (L.sludge) gfx_slab(L.sludgeY, 6.0f, TEX_SLUDGE, (Color){ 140, 160, 90, 255 }, time * 12.0f);
    if (L.water) gfx_slab(L.waterY, L.waterHalf, TEX_WATER, (Color){ 50, 56, 64, 255 }, time * 3.0f);

    gfx_set_emit(true);
    for (int i = 0; i < L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (!(b->flags & F_EMIT)) continue;
        float k = (L.id == W_DRAINS && b->h.y < 0.2f) ? flicker(time + i, 0.4f) : 1.0f;
        gfx_glow(wp(b->c, eye), b->h, scale_col(b->tint, k));
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
        if (L.rain) gfx_glow(m->pos, (Vector3){ 0.005f, 0.16f, 0.005f }, scale_col(L.moteCol, 0.5f));
        else if (!L.moteGlow) gfx_glow(m->pos, (Vector3){ sz, sz, sz }, scale_col(L.moteCol, a));
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
        Vector3 up = wp(u->pos, eye);
        if (u->kind == USE_CANDLE && u->done) gfx_glow((Vector3){ up.x, up.y + 0.46f + sinf(time * 13 + i) * 0.006f, up.z }, (Vector3){ 0.02f, 0.05f, 0.02f }, (Color){ 255, 160, 70, 255 });
    }
    // soft glow around everything bright
    gfx_begin_glow();
    for (int i = 0; i < L.boxes.size; i++) {   // television pictures: the snow itself is the light
        const Box *b = &L.boxes.data[i];
        if (!(b->flags & F_SCREEN)) continue;
        Vector3 c = wp(b->c, eye);
        if (Vector3Distance(c, eye) > cull) continue;
        gfx_box(c, b->h, b->tex, b->tint, b->scale);
        gfx_halo(c, 1.6f, (Color){ 150, 160, 170, 255 }, 0.25f);
    }
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
        Box wb = *b; wb.c = wp(b->c, eye);
        if (dist_to_box(eye, &wb) > cull) continue;
        float k = (L.id == W_DRAINS && b->h.y < 0.2f) ? flicker(time + i, 0.4f) : 1.0f;
        Vector3 ho = Vector3Add(wb.c, Vector3Scale(Vector3Normalize(Vector3Subtract(eye, wb.c)), 0.45f));   // pull it off the wall it hangs on
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
        Fig f = { FIG_PENITENT, { 2.4f, 0, -25.2f }, 0, 0, cam.position, 1.0f, 0.3f, time, { 0, 0, 1 }, (Color){ 16, 14, 16, 255 }, 0 };
        if (g_launches >= 4) f.pos = (Vector3){ 1.1f, 0, -13.0f };
        figure_draw(&f);
    }
    if (g_launches >= 6) for (int i = 0; i < 4; i++) {
        Fig f = { FIG_KNEELER, { (i & 1) ? 1.9f : -1.9f, 0, -6.0f - i * 4.0f }, (i & 1) ? -1.5708f : 1.5708f, 0, cam.position, 0.0f, 0.1f, time + i, { 0, 0, 1 }, (Color){ 20, 18, 20, 255 }, 0 };
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
// once you have knelt in the circle, you know who is in the bed
static const char *END_LINES_KNOWN[] = {
    "you lie down beside him.",
    "he is warm. he is breathing.",
    "he has your face. he always had your face.",
    "upstairs, they are kneeling around a bed.",
    "they will keep you asleep for as long as they need you.",
    "",
    "you were never the one dreaming.",
    "",
    "NEMA",
};

// touch the wrong thing and you are somewhere else: any dream but this one, the shallow ones or the deep
static void link_random(void) {
    static const WorldId DEST[] = { W_SHAFT, W_DRAINS, W_VOID, W_GARDEN, W_CHAPEL, W_WARD, W_STATIC, W_DINNER, W_WOMB, W_CITY };
    int n = (int)(sizeof DEST / sizeof *DEST);
    WorldId to;
    do to = DEST[GetRandomValue(0, n - 1)]; while (to == L.id);
    audio_play_ex(SFX_SWELL, 0.6f, 1.2f);
    glitch = 0.4f;
    go(to, false);
}

static void use_thing(Use *u) {
    if (u->kind == USE_LINK && u->arg == 1) {   // you pick up the telephone
        static const char *VOICE[] = { "a child's voice: \"are you coming home?\"", "someone breathing, and a bell, very far away.", "your own voice: \"don't wake him.\"" };
        say(VOICE[GetRandomValue(0, 2)], 5);
    }
    if (u->kind == USE_LINK) { link_random(); return; }
    if (u->kind == USE_FACE) {   // his face is your face. the heart stops
        u->done = true; reading = NOTE_SLEEPER;
        if (!g_secret) { g_secret = 1; save_memory(); }
        L.heart.y = -100;
        audio_play_ex(SFX_SWELL, 0.7f, 0.6f);
        return;
    }
    if (u->kind == USE_SIT) {   // you sit down in the place that was kept for you
        dinner.sitT = 0.001f; dinner.seat = u->pos;
        P.pos = (Vector3){ u->pos.x, 0.0f, u->pos.z }; P.vel = (Vector3){ 0 }; P.yaw = 270; P.pitch = -8; P.crouch = 0.55f;
        audio_play_ex(SFX_CREAK, 0.5f, 0.7f);
        u->done = true;
        return;
    }
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
                if (!tr.on && dinner.sitT <= 0) {
                    Input in = input_read();
                    if (bot) bot_input(&in, &P, &L, (float)clock);
                    player_update(&P, &L, &in, DT);
                    if (L.wrap > 0) { P.pos.x = wrapf(P.pos.x, L.wrap); P.pos.z = wrapf(P.pos.z, L.wrap); }   // off one edge, in at the other
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
                        if (u->done && u->kind != USE_NOTE && u->kind != USE_LINK) continue;
                        Vector3 d = Vector3Subtract(wp(u->pos, eye), eye);
                        float len = Vector3Length(d);
                        if (len > 2.2f) continue;
                        float dot = Vector3DotProduct(Vector3Scale(d, 1.0f / len), fwd);
                        if (dot > best) { best = dot; near = u; }
                    }
                }
                static const char *HINT[] = { [USE_NOTE] = "E  read", [USE_CANDLE] = "E  light it", [USE_LILY] = "E  pick it", [USE_LINK] = "E  touch the screen", [USE_SIT] = "E  sit down", [USE_FACE] = "E  touch his face" };
                useHint = near ? HINT[near->kind] : NULL;
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
                        if (pk->fx == FX_VEIL) say("EFFECT: VEIL - kneel, and the things that look for you look past you. every head in the church has turned to you. (R to wake up)", 10);
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
                if (L.id == W_DRAINS || L.id == W_WOMB) {   // you hear them before you see them: wet clicking, faster when they have heard you
                    static float clickT[4];
                    for (int i = 0; i < L.watchers.size && i < 4; i++) {
                        Watcher *w = &L.watchers.data[i];
                        if (w->state < 0) continue;
                        clickT[i] -= frameDt;
                        if (clickT[i] <= 0) {
                            clickT[i] = w->state == 2 ? frand_(0.5f, 0.9f) : w->state == 1 ? frand_(1.2f, 2.0f) : frand_(6.0f, 12.0f);
                            if (L.t < 20) continue;   // at first there is nothing to hear at all
                            play_from(SFX_CLICK, w->pos, 0.9f, frand_(0.8f, 1.05f));
                            if (w->state > 0 && GetRandomValue(0, 2) == 0) play_from(SFX_THUD, w->pos, 0.4f, frand_(0.9f, 1.1f));
                        }
                    }
                }
                if (P.slipped) { P.slipped = false; audio_play(SFX_KNOCK); }
                if (city_update(frameDt, player_eye(&P), fwd)) { dead = true; say("he was always that tall.", 4); audio_play_ex(SFX_BREATH, 0.8f, 0.5f); }
                if (dinner_update(frameDt, player_eye(&P), fwd)) { dead = true; say("it is rude to leave the table.", 4); audio_play_ex(SFX_BREATH, 0.7f, 0.8f); }
                if (grey_update(frameDt)) { dead = true; say("you forget something.", 5); audio_play_ex(SFX_SWELL, 0.6f, 0.6f); }
                if (ward_update(frameDt, player_eye(&P))) { dead = true; say("it walked where you walked.", 4); audio_play_ex(SFX_BREATH, 0.7f, 0.8f); }
                if (update_mass(frameDt, player_eye(&P))) { dead = true; say("he counted one too many.", 4); audio_play_ex(SFX_BREATH, 0.7f, 0.7f); }
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
            float tone = L.id == W_HUB ? 1.0f : L.id == W_SHAFT ? 0.75f : L.id == W_DRAINS ? 0.9f : L.id == W_VOID ? 1.4f : L.id == W_GARDEN ? 1.15f : L.id == W_CHAPEL ? 0.8f : L.id == W_WARD ? 1.25f : L.id == W_STATIC ? 0.6f : L.id == W_DINNER ? 0.85f : L.id == W_WOMB ? 0.55f : L.id == W_CITY ? 0.7f : L.id == W_END ? 2.0f : 1.0f;
            float mus = L.id == W_DINNER ? 0.6f : L.id == W_GARDEN ? 0.8f : L.id == W_VOID ? 0.5f : L.id == W_END ? 0.8f : L.id == W_HUB ? 0.4f : 0.0f;
            bool home = L.id == W_HUB;   // the house: a music box in tune, a low warm hum, nothing else
            if (home) tension = madness = 0;
            if (L.id == W_WOMB) tension = L.heart.y > -50 ? fmaxf(tension, 0.65f) : 0.0f;   // the heart is everywhere down here, until it stops
            audio_music(frozen ? 0.0f : mus, home ? 0.0f : fminf(1.0f, madness * 0.9f + (blackout > 0 ? 0.4f : 0.0f)));
            float whisper = L.nearest < 18 ? 1.0f - L.nearest / 18.0f : 0.0f;
            if (L.id == W_STATIC) whisper = fmaxf(whisper, 0.35f);   // the hiss of the screens
            if (home) whisper = 0;
            if (!frozen) audio_set(tone, tension, whisper, tr.on ? 0.3f : L.id == W_STATIC ? 0.25f : home ? 0.45f : 0.9f);
            {   // the mass behind the walls gets louder the deeper you go; something breathes when one of them is close
                float choir = L.id == W_HUB ? 0.0f : (L.id == W_END || L.id == W_CHAPEL) ? 1.0f : L.id == W_STATIC ? 0.0f : 0.35f;
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
        if (levelLoaded && L.id == W_STATIC) gfx_update_static(time, fmodf(time, 19.0f) < 0.3f);

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
                const char **lines = g_secret ? END_LINES_KNOWN : END_LINES;
                int n = g_secret ? (int)(sizeof END_LINES_KNOWN / sizeof *END_LINES_KNOWN) : (int)(sizeof END_LINES / sizeof *END_LINES);
                for (int i = 0; i < n; i++) {
                    float a = fminf(1.0f, fmaxf(0.0f, (endT - 2.0f - i * 1.7f) * 0.6f));
                    Color lc = i == n - 1 ? (Color){ 150, 24, 20, 255 } : (Color){ 220, 215, 200, 255 };
                    lc.a = (unsigned char)(a * 255);
                    if (a > 0 && lines[i][0]) text_c(lines[i], 54 + i * 16, 10, lc);
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
        if (shotWorld == W_WARD && getenv("MURK_LAP")) { g_wardLoop = atoi(getenv("MURK_LAP")); level_free(&L); level_build(&L, W_WARD, dreams); }   // which lap of the ward
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
