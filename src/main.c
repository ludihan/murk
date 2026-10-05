#include "common.h"
#include "gfx.h"
#include "world.h"
#include "player.h"
#include "audio.h"
#include "figure.h"
#include <rlgl.h>
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
static float nextEvent = 30;

// eyes that hang in the fog at the edge of sight and are gone when you face them
typedef struct { Vector3 pos; float life, seenT; bool on; } Lurker;
static Lurker lurk[3];
static float lurkTimer = 8;
static char msg[320];
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
    [NOTE_NURSERY] = "He talked in his sleep again. He described his old room, but the room was enormous\nand he was very small in it, and his mother was looking for him.\nFather A. wrote down every word.",
    [NOTE_DRAINS] = "Do not go down to the drains after dark.\nBrother M. went down to see what was drinking from the water.\nWe hear him sometimes when we are quiet. Tapping.",
    [NOTE_CLIMB] = "It climbs the way you do. It has learned your hands.\nWhen you climb, it climbs. When you stop, it stops.\nDo not stop for long. The water is coming up behind it.",
    [NOTE_EYE] = "There is an eye above the steps.\nIt does not see what does not move. When it opens, be still,\nor be behind the old stones. It closes again. It always closes again.",
    [NOTE_GIANT] = "When she comes in to see him, get under something. She sees everything on the open floor.\nDon't run. She hears the floorboards.\nAnd don't wake him. If he cries, she comes at once.",
    [NOTE_CANDLES] = "Turn the three valves and the big pool will drain. The lamp is at the bottom of it.\nSomething lives in that pool. It is blind, and it will hear the water going.\nWalk softly. The tiles carry every step. Kneel if it is close. Do not run.",
    [NOTE_FAMILY] = "To the family.\nThank you for your son. He will sleep for as long as we need him to.\nYou may visit on Sundays. Please do not bring anything that rings.\n\n- the Congregation of the Lower Church",
    [NOTE_AWAKE] = "If you are reading this then you are awake.\nYou are not supposed to be awake.\nLie back down.",
    [NOTE_SLEEPER] = "You keep coming down here. You keep looking for the way out.\nThere is no way out for you. You are what he is dreaming, and we need him to keep dreaming.\nGo up and look at the bed.",
    [NOTE_WARD] = "ROOM 6.\nPatient has not woken in eleven years. Vital signs unremarkable.\nMother visits Sundays and will not leave when asked. She says he is dreaming of a house.\nShe says she can hear it through the wall.",
    [NOTE_STATIC] = "If you see the grey man, walk away from him. Don't run, there is no need.\nHe doesn't hurt anyone. He only wants to stand where you are standing.\nIf he gets there, you will forget something you went a long way to find.",
    [NOTE_WOMB] = "He is not asleep in the house. He was never in the house.\nHe is down here, where it is warm, and the house is what he dreams so that he will not be afraid.\nIf you go on, do not wake him. If you wake him, there will be nothing for you to go back to.",
    [NOTE_CITY] = "Nobody lives here any more. They leave the lights on anyway.\nIf you see the tall one at the end of a street, don't walk toward him, and don't stare.\nHe is never there when you look again. He is only nearer.",
    [NOTE_MORGUE] = "Drawer 6. Male, young. No identification. Found asleep and could not be woken.\nHe was brought down here by mistake and the mistake was not corrected.\nSome nights the staff hear him turning over.",
    [NOTE_THEATRE] = "TONIGHT: THE SLEEPER. A play in one act.\nThe audience is asked to remain seated during the performance.\nThe house lights will be lowered between scenes. Please do not leave your seat in the dark.",
    [NOTE_BELL] = "When the bell tolls, kneel with the others.\nThe priest counts the heads.\nHe must not count one that is standing.",
    [NOTE_FAIR] = "ADMIT ONE. Valid for one night only.\nThe management is not responsible for anything you leave behind\nor for anything that leaves with you.\n\nOn the back, in pencil: he loved the fair. he went in the mirrors and when he came out he was quiet.",
    [NOTE_MIRROR] = "HALL OF MIRRORS. Please do not touch the glass.\nIf one of your reflections does something you did not do, do not stop, and do not stare at it.\nIt can only move when you move. It can only go as far as the glass goes.\nThe fortune teller is at the end. She has been expecting you.",
    [NOTE_LAKE] = "Don't stand still on the ice. It takes your weight a little at a time and then all at once.\nWhere it's black it's thin. Get down and crawl.\nDon't run. There's something under there that comes to the sound of running.\nThe matches are in the hut.",
    [NOTE_SCHOOL] = "HIDE AND SEEK. The rules.\n1. Hide before it gets to ten.\n2. Don't let it see you. Don't make a noise.\n3. It can hear a door shut. If you hide after ten, it knows which one.\n4. Never hide in the same place twice.\n5. Nobody has ever been found. Nobody has ever come out either.",
    [NOTE_LINES] = "I must not wake him. I must not wake him. I must not wake him.\nI must not wake him. I must not wake him. I must not wake him.\nI must not wake him. I must not wake him. I must not\n\nhe is awake he is awake he is awake he is in the cupboard",
    [NOTE_DIARY] = "",
};
static int reading = -1;   // the note on screen, or -1
static const char *useHint;
// the shaft: something on the wall below you that climbs when you climb, and only while you aren't looking at it
static struct { bool on, seen; float y, lastPY, scrapeT; Vector3 pos, n; } climber;
static float radioT, wardFollow;
static struct { bool bang, cried, ran; float s, shown, stepT, strobeT, stare; } mom;   // the woman in the ward
// the nursery's mother: outside, coming in, looking, leaving, or coming for you
static struct { int state; float t, stepT, seeT, lostT, shake, pause, cryT; Vector3 pos, face, target; int wp; bool cried, heard; } giant;
// the baths: what lives in the pool. asleep under the water (0), draining (1), climbing out (2), hunting by ear (3)
static struct { int state, node; float t, clickT, timer; Vector3 pos, goal; bool lockerBang, ran; } swim;
// the mortuary: drawers that open as you pass, the ones that sit up, and the one that gets down in the dark
static struct { float strobeT, stepT, shown_t; bool open[6], walking; Vector3 walker, shown; int from; } morg;
// the theatre: scene, then the house lights go down, and every time they come up the puppets are somewhere else
static struct { int scene; float t; bool dark; Vector3 pup[3]; } show;
// below: the body that drops, the thing that comes out of it and runs you down the tunnel, the arms, the mouths
static struct { int state, drop; float t, runT, stillT, mouthT; Vector3 pos; } below;   // the ward's radio, and how far along the thing that follows you is
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
// the fair, the lake, the school, the cat and the house
#define HIST 128
static struct {
    struct { bool on; float t; int crowd; } ride;
    struct { float t, peak; int tries; bool rang; } ham;
    int dbl;              // 0: every one of you in the glass is you. 1: one has stopped, and opened its eyes. 2: it is out. 3: you left the hall and it stayed
    Vector3 dpos, last; float dstride, dstare, selfStride, carA, wheelA, histT;
    Vector3 hist[HIST]; float histYaw[HIST]; int histN;
} fair;
#define CRACKS 40
static struct {
    bool on, warned, heardOnce, tug;
    int state, ncr, stage;
    float stress, singT, cool, shake, tugT, tug2;
    Vector3 crack, thing, dir, heard;
    struct { Vector3 p; float r; } cracks[CRACKS];
} lake;
#define STRAIL 160
static struct {
    int state;            // 0 counting, 1 seeking, 2 going home, 3 it has seen you, 4 going straight to the door it heard
    int count, wp, round, heardDoor, lastSpot, trailI, occupied, found;
    float t, pause, stride, lostT, breathT;
    Vector3 pos, face, trail[STRAIL]; int trailN;
    bool unlocked, lookedUp;
} seek;
static struct { bool on; int idx; Vector3 out, n; } hide;
#define CAT_TRAIL 64
static struct {
    Vector3 pos, lookAt, trail[CAT_TRAIL]; int trailN, idx;
    float yaw, stride, t, fear, hissT, sulk, meowT;
    int mode, petted;     // mode: 0 asleep, 1 at your heel, 2 sitting, 3 bolting
    bool follow, withYou, gone, told;
} cat;
static struct { float knockT, humT, upT; int note, station; bool sitterGone; } house;
static bool visited[W_COUNT];
static int notesRead;
static bool noteSeen[NOTE_COUNT];
static struct { int left; float t; } match;
static struct { float on, q, extra; bool moving; } echo;   // the footsteps behind yours
static float subT;                                           // a face, for one frame, when the lights come back
static void cat_reset_trail(void) { cat.trailN = 0; cat.idx = 0; }


static void use_thing(Use *u);
static void on_caught(void);
// when one of them gets you. each of them does it its own way
typedef enum { CS_POUNCE, CS_DROWN, CS_ARMS, CS_MOTHER, CS_GIANT, CS_PRIEST, CS_TALL, CS_HOST, CS_HAND, CS_SHEET, CS_PUPPET, CS_GREY, CS_FALL, CS_MIRROR, CS_ICE, CS_SEEK } CatchStyle;
static struct { bool on; float t; CatchStyle style; FigKind kind; Color tint; float scale; char line[96]; } catchS;
// when the screen is gone, what colour it is gone to, and where you are made to look
static const struct { float black; Color fade; float pitch; } CATCH[] = {
    [CS_POUNCE] = { 0.95f, { 0, 0, 0, 255 }, 0 },          [CS_DROWN] = { 1.7f, { 3, 16, 20, 255 }, -15 },
    [CS_ARMS] = { 1.15f, { 0, 0, 0, 255 }, 0 },            [CS_MOTHER] = { 1.3f, { 0, 0, 0, 255 }, 0 },
    [CS_GIANT] = { 1.5f, { 0, 0, 0, 255 }, 62 },           [CS_PRIEST] = { 1.7f, { 0, 0, 0, 255 }, 4 },
    [CS_TALL] = { 1.8f, { 0, 0, 0, 255 }, 68 },            [CS_HOST] = { 1.6f, { 0, 0, 0, 255 }, 28 },
    [CS_HAND] = { 1.6f, { 40, 4, 6, 255 }, -50 },          [CS_SHEET] = { 1.05f, { 198, 202, 204, 255 }, 0 },
    [CS_PUPPET] = { 1.25f, { 0, 0, 0, 255 }, 0 },          [CS_GREY] = { 2.2f, { 112, 112, 112, 255 }, 0 },
    [CS_FALL] = { 1.45f, { 0, 0, 0, 255 }, -72 },
    [CS_MIRROR] = { 1.6f, { 206, 210, 216, 255 }, 0 },  [CS_ICE] = { 1.8f, { 2, 8, 18, 255 }, -25 },
    [CS_SEEK] = { 1.7f, { 0, 0, 0, 255 }, 32 },
};
static void caught(CatchStyle style, FigKind k, Color tint, float scale, const char *line) {
    if (catchS.on) return;
    catchS.on = true; catchS.t = 0; catchS.style = style; catchS.kind = k; catchS.tint = tint; catchS.scale = scale;
    snprintf(catchS.line, sizeof catchS.line, "%s", line);
    reading = -1;
    on_caught();
    switch (style) {   // what you hear at the moment it has you
    case CS_POUNCE: audio_play_ex(SFX_CLICK, 1.0f, 1.3f); break;
    case CS_DROWN:  audio_play_ex(SFX_SWELL, 0.8f, 0.5f); audio_play_ex(SFX_BREATH, 0.7f, 0.5f); break;
    case CS_ARMS:   audio_play_ex(SFX_BREATH, 0.9f, 0.8f); break;
    case CS_GIANT:  audio_play_ex(SFX_GIANT, 1.0f, 0.7f); break;
    case CS_PRIEST: audio_play_ex(SFX_CHANT, 1.0f, 0.9f); break;
    case CS_TALL:   audio_play_ex(SFX_SWELL, 1.0f, 0.4f); break;
    case CS_HOST:   audio_play_ex(SFX_BELL, 1.0f, 1.0f); break;
    case CS_HAND:   audio_play_ex(SFX_ROAR, 0.9f, 0.5f); break;
    case CS_SHEET:  audio_play_ex(SFX_BREATH, 1.0f, 1.1f); break;
    case CS_PUPPET: audio_play_ex(SFX_BELL, 0.9f, 1.5f); break;
    case CS_FALL:   audio_play_ex(SFX_CLICK, 1.0f, 0.9f); break;
    case CS_MIRROR: audio_play_ex(SFX_SWELL, 0.8f, 1.0f); break;
    case CS_ICE:    audio_play_ex(SFX_ICE, 1.0f, 1.6f); audio_play_ex(SFX_BANG, 0.8f, 0.5f); break;
    case CS_SEEK:   audio_play_ex(SFX_GIGGLE, 1.0f, 0.9f); break;
    default: break;   // the mother and the grey man: nothing at all, at first
    }
}

static const char *FX_NAME[FX_COUNT] = { "LAMP", "GLOVES", "BOOTS", "FEATHER", "VEIL", "COMPASS", "MATCHES", "SLIPPERS" };
static const char *FX_DESC[FX_COUNT] = {
    "press F to toggle it. the dark pulls back a little.",
    "your grip drains slower. you climb faster.",
    "you can jump once more in the air.",
    "hold SPACE in the air. you fall like something that was never heavy.",
    "kneel, and the things that look for you look past you.",
    "it points at what you came for. when something is close, it points at that instead.",
    "press Q to strike one. five to a dream. the dark goes back a little way, for a little while.",
    "your feet hardly make a sound. the ones that listen will have to listen harder.",
};
static const WorldId FX_HOME[FX_COUNT] = { W_BATHS, W_SHAFT, W_VOID, W_NURSERY, W_CHAPEL, W_FAIR, W_LAKE, W_SCHOOL };   // which door each one is behind

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
    if (id == W_HUB) { int n = 0; for (int f = 0; f < FX_COUNT; f++) n += (P.fx >> f) & 1; if (n > g_rot) g_rot = n; }   // the more you bring home, the worse home gets
    visited[id] = true;
    memset(&fair, 0, sizeof fair); memset(&lake, 0, sizeof lake); memset(&seek, 0, sizeof seek); memset(&hide, 0, sizeof hide); memset(&echo, 0, sizeof echo);
    lake.singT = 4; lake.tugT = 8; seek.t = 3.0f; seek.lastSpot = seek.heardDoor = -1; seek.occupied = GetRandomValue(0, 7); seek.face = (Vector3){ 0, 0, 1 };
    match.left = 5; match.t = 0; subT = 0;
    if (id == W_WARD) { g_wardLoop = 0; radioT = 3.0f; wardFollow = 0; memset(&mom, 0, sizeof mom); }
    if (levelLoaded) {
        if (recN > 40 && L.id != W_SHAFT && L.id != W_END) { memcpy(ghostPath[L.id], rec, recN * sizeof *rec); ghostN[L.id] = recN; }
        level_free(&L);
    }
    recN = 0; recT = 0;
    memset(&climber, 0, sizeof climber);
    memset(&gaze, 0, sizeof gaze); gaze.t = frand_(14, 20);
    memset(&mass, 0, sizeof mass); mass.t = frand_(12, 16);
    memset(&grey, 0, sizeof grey); grey.t = frand_(25, 40);
    memset(&dinner, 0, sizeof dinner);
    memset(&giant, 0, sizeof giant); giant.t = frand_(18, 26);
    memset(&swim, 0, sizeof swim); swim.pos = (Vector3){ 0, -4, 0 };
    memset(&below, 0, sizeof below); below.drop = -1;
    memset(&morg, 0, sizeof morg); morg.from = -1;
    memset(&show, 0, sizeof show); show.t = frand_(8, 10);
    show.pup[0] = (Vector3){ -3, 1.2f, -4 }; show.pup[1] = (Vector3){ 3, 1.2f, -4 }; show.pup[2] = (Vector3){ 0, 1.2f, -3 };
    memset(&tallm, 0, sizeof tallm); tallm.t = frand_(30, 45); tallm.dist = 46; ringT = frand_(20, 40);
    ghost.on = id != W_HUB && ghostN[id] > 40 && dreams >= 2 && GetRandomValue(0, 1) == 0; ghost.noticed = false; ghost.t = -frand_(30, 60);
    level_build(&L, id, ++dreams);
    levelLoaded = true;
    player_spawn(&P, &L);
    if (id == W_HUB && wake) {
        P.pos = L.wakePos;
        P.yaw = L.wakeYaw;
    }
    nameT = 3.5f;
    blackout = 0; nextBlackout = frand_(30, 50); phantomLeft = 0; phantomT = frand_(25, 45);
    memset(lurk, 0, sizeof lurk); lurkTimer = frand_(40, 80);
    nextEvent = frand_(18, 30);   // a moment of quiet, and no more than that
    seek.pos = L.bed; fair.last = P.pos; lake.crack = P.pos;
    if (id == W_HUB) {   // the cat: by the bed when you wake, asleep by the fire the first time
        cat.withYou = false; cat.gone = false; cat_reset_trail();
        if (dreams <= 1 && !wake) { cat.pos = (Vector3){ 27.0f, -3.0f, -2.7f }; cat.mode = 0; cat.yaw = 1.2f; }
        else { cat.pos = Vector3Add(P.pos, (Vector3){ 1.1f, 0, 0.6f }); cat.mode = 2; cat.yaw = -1.5708f; }
    } else if (cat.withYou) {
        float a = P.yaw * DEG2RAD;
        cat.pos = (Vector3){ P.pos.x - sinf(a) * 1.2f, P.pos.y, P.pos.z + cosf(a) * 1.2f };
        cat.mode = 1; cat.gone = false; cat_reset_trail();
    }
    lampOn = (P.fx & (1u << FX_LAMP)) ? lampOn : 0;
    if (id == W_HUB && !wake && dreams <= 1) say("WASD walk · SHIFT run · CTRL kneel · E use · hold LMB at rusty walls to grip · R wake up. there is a cat asleep by the fire.", 12);
    if (id == W_SHAFT) say("hold LMB on the rusty plates. W climbs, A/D shuffle, SPACE lunges. don't let go.", 9);
    if (id == W_BATHS) say("the baths have been closed a long time. the water is very still.", 7);
    if (id == W_STATIC) say("nothing is on.", 5);
    if (id == W_MORGUE) say("it is very cold. everyone here has a tag on one toe.", 6);
    if (id == W_THEATRE) say("you are late. the performance has started.", 6);
    if (id == W_CITY) say("it is raining. every light is on, and nobody is home.", 6);
    if (id == W_WOMB) say("it is warm here. something is breathing all around you.", 6);
    if (id == W_DINNER) say("dinner is served. there is a place for you.", 6);
    if (id == W_WARD) say("the lights hum. the corridor goes on ahead of you.", 6);
    if (id == W_VOID) say("there is nothing underneath.", 6);
    if (id == W_FAIR) say("the fair is open. every bulb is lit. nobody came.", 6);
    if (id == W_LAKE) say("it is so cold. the lake is frozen black, and the ice is singing.", 6);
    if (id == W_SCHOOL) say("somewhere close, a child is counting.", 6);
    if (id == W_HUB && wake && g_rot >= 3 && GetRandomValue(0, 1)) say("the house is very quiet. somebody has been in your room.", 5);
}


static void go(WorldId to, bool wake) {
    if (tr.on) return;
    tr.on = true; tr.t = 0; tr.to = to; tr.wake = wake; tr.loaded = false;
    if (wake || to == W_HUB) cat.withYou = false;
    else if (cat.follow && cat.sulk <= 0 && cat.mode != 0 && !cat.gone && Vector3Distance(cat.pos, P.pos) < 5.0f && (L.id == W_HUB || cat.withYou)) {
        if (to == W_SHAFT || to == W_VOID || to == W_END) { cat.withYou = false; say("the cat stops at the door. it won't go through that one.", 4); }
        else cat.withYou = true;
    } else cat.withYou = false;
    audio_play(wake ? SFX_WAKE : SFX_DOOR);
    if (wake) { flash = 1; g_wakes++; save_memory(); }
}

static void new_game(void) {
    memset(&P, 0, sizeof P);
    memset(&cat, 0, sizeof cat); memset(&house, 0, sizeof house); memset(visited, 0, sizeof visited); memset(noteSeen, 0, sizeof noteSeen);
    notesRead = 0; g_rot = 0;
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
    { "she comes in to look at him every night.", "the floor is so far down.", "everything in here is too big.", "don't wake him." },
    { "they know all the words.", "someone in the back is weeping.", "he has counted to the same number every week.", "your place in the pew is warm." },
    { "visiting hours are over.", "the radio is tuned to nothing.", "this corridor is longer than it was.", "don't stop walking." },
    { "nothing is on.", "it is so quiet you can hear the screens.", "the grey man doesn't mean any harm.", "every channel is the same channel." },
    { "they have been waiting for you to sit down.", "nobody has touched their food.", "the candles never burn down.", "it is rude to leave the table." },
    { "it is warm here.", "something is breathing all around you.", "this is where he is.", "you were born somewhere like this." },
    { "every window is somebody's.", "the rain doesn't touch you.", "don't look down the long streets.", "he is taller every time." },
    { "it is so cold in here.", "they are all wearing tags.", "one of the drawers is open.", "someone is breathing under a sheet." },
    { "the audience is asked to remain seated.", "you are late. they have saved you a seat.", "nobody is pulling the strings.", "the play is about you." },
    { "every bulb is lit. nobody came.", "the horses are still going round.", "in the glass, your eyes are shut.", "he came here once. he came out quiet." },
    { "it's so cold your teeth hurt.", "the ice is singing.", "there are faces in it. they are looking up.", "keep moving. keep moving." },
    { "...seven... eight...", "come out, come out.", "nobody has ever been found.", "someone is giggling in a cupboard." },
    { "stay.", "stay.", "stay.", "stay." },
};
// something happens every half minute or so, so no dream ever just sits there
static void director(float dt) {
    if (tr.on || L.id == W_END) return;
    nextEvent -= dt;
    if (nextEvent > 0) return;
    nextEvent = frand_(30, 55);
    int kind = dreams < 2 ? 0 : GetRandomValue(0, 9);
    if (kind <= 4) {
        if (GetRandomValue(0, 1) == 0) say(WHISPERS[L.id][GetRandomValue(0, 3)], 4.0f);
        static const Sfx AMB[] = { SFX_KNOCK, SFX_BELL, SFX_CHANT, SFX_PRAYER, SFX_HUM, SFX_CREAK, SFX_SCRAPE, SFX_RUN, SFX_BREATH };
        Sfx a = AMB[GetRandomValue(0, 8)];
        play_behind(a, a == SFX_HUM || a == SFX_PRAYER ? 0.35f : 0.5f, frand_(0.85f, 1.0f));
    } else if (kind <= 6) echo.on = 45;   // for a while, someone walks behind you, in step
    else if (kind == 7 && dreams >= 3) { subT = 0.075f; audio_play_ex(SFX_SCREECH, 0.45f, 1.6f); glitch = 0.5f; madness = 1; }
    else if (kind == 8) lurkTimer = 0;
    else play_behind(SFX_GIGGLE, 0.45f, frand_(0.8f, 1.1f));
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
    { "...she is in the corridor with you now...", "...she only moves in the dark. she only moves when you don't look..." },
    { "...whatever you hear, don't turn around...", "...she is right behind you. keep walking..." },
};
// the corridor as a line you walk along: down A, across the dogleg, down C. s is how far along it you are
static Vector3 ward_pt(float s) {
    if (s < 23.2f) return (Vector3){ 0, 0, 8.0f - s };
    if (s < 31.2f) return (Vector3){ s - 23.2f, 0, -15.2f };
    return (Vector3){ 8.0f, 0, -15.2f - (s - 31.2f) };
}
static Vector3 ward_dir(float s) { return s < 23.2f ? (Vector3){ 0, 0, -1 } : s < 31.2f ? (Vector3){ 1, 0, 0 } : (Vector3){ 0, 0, -1 }; }
static float ward_s(Vector3 p) {
    if (p.z > -14.0f && p.x < 2.0f) return 8.0f - p.z;
    if (p.z > -16.4f) return 23.2f + Clamp(p.x, 0, 8);
    return 31.2f + (-15.2f - p.z);
}
static bool ward_update(float dt, Vector3 eye) {
    if (L.id != W_WARD) return false;
    Vector3 fwd = player_forward(&P);
    if (P.pos.z < -30.0f && P.pos.x > 4.0f) {   // another lap
        g_wardLoop++;
        level_free(&L);
        level_build(&L, W_WARD, dreams);
        P.pos.x -= 8.0f; P.pos.z += 36.0f;
        recN = 0; wardFollow = 0; radioT = 2.0f;
        memset(&mom, 0, sizeof mom);
        mom.s = 40.0f; mom.shown = 40.0f;   // on the fourth lap she starts at the far end
        if (g_wardLoop == 3) { audio_play_ex(SFX_SCREECH, 0.5f, 0.6f); blackout = 1.0f; }
        if (g_wardLoop == 4) { say("...don't turn around...", 5); play_behind(SFX_BREATH, 0.7f, 0.8f); }
    }
    int k = g_wardLoop > 4 ? 4 : g_wardLoop;
    radioT -= dt;
    if (radioT <= 0) {
        radioT = frand_(18, 30);
        play_from(SFX_PRAYER, (Vector3){ 0.75f, 0.9f, -3.4f }, 0.45f, 0.7f);
        say(RADIO[k][GetRandomValue(0, 1)], 4);
    }
    Vector3 door6 = { -1.2f, 1.2f, -6.0f };
    float d6 = Vector3Distance(P.pos, (Vector3){ door6.x, 0, door6.z });
    // first lap: someone very small is crying behind the door of room six
    if (g_wardLoop == 0 && !mom.cried && d6 < 5) { mom.cried = true; play_from(SFX_CRY, door6, 0.45f, 1.0f); }
    // second lap: as you pass it, something inside throws itself at that door
    if (g_wardLoop == 1 && !mom.bang && d6 < 1.8f) { mom.bang = true; play_from(SFX_BANG, door6, 1.0f, 0.9f); madness = 1; }
    // third lap: she is at the far end with her back to you. the lights go, she is gone, and something runs behind you
    for (int i = 0; i < L.effigies.size; i++) {
        Effigy *e = &L.effigies.data[i];
        if (e->kind == FIG_MOTHER && e->pos.y > -10 && Vector3Distance(e->pos, P.pos) < 8.0f) {
            e->pos.y = -100; blackout = 1.2f; mom.ran = true; mom.stepT = 1.3f;
        }
    }
    if (mom.ran && mom.stepT > 0) { mom.stepT -= dt; if (mom.stepT <= 0) play_behind(SFX_RUN, 0.8f, 1.0f); }
    float ps = ward_s(P.pos);
    // fourth lap: the lights strobe, and she comes down the corridor whenever it is dark or you are not looking
    if (g_wardLoop == 3) {
        mom.strobeT -= dt;
        if (mom.strobeT <= 0) { mom.strobeT = frand_(2.5f, 5.0f); blackout = frand_(0.4f, 1.0f); }
        Vector3 mp = ward_pt(mom.s);
        bool seen = blackout <= 0 && level_seen(&L, eye, fwd, mp);
        if (!seen) {
            float sp = blackout > 0 ? 5.0f : 2.2f, dir = ps < mom.s ? -1.0f : 1.0f;
            mom.s += dir * fminf(fabsf(ps - mom.s), sp * dt);
        }
        mom.stepT -= dt;
        if (mom.stepT <= 0) { mom.stepT = 1.0f / 6.0f; mom.shown = mom.s; }   // she is only ever seen in jerks
        if (fabsf(ps - mom.s) < 0.9f) return true;
    }
    // last lap: no light at all, and she is right behind you. you can hear her. don't look
    if (g_wardLoop >= 4) {
        mom.s = mom.shown = ps - 1.3f;
        Vector3 back = ward_dir(fmaxf(0, mom.s));
        float look = Vector3DotProduct((Vector3){ fwd.x, 0, fwd.z }, back);
        mom.stare = look < -0.35f ? mom.stare + dt : 0;
        if (mom.stare > 0.35f) return true;
        mom.stepT -= dt;
        if (mom.stepT <= 0) { mom.stepT = frand_(3, 6); play_behind(GetRandomValue(0, 2) ? SFX_BREATH : SFX_CRY, 0.6f, 0.8f); }
    }
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

static const Vector3 BN[] = { { 0, 0, 26 }, { 0, 0, 16 }, { 0, 0, 9 }, { -9, 0, 0 }, { 9, 0, 0 }, { 0, 0, -9 }, { -9, 0, 9 }, { 9, 0, 9 },
                               { -9, 0, -9 }, { 9, 0, -9 }, { 0, 0, -18 }, { 0, 0, -24 }, { 20, 0, 0 }, { 30, 0, 0 }, { -20, 0, 0 }, { -26, 0, 4 } };
static const int BE[][2] = { { 0, 1 }, { 1, 2 }, { 2, 6 }, { 2, 7 }, { 6, 3 }, { 7, 4 }, { 3, 8 }, { 4, 9 }, { 8, 5 }, { 9, 5 }, { 5, 10 }, { 10, 11 }, { 4, 12 }, { 12, 13 }, { 3, 14 }, { 14, 15 } };
#define BNN 16
static int bath_node(Vector3 p) {
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < BNN; i++) { float d = Vector3Distance((Vector3){ p.x, 0, p.z }, BN[i]); if (d < bd) { bd = d; best = i; } }
    return best;
}
static int bath_next(int from, int to) {   // one step along the halls toward `to`
    int dist[BNN], q[BNN], h = 0, t = 0;
    for (int i = 0; i < BNN; i++) dist[i] = -1;
    dist[to] = 0; q[t++] = to;
    while (h < t) {
        int c = q[h++];
        for (int e = 0; e < (int)(sizeof BE / sizeof *BE); e++) {
            int n = BE[e][0] == c ? BE[e][1] : BE[e][1] == c ? BE[e][0] : -1;
            if (n >= 0 && dist[n] < 0) { dist[n] = dist[c] + 1; q[t++] = n; }
        }
    }
    int best = from;
    for (int e = 0; e < (int)(sizeof BE / sizeof *BE); e++) {
        int n = BE[e][0] == from ? BE[e][1] : BE[e][1] == from ? BE[e][0] : -1;
        if (n >= 0 && dist[n] >= 0 && dist[n] < dist[best]) best = n;
    }
    return best;
}
static bool baths_update(float dt, Vector3 eye) {
    if (L.id != W_BATHS) return false;
    Vector3 fwd = player_forward(&P);
    // before anything else: the one behind the shower curtain, and a locker that someone is inside
    for (int i = 0; i < L.effigies.size; i++) {
        Effigy *e = &L.effigies.data[i];
        if (e->pos.y > -10 && Vector3Distance(e->pos, P.pos) < 5.0f) { e->pos.y = -100; blackout = 0.7f; swim.ran = true; swim.timer = 1.0f; }
    }
    if (swim.ran && swim.timer > 0) { swim.timer -= dt; if (swim.timer <= 0) play_behind(SFX_RUN, 0.7f, 0.9f); }
    if (!swim.lockerBang && P.pos.x > 21 && fabsf(P.pos.z) < 3) { swim.lockerBang = true; play_from(SFX_BANG, (Vector3){ 23.8f, 1.2f, -2.6f }, 1.0f, 1.1f); madness = 1; }
    bool inPool = fabsf(P.pos.x) < 6 && fabsf(P.pos.z) < 6;
    if (swim.state == 0) {   // asleep, under the water. go in and it is awake at once
        if (inPool && P.pos.y < -0.6f) return true;
        int all = 0, done = 0;
        for (int i = 0; i < L.uses.size; i++) if (L.uses.data[i].kind == USE_VALVE) { all++; done += L.uses.data[i].done; }
        if (all && done == all) { swim.state = 1; swim.t = 0; audio_play_ex(SFX_SCRAPE, 0.8f, 0.5f); say("somewhere, a great deal of water starts to go down.", 5); }
        return false;
    }
    if (swim.state == 1) {   // the pool empties
        swim.t += dt;
        L.waterY = -0.25f - fminf(1.0f, swim.t / 10.0f) * 4.1f;
        if (fmodf(swim.t, 2.5f) < dt) play_from(SFX_SCRAPE, (Vector3){ 0, -3, 0 }, 0.5f, 0.4f);
        if (swim.t > 10) {
            L.water = false;
            for (int i = 0; i < L.pickups.size; i++) L.pickups.data[i].locked = false;
            swim.state = 2; swim.t = 0;
            play_from(SFX_ROAR, (Vector3){ 0, -3, 0 }, 0.5f, 0.6f);
        }
        return false;
    }
    if (swim.state == 2) {   // it climbs out, up the side of the pool
        swim.t += dt;
        float k = fminf(1.0f, swim.t / 3.0f);
        swim.pos = (Vector3){ 5.6f * fminf(1, k * 2), -4.0f + 4.0f * fmaxf(0, k * 2 - 1), -2 };
        if (k >= 1) { swim.state = 3; swim.pos = (Vector3){ 7.0f, 0, -2 }; swim.node = 4; swim.goal = swim.pos; swim.timer = 0; }
        return false;
    }
    // hunting: it goes where it last heard you, through the halls
    float noise = P.noise * 1.25f;   // the tiles carry everything
    float d = Vector3Distance((Vector3){ swim.pos.x, 0, swim.pos.z }, (Vector3){ P.pos.x, 0, P.pos.z });
    if (fabsf(swim.pos.y - P.pos.y) < 1.5f && d < 1.0f) return true;
    if (noise > 0.05f && d < noise * 18.0f) { swim.goal = P.pos; swim.timer = 4; }
    Vector3 tgt = swim.goal;
    bool direct = ray_clear((Vector3){ swim.pos.x, swim.pos.y + 0.5f, swim.pos.z }, (Vector3){ tgt.x, tgt.y + 0.5f, tgt.z }) && fabsf(tgt.y - swim.pos.y) < 1.0f;
    if (!direct) { int at = bath_node(swim.pos), go = bath_node(tgt); tgt = at == go ? BN[go] : BN[bath_next(at, go)]; if (Vector3Distance(swim.pos, BN[at]) > 1.5f && at != go) tgt = BN[at]; }
    Vector3 dir = Vector3Subtract(tgt, swim.pos); dir.y = 0;
    float len = Vector3Length(dir), sp = swim.timer > 0 ? 3.3f : 1.3f;
    if (len > 0.2f) swim.pos = Vector3Add(swim.pos, Vector3Scale(dir, fminf(len, sp * dt) / len));
    else if (swim.timer > 0) swim.timer -= dt;   // it stops where it heard you, and listens
    else swim.goal = BN[GetRandomValue(0, BNN - 1)];
    swim.clickT -= dt;
    if (swim.clickT <= 0) { swim.clickT = swim.timer > 0 ? frand_(0.6f, 1.0f) : frand_(4, 8); play_from(SFX_CLICK, swim.pos, 0.9f, frand_(0.8f, 1.0f)); }
    (void)eye; (void)fwd;
    return false;
}

static const Vector3 DRAWER[6] = { { -7.6f, 1.5f, 2 }, { 7.6f, 0.6f, -2 }, { -7.6f, 2.4f, -6 }, { 7.6f, 1.5f, -10 }, { -7.6f, 0.6f, -14 }, { 7.6f, 1.5f, -18 } };
static bool morgue_update(float dt, Vector3 eye, Vector3 fwd) {
    if (L.id != W_MORGUE) return false;
    for (int i = 0; i < 6; i++) if (!morg.open[i] && fabsf(P.pos.z - DRAWER[i].z) < 2.0f && L.t > 6) {   // a drawer slides out on its own as you come level with it
        morg.open[i] = true; play_from(SFX_SCREECH, DRAWER[i], 0.35f, 1.4f);
    }
    // while you aren't looking, they sit up, one by one, and face you
    for (int i = 0; i < L.effigies.size; i++) {
        Effigy *e = &L.effigies.data[i];
        if (e->kind != FIG_SLEEPER || e->seen || i == morg.from) continue;
        if (GetRandomValue(0, 100000) < (int)(dt * 100000 * fminf(0.05f, L.t * 0.0012f))) {
            e->kind = FIG_SEATED; e->pos.y = 0.49f; e->yaw = atan2f(P.pos.x - e->pos.x, -(P.pos.z - e->pos.z)); e->look = 1;
            play_from(SFX_CREAK, e->pos, 0.5f, 0.6f);
        }
    }
    // past halfway, the lights start to fail, and one of them is off its table and coming, only in the dark
    if (P.pos.z < -4 || morg.walking) {
        morg.strobeT -= dt;
        if (morg.strobeT <= 0) { morg.strobeT = frand_(2.5f, 4.5f); blackout = frand_(0.6f, 1.1f); }
        if (!morg.walking) {
            float bd = -1;
            for (int i = 0; i < L.effigies.size; i++) { Effigy *e = &L.effigies.data[i]; float d = Vector3Distance(e->pos, P.pos); if (d > bd && !e->seen) { bd = d; morg.from = i; } }
            if (morg.from >= 0) {
                Effigy *e = &L.effigies.data[morg.from];
                morg.walker = (Vector3){ e->pos.x + (e->pos.x > 0 ? -0.9f : 0.9f), 0, e->pos.z };
                e->pos.y = -100; morg.walking = true; morg.shown = morg.walker;
            }
        }
        if (morg.walking) {
            bool seen = blackout <= 0 && level_seen(&L, eye, fwd, morg.walker);
            Vector3 d = { P.pos.x - morg.walker.x, 0, P.pos.z - morg.walker.z };
            float len = Vector3Length(d);
            if (!seen && len > 0.01f) morg.walker = Vector3Add(morg.walker, Vector3Scale(d, fminf(len, (blackout > 0 ? 4.5f : 0.9f) * dt) / len));
            morg.stepT -= dt;
            if (morg.stepT <= 0) { morg.stepT = 0.2f; morg.shown = morg.walker; }
            if (len < 0.9f) return true;
        }
    }
    return false;
}
static bool theatre_update(float dt) {
    if (L.id != W_THEATRE) return false;
    show.t -= dt;
    if (!show.dark && show.t <= 0) {   // the house lights go down
        show.dark = true; show.t = 2.4f; blackout = 2.4f;
        audio_play_ex(SFX_CREAK, 0.6f, 0.5f);
        show.scene++;
        for (int i = 0; i < 3; i++) {
            if (show.scene < 3) { show.pup[i] = (Vector3){ frand_(-5, 5), 1.2f, frand_(-7, -3) }; continue; }   // a new scene: they have moved about the stage
            Vector3 d = Vector3Subtract(P.pos, show.pup[i]); d.y = 0;
            float len = Vector3Length(d), step = fminf(len - 1.4f, frand_(3.5f, 6.0f));   // and then, off the stage and up the aisle
            if (step > 0) show.pup[i] = Vector3Add(show.pup[i], Vector3Scale(d, step / len));
            show.pup[i].y = (show.pup[i].z < -1.0f && fabsf(show.pup[i].x) < 10) ? 1.2f : 0.0f;
        }
    } else if (show.dark && show.t <= 0) {   // and up again
        show.dark = false; show.t = frand_(8, 12) * (show.scene >= 3 ? 0.7f : 1.0f);
        audio_play_ex(SFX_BELL, 0.4f, 1.4f);
        for (int i = 0; i < 3; i++) if (Vector3Distance((Vector3){ show.pup[i].x, 0, show.pup[i].z }, (Vector3){ P.pos.x, 0, P.pos.z }) < 1.7f) return true;
    }
    return false;
}

static float below_floor(float x) { return x >= -9 ? -2.0f : x >= -40 ? -2.0f - 2.0f * (-9 - x) / 31.0f : -4.0f; }
static Vector3 arm_tip(int i, float t) {   // the arms in the hall: where each hand is reaching to
    float x = -42.5f - (i / 2) * 2.25f, side = (i & 1) ? 1.0f : -1.0f, y = -2.6f + ((i / 2) % 2) * 0.6f;
    Vector3 sh = { x, y, -29 + side * 3.95f };
    Vector3 chest = { P.pos.x, P.pos.y + 1.1f, P.pos.z };
    float d = Vector3Distance(sh, chest), want = Clamp(1.6f - (d - 1.5f) * 0.6f, 0.5f, 2.4f);
    Vector3 dir = Vector3Normalize(Vector3Lerp((Vector3){ 0, 0.1f, -side }, Vector3Normalize(Vector3Subtract(chest, sh)), d < 5 ? 0.85f : 0.2f));
    float tw = sinf(t * 9.0f + i * 2.1f) * 0.08f;
    return Vector3Add(sh, Vector3Scale(dir, want + tw));
}
static bool below_update(float dt) {
    if (L.id != W_WOMB) return false;
    const float Z = -29;
    if (below.state == 0 && P.pos.x < -2 && P.pos.x > -9) {   // one of them comes down behind you
        float bd = 1e9f;
        for (int i = 0; i < L.effigies.size; i++) {
            Effigy *e = &L.effigies.data[i];
            if (e->kind != FIG_COCOON) continue;
            float d = fabsf(e->pos.x - 4.0f) + fabsf(e->pos.z - Z);
            if (d < bd) { bd = d; below.drop = i; }
        }
        below.state = 1; below.t = 0;
        if (below.drop >= 0) play_from(SFX_CREAK, L.effigies.data[below.drop].pos, 1.0f, 0.5f);
    }
    if (below.state == 1) {
        below.t += dt;
        if (below.drop >= 0) {
            Effigy *e = &L.effigies.data[below.drop];
            if (e->pos.y > -0.6f) { e->pos.y -= dt * 14.0f; if (e->pos.y <= -0.6f) { play_from(SFX_GIANT, e->pos, 0.7f, 1.4f); } }
        }
        if (below.t > 1.6f) {   // and it gets out
            Vector3 c = below.drop >= 0 ? L.effigies.data[below.drop].pos : (Vector3){ 4, 0, Z };
            below.pos = (Vector3){ c.x, -2, c.z };
            if (below.drop >= 0) L.effigies.data[below.drop].pos.y = -100;
            below.state = 2; play_from(SFX_ROAR, below.pos, 0.9f, 1.1f); madness = 1;
        }
        return false;
    }
    if (below.state == 2) {   // it runs you down
        Vector3 d = { P.pos.x - below.pos.x, 0, P.pos.z - below.pos.z };
        float len = Vector3Length(d);
        if (len < 1.0f) return true;
        if (P.pos.x < -41.5f) {   // it will not come into the hall of arms
            below.state = 3; play_from(SFX_SCREECH, below.pos, 0.8f, 0.5f);
            return false;
        }
        float step = fminf(len, 5.3f * dt);
        below.pos.x += d.x / len * step; below.pos.z += d.z / len * step;
        if (below.pos.x < -9) below.pos.z += (Z - below.pos.z) * fminf(1, dt * 4);
        below.pos.y = below_floor(below.pos.x);
        below.runT -= dt;
        if (below.runT <= 0) { below.runT = 1.3f; play_from(SFX_RUN, below.pos, 0.9f, 1.1f); }
        return false;
    }
    // the hall of arms: walk through. if you stop near them, they take hold
    if (P.pos.x < -40 && P.pos.x > -60) {
        bool near = false;
        Vector3 chest = { P.pos.x, P.pos.y + 1.1f, P.pos.z };
        for (int i = 0; i < 16; i++) if (Vector3Distance(arm_tip(i, L.t), chest) < 1.0f) near = true;
        below.stillT = (near && P.speedMeter < 0.6f) ? below.stillT + dt : fmaxf(0, below.stillT - dt);
        if (below.stillT > 1.1f) return true;
    }
    // the passage of mouths: they speak as you pass
    if (P.pos.x < -60 && P.pos.x > -78) {
        below.mouthT -= dt;
        if (below.mouthT <= 0) { below.mouthT = frand_(1.5f, 3.0f); play_from(SFX_PRAYER, (Vector3){ P.pos.x - 1.5f, P.pos.y + 1.4f, Z + (GetRandomValue(0, 1) ? 1.5f : -1.5f) }, 0.6f, frand_(0.7f, 1.0f)); }
    }
    return false;
}

static bool giant_walk(Vector3 to, float speed, float dt) {
    Vector3 d = Vector3Subtract(to, giant.pos); d.y = 0;
    float len = Vector3Length(d);
    if (len < 0.3f) return true;
    giant.face = Vector3Scale(d, 1.0f / len);
    giant.pos = Vector3Add(giant.pos, Vector3Scale(giant.face, fminf(len, speed * dt)));
    giant.stepT -= dt;
    if (giant.stepT <= 0) {   // every step shakes the floor
        giant.stepT = speed > 3 ? 0.65f : 1.35f;
        float near = Vector3Distance(giant.pos, P.pos);
        play_from(SFX_GIANT, giant.pos, 1.0f, frand_(0.85f, 1.0f));
        giant.shake = fmaxf(giant.shake, Clamp(1.2f - near / 30.0f, 0.15f, 1.0f));
    }
    return false;
}
static bool nursery_update(float dt, Vector3 eye) {
    if (L.id != W_NURSERY) return false;
    static const Vector3 WP[] = { { 2, 0, -11 }, { -4, 0, -4 }, { -14, 0, -4 }, { -17, 0, 4 }, { -6, 0, 8 }, { 4, 0, 10 }, { 12, 0, 4 }, { 18, 0, -6 }, { 6, 0, -2 }, { -2, 0, 14 } };
    const int NW = (int)(sizeof WP / sizeof *WP);
    giant.shake = fmaxf(0, giant.shake - dt * 1.6f);
    bool took = false;
    for (int i = 0; i < L.pickups.size; i++) if (L.pickups.data[i].taken) took = true;
    if (took && !giant.cried) {   // you took it off his pillow, and he woke
        giant.cried = true; giant.cryT = 0;
        if (giant.state == 0) { giant.state = 1; giant.pos = L.bed; audio_play_ex(SFX_DOOR, 1.0f, 0.4f); }
        giant.heard = true;
    }
    if (giant.cried) { giant.cryT -= dt; if (giant.cryT <= 0) { giant.cryT = 2.6f; play_from(SFX_CRY, (Vector3){ -14, 6, -13 }, 1.0f, 0.55f); } }
    Vector3 chest = { P.pos.x, P.pos.y + 1.1f, P.pos.z }, head = { giant.pos.x, 14.5f, giant.pos.z };
    float d = Vector3Distance((Vector3){ giant.pos.x, 0, giant.pos.z }, (Vector3){ P.pos.x, 0, P.pos.z });
    bool clear = giant.state > 0 && ray_clear(head, chest);
    switch (giant.state) {
    case 0:   // outside: first you hear her coming along the landing
        giant.t -= dt;
        if (giant.t < 7 && giant.t + dt >= 7) say("something very heavy is coming along the landing.", 4);
        if (giant.t < 7) { giant.stepT -= dt; if (giant.stepT <= 0) { giant.stepT = 1.4f; play_from(SFX_GIANT, L.bed, 0.4f + 0.08f * (7 - giant.t), 0.9f); } }
        if (giant.t <= 0) { giant.state = 1; giant.pos = L.bed; giant.face = (Vector3){ 0, 0, 1 }; audio_play_ex(SFX_DOOR, 1.0f, 0.4f); }
        return false;
    case 1:   // coming in
        if (giant_walk((Vector3){ 2.6f, 0, -13 }, 1.6f, dt)) { giant.state = giant.heard ? 4 : 2; giant.t = frand_(35, 50); giant.wp = GetRandomValue(0, NW - 1); }
        break;
    case 2:   // looking: from place to place, stopping to turn her head
        giant.t -= dt;
        if (giant.pause > 0) {
            giant.pause -= dt;
            float a = sinf(giant.pause * 0.8f) * 0.9f;
            giant.face = Vector3Normalize((Vector3){ giant.face.x * cosf(a * dt) - giant.face.z * sinf(a * dt), 0, giant.face.x * sinf(a * dt) + giant.face.z * cosf(a * dt) });
        } else if (giant_walk(WP[giant.wp], 1.5f, dt)) { giant.pause = frand_(2, 4); giant.wp = (giant.wp + 1 + GetRandomValue(0, NW - 2)) % NW; }
        if (giant.t <= 0) giant.state = 3;
        break;
    case 3:   // leaving
        if (giant_walk((Vector3){ 2.6f, 0, -17 }, 1.6f, dt)) { giant.state = 0; giant.t = frand_(35, 55); giant.pos = L.bed; giant.seeT = 0; }
        break;
    case 4:   // she knows where you are
        if (clear) { giant.target = P.pos; giant.lostT = 0; } else giant.lostT += dt;
        giant_walk(giant.target, 5.2f, dt);
        if (giant.lostT > 5) { giant.state = 2; giant.t = frand_(20, 30); giant.pause = 3; giant.heard = false; }
        if (d < 3.2f && clear) return true;
        return false;
    }
    // what she notices: anything out on the open floor in front of her, or anyone running
    Vector3 to = Vector3Normalize((Vector3){ P.pos.x - giant.pos.x, 0, P.pos.z - giant.pos.z });
    bool inView = d < 30 && Vector3DotProduct(to, giant.face) > 0.35f && clear;
    giant.seeT = inView ? giant.seeT + dt * (P.crouch > 0.5f ? 0.5f : 1.0f) : fmaxf(0, giant.seeT - dt * 0.5f);
    if (P.noise > 0.6f && d < 24) giant.heard = true;
    if (giant.seeT > 0.7f || giant.heard) {
        giant.state = 4; giant.target = P.pos; giant.lostT = 0; giant.heard = true;
        madness = 1; play_from(SFX_GIANT, giant.pos, 1.0f, 0.6f); giant.shake = 1;
    }
    (void)eye;
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

// ---------------------------------------------------------------- the fair: the carousel, the hammer, and the hall of mirrors
static bool fair_in_hall(Vector3 p) { return (fabsf(p.x) < 1.1f && p.z < -12.0f && p.z > -44.2f) || (fabsf(p.x) < 4.0f && p.z <= -44.0f && p.z > -50.0f); }
static const char *FORTUNES[] = {
    "the card says: YOU WILL GO ON A LONG JOURNEY. YOU WILL NOT COME BACK FROM IT.",
    "the card says: SOMEONE CLOSE TO YOU IS NOT WHO THEY SAY THEY ARE. IT IS YOU.",
    "the card says: A TALL STRANGER. HE IS NEARER THAN YESTERDAY.",
    "the card says: DO NOT OPEN YOUR EYES.",
    "the card says: YOUR MOTHER IS PROUD OF YOU. SHE IS STANDING BEHIND YOU.",
    "the card says: LUCKY NUMBER: 6. LUCKY ROOM: 6.",
    "the card is blank on both sides. it is warm.",
    "the card says: YOU HAVE BEEN ASLEEP FOR ELEVEN YEARS.",
    "the card says: THE NEXT THING YOU HEAR WILL BE A LIE.",
    "the card says: SOMEONE WILL ASK YOU TO SIT DOWN. DON'T.",
    "the card says: YOU ALREADY KNOW WHO IS IN THE BED.",
    "the card says: GOOD NEWS. THEY HAVE KEPT YOUR PLACE.",
    "the card says: THE WATER REMEMBERS YOU.",
    "the card says: COUNT THE DOORS. THEN COUNT THEM AGAIN.",
    "the card just says your name, over and over, in a child's handwriting.",
    "the card says: BEHIND YOU.",
};
static bool fair_update(float dt, Vector3 eye, Vector3 fwd) {
    if (L.id != W_FAIR) return false;
    fair.carA += dt * (fair.ride.on ? 0.55f : 0.22f);
    fair.wheelA += dt * 0.07f;
    if (fair.ham.t > 0) {   // the puck going up the pole, and coming down
        float before = fair.ham.t;
        fair.ham.t -= dt;
        if (before > 0.6f && fair.ham.t <= 0.6f) {
            if (fair.ham.rang) { audio_play_ex(SFX_BELL, 0.8f, 1.6f); say(fair.ham.tries >= 3 ? "DING. somewhere, a crowd that isn't there cheers, and stops all at once." : "DING. nobody cheers.", 4); }
            else { static const char *MISS[] = { "BABY.", "TINY. a child laughs, close behind you.", "WEAK. he could do it. he always could.", "NOT BAD. the puck stays at the top a moment too long, and then it comes down." }; say(MISS[GetRandomValue(0, 3)], 4); }
        }
    }
    if (fair.ride.on) {   // round and round. every time you come past, there are more of them watching
        fair.ride.t += dt;
        float a = fair.carA;
        P.pos = (Vector3){ L.heart.x + cosf(a) * 4.0f, 0.3f + 0.25f * sinf(a * 3.0f), L.heart.z + sinf(a) * 4.0f };
        P.vel = (Vector3){ 0 }; P.speedMeter = 0; P.noise = 0;
        int crowd = (int)(fair.ride.t / 3.0f);
        if (crowd > fair.ride.crowd && crowd <= 8) { fair.ride.crowd = crowd; play_behind(crowd % 2 ? SFX_CREAK : SFX_STEP, 0.5f, 0.7f); if (crowd == 4) say("there are people standing round the carousel now. they are all looking at you.", 4); }
        if (fair.ride.t > 23.0f) {
            fair.ride.on = false; fair.ride.crowd = 0;
            P.pos = (Vector3){ L.heart.x + cosf(a) * 5.8f, 0.05f, L.heart.z + sinf(a) * 5.8f };
            say("the music stops. there is nobody there. the horses go on round without you.", 5);
        }
    }
    // your history, for the one in the glass who is a little late
    fair.histT -= dt;
    if (fair.histT <= 0) {
        fair.histT = 1.0f / 30.0f;
        memmove(fair.hist + 1, fair.hist, (HIST - 1) * sizeof *fair.hist); memmove(fair.histYaw + 1, fair.histYaw, (HIST - 1) * sizeof *fair.histYaw);
        fair.hist[0] = P.pos; fair.histYaw[0] = P.yaw; if (fair.histN < HIST) fair.histN++;
    }
    Vector3 mv = { P.pos.x - fair.last.x, 0, P.pos.z - fair.last.z };
    float moved = fminf(Vector3Length(mv), 0.5f);
    fair.last = P.pos;
    fair.selfStride += moved * 3.2f;
    bool corr = fabsf(P.pos.x) < 1.1f && P.pos.z < -12.0f && P.pos.z > -44.0f;
    if (fair.dbl == 0 && corr && P.pos.z < -26.0f) {   // one of you stops
        fair.dbl = 1; fair.dpos = (Vector3){ -2.0f - P.pos.x, 0, P.pos.z };
        audio_play_ex(SFX_SWELL, 0.6f, 0.5f); madness = 1;
        say("in the glass on your left, one of you has stopped walking. its eyes are open.", 5);
    }
    if (fair.dbl == 1) {   // it keeps pace, a little way behind you, in the glass. it only moves when you do
        float want = P.pos.z + 5.0f, dz = want - fair.dpos.z;
        float step = fminf(fabsf(dz), moved * 0.95f);
        fair.dpos.z += dz > 0 ? step : -step;
        fair.dstride += step * 3.2f;
        Vector3 to = Vector3Subtract((Vector3){ fair.dpos.x, 1.6f, fair.dpos.z }, eye);   // (no ray: the glass is in the way of every ray, and you can still see through it)
        float tl = Vector3Length(to);
        bool seen = tl < 20.0f && Vector3DotProduct(Vector3Scale(to, 1.0f / tl), fwd) > 0.9f;
        fair.dstare = seen ? fair.dstare + dt : fmaxf(0, fair.dstare - dt * 0.5f);
        if (P.pos.z < -37.0f || fair.dstare > 2.5f || P.pos.z < -44.0f) {   // and then it comes through
            fair.dbl = 2; fair.dpos.x = -0.55f;
            audio_play_ex(SFX_SCREECH, 1.0f, 1.5f); audio_play_ex(SFX_BANG, 0.8f, 1.7f);
            blackout = 0.35f; glitch = 0.6f; madness = 1;
            say(fair.dstare > 2.5f ? "you looked at it for too long. the glass breaks." : "the glass breaks behind you.", 4);
        }
    } else if (fair.dbl >= 2) {   // out. every step you take, it takes one
        bool inside = fair_in_hall(P.pos);
        if (!inside && fair.dbl == 2) { fair.dbl = 3; say("it stops at the edge of the hall. it can't come out any further than the glass goes.", 5); }
        if (inside && fair.dbl == 3) fair.dbl = 2;
        if (fair.dbl == 2) {
            Vector3 d = { P.pos.x - fair.dpos.x, 0, P.pos.z - fair.dpos.z };
            float len = Vector3Length(d), step = fminf(len, moved * 0.93f);
            if (len > 0.01f) { fair.dpos = Vector3Add(fair.dpos, Vector3Scale(d, step / len)); fair.dstride += step * 3.2f; }
            if (len < 0.85f) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- the lake: the ice, and what is under it
static bool lake_on_ice(void) {
    Vector3 h = L.heart;
    if (fabsf(P.pos.x - h.x) < 1.75f && fabsf(P.pos.z - h.z) < 1.75f) return false;   // the hut's floor
    if (fabsf(P.pos.x) < 1.1f && P.pos.z > 43.9f) return false;                         // the jetty
    return P.pos.y < 0.35f && P.grounded;
}
static bool lake_thin(Vector3 p) {
    for (int i = 0; i < L.blooms.size; i++) { const Bloom *b = &L.blooms.data[i]; if (Vector3Distance((Vector3){ p.x, 0, p.z }, b->pos) < b->size * 0.85f) return true; }
    return false;
}
static bool lake_update(float dt, Vector3 eye) {
    if (L.id != W_LAKE) return false;
    lake.shake = fmaxf(0, lake.shake - dt * 1.5f);
    for (int i = 0; i < L.effigies.size; i++) {   // the people standing out on the ice are never there when you get to them
        Effigy *e = &L.effigies.data[i];
        if (e->pos.y > -10 && Vector3Distance(e->pos, P.pos) < 13.0f) { e->pos.y = -100; play_behind(SFX_ICE, 0.35f, 1.3f); }
    }
    lake.singT -= dt;
    if (lake.singT <= 0) {   // the whole lake sings, now and then, somewhere off in the dark
        lake.singT = frand_(5, 12);
        float a = frand_(0, 6.28f);
        play_from(SFX_ICE, (Vector3){ P.pos.x + sinf(a) * 30, 0, P.pos.z + cosf(a) * 30 }, 0.6f, frand_(0.7f, 1.2f));
    }
    lake.tugT -= dt;
    if (lake.tugT <= 0) { lake.tugT = frand_(14, 26); lake.tug2 = 0.6f; if (Vector3Distance(P.pos, (Vector3){ 3, 0, 41 }) < 25) play_from(SFX_CREAK, (Vector3){ 3.6f, 0.4f, 41.4f }, 0.8f, 0.6f); }
    lake.tug2 = fmaxf(0, lake.tug2 - dt);
    bool on = lake_on_ice(), thin = on && lake_thin(P.pos);
    if (on) {   // standing still, the ice under you starts to go
        if (Vector3Distance((Vector3){ P.pos.x, 0, P.pos.z }, (Vector3){ lake.crack.x, 0, lake.crack.z }) > (thin ? 99.0f : 1.3f)) {
            if (lake.stress > 0.2f && lake.ncr < CRACKS) { lake.cracks[lake.ncr].p = lake.crack; lake.cracks[lake.ncr].r = lake.stress; lake.ncr++; }
            lake.crack = P.pos; lake.stress = 0; lake.stage = 0;
        }
        if (thin) lake.crack = P.pos;
        bool still = P.speedMeter < 0.5f;
        float rate = thin ? (still ? 0.55f : P.speedMeter > 4.5f ? 0.35f : 0.16f) : (still ? 0.21f : 0.0f);
        if (P.crouch > 0.5f) rate *= thin ? 0.15f : 0.4f;
        lake.stress = rate > 0 ? lake.stress + rate * dt : fmaxf(0, lake.stress - 0.05f * dt);
        static const float STAGE[3] = { 0.3f, 0.6f, 0.85f };
        if (lake.stage < 3 && lake.stress > STAGE[lake.stage]) {
            audio_play_ex(SFX_ICE, 0.5f + 0.2f * lake.stage, 1.4f + 0.3f * lake.stage);
            if (lake.stage == 0 && !lake.warned) { lake.warned = true; say("the ice groans under you. keep moving.", 4); }
            if (lake.stage == 2) say("it's going.", 2);
            lake.stage++;
        }
        if (lake.stress >= 1.0f) return true;
    } else { lake.stress = fmaxf(0, lake.stress - dt * 0.5f); lake.stage = 0; lake.crack = P.pos; }
    // the thing under the ice. it circles you, and if you run, or land hard, it comes to where it heard you, fast
    if (!lake.on) {
        if (L.t < 18) return false;
        float a = frand_(0, 6.28f);
        lake.thing = (Vector3){ P.pos.x + sinf(a) * 32, 0, P.pos.z + cosf(a) * 32 }; lake.on = true; lake.dir = (Vector3){ 0, 0, 1 };
        play_from(SFX_BANG, lake.thing, 0.4f, 0.4f);
    }
    lake.cool -= dt;
    if (on && P.noise > 0.6f && lake.cool <= 0 && lake.state == 0) {
        lake.state = 1; lake.heard = P.pos;
        if (!lake.heardOnce) { lake.heardOnce = true; say("far off under the ice, something turns toward the sound.", 4); }
        play_from(SFX_ICE, lake.thing, 0.8f, 0.5f);
    }
    Vector3 tgt; float sp;
    if (lake.state == 1) { tgt = lake.heard; sp = 8.5f; }
    else { float a = L.t * 0.13f; tgt = (Vector3){ P.pos.x + sinf(a) * 9.0f, 0, P.pos.z + cosf(a) * 9.0f }; sp = 2.2f; }
    Vector3 d = { tgt.x - lake.thing.x, 0, tgt.z - lake.thing.z };
    float len = Vector3Length(d);
    if (len > 0.05f) { lake.dir = Vector3Lerp(lake.dir, Vector3Scale(d, 1.0f / len), fminf(1, dt * 2.0f)); lake.thing = Vector3Add(lake.thing, Vector3Scale(d, fminf(len, sp * dt) / len)); }
    lake.thing.x = Clamp(lake.thing.x, -58, 58); lake.thing.z = Clamp(lake.thing.z, -58, 42);
    if (lake.state == 1 && len < 1.0f) {   // it hits the ice from underneath
        lake.state = 0; lake.cool = 5.0f;
        float near = Vector3Distance((Vector3){ P.pos.x, 0, P.pos.z }, (Vector3){ lake.heard.x, 0, lake.heard.z });
        play_from(SFX_BANG, lake.thing, 1.0f, 0.42f); play_from(SFX_ICE, lake.thing, 1.0f, 0.8f);
        if (near < 3.0f && on) { lake.shake = 1; lake.stress += 0.55f; lake.crack = P.pos; madness = 1; say("it hits the ice right under your feet.", 3); }
        else lake.shake = fmaxf(lake.shake, 0.4f);
    }
    (void)eye;
    return false;
}

// ---------------------------------------------------------------- the school: hide and seek
// it counts to ten at home with its face to the wall, banging its forehead on the wall at every number. then it walks
// the school, and checks the cupboards. it hears a door shut after ten. it looks first where you hid last time
static const struct { Vector3 p; int check; } ROUTE[] = {
    { { 0, 0, 3 }, -1 }, { { 0, 0, -1.0f }, -1 }, { { -3, 0, -1.0f }, -1 }, { { -6, 0, 2.6f }, -1 }, { { -10.8f, 0, 2.6f }, 0 },
    { { -11.2f, 0, -1.8f }, -1 }, { { -10.8f, 0, -6.4f }, 1 }, { { -6, 0, -6.6f }, -1 }, { { -3, 0, -1.0f }, -1 }, { { 0, 0, -1.0f }, -1 },
    { { 0, 0, -3.0f }, -1 }, { { 3, 0, -3.0f }, -1 }, { { 9, 0, -3.0f }, -1 }, { { 14.6f, 0, -8.6f }, 6 }, { { 9, 0, -9.6f }, -1 },
    { { 3, 0, -3.0f }, -1 }, { { 0, 0, -3.0f }, -1 }, { { 1.0f, 0, -8.5f }, 4 }, { { 1.0f, 0, -13.5f }, 5 }, { { 0, 0, -20 }, -1 },
    { { -3, 0, -20 }, -1 }, { { -6, 0, -16.5f }, -1 }, { { -10.8f, 0, -15.6f }, 2 }, { { -10.8f, 0, -25.0f }, 3 }, { { -5, 0, -26.5f }, -1 },
    { { -3, 0, -20 }, -1 }, { { 0, 0, -20 }, -1 }, { { 0, 0, -22 }, -1 }, { { 3, 0, -22 }, -1 }, { { 7.8f, 0, -25.0f }, 7 },
    { { 4, 0, -23 }, -1 }, { { 0, 0, -22 }, -1 }, { { 0, 0, -30 }, -1 }, { { 0, 0, -12 }, -1 }, { { 0, 0, 3 }, -1 },
};
#define NROUTE ((int)(sizeof ROUTE / sizeof *ROUTE))
static Vector3 hide_n(int face) { static const Vector3 N[4] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } }; return N[face & 3]; }
static void seek_trail_push(Vector3 p) {
    if (seek.trailN && Vector3Distance(seek.trail[seek.trailN - 1], p) < 0.4f) return;
    if (seek.trailN == STRAIL) { memmove(seek.trail, seek.trail + 1, (STRAIL - 1) * sizeof *seek.trail); seek.trailN--; if (seek.trailI > 0) seek.trailI--; }
    seek.trail[seek.trailN++] = p;
}
static void seek_follow_start(void) {   // find where on your trail it can join you from
    seek.trailI = seek.trailN - 1;
    for (int i = seek.trailN - 1; i >= 0; i--) if (ray_clear((Vector3){ seek.pos.x, 1.2f, seek.pos.z }, (Vector3){ seek.trail[i].x, 1.2f, seek.trail[i].z })) { seek.trailI = i; break; }
}
static bool seek_walk(Vector3 to, float sp, float dt) {
    Vector3 d = { to.x - seek.pos.x, 0, to.z - seek.pos.z };
    float len = Vector3Length(d);
    if (len < 0.15f) return true;
    seek.face = Vector3Lerp(seek.face, Vector3Scale(d, 1.0f / len), fminf(1, dt * 6));
    float st = fminf(len, sp * dt);
    seek.pos = Vector3Add(seek.pos, Vector3Scale(d, st / len));
    seek.stride += st * 2.4f;
    return false;
}
static bool seek_follow(float sp, float dt) {   // along the way you went
    if (seek.trailI >= seek.trailN) seek.trailI = seek.trailN - 1;
    if (seek.trailI < 0) return seek_walk(P.pos, sp, dt);
    Vector3 t = seek.trail[seek.trailI];
    if (seek_walk(t, sp, dt) && seek.trailI < seek.trailN - 1) seek.trailI++;
    return seek.trailI >= seek.trailN - 1 && Vector3Distance((Vector3){ seek.pos.x, 0, seek.pos.z }, (Vector3){ t.x, 0, t.z }) < 0.3f;
}
static const char *NUMBERS[10] = { "...one...", "...two...", "...three...", "...four...", "...five...", "...six...", "...seven...", "...eight...", "...nine...", "...TEN." };
static bool school_update(float dt, Vector3 eye) {
    if (L.id != W_SCHOOL) return false;
    if (!hide.on) seek_trail_push(P.pos);
    for (int i = 0; i < L.effigies.size; i++) {   // the boy in the corner
        Effigy *e = &L.effigies.data[i];
        if (e->pos.y > -10 && Vector3Distance(e->pos, P.pos) < 3.2f) { e->pos.y = -100; play_behind(SFX_GIGGLE, 0.7f, 1.2f); blackout = 0.15f; }
    }
    Vector3 head = { seek.pos.x, 2.5f, seek.pos.z }, chest = { P.pos.x, P.pos.y + 1.1f - P.crouch * 0.5f, P.pos.z };
    float d = Vector3Distance((Vector3){ seek.pos.x, 0, seek.pos.z }, (Vector3){ P.pos.x, 0, P.pos.z });
    bool canSee = false;
    if (!hide.on && seek.state != 0 && seek.state != 3) {
        Vector3 to = Vector3Subtract(chest, head);
        float tl = Vector3Length(to), range = P.crouch > 0.5f ? 8.0f : 16.0f;
        Vector3 tn = Vector3Normalize((Vector3){ to.x, 0, to.z });
        canSee = tl < range && Vector3DotProduct(tn, seek.face) > 0.2f && ray_clear(head, chest);
        if (!canSee && P.noise > 0.6f && d < 14.0f && ray_clear(head, chest)) canSee = true;   // it heard you, and it turned round
    }
    if (canSee) {
        seek.state = 3; seek.lostT = 0; seek_follow_start();
        audio_play_ex(SFX_GIGGLE, 1.0f, 0.8f); play_from(SFX_ROAR, seek.pos, 0.7f, 1.4f); madness = 1;
        say("it has taken its hands away from its face.", 4);
    }
    float sp = seek.round == 0 ? 2.1f : 2.8f;
    switch (seek.state) {
    case 0: {   // counting
        seek.face = (Vector3){ 0, 0, 1 }; seek.pos = L.bed;
        seek.t -= dt;
        if (seek.t <= 0 && seek.count < 10) {
            say(NUMBERS[seek.count], 1.4f);
            play_from(SFX_THUD, (Vector3){ L.bed.x, 2.5f, L.bed.z + 0.6f }, 0.8f, 0.9f);
            seek.count++;
            seek.t = seek.round == 0 ? 1.9f : 1.25f;
        } else if (seek.t <= 0) {
            say(seek.round == 0 ? "READY OR NOT, HERE I COME." : "READY OR NOT. I KNOW WHERE YOU HIDE.", 3);
            audio_play_ex(SFX_GIGGLE, 0.8f, 1.0f);
            seek.state = 1; seek.wp = 0; seek.pause = 0; seek.lookedUp = false;
            if (hide.on && hide.idx == seek.lastSpot && seek.round > 0) { seek.state = 4; seek.heardDoor = hide.idx; seek_follow_start(); }
        }
        return false;
    }
    case 1: {   // walking the school, checking the cupboards on the way
        if (seek.pause > 0) {
            seek.pause -= dt;
            int c = ROUTE[seek.wp].check;
            seek.breathT -= dt;
            if (c >= 0 && hide.on && hide.idx == c && seek.breathT <= 0) {
                seek.breathT = 9;
                play_from(SFX_BREATH, seek.pos, 0.9f, 0.7f);
                say(GetRandomValue(0, 1) ? "it is right outside. you can hear it breathing through the slats." : "it has put its hand on the handle.", 3);
            }
            if (c >= 0) for (int i = 0; i < L.uses.size; i++) if (L.uses.data[i].kind == USE_HIDE && (L.uses.data[i].arg & 15) == c) seek.face = Vector3Negate(hide_n(L.uses.data[i].arg >> 4));   // it stands and faces the door
            if (seek.pause <= 0) seek.wp++;
            break;
        }
        if (seek.wp >= NROUTE) { seek.state = 2; break; }
        if (seek_walk(ROUTE[seek.wp].p, sp, dt)) {
            seek.pause = ROUTE[seek.wp].check >= 0 ? 2.2f : (seek.wp == 14 ? 2.5f : 0.0f);
            if (seek.pause <= 0) seek.wp++;
            else play_from(SFX_CREAK, seek.pos, 0.4f, 0.8f);
        }
        break;
    }
    case 2:     // it gives up, and goes back to start again
        if (seek_walk(L.bed, sp, dt)) {
            seek.round++; seek.state = 0; seek.count = 0; seek.t = 2.5f;
            seek.lastSpot = hide.on ? hide.idx : -1;
            say(seek.round == 1 ? "...i give up... let's go again." : "...i give up... again.", 4);
            audio_play_ex(SFX_GIGGLE, 0.6f, 1.1f);
            if (seek.round >= 2 && !seek.unlocked) {
                seek.unlocked = true;
                for (int i = 0; i < L.pickups.size; i++) L.pickups.data[i].locked = false;
                play_from(SFX_SCREECH, (Vector3){ 4.2f, 0.7f, -26.9f }, 0.5f, 1.6f);
                say("somewhere, a cupboard clicks open. lost property.", 5);
            }
        }
        break;
    case 3: {   // it has seen you. it runs where you went
        seek_follow(4.9f, dt);
        bool sees = !hide.on && ray_clear(head, chest);
        seek.lostT = sees ? 0 : seek.lostT + dt;
        if (hide.on && seek.lostT < 2.0f) { seek.state = 4; seek.heardDoor = hide.idx; }
        if (d < 1.0f && !hide.on) return true;
        if (seek.lostT > 6.0f) {   // lost you. it covers its eyes again and goes back to looking
            seek.state = 1;
            float bd = 1e9f; for (int i = 0; i < NROUTE; i++) { float dd = Vector3Distance(ROUTE[i].p, seek.pos); if (dd < bd) { bd = dd; seek.wp = i; } }
            say("it has covered its eyes again.", 3);
        }
        break;
    }
    case 4:     // it heard the door. it knows which one
        if (seek_follow(3.6f, dt) || (hide.on && Vector3Distance((Vector3){ seek.pos.x, 0, seek.pos.z }, (Vector3){ hide.out.x, 0, hide.out.z }) < 0.6f)) {
            if (hide.on) { seek.found = 1; return true; }
            seek.state = 1;
        }
        break;
    }
    (void)eye;
    return false;
}
static void hide_in(Use *u) {
    int idx = u->arg & 15;
    if (seek.occupied == idx) {
        say("there is someone already in here: a child, hugging its knees. it puts a finger to its lips.", 5);
        audio_play_ex(SFX_GIGGLE, 0.5f, 1.3f); madness = 1;
        return;
    }
    hide.on = true; hide.idx = idx; hide.n = hide_n(u->arg >> 4);
    hide.out = (Vector3){ u->pos.x, 0.05f, u->pos.z };
    P.pos = (Vector3){ u->pos.x - hide.n.x * 0.3f, 0.05f, u->pos.z - hide.n.z * 0.3f };   // (just behind the slats, looking out)
    P.vel = (Vector3){ 0 }; P.crouch = 0; P.noise = 0;
    P.yaw = atan2f(hide.n.x, -hide.n.z) * RAD2DEG; P.pitch = 0;
    audio_play_ex(SFX_CREAK, 0.6f, 1.2f);
    if ((seek.state == 1 || seek.state == 3) && Vector3Distance(seek.pos, P.pos) < 24.0f) {
        seek.state = 4; seek.heardDoor = idx; seek_follow_start();
        say("the door clicks shut. somewhere in the school, it has stopped walking.", 4);
    }
}
static void hide_out(void) {
    hide.on = false;
    P.pos = hide.out; P.vel = (Vector3){ 0 };
    audio_play_ex(SFX_CREAK, 0.6f, 1.0f);
}

// ---------------------------------------------------------------- the house: the cat, the radio, the piano, and what the house is turning into
static void cat_trail_push(Vector3 p) {
    if (cat.trailN && Vector3Distance(cat.trail[cat.trailN - 1], p) < 0.35f) return;
    if (cat.trailN == CAT_TRAIL) { memmove(cat.trail, cat.trail + 1, (CAT_TRAIL - 1) * sizeof *cat.trail); cat.trailN--; if (cat.idx > 0) cat.idx--; }
    cat.trail[cat.trailN++] = p;
}
static bool threat(Vector3 *out);
static void cat_update(float dt) {
    bool here = L.id == W_HUB || (cat.withYou && !cat.gone);
    if (!here) return;
    cat.t += dt;
    cat_trail_push(P.pos);
    float dp = Vector3Distance((Vector3){ cat.pos.x, 0, cat.pos.z }, (Vector3){ P.pos.x, 0, P.pos.z });
    Vector3 tp; float td = 1e9f;
    bool th = L.id != W_HUB && threat(&tp);
    if (th) td = Vector3Distance(tp, cat.pos);
    cat.fear += ((th && td < 16.0f ? 1.0f : 0.0f) - cat.fear) * fminf(1, dt * 3);
    cat.lookAt = th && td < 16.0f ? (Vector3){ tp.x, tp.y + 1.0f, tp.z } : player_eye(&P);
    if (cat.mode == 3) {   // bolting, back the way you came, and gone
        cat.t += dt;
        Vector3 away = Vector3Normalize((Vector3){ cat.pos.x - tp.x, 0, cat.pos.z - tp.z });
        cat.pos = Vector3Add(cat.pos, Vector3Scale(away, 6.5f * dt)); cat.stride += dt * 18; cat.yaw = atan2f(away.x, -away.z);
        if (cat.t > 2.0f) cat.gone = true;
        return;
    }
    if (th && td < 16.0f) {   // it knows. it turns to face it, and hisses
        cat.yaw = atan2f(tp.x - cat.pos.x, -(tp.z - cat.pos.z));
        cat.hissT -= dt;
        if (cat.hissT <= 0) { cat.hissT = frand_(1.8f, 3.0f); play_from(SFX_HISS, cat.pos, 0.9f, frand_(0.9f, 1.1f)); }
        if (td < 6.5f) { cat.mode = 3; cat.t = 0; play_from(SFX_HISS, cat.pos, 1.0f, 1.3f); say("the cat bolts.", 3); }
        return;
    }
    if (cat.mode == 0) {   // asleep, until you come near
        if (dp < 3.5f && cat.sulk <= 0) { cat.mode = 1; cat_reset_trail(); if (!cat.told) { cat.told = true; say("the cat wakes, and stretches, and comes to you.", 4); } }
        return;
    }
    if (cat.sulk > 0) {   // after what happened last time, it won't look at you
        cat.sulk -= dt;
        cat.mode = 2;
        cat.yaw = atan2f(-(P.pos.x - cat.pos.x), (P.pos.z - cat.pos.z));
        return;
    }
    if (dp < 1.5f || cat.trailN == 0) {   // close enough. sit, and watch you
        if (P.speedMeter < 0.4f) cat.mode = 2;
        Vector3 to = { P.pos.x - cat.pos.x, 0, P.pos.z - cat.pos.z };
        if (Vector3Length(to) > 0.05f) cat.yaw = atan2f(to.x, -to.z);
        cat.idx = cat.trailN > 0 ? cat.trailN - 1 : 0;
        return;
    }
    cat.mode = 1;
    if (dp > 30.0f && cat.trailN > 2) { cat.idx = cat.trailN - 3; cat.pos = cat.trail[cat.idx]; }   // it finds its own way
    while (cat.idx < cat.trailN - 1 && Vector3Distance(cat.trail[cat.idx], P.pos) > 1.4f && Vector3Distance(cat.trail[cat.idx], cat.pos) < 0.2f) cat.idx++;
    Vector3 t = cat.trail[cat.idx];
    Vector3 d = Vector3Subtract(t, cat.pos);
    float len = Vector3Length(d), sp = dp > 4.0f ? 4.6f : 2.0f;
    if (len > 0.01f) {
        float st = fminf(len, sp * dt);
        cat.pos = Vector3Add(cat.pos, Vector3Scale(d, st / len));
        cat.yaw = atan2f(d.x, -d.z); cat.stride += st * 9.0f;
    }
    if (len < 0.2f && cat.idx < cat.trailN - 1) cat.idx++;
}
static const char *RADIO_ST[5][5] = {
    { "THE WEATHER. tonight in the city: rain. it has rained there for eleven years.", "THE WEATHER. out on the lake, minus forty. do not stand still.", "THE WEATHER. over the steps: clear skies, and an eye.", "THE WEATHER. a warm front, coming up from below. it is breathing.", "THE WEATHER. no change in the house. the house is always the same." },
    { "LOST AND FOUND. found, at the top of a shaft: a pair of gloves, still warm.", "LOST AND FOUND. lost: one boy, eleven years ago. answers to nothing.", "LOST AND FOUND. found at the bottom of the pool: a lamp. please collect before the water comes back.", "LOST AND FOUND. lost: one reflection. last seen in the hall of mirrors. do not approach it.", "LOST AND FOUND. found: a cat. it says it is yours. it says you left it." },
    { "REQUESTS. this one goes out to the boy in room six, from his mother. she says: wake up. she says: don't.", "REQUESTS. from the congregation of the lower church: please, everyone, keep very still.", "REQUESTS. a request from the children at the school: come out, come out, wherever you are.", "REQUESTS. for the family at the dinner table: your guest is on his way.", "REQUESTS. this one is for you. it was always for you." },
    { "THE NEWS. police are no closer to finding the family last seen sitting down to dinner.", "THE NEWS. the fair is in town again. nobody saw it arrive.", "THE NEWS. a man of very great height was seen in the city, standing at the end of a street. he was nearer when they looked again.", "THE NEWS. the lake has frozen over. there are faces in it.", "THE NEWS. a boy has not woken up. that is all the news there is." },
    { "...is anyone listening? if you can hear this, you are asleep.", "...don't go up to the attic. don't go up to the attic. don't...", "...it's me. it's you. i can hear you breathing through the speaker...", "...stay in the house. stay in the house. stay...", "...the lullaby is wrong. someone has changed one note. listen..." },
};
static const char *STATION[5] = { "the weather", "lost and found", "requests", "the news", "nothing, between stations" };
static const char *FRIDGE[] = {
    "a plate under a cloth. a label on it in your mother's writing: FOR HIM. SUNDAY.",
    "milk. it is warm.",
    "the light inside doesn't come on. something in the back of it is breathing out cold air.",
    "a birthday cake with eleven candles in it. none of it has been eaten. the icing says your name.",
    "jars of something dark, in rows, all labelled with the same date.",
    "it is completely empty, and very clean, and it smells of the baths.",
};
static const char *SCOPE[] = {
    "across the fields there is another house, exactly like this one. a light is on in its attic.",
    "in the attic of the other house, someone is standing at the window with a telescope, looking back at you.",
    "the other house is nearer than it was.",
    "the other house is right across the road now. its front door is standing open.",
    "you can't see the other house. something is standing very close to the glass, blocking the view. it is breathing on it.",
    "you see yourself, from outside, standing at the attic window, looking through a telescope. behind you, the dust sheet is gone.",
};
static const char *PET[] = { "it purrs.", "it pushes its head into your hand.", "it has been waiting for you.", "it purrs, and looks past you at the stairs.", "it is very warm. it is the only warm thing." };
static char diary[900];
static void hub_update(float dt) {
    if (L.id != W_HUB) return;
    if (g_rot >= 3) {   // someone in the armchair. come close and it is only a dent in the cushion
        for (int i = 0; i < L.effigies.size; i++) {
            Effigy *e = &L.effigies.data[i];
            if (e->kind == FIG_SEATED && e->pos.y > -10 && Vector3Distance(e->pos, P.pos) < 5.5f) {
                e->pos.y = -100; play_from(SFX_CREAK, e->pos, 0.8f, 0.6f);
                say("the armchair is empty. the cushion is still warm.", 4); house.sitterGone = true;
            }
        }
        house.knockT -= dt;
        if (house.knockT <= 0) {   // and someone on the other side of the way out, knocking to be let in
            house.knockT = frand_(35, 70);
            play_from(SFX_KNOCK, (Vector3){ 38.3f, -1.8f, 0 }, 0.8f, 0.75f);
            if (g_rot >= 5) play_from(SFX_GIGGLE, (Vector3){ 15, 4, -12 }, 0.4f, 1.0f);
        }
    }
    if (house.humT > 0) {
        house.humT -= dt;
        if (house.humT <= 0) { if (g_rot >= 3) play_from(SFX_GIGGLE, (Vector3){ 15, 4, -11 }, 0.6f, 1.0f); else play_from(SFX_HUM, (Vector3){ 15, 4, -11 }, 0.5f, 1.0f); }
    }
}
// whichever of them is hunting you here, if any, and where it is
static bool threat(Vector3 *out) {
    Vector3 eye = player_eye(&P), best = { 0 };
    float bd = 1e9f; bool any = false;
    #define CAND(p) do { Vector3 q_ = (p); float d_ = Vector3Distance(q_, P.pos); if (d_ < bd) { bd = d_; best = q_; any = true; } } while (0)
    switch (L.id) {
    case W_BATHS:   if (swim.state >= 2) CAND(swim.pos); break;
    case W_NURSERY: if (giant.state > 0) CAND(giant.pos); break;
    case W_WARD:    if (g_wardLoop >= 3) CAND(ward_pt(fmaxf(0, mom.s))); break;
    case W_STATIC:  if (grey.on) CAND(wp(grey.pos, eye)); break;
    case W_DINNER:  if (dinner.host.x != 0 || dinner.host.y != 0) CAND(wp(dinner.host, eye)); break;
    case W_WOMB:    if (below.state == 2) CAND(below.pos); break;
    case W_CITY:    if (tallm.on) CAND(wp(tallm.pos, eye)); break;
    case W_MORGUE:  if (morg.walking) CAND(morg.walker); break;
    case W_THEATRE: for (int i = 0; i < 3; i++) CAND(show.pup[i]); break;
    case W_CHAPEL:  if (mass.phase == 2) { const Effigy *pr = priest_of(); if (pr) CAND(pr->pos); } break;
    case W_SHAFT:   if (climber.on) CAND(climber.pos); break;
    case W_FAIR:    if (fair.dbl >= 1) CAND(fair.dpos); break;
    case W_LAKE:    if (lake.on) CAND(((Vector3){ lake.thing.x, P.pos.y, lake.thing.z })); break;
    case W_SCHOOL:  if (seek.state >= 1) CAND(seek.pos); break;
    default: break;
    }
    #undef CAND
    *out = best;
    return any;
}

static void on_caught(void) {
    if (cat.withYou && !cat.gone) cat.sulk = 90;   // the cat saw. it will hold it against you for a while
    hide.on = false; fair.ride.on = false;
}

static void scare_update(float dt) {
    if (L.id == W_HUB) { blackout = 0; L.nearest = 99; return; }   // nothing follows you into the house
    Vector3 eye = player_eye(&P), fwd = player_forward(&P);
    // ---- the game hitches: everything stops and the sound drops out, then it all lurches back
    if (dreams >= 3 && !tr.on) {
        nextFreeze -= dt;
        if (nextFreeze <= 0) { freezeT = frand_(0.5f, 1.1f); nextFreeze = frand_(150, 300); haunt_title(); }
    }
    // ---- phantom footsteps and knocks while you stand still in a haunted place
    if (dreams >= 1 && L.id != W_END && P.speedMeter < 0.5f && !tr.on) {
        phantomT -= dt;
        if (phantomT <= 0) {
            phantomT = frand_(25, 50);
            if (GetRandomValue(0, 2) == 0) play_behind(GetRandomValue(0, 1) ? SFX_KNOCK : SFX_CREAK, 0.4f, frand_(0.8f, 1.0f));
            else { phantomLeft = GetRandomValue(3, 6); stepGap = 0; }
        }
    }
    if (phantomLeft > 0) {
        stepGap -= dt;
        if (stepGap <= 0) { play_behind(SFX_STEP, 0.3f, 0.6f); stepGap = 0.62f; phantomLeft--; }
    }
    // ---- blackouts: the lights just stop. in the drains, things keep walking.
    bool haunted = L.id == W_BATHS;
    if (blackout > 0) {
        blackout -= dt;
        if (blackout <= 0 && dreams >= 2 && !catchS.on && GetRandomValue(0, 3) == 0) { subT = 0.075f; audio_play_ex(SFX_CLICK, 1.0f, 1.5f); }   // the lights come back, and for a moment, it is right there
    } else if (haunted && !tr.on) {
        nextBlackout -= dt;
        if (nextBlackout <= 0) {
            blackout = frand_(0.9f, 2.0f);
            nextBlackout = fmaxf(30.0f, 60.0f - dreams * 2.0f) * frand_(0.7f, 1.3f);
            audio_play(SFX_KNOCK);
        }
    }
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
        if (L.id == W_CHAPEL && e->kind == FIG_PENITENT && !e->seen && e->yaw < 1.0f && GetRandomValue(0, 100000) < (int)(dt * 100000 * fminf(0.02f, L.t * 0.0002f)))
            e->yaw = 3.14159f;   // when you look again, this one is facing the back of the church. facing you
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
        if (lurkTimer <= 0 && !tr.on && !anyLurk && dreams >= 2) {
            lurkTimer = frand_(70, 130);
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
    switch (f) {
    case FX_LAMP: return (Color){ 255, 225, 120, 255 };   case FX_GLOVES: return (Color){ 255, 140, 60, 255 };
    case FX_BOOTS: return (Color){ 190, 140, 255, 255 };  case FX_FEATHER: return (Color){ 255, 150, 220, 255 };
    case FX_VEIL: return (Color){ 200, 30, 30, 255 };     case FX_COMPASS: return (Color){ 110, 230, 255, 255 };
    case FX_MATCHES: return (Color){ 255, 110, 40, 255 }; default: return (Color){ 180, 240, 170, 255 };
    }
}

static Color scale_col(Color c, float k) {
    return (Color){ (unsigned char)fminf(255, c.r * k), (unsigned char)fminf(255, c.g * k), (unsigned char)fminf(255, c.b * k), 255 };
}

// ---------------------------------------------------------------- each of them, at the moment it has you
static float smooth01(float x) { x = Clamp(x, 0, 1); return x * x * (3 - 2 * x); }
static void draw_at_pivot(const Fig *f, Vector3 pivot, Vector3 axis, float deg) {   // a figure turned about some point: bending down over you, or hanging upside down
    rlPushMatrix();
    rlTranslatef(pivot.x, pivot.y, pivot.z);
    rlRotatef(deg, axis.x, axis.y, axis.z);
    rlTranslatef(-pivot.x, -pivot.y, -pivot.z);
    figure_draw(f);
    rlPopMatrix();
}
static void draw_catch(Vector3 eye, float time) {
    float t = catchS.t;
    Vector3 f = player_forward(&P); f.y = 0; f = Vector3Normalize(f);
    Vector3 r = { -f.z, 0, f.x }, floor = { eye.x, eye.y - 1.58f, eye.z };
    float yawToEye = atan2f(-f.x, f.z);   // facing back at you
    // somewhere `dist` in front of you, with its head `headY` above or below your eyes
    #define AT(dist, headY, hy, hz) ((Vector3){ eye.x + f.x * ((dist) + (hz)), eye.y + (headY) - (hy), eye.z + f.z * ((dist) + (hz)) })
    Fig g = { catchS.kind, eye, yawToEye, t * 8.0f, eye, 1.0f, 0.25f, t * 3.0f, { 0, 0, 1 }, catchS.tint, catchS.scale };
    switch (catchS.style) {
    case CS_POUNCE: {   // a moment of nothing but clicking, then it comes up off the floor at you
        float k = smooth01((t - 0.32f) / 0.15f);
        g.pos = AT(1.6f - 1.15f * k, -1.4f + 1.4f * k, 1.05f, 0.6f);
        g.tilt = 0.4f + sinf(t * 40) * 0.25f * k;
        gfx_set_light(0.9f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    }
    case CS_DROWN: {    // you go down, and a pale face comes up toward you through the dark water
        float k = smooth01((t - 0.55f) / 0.8f);
        g.pos = AT(1.3f - 0.85f * k, -1.0f + 0.95f * k, 1.05f, 0.6f);
        gfx_set_light(0.75f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    }
    case CS_ARMS: {     // arms out of the walls, closing in from every side until they cover your eyes
        Vector3 u = { 0, 1, 0 };
        float k = smooth01(t / 0.95f);
        gfx_set_light(0.8f, 0, 0.3f, 6, 0);
        for (int i = 0; i < 12; i++) {
            float a = i * 0.5236f + 0.2f;
            Vector3 dir = Vector3Add(Vector3Scale(r, cosf(a)), Vector3Scale(u, sinf(a) * 0.8f));
            Vector3 root = Vector3Add(Vector3Add(eye, Vector3Scale(dir, 1.7f)), Vector3Scale(f, 0.9f));
            Vector3 tip = Vector3Add(Vector3Add(eye, Vector3Scale(dir, 0.9f * (1 - k) + 0.04f)), Vector3Scale(f, 0.55f - 0.25f * k));
            Vector3 el = Vector3Add(Vector3Lerp(root, tip, 0.5f), Vector3Scale(f, 0.2f));
            Color sk = { 196, 170, 166, 255 };
            gfx_limb(root, el, 0.07f, 0.055f, TEX_SKIN, sk);
            gfx_limb(el, tip, 0.055f, 0.04f, TEX_SKIN, sk);
            for (int j = 0; j < 4; j++) gfx_limb(tip, Vector3Add(tip, Vector3Add(Vector3Scale(dir, -0.18f), Vector3Scale(u, (j - 1.5f) * 0.04f))), 0.014f, 0.006f, TEX_SKIN, sk);
        }
        break;
    }
    case CS_MOTHER: {   // she is already there, a hand's width away, head bent right over, not moving. then her head comes up
        float k = smooth01((t - 0.95f) / 0.2f);
        g.pos = AT(0.65f - 0.3f * k, 0, 1.8f, 0.07f);
        g.tilt = 1.3f * (1 - k) + sinf(t * 50) * 0.08f * k;
        g.t = floorf(t * 6) / 6;
        gfx_set_light(0.7f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    }
    case CS_GIANT: {    // you look up at her, and her hand comes down over you
        Fig m = g; m.pos = (Vector3){ floor.x + f.x * 6, floor.y, floor.z + f.z * 6 }; m.look = 1;
        gfx_set_light(0.6f, 0, 0.3f, 6, 0);
        figure_draw(&m);
        float k = smooth01((t - 0.25f) / 1.0f);
        Vector3 hp = { eye.x + f.x * 1.1f, eye.y + 16.0f - 9.2f * k, eye.z + f.z * 1.1f };
        Fig h = { FIG_HAND, hp, yawToEye, 0, eye, 0, 0, -3.49f + k * 6.98f, { 0, 0, 1 }, { 0 }, 2.4f };
        draw_at_pivot(&h, hp, r, 180);
        break;
    }
    case CS_PRIEST: {   // the skull comes down to you, slowly
        float k = smooth01(t / 1.4f);
        g.pos = AT(1.2f - 0.65f * k, 0.1f, 2.25f, 0.1f);
        g.look = 1;
        gfx_set_light(0.8f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    }
    case CS_TALL: {     // he is right over you, and he bends down, a very long way, until his face is in yours
        float k = smooth01(t / 1.55f);
        g.pos = (Vector3){ floor.x + f.x * 6.6f, floor.y, floor.z + f.z * 6.6f };   // far enough off that, bending, his face comes all the way down to yours
        g.look = 0;
        gfx_set_light(0.75f, 0, 0.3f, 6, 0);
        draw_at_pivot(&g, g.pos, r, 76.0f * k);
        break;
    }
    case CS_HOST: {     // you are in a chair, and he leans over you, and the eye in the flower opens
        float k = smooth01(t / 1.3f);
        g.pos = (Vector3){ floor.x + f.x * 3.6f, floor.y + 0.7f, floor.z + f.z * 3.6f };   // (you are sitting: the floor is further down than your eyes think)
        g.look = 0;
        gfx_set_light(0.7f, 0, 0.3f, 6, 0);
        draw_at_pivot(&g, g.pos, r, 73 * k);
        break;
    }
    case CS_HAND: {     // a hand round you, lifting you off the steps, closing
        float k = smooth01(t / 1.5f);
        Vector3 hp = { eye.x + f.x * 0.5f, eye.y - 2.55f * catchS.scale - 1.3f, eye.z + f.z * 0.5f };
        g.pos = hp; g.t = -3.49f + k * 6.98f;
        gfx_set_light(0.7f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    }
    case CS_SHEET:      // its face, through the sheet, right in front of you
        g.pos = AT(0.55f, -0.05f, 1.78f, 0.04f);
        g.tilt = 0.15f + sinf(t * 2) * 0.05f;
        gfx_set_light(0.85f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    case CS_PUPPET:     // the light snaps on, and it is right there, its head jerking on the strings
        g.pos = AT(0.5f, 0, 1.86f, 0.04f);
        g.t = floorf(time * 14) * 0.9f;
        g.tilt = sinf(floorf(time * 14) * 2.7f) * 0.6f;
        gfx_set_light(1.4f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    case CS_GREY:       // he just stands there, and looks at you
        g.pos = AT(0.85f, 0, 2.02f, 0.15f);
        g.tilt = 0;
        gfx_set_light(0.8f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    case CS_MIRROR:     // your own face, close enough to touch, eyes shut. then they open
        g.pos = AT(0.5f, 0, 1.68f, 0.05f);
        g.look = t > 0.85f ? 1.0f : 0.0f;
        g.tilt = t > 0.85f ? sinf(t * 47) * 0.06f : 0.08f;
        gfx_set_light(0.9f, 0, 0.3f, 6, 0);
        figure_draw(&g);
        break;
    case CS_ICE: {      // under the ice. out of the black water underneath, a face as wide as a door comes up to yours
        float k = smooth01((t - 0.45f) / 0.9f);
        Vector3 u = { 0, 1, 0 }, fc = { eye.x + f.x * (2.4f - 1.5f * k), eye.y - 1.6f + 1.5f * k, eye.z + f.z * (2.4f - 1.5f * k) };
        Color pale = { 112, 128, 142, 255 };
        gfx_set_light(0.6f, 0, 0.3f, 6, 0);
        for (int s = -1; s <= 1; s += 2) gfx_ellipsoid(Vector3Add(Vector3Add(Vector3Subtract(fc, Vector3Scale(f, 0.3f)), Vector3Scale(r, s * 0.3f)), Vector3Scale(u, 0.25f)), Vector3Scale(r, 0.18f), Vector3Scale(u, 0.12f), Vector3Scale(f, 0.1f), TEX_CONCRETE, (Color){ 4, 6, 8, 255 });
        gfx_ellipsoid(fc, Vector3Scale(r, 0.85f), Vector3Scale(u, 1.1f), Vector3Scale(f, 0.4f), TEX_SKIN, pale);
        gfx_ellipsoid(Vector3Add(Vector3Subtract(fc, Vector3Scale(f, 0.35f)), Vector3Scale(u, -0.45f)), Vector3Scale(r, 0.26f), Vector3Scale(u, 0.08f + 0.28f * k), Vector3Scale(f, 0.05f), TEX_CONCRETE, (Color){ 2, 3, 6, 255 });
        for (int k2 = 0; k2 < 16; k2++) {   // her hair, floating up and out all round her
            float a = k2 * 0.3927f;
            Vector3 dir = Vector3Add(Vector3Scale(r, cosf(a)), Vector3Scale(u, sinf(a))), h0 = Vector3Add(fc, Vector3Scale(dir, 0.8f));
            Vector3 h1 = Vector3Add(Vector3Add(h0, Vector3Scale(dir, 1.2f + 0.3f * sinf(t * 2 + k2))), Vector3Scale(f, 0.5f));
            gfx_limb(h0, h1, 0.06f, 0.01f, TEX_CLOTH, (Color){ 6, 8, 10, 255 });
        }
        gfx_set_emit(true);
        for (int s = -1; s <= 1; s += 2) gfx_ellipsoid(Vector3Add(Vector3Add(Vector3Subtract(fc, Vector3Scale(f, 0.41f)), Vector3Scale(r, s * 0.3f)), Vector3Scale(u, 0.25f)), Vector3Scale(r, 0.035f), Vector3Scale(u, 0.03f), Vector3Scale(f, 0.02f), TEX_SKIN, (Color){ 230, 236, 240, 255 });
        gfx_set_emit(false);
        break;
    }
    case CS_SEEK: {     // it bends down, all the way, until its face is in front of yours. then it takes its hands away
        float k = smooth01(t / 1.0f);
        g.pos = (Vector3){ floor.x + f.x * 2.65f, floor.y, floor.z + f.z * 2.65f };
        g.look = t > 1.05f ? 1.0f : 0.0f;
        {   // it looks up out of the bend, so that once it is all the way down its face is level with yours
            float b = 47.0f * DEG2RAD;
            Vector3 hc = { g.pos.x - f.x * 0.28f, g.pos.y + 2.5f, g.pos.z - f.z * 0.28f };
            g.lookAt = (Vector3){ hc.x - f.x * cosf(b) * 5, hc.y + sinf(b) * 5, hc.z - f.z * cosf(b) * 5 };
        }
        gfx_set_light(0.85f, 0, 0.3f, 6, 0);
        draw_at_pivot(&g, g.pos, r, 47.0f * k);
        break;
    }
    case CS_FALL: {     // you look down and it is coming up the wall at your hands. then you are falling
        float k = smooth01(t / 0.3f);
        Fig c = g; c.kind = FIG_CLIMBER; c.wallN = Vector3Negate(f);
        c.pos = (Vector3){ eye.x + f.x * 0.7f, eye.y - 2.6f + 2.0f * k, eye.z + f.z * 0.7f };
        c.stride = t * 30;
        gfx_set_light(0.8f, 0, 0.3f, 6, 0);
        figure_draw(&c);
        break;
    }
    }
    #undef AT
}

// ---------------------------------------------------------------- the new places, drawn
static void box_on(Vector3 base, Quaternion q, Vector3 off, Vector3 h, TexId t, Color c) {
    gfx_box_rot(Vector3Add(base, Vector3RotateByQuaternion(off, q)), h, q, t, c, 1.0f);
}
static void draw_crack(Vector3 c, float r, int seed) {
    int n = 5 + seed % 3;
    Color w = { 214, 228, 238, 255 };
    for (int k = 0; k < n; k++) {
        float a = k * 6.283f / n + sinf(seed * 7.1f + k) * 0.5f, len = (0.6f + 0.4f * fabsf(sinf(seed * 3.3f + k * 1.7f))) * r * 2.4f;
        Vector3 p0 = { c.x, 0.012f, c.z };
        Vector3 p1 = { c.x + sinf(a) * len * 0.5f + sinf(a + 1.4f) * 0.08f * r, 0.012f, c.z + cosf(a) * len * 0.5f + cosf(a + 1.4f) * 0.08f * r };
        Vector3 p2 = { c.x + sinf(a + 0.2f) * len, 0.012f, c.z + cosf(a + 0.2f) * len };
        gfx_limb(p0, p1, 0.022f, 0.015f, TEX_SNOW, w);
        gfx_limb(p1, p2, 0.015f, 0.004f, TEX_SNOW, w);
    }
}
static Vector3 frozen_face(int i) { return (Vector3){ sinf(i * 2.31f + 0.4f) * 42.0f, 0.004f, cosf(i * 1.73f) * 40.0f - 6.0f }; }
static void draw_more(Vector3 eye, float time) {
    static int devFig = -2; static float df[5];   // dev hook: MURK_FIG="kind,x,y,z,yaw,look" puts one of them in the scene
    if (devFig == -2) { const char *e = getenv("MURK_FIG"); if (!e || sscanf(e, "%d,%f,%f,%f,%f,%f", &devFig, &df[0], &df[1], &df[2], &df[3], &df[4]) != 6) devFig = -1; }
    if (devFig >= 0) { Fig g = { (FigKind)devFig, { df[0], df[1], df[2] }, df[3], time * 3, eye, df[4], 0.1f, time, { 0, 0, 1 }, { 0 }, 0 }; figure_draw(&g); }
    if (L.id == W_HUB || (cat.withYou && !cat.gone)) {
        Fig c = { FIG_CAT, cat.pos, cat.yaw, cat.stride, cat.lookAt, cat.mode == 0 ? 0.0f : cat.fear, cat.mode == 0 ? 2.0f : cat.mode == 2 ? 1.0f : 0.0f, time, { 0, 0, 1 }, { 0 }, 0 };
        figure_draw(&c);
    }
    if (L.id == W_HUB) {   // the rocking horse in the attic. once the house has turned, it rocks by itself
        float rock = g_rot >= 2 ? sinf(time * 1.5f) * 0.22f : 0.0f;
        Quaternion q = QuaternionFromAxisAngle((Vector3){ 0, 0, 1 }, rock);
        Vector3 b = L.bed;
        Color wh = { 230, 224, 210, 255 }, rd = { 170, 40, 40, 255 };
        for (int s = -1; s <= 1; s += 2) box_on(b, q, (Vector3){ 0, 0.06f, s * 0.16f }, (Vector3){ 0.6f, 0.03f, 0.025f }, TEX_WOOD, rd);
        for (int i = 0; i < 4; i++) box_on(b, q, (Vector3){ (i & 1) ? 0.3f : -0.3f, 0.32f, (i & 2) ? 0.13f : -0.13f }, (Vector3){ 0.03f, 0.25f, 0.03f }, TEX_WOOD, wh);
        box_on(b, q, (Vector3){ 0, 0.68f, 0 }, (Vector3){ 0.42f, 0.13f, 0.13f }, TEX_WOOD, wh);
        box_on(b, q, (Vector3){ 0, 0.83f, 0 }, (Vector3){ 0.14f, 0.03f, 0.14f }, TEX_CLOTH, rd);
        box_on(b, q, (Vector3){ 0.4f, 0.9f, 0 }, (Vector3){ 0.08f, 0.22f, 0.08f }, TEX_WOOD, wh);
        box_on(b, q, (Vector3){ 0.55f, 1.05f, 0 }, (Vector3){ 0.18f, 0.08f, 0.075f }, TEX_WOOD, wh);
        for (int s = -1; s <= 1; s += 2) box_on(b, q, (Vector3){ 0.6f, 1.09f, s * 0.077f }, (Vector3){ 0.02f, 0.02f, 0.004f }, TEX_CONCRETE, (Color){ 10, 6, 6, 255 });
        box_on(b, q, (Vector3){ -0.45f, 0.7f, 0 }, (Vector3){ 0.08f, 0.15f, 0.03f }, TEX_CLOTH, (Color){ 40, 30, 30, 255 });
    }
    if (L.id == W_FAIR) {
        // the carousel, turning, its horses going up and down on their poles, and one rider who never gets off
        Vector3 c = L.heart; float a = fair.carA;
        Color gilt = { 220, 180, 90, 255 }, crim = { 150, 30, 40, 255 };
        for (int k = 0; k < 2; k++) {
            Quaternion q = QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -a + k * 0.7854f);
            gfx_box_rot((Vector3){ c.x, 0.12f, c.z }, (Vector3){ 3.25f, 0.12f, 3.25f }, q, TEX_WOOD, (Color){ 130, 80, 60, 255 }, 1.0f);
            gfx_box_rot((Vector3){ c.x, 4.6f, c.z }, (Vector3){ 3.4f, 0.15f, 3.4f }, q, TEX_CLOTH, crim, 2.0f);
            gfx_box_rot((Vector3){ c.x, 5.0f, c.z }, (Vector3){ 2.2f, 0.25f, 2.2f }, q, TEX_CLOTH, (Color){ 230, 220, 200, 255 }, 2.0f);
            gfx_box_rot((Vector3){ c.x, 5.4f, c.z }, (Vector3){ 1.1f, 0.25f, 1.1f }, q, TEX_CLOTH, crim, 2.0f);
        }
        for (int k = 0; k < 16; k++) {
            float t = -a + k * 0.3927f;
            Quaternion q = QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, t);
            gfx_box_rot((Vector3){ c.x + cosf(-t) * 4.4f, 4.3f, c.z + sinf(-t) * 4.4f }, (Vector3){ 0.05f, 0.22f, 0.86f }, q, TEX_CLOTH, k % 2 ? crim : (Color){ 230, 220, 200, 255 }, 1.0f);
        }
        for (int k = 0; k < 8; k++) {
            float th = a + k * 0.7854f, y = 0.95f + 0.32f * sinf(a * 3.0f + k * 1.7f);
            Vector3 hp = { c.x + cosf(th) * 3.4f, y, c.z + sinf(th) * 3.4f };
            Quaternion q = QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, -th - 1.5708f);
            Color hc = k % 2 ? (Color){ 230, 226, 214, 255 } : (Color){ 40, 36, 40, 255 };
            gfx_limb((Vector3){ hp.x, 0.25f, hp.z }, (Vector3){ hp.x, 4.5f, hp.z }, 0.03f, 0.03f, TEX_RUST, gilt);
            box_on(hp, q, (Vector3){ 0, 0, 0 }, (Vector3){ 0.48f, 0.16f, 0.13f }, TEX_WOOD, hc);
            box_on(hp, q, (Vector3){ 0.4f, 0.22f, 0 }, (Vector3){ 0.1f, 0.22f, 0.08f }, TEX_WOOD, hc);
            box_on(hp, q, (Vector3){ 0.55f, 0.38f, 0 }, (Vector3){ 0.2f, 0.09f, 0.08f }, TEX_WOOD, hc);
            for (int s = -1; s <= 1; s += 2) box_on(hp, q, (Vector3){ 0.6f, 0.43f, s * 0.082f }, (Vector3){ 0.03f, 0.025f, 0.004f }, TEX_CONCRETE, (Color){ 140, 10, 10, 255 });   // red glass eyes
            box_on(hp, q, (Vector3){ 0, 0.18f, 0 }, (Vector3){ 0.16f, 0.04f, 0.15f }, TEX_CLOTH, crim);
            for (int i = 0; i < 4; i++) box_on(hp, q, (Vector3){ (i & 1) ? 0.36f : -0.36f, -0.3f, (i & 2) ? 0.08f : -0.08f }, (Vector3){ 0.035f, 0.24f, 0.035f }, TEX_WOOD, hc);
            if (k == 5) {
                Fig r = { FIG_SEATED, { hp.x, y - 0.12f, hp.z }, th + 3.14159f, 0, eye, fair.ride.on ? 1.0f : 0.2f, 0.3f, time, { 0, 0, 1 }, (Color){ 200, 190, 180, 255 }, 0.62f };
                figure_draw(&r);
            }
        }
        if (fair.ride.on) for (int k = 0; k < fair.ride.crowd; k++) {   // the ones who come to watch you go round
            float ang = k * 2.4f + 0.6f, rr = 9.5f - k * 0.5f;
            Vector3 pp = { c.x + cosf(ang) * rr, 0, c.z + sinf(ang) * rr };
            Fig g = { FIG_PENITENT, pp, atan2f(eye.x - pp.x, -(eye.z - pp.z)), 0, eye, 1.0f, 0.3f * sinf(k * 1.3f), time + k, { 0, 0, 1 }, (Color){ 16, 14, 16, 255 }, 0 };
            figure_draw(&g);
        }
        // the big wheel, and somebody in one of the cars
        Vector3 wc = { 16, 11, -14 };
        Color steel = { 170, 160, 160, 255 };
        for (int i = 0; i < 12; i++) {
            float ang = fair.wheelA + i * 0.5236f, an2 = ang + 0.5236f;
            for (int s = -1; s <= 1; s += 2) {
                Vector3 rim = { wc.x + cosf(ang) * 9, wc.y + sinf(ang) * 9, wc.z + s * 1.0f }, rim2 = { wc.x + cosf(an2) * 9, wc.y + sinf(an2) * 9, wc.z + s * 1.0f };
                gfx_limb((Vector3){ wc.x, wc.y, wc.z + s * 1.0f }, rim, 0.05f, 0.04f, TEX_RUST, steel);
                gfx_limb(rim, rim2, 0.07f, 0.07f, TEX_RUST, steel);
            }
            Vector3 hang = { wc.x + cosf(ang) * 9, wc.y + sinf(ang) * 9, wc.z };
            gfx_limb((Vector3){ hang.x, hang.y, wc.z - 1.0f }, (Vector3){ hang.x, hang.y, wc.z + 1.0f }, 0.04f, 0.04f, TEX_RUST, steel);
            gfx_box((Vector3){ hang.x, hang.y - 0.85f, hang.z }, (Vector3){ 0.55f, 0.4f, 0.5f }, TEX_WOOD, i % 3 == 0 ? (Color){ 60, 100, 160, 255 } : i % 3 == 1 ? (Color){ 170, 50, 50, 255 } : (Color){ 200, 170, 60, 255 }, 1.0f);
            if (i == 3) { Fig r = { FIG_SEATED, { hang.x, hang.y - 1.55f, hang.z }, 3.14159f, 0, eye, 1.0f, 0.4f, time, { 0, 0, 1 }, (Color){ 30, 26, 28, 255 }, 0.75f }; figure_draw(&r); }
        }
        // balloons tied to the poles, all leaning the same way though there is no wind
        for (int i = 0; i < 10; i++) {
            Vector3 pole = { (i % 2) ? 3.6f : -3.6f, 4.3f, 30 - (i / 2) * 8.0f };
            Vector3 bp = { pole.x + 0.3f * sinf(time * 0.6f + i), 5.4f + 0.15f * sinf(time * 1.1f + i * 2), pole.z - 0.4f };
            gfx_limb(pole, bp, 0.006f, 0.006f, TEX_CLOTH, (Color){ 220, 220, 220, 255 });
            gfx_ellipsoid((Vector3){ bp.x, bp.y + 0.3f, bp.z }, (Vector3){ 0.26f, 0, 0 }, (Vector3){ 0, 0.33f, 0 }, (Vector3){ 0, 0, 0.26f }, TEX_SKIN, i == 7 ? (Color){ 230, 230, 230, 255 } : (Color){ 200, 30, 36, 255 });
        }
        if (fair.ham.t > 0) {   // the puck
            float u = 1.0f - fair.ham.t / 1.2f, h = fair.ham.peak * sinf(3.14159f * Clamp(u, 0, 1));
            gfx_box((Vector3){ -5.6f, 0.45f + h, 9.45f }, (Vector3){ 0.13f, 0.07f, 0.05f }, TEX_RUST, (Color){ 200, 40, 30, 255 }, 1.0f);
        }
        // in the hall: you, in the glass, both sides. your eyes are shut. one of you is late
        if (P.pos.z < -11.0f && P.pos.z > -45.0f && fabsf(P.pos.x) < 3.6f) for (int s = -1; s <= 1; s += 2) {
            if (s < 0 && fair.dbl >= 1) continue;   // the left glass: that one isn't yours any more
            Vector3 src = P.pos; float yaw = P.yaw;
            if (s > 0 && P.pos.z < -18.0f && fair.histN > 14) { src = fair.hist[14]; yaw = fair.histYaw[14]; }
            if (fabsf(src.x) > 1.0f) continue;
            Vector3 rp = { 2.0f * s - src.x, 0, src.z };
            if (rp.z > -12.2f || rp.z < -43.8f) continue;
            Fig g = { FIG_SELF, rp, -yaw * DEG2RAD, fair.selfStride, eye, 0.0f, 0.06f, time, { 0, 0, 1 }, { 0 }, 0 };
            figure_draw(&g);
        }
        if (fair.dbl >= 1) {
            Fig g = { FIG_SELF, fair.dpos, atan2f(P.pos.x - fair.dpos.x, -(P.pos.z - fair.dpos.z)), fair.dstride, eye, fair.dbl >= 2 ? 0.95f : 0.7f, 0.18f + sinf(time * 1.3f) * 0.08f, time, { 0, 0, 1 }, { 0 }, 0 };
            figure_draw(&g);
            if (fair.dbl >= 2) for (int i = 0; i < 9; i++)   // what is left of the glass
                gfx_box((Vector3){ -0.85f + sinf(i * 3.1f) * 0.3f, 0.01f, -37.0f + cosf(i * 2.3f) * 2.5f }, (Vector3){ 0.06f + 0.04f * (i % 3), 0.004f, 0.05f }, TEX_WATER, (Color){ 120, 130, 150, 255 }, 1.0f);
        }
    }
    if (L.id == W_LAKE) {
        for (int i = 0; i < lake.ncr; i++) draw_crack(lake.cracks[i].p, lake.cracks[i].r, i);
        if (lake.stress > 0.05f) draw_crack(lake.crack, lake.stress, 77);
        for (int i = 0; i < 16; i++) {   // the ones in the ice, their hands up against it, looking up at you
            Vector3 fc = frozen_face(i);
            if (fabsf(fc.x) < 4 && fc.z > 36) continue;
            if (Vector3Distance(fc, eye) > 30) continue;
            Color pale = { 120, 136, 152, 255 };
            gfx_ellipsoid(fc, (Vector3){ 0.17f, 0, 0 }, (Vector3){ 0, 0.003f, 0 }, (Vector3){ 0, 0, 0.22f }, TEX_SKIN, pale);
            for (int s = -1; s <= 1; s += 2) {
                gfx_ellipsoid((Vector3){ fc.x + s * 0.3f, 0.004f, fc.z - 0.25f }, (Vector3){ 0.07f, 0, 0 }, (Vector3){ 0, 0.002f, 0 }, (Vector3){ 0, 0, 0.1f }, TEX_SKIN, pale);
                Vector3 ec = { fc.x + s * 0.065f, 0.006f, fc.z - 0.05f };
                gfx_ellipsoid(ec, (Vector3){ 0.035f, 0, 0 }, (Vector3){ 0, 0.002f, 0 }, (Vector3){ 0, 0, 0.025f }, TEX_CONCRETE, (Color){ 8, 10, 14, 255 });
                Vector3 to = Vector3Subtract(P.pos, fc); float tl = Vector3Length(to);
                Vector3 off = tl > 0.1f ? Vector3Scale(to, 0.018f / tl) : (Vector3){ 0 };
                gfx_ellipsoid((Vector3){ ec.x + off.x, 0.008f, ec.z + off.z }, (Vector3){ 0.012f, 0, 0 }, (Vector3){ 0, 0.002f, 0 }, (Vector3){ 0, 0, 0.012f }, TEX_SKIN, (Color){ 200, 210, 220, 255 });
            }
            gfx_ellipsoid((Vector3){ fc.x, 0.006f, fc.z + 0.11f }, (Vector3){ 0.04f, 0, 0 }, (Vector3){ 0, 0.002f, 0 }, (Vector3){ 0, 0, 0.05f }, TEX_CONCRETE, (Color){ 8, 10, 14, 255 });
        }
        if (lake.on) {   // its shadow under the ice: a face as big as a door, hair spread out behind it like weed
            Vector3 fw = Vector3Normalize((Vector3){ lake.dir.x, 0, lake.dir.z }), rt = { -fw.z, 0, fw.x };
            Vector3 c = { lake.thing.x, 0.004f, lake.thing.z };
            float glow = lake.state == 1 ? 1.25f : 1.0f;
            gfx_ellipsoid(Vector3Subtract(c, Vector3Scale(fw, 1.6f)), Vector3Scale(rt, 1.2f), (Vector3){ 0, 0.003f, 0 }, Vector3Scale(fw, 2.8f), TEX_WATER, (Color){ 14, 20, 30, 255 });
            Vector3 fc = Vector3Add(c, Vector3Scale(fw, 0.6f));
            gfx_ellipsoid(fc, Vector3Scale(rt, 0.8f), (Vector3){ 0, 0.005f, 0 }, Vector3Scale(fw, 1.0f), TEX_SKIN, scale_col((Color){ 104, 118, 134, 255 }, glow));
            for (int s = -1; s <= 1; s += 2) {
                Vector3 ec = Vector3Add(Vector3Add(fc, Vector3Scale(fw, 0.28f)), Vector3Scale(rt, s * 0.3f)); ec.y = 0.01f;
                gfx_ellipsoid(ec, Vector3Scale(rt, 0.14f), (Vector3){ 0, 0.002f, 0 }, Vector3Scale(fw, 0.08f), TEX_SKIN, scale_col((Color){ 170, 182, 196, 255 }, glow));
                gfx_ellipsoid((Vector3){ ec.x, 0.012f, ec.z }, Vector3Scale(rt, 0.035f), (Vector3){ 0, 0.002f, 0 }, Vector3Scale(fw, 0.035f), TEX_CONCRETE, (Color){ 4, 4, 6, 255 });
            }
            Vector3 mc = Vector3Subtract(fc, Vector3Scale(fw, 0.4f)); mc.y = 0.01f;
            gfx_ellipsoid(mc, Vector3Scale(rt, 0.22f), (Vector3){ 0, 0.002f, 0 }, Vector3Scale(fw, 0.08f + 0.06f * fabsf(sinf(time * 0.8f))), TEX_CONCRETE, (Color){ 4, 6, 10, 255 });
            for (int k = 0; k < 14; k++) {
                float sd = (k - 6.5f) / 6.5f, sw = sinf(time * 0.9f + k * 0.8f) * 0.4f;
                Vector3 s0 = Vector3Add(Vector3Subtract(fc, Vector3Scale(fw, 0.7f)), Vector3Scale(rt, sd * 0.6f));
                Vector3 s1 = Vector3Add(Vector3Subtract(s0, Vector3Scale(fw, 1.6f)), Vector3Scale(rt, sd * 0.9f + sw));
                Vector3 s2 = Vector3Add(Vector3Subtract(s1, Vector3Scale(fw, 1.8f)), Vector3Scale(rt, sd * 1.2f - sw));
                s0.y = s1.y = s2.y = 0.007f;
                gfx_limb(s0, s1, 0.05f, 0.035f, TEX_CLOTH, (Color){ 6, 8, 12, 255 });
                gfx_limb(s1, s2, 0.035f, 0.01f, TEX_CLOTH, (Color){ 6, 8, 12, 255 });
            }
        }
        float tug = lake.tug2 > 0 ? sinf(lake.tug2 * 40.0f) * 0.12f * lake.tug2 : 0.0f;   // the rope, going down into the hole
        gfx_limb((Vector3){ 3.8f, 0.6f, 41.6f }, (Vector3){ 3.0f + tug, -0.05f, 41.0f + tug }, 0.02f, 0.02f, TEX_CLOTH, (Color){ 150, 130, 100, 255 });
    }
    if (L.id == W_SCHOOL) {
        Vector3 sp = seek.pos;
        float interval = seek.round == 0 ? 1.9f : 1.25f;
        if (seek.state == 0 && seek.count > 0 && seek.t > interval - 0.18f) sp.z += 0.12f;   // its forehead against the wall, at every number
        Fig g = { FIG_SEEKER, sp, atan2f(seek.face.x, -seek.face.z), seek.stride, eye, seek.state == 3 ? 1.0f : 0.0f, 0.12f * sinf(time * 0.7f), time, { 0, 0, 1 }, { 0 }, 0 };
        figure_draw(&g);
    }
}
static void draw_more_glow(Vector3 eye, float time) {
    if (L.id == W_HUB) for (int f = 0; f < FX_COUNT; f++) if (P.fx & (1u << f)) {   // what you have brought home, on the mantelpiece
        Quaternion q = QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, time * 0.8f + f);
        gfx_box_rot((Vector3){ 26.5f, -3.0f + 1.6f, -4.75f + f * 0.3f }, (Vector3){ 0.06f, 0.06f, 0.06f }, q, TEX_CONCRETE, fx_color((EffectId)f), 100.0f);
    }
    if (L.id == W_FAIR) {
        Vector3 c = L.heart;
        for (int k = 0; k < 24; k++) {
            float t = fair.carA + k * 0.2618f;
            gfx_glow((Vector3){ c.x + cosf(t) * 4.5f, 4.75f, c.z + sinf(t) * 4.5f }, (Vector3){ 0.05f, 0.05f, 0.05f }, ((k + (int)(time * 4)) % 3) ? (Color){ 255, 220, 120, 255 } : (Color){ 255, 90, 80, 255 });
        }
        Vector3 wc = { 16, 11, -14 };
        for (int i = 0; i < 24; i++) {
            float ang = fair.wheelA + i * 0.2618f;
            gfx_glow((Vector3){ wc.x + cosf(ang) * 9, wc.y + sinf(ang) * 9, wc.z - 1.05f }, (Vector3){ 0.07f, 0.07f, 0.07f }, (i + (int)(time * 3)) % 4 == 0 ? (Color){ 255, 255, 255, 255 } : (Color){ 120, 180, 255, 255 });
        }
        float pulse = 0.75f + 0.25f * sinf(time * 2.0f);
        gfx_ellipsoid((Vector3){ 0, 1.0f, -47.4f }, (Vector3){ 0.17f, 0, 0 }, (Vector3){ 0, 0.17f, 0 }, (Vector3){ 0, 0, 0.17f }, TEX_CONCRETE, scale_col((Color){ 200, 150, 255, 255 }, pulse));
    }
    if (match.t > 0) {   // the match in your hand
        Vector3 f = player_forward(&P), r = Vector3Normalize(Vector3CrossProduct(f, (Vector3){ 0, 1, 0 }));
        Vector3 mp = Vector3Add(Vector3Add(eye, Vector3Scale(f, 0.45f)), Vector3Add(Vector3Scale(r, 0.16f), (Vector3){ 0, -0.17f, 0 }));
        gfx_glow(mp, (Vector3){ 0.012f, 0.025f + 0.006f * sinf(time * 40), 0.012f }, (Color){ 255, 190, 90, 255 });
    }
    (void)eye;
}
static void draw_more_halo(Vector3 eye, float time) {
    if (L.id == W_HUB) for (int f = 0; f < FX_COUNT; f++) if (P.fx & (1u << f)) gfx_halo((Vector3){ 26.62f, -1.4f, -4.75f + f * 0.3f }, 0.5f, fx_color((EffectId)f), 0.6f);
    if (L.id == W_FAIR) gfx_halo((Vector3){ 0, 1.0f, -47.2f }, 1.4f, (Color){ 180, 120, 255, 255 }, 0.5f + 0.2f * sinf(time * 2));
    if (match.t > 0) {
        Vector3 f = player_forward(&P), r = Vector3Normalize(Vector3CrossProduct(f, (Vector3){ 0, 1, 0 }));
        Vector3 mp = Vector3Add(Vector3Add(eye, Vector3Scale(f, 0.45f)), Vector3Add(Vector3Scale(r, 0.16f), (Vector3){ 0, -0.17f, 0 }));
        gfx_halo(mp, 0.35f, (Color){ 255, 170, 80, 255 }, 0.7f * fminf(1, match.t));
    }
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
    {   // in the dark you see what you are close to; with the lamp, a beam goes where you look
        float amb = L.ambient * (blackout > 0 ? 0.1f : 1.0f);
        if (match.t > 0) {
            float fl = (0.85f + 0.15f * sinf(time * 31.0f) * sinf(time * 13.0f)) * fminf(1.0f, match.t);
            gfx_set_light(fmaxf(amb, 0.2f * fl), lampOn > 0.5f ? 2.6f : 0.0f, 0.42f, 11.0f, 1.8f * fl);
        }
        else if (amb >= 0.95f) gfx_set_light(amb, 0, 0.3f, 6, 0);
        else if (lampOn > 0.5f) gfx_set_light(amb, 2.6f, 0.42f, 11.0f, 0.3f);
        else gfx_set_light(amb, 0.5f, 0.32f, 3.5f, 0.45f);
    }
    gfx_sky(&L.sky, time, P.pos);
    float cull = 3.2f / dens + 6;
    Vector3 eye = cam.position;
    for (int i = 0; i < (int)L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (b->flags & (F_EMIT | F_SCREEN | F_HIDDEN)) continue;
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
        Fig f = { (L.id == W_BATHS || L.id == W_WOMB) ? FIG_CRAWLER : FIG_PENITENT, w->pos,
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
        } else if (u->kind == USE_VALVE) {   // a pipe up the wall with a red wheel on it
            gfx_box((Vector3){ u->pos.x, u->pos.y, u->pos.z }, (Vector3){ 0.08f, 1.6f, 0.08f }, TEX_RUST, (Color){ 120, 120, 110, 255 }, 1.0f);
            float a = u->done ? 2.5f : 0.0f;
            for (int k = 0; k < 8; k++) {
                float a0 = a + k * 0.785f, a1 = a0 + 0.785f;
                gfx_limb((Vector3){ u->pos.x + cosf(a0) * 0.25f, u->pos.y + sinf(a0) * 0.25f, u->pos.z + 0.12f }, (Vector3){ u->pos.x + cosf(a1) * 0.25f, u->pos.y + sinf(a1) * 0.25f, u->pos.z + 0.12f }, 0.025f, 0.025f, TEX_RUST, (Color){ 170, 30, 26, 255 });
            }
        }
    }
    for (int i = 0; i < L.effigies.size; i++) {
        const Effigy *e = &L.effigies.data[i];
        Vector3 ep = wp(e->pos, eye);
        if (Vector3Distance(eye, ep) > cull + 2) continue;
        Fig f = { (FigKind)e->kind, ep, e->yaw, 0, eye, e->look * 0.9f, e->tilt, time + i, { 0, 0, 1 }, { 0 }, e->scale };
        if (f.kind == FIG_HAND) f.pos.y += sinf(time * 0.13f + i) * 2.0f + (gaze.phase == 2 ? gaze.noticed * 3.0f : 0);   // and higher, the more the eye has noticed
        if (L.id == W_MORGUE) f.tint = (Color){ 206, 210, 212, 255 };    // under sheets
        if (L.id == W_THEATRE && f.kind == FIG_SEATED) f.tint = (Color){ 196, 184, 172, 255 };   // mannequins
        if (L.id == W_FAIR && f.kind == FIG_SEATED) f.tint = (Color){ 120, 40, 100, 255 };       // the fortune teller, in her shawl
        if (L.id == W_LAKE && f.kind == FIG_SELF) f.tint = (Color){ 176, 186, 200, 255 };        // nightclothes, frozen stiff
        if (L.id == W_CHAPEL) {   // the congregation stands, and kneels on the third bell; the priest only looks up to count
            bool taken = veil_taken();
            if (f.kind != FIG_PRIEST) { f.kind = mass.phase == 2 ? FIG_KNEELER : FIG_PENITENT; f.look = (taken || mass.phase == 2) ? 1.0f : e->look * 0.5f; }
            else f.look = (mass.phase == 2 || taken) ? 1.0f : 0.0f;
        }
        figure_draw(&f);
    }
    if (L.id == W_WARD && g_wardLoop >= 3) {   // her, in stop motion
        Vector3 mp = ward_pt(fmaxf(0, mom.shown));
        float jt = floorf(time * 6.0f) / 6.0f;
        Fig f = { FIG_MOTHER, mp, atan2f(eye.x - mp.x, -(eye.z - mp.z)), 0, eye, 0.6f, 1.25f + 0.15f * sinf(jt * 13.0f), jt, { 0, 0, 1 }, { 0 }, 0 };
        figure_draw(&f);
    }
    if (L.id == W_DINNER && dinner.sitT <= 0 && (dinner.host.x != 0 || dinner.host.y != 0)) {   // the host: very tall, and his hands are wet
        Vector3 hp = wp(dinner.host, eye); hp.y = 0;
        Fig f = { FIG_GARDENER, hp, atan2f(eye.x - hp.x, -(eye.z - hp.z)), dinner.hostStride, eye, 1.0f, 0.2f, time, { 0, 0, 1 }, (Color){ 70, 30, 30, 255 }, 0 };
        figure_draw(&f);
    }
    if (L.id == W_MORGUE) {
        for (int i = 0; i < 6; i++) if (morg.open[i]) {   // drawers out, and a hand hanging over the edge of one
            Vector3 c = DRAWER[i]; float s = c.x > 0 ? -1.0f : 1.0f;
            gfx_box((Vector3){ c.x + s * 0.9f, c.y, c.z }, (Vector3){ 0.9f, 0.35f, 0.42f }, TEX_CONCRETE, (Color){ 140, 150, 160, 255 }, 1.0f);
            gfx_ellipsoid((Vector3){ c.x + s * 1.0f, c.y + 0.38f, c.z }, (Vector3){ 0.7f, 0, 0 }, (Vector3){ 0, 0.1f, 0 }, (Vector3){ 0, 0, 0.3f }, TEX_CLOTH, (Color){ 200, 204, 206, 255 });
            if (i % 2 == 0) gfx_limb((Vector3){ c.x + s * 1.5f, c.y + 0.35f, c.z + 0.3f }, (Vector3){ c.x + s * 1.55f, c.y - 0.25f, c.z + 0.45f }, 0.035f, 0.025f, TEX_SKIN, (Color){ 150, 160, 170, 255 });
        }
        if (morg.walking) {
            Fig f = { FIG_SHEET, morg.shown, atan2f(P.pos.x - morg.shown.x, -(P.pos.z - morg.shown.z)), 0, eye, 0.8f, 0.2f, floorf(time * 5) / 5, { 0, 0, 1 }, { 0 }, 0 };
            figure_draw(&f);
        }
    }
    if (L.id == W_THEATRE) for (int i = 0; i < 3; i++) {
        Fig f = { FIG_PUPPET, show.pup[i], atan2f(P.pos.x - show.pup[i].x, -(P.pos.z - show.pup[i].z)), 0, eye, show.scene >= 3 ? 1.0f : 0.0f, 0.2f * i, floorf(time * 6) / 6 + i, { 0, 0, 1 }, { 0 }, 0 };
        figure_draw(&f);
    }
    if (L.id == W_WOMB) {
        if (below.state == 2) {
            Fig f = { FIG_CRAWLER, below.pos, atan2f(P.pos.x - below.pos.x, -(P.pos.z - below.pos.z)), time * 9.0f, eye, 0.9f, 0.2f, time, { 0, 0, 1 }, { 0 }, 0 };
            figure_draw(&f);
        }
        if (Vector3Distance(eye, (Vector3){ -50, -3, -29 }) < 30) for (int i = 0; i < 16; i++) {   // arms out of the walls, reaching
            float x = -42.5f - (i / 2) * 2.25f, side = (i & 1) ? 1.0f : -1.0f, y = -2.6f + ((i / 2) % 2) * 0.6f;
            Vector3 sh = { x, y, -29 + side * 3.95f }, tip = arm_tip(i, time);
            Vector3 el = Vector3Add(Vector3Lerp(sh, tip, 0.5f), (Vector3){ 0, 0.25f, 0 });
            Color sk = { 196, 170, 166, 255 };
            gfx_limb(sh, el, 0.07f, 0.055f, TEX_SKIN, sk);
            gfx_limb(el, tip, 0.055f, 0.04f, TEX_SKIN, sk);
            Vector3 fd = Vector3Normalize(Vector3Subtract(tip, el)), sd = Vector3Normalize(Vector3CrossProduct(fd, (Vector3){ 0, 1, 0 }));
            for (int k = 0; k < 4; k++) {
                float o = (k - 1.5f) * 0.035f;
                gfx_limb(Vector3Add(tip, Vector3Scale(sd, o)), Vector3Add(tip, Vector3Add(Vector3Scale(fd, 0.2f), Vector3Scale(sd, o * 2.2f))), 0.014f, 0.006f, TEX_SKIN, sk);
            }
        }
        if (Vector3Distance(eye, (Vector3){ -69, -5, -29 }) < 25) for (int i = 0; i < 12; i++) {   // faces in the walls, mouths working
            float x = -61.5f - (i / 2) * 2.9f, side = (i & 1) ? 1.0f : -1.0f, fy = below_floor(x) - (x < -60 ? 2.0f * (-60 - x) / 18.0f : 0) + 1.5f;
            Vector3 fc = { x, fy, -29 + side * 1.48f }, n = { 0, 0, -side };
            float open = 0.5f + 0.5f * sinf(time * 2.2f + i * 1.7f);
            gfx_ellipsoid(fc, (Vector3){ 0.22f, 0, 0 }, (Vector3){ 0, 0.3f, 0 }, Vector3Scale(n, 0.12f), TEX_SKIN, (Color){ 200, 176, 170, 255 });
            for (int s = -1; s <= 1; s += 2) gfx_ellipsoid(Vector3Add(fc, (Vector3){ s * 0.08f, 0.09f, n.z * 0.1f }), (Vector3){ 0.035f, 0, 0 }, (Vector3){ 0, 0.025f, 0 }, Vector3Scale(n, 0.03f), TEX_CONCRETE, (Color){ 3, 2, 2, 255 });
            gfx_ellipsoid(Vector3Add(fc, (Vector3){ 0, -0.12f, n.z * 0.1f }), (Vector3){ 0.06f, 0, 0 }, (Vector3){ 0, 0.03f + 0.09f * open, 0 }, Vector3Scale(n, 0.03f), TEX_CONCRETE, (Color){ 3, 2, 2, 255 });
        }
    }
    if (L.id == W_BATHS && swim.state >= 2) {
        Fig f = { FIG_CRAWLER, swim.pos, atan2f(P.pos.x - swim.pos.x, -(P.pos.z - swim.pos.z)), swim.t * 4 + Vector3Length(swim.pos) * 4, eye, 0.6f, 0.3f, time, { 0, 0, 1 }, (Color){ 140, 150, 146, 255 }, 0 };
        figure_draw(&f);
    }
    if (L.id == W_NURSERY) {
        if (giant.state > 0) {   // sixteen metres of her, her head bent over against the ceiling
            float jt = floorf(time * 5.0f) / 5.0f;
            Fig f = { FIG_MOTHER, giant.pos, atan2f(giant.face.x, -giant.face.z), 0, eye, giant.state == 4 ? 0.8f : 0.2f, 1.1f + 0.1f * sinf(jt * 7.0f), jt, { 0, 0, 1 }, { 0 }, 8.0f };
            gfx_set_light(0.55f, 0, 0.3f, 6, 0);   // pale in the moonlight, however dark the room
            figure_draw(&f);
            gfx_set_light(L.ambient, lampOn > 0.5f ? 2.6f : 0.5f, lampOn > 0.5f ? 0.42f : 0.32f, lampOn > 0.5f ? 11.0f : 3.5f, lampOn > 0.5f ? 0.3f : 0.45f);
        }
        float rock = sinf(time * 1.1f) * 0.16f;   // the rocking chair, rocking with nobody in it
        Quaternion q = QuaternionFromAxisAngle((Vector3){ 1, 0, 0 }, rock);
        Vector3 c = L.heart;
        gfx_box_rot((Vector3){ c.x, 3.2f, c.z }, (Vector3){ 2.2f, 0.25f, 2.0f }, q, TEX_WOOD, (Color){ 110, 80, 60, 255 }, 3.0f);
        gfx_box_rot(Vector3Add((Vector3){ c.x, 6.4f, c.z }, Vector3RotateByQuaternion((Vector3){ 0, 0, 1.9f }, q)), (Vector3){ 2.2f, 3.0f, 0.25f }, q, TEX_WOOD, (Color){ 110, 80, 60, 255 }, 3.0f);
        for (int s = -1; s <= 1; s += 2) gfx_box_rot((Vector3){ c.x + s * 1.9f, 0.9f, c.z }, (Vector3){ 0.18f, 0.18f, 2.6f }, q, TEX_WOOD, (Color){ 90, 64, 48, 255 }, 3.0f);
        Vector3 hub = { -14, 16.5f, -13 };   // the mobile over the crib, turning
        for (int k = 0; k < 5; k++) {
            float a = time * 0.25f + k * 1.2566f;
            Vector3 hp = { hub.x + sinf(a) * 2.2f, 13.0f + (k % 2) * 0.8f, hub.z + cosf(a) * 2.2f };
            gfx_limb((Vector3){ hp.x, 24.0f, hp.z }, hp, 0.02f, 0.02f, TEX_CLOTH, (Color){ 40, 40, 40, 255 });
            gfx_ellipsoid((Vector3){ hp.x, hp.y - 0.5f, hp.z }, (Vector3){ 0.5f, 0, 0 }, (Vector3){ 0, 0.5f, 0 }, (Vector3){ 0, 0, 0.12f }, TEX_SKIN, k % 2 ? (Color){ 200, 190, 140, 255 } : (Color){ 150, 160, 190, 255 });
        }
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
    draw_more(eye, time);
    if (catchS.on) draw_catch(eye, time);
    if (subT > 0 && !catchS.on) {   // one frame of it, right in front of you
        Vector3 f = player_forward(&P); f.y = 0; f = Vector3Normalize(f);
        Fig g = { FIG_CRAWLER, { eye.x + f.x * (0.42f + 0.6f), eye.y - 1.05f, eye.z + f.z * (0.42f + 0.6f) }, atan2f(-f.x, f.z), 0, eye, 1.0f, 0.5f, time * 9, { 0, 0, 1 }, { 0 }, 0 };
        gfx_set_light(0.9f, 0, 0.3f, 6, 0);
        figure_draw(&g);
    }
    // lurkers: someone standing very still at the edge of the fog
    for (int i = 0; i < 3; i++) {
        const Lurker *k = &lurk[i];
        if (!k->on) continue;
        Fig f = { FIG_PENITENT, { k->pos.x, k->pos.y - 2.0f, k->pos.z }, atan2f(eye.x - k->pos.x, -(eye.z - k->pos.z)), 0, eye, 0.9f, 0.5f * sinf(i * 2.3f), time, { 0, 0, 1 }, (Color){ 12, 10, 12, 255 }, 0 };
        figure_draw(&f);
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
        float k = (L.id == W_BATHS && b->h.y < 0.2f) ? flicker(time + i, 0.4f) : 1.0f;
        if (blackout > 0 && (L.id == W_MORGUE || L.id == W_THEATRE)) k = 0.04f;
        gfx_glow(wp(b->c, eye), b->h, scale_col(b->tint, k));
    }
    draw_more_glow(eye, time);
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
    for (int i = 0; i < L.boxes.size; i++) {
        const Box *b = &L.boxes.data[i];
        if (!(b->flags & F_EMIT)) continue;
        float mx = fmaxf(b->h.x, fmaxf(b->h.y, b->h.z));
        if (mx > 1.6f || b->h.y > 3.0f) continue;
        Box wb = *b; wb.c = wp(b->c, eye);
        if (dist_to_box(eye, &wb) > cull) continue;
        float k = (L.id == W_BATHS && b->h.y < 0.2f) ? flicker(time + i, 0.4f) : 1.0f;
        if (blackout > 0 && (L.id == W_MORGUE || L.id == W_THEATRE)) k = 0;
        Vector3 ho = Vector3Add(wb.c, Vector3Scale(Vector3Normalize(Vector3Subtract(eye, wb.c)), 0.45f));   // pull it off the wall it hangs on
        if (mx < 0.06f) gfx_halo(ho, 0.45f, b->tint, 0.28f * k);   // a candle flame: a small warm smudge, not a bloom
        else gfx_halo(ho, 0.9f + mx * 2.6f, b->tint, 0.55f * k);
    }
    for (int i = 0; i < L.pickups.size; i++) {
        const Pickup *pk = &L.pickups.data[i];
        if (!pk->taken) gfx_halo(pk->pos, 2.6f + sinf(time * 3) * 0.3f, fx_color(pk->fx), 0.9f);
    }
    draw_more_halo(eye, time);
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

// a message too long for one line is broken into several, the last of them ending at y
static void text_wrap_c(const char *s, int y, int size, Color c) {
    char lines[6][160]; int n = 0, maxw = RT_W - 40;
    char line[160] = "";
    while (*s && n < 6) {
        int w = 0; while (s[w] && s[w] != ' ') w++;
        char tryl[160]; snprintf(tryl, sizeof tryl, "%s%s%.*s", line, line[0] ? " " : "", w, s);
        if (line[0] && MeasureText(tryl, size) > maxw) { snprintf(lines[n++], 160, "%s", line); snprintf(line, sizeof line, "%.*s", w, s); }
        else snprintf(line, sizeof line, "%s", tryl);
        s += w; while (*s == ' ') s++;
    }
    if (line[0] && n < 6) snprintf(lines[n++], 160, "%s", line);
    for (int i = 0; i < n; i++) text_c(lines[i], y - (n - 1 - i) * (size + 3), size, c);
}
// what the compass points at: whatever you came for here, or, at home, the nearest door to something you haven't got
static bool compass_target(Vector3 *t) {
    float bd = 1e9f; bool any = false;
    if (L.id == W_HUB) {
        bool out = (P.fx & FX_OUT) == FX_OUT;
        for (int i = 0; i < L.portals.size; i++) {
            const Portal *pt = &L.portals.data[i];
            bool want = out ? pt->to == W_END : false;
            if (!out) for (int f = 0; f < FX_COUNT; f++) if (FX_HOME[f] == pt->to && !(P.fx & (1u << f))) want = true;
            float d = Vector3Distance(pt->pos, P.pos);
            if (want && d < bd) { bd = d; *t = pt->pos; any = true; }
        }
        return any;
    }
    for (int i = 0; i < L.pickups.size; i++) {
        const Pickup *pk = &L.pickups.data[i];
        float d = Vector3Distance(pk->pos, P.pos);
        if (!pk->taken && d < bd) { bd = d; *t = pk->pos; any = true; }
    }
    return any;
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
        char nm[32]; snprintf(nm, sizeof nm, f == FX_MATCHES ? "%s %d" : "%s", FX_NAME[f], match.left);
        if (f == FX_MATCHES && match.t > 0) c = (Color){ 255, 220, 140, 255 };
        text(nm, x, 8, 10, c);
        x += MeasureText(nm, 10) + 8;
    }

    if (nameT > 0 && L.name) {
        unsigned char a = (unsigned char)(fminf(1.0f, nameT) * 255);
        text_c(L.name, RT_H / 2 - 70, 20, (Color){ 220, 210, 190, a });
    }
    if (hide.on) {   // inside the cupboard: dark, and four slits of light
        int top = cy - 34, bot = cy + 30;
        DrawRectangle(0, 0, RT_W, top, (Color){ 0, 0, 0, 250 });
        DrawRectangle(0, bot, RT_W, RT_H - bot, (Color){ 0, 0, 0, 250 });
        for (int k = 0; k < 4; k++) DrawRectangle(0, top + 6 + k * 16, RT_W, 10, (Color){ 0, 0, 0, 245 });
        DrawRectangle(0, top, 40, bot - top, (Color){ 0, 0, 0, 250 }); DrawRectangle(RT_W - 40, top, 40, bot - top, (Color){ 0, 0, 0, 250 });
        DrawRectangle(RT_W / 2 - 1, top, 3, bot - top, (Color){ 0, 0, 0, 255 });
    }
    if (useHint && reading < 0) text_c(useHint, hide.on ? RT_H - 52 : cy + 10, 10, (Color){ 190, 180, 160, 200 });
    if (P.fx & (1u << FX_COMPASS)) {   // the compass: it points where you should go, unless something is near. then it points at that, and shakes
        int ox = RT_W - 26, oy = 28;
        Vector3 tgt, tp; bool has = compass_target(&tgt), danger = false;
        if (L.id != W_HUB && threat(&tp) && Vector3Distance(tp, P.pos) < 20) { tgt = tp; has = true; danger = true; }
        float a = time * 0.7f;   // nothing to point at: it wanders
        if (has) {
            float yaw = P.yaw * DEG2RAD;
            Vector3 d = { tgt.x - P.pos.x, 0, tgt.z - P.pos.z };
            a = atan2f(d.x * cosf(yaw) + d.z * sinf(yaw), d.x * sinf(yaw) - d.z * cosf(yaw));
            if (danger) a += sinf(time * 40) * 0.25f;
        }
        Color cc = danger ? (Color){ 230, 60, 50, 255 } : fx_color(FX_COMPASS);
        DrawCircle(ox, oy, 15, (Color){ 0, 0, 0, 150 });
        DrawCircleLines(ox, oy, 15, cc);
        DrawLineEx((Vector2){ (float)ox, (float)oy }, (Vector2){ ox + sinf(a) * 12, oy - cosf(a) * 12 }, 2, cc);
        DrawLineEx((Vector2){ (float)ox, (float)oy }, (Vector2){ ox - sinf(a) * 6, oy + cosf(a) * 6 }, 1, (Color){ 150, 150, 150, 200 });
    }
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
    if (msgT > 0) text_wrap_c(msg, RT_H - 34, 10, (Color){ 210, 200, 180, (unsigned char)(fminf(1.0f, msgT) * 255) });
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
    static const WorldId DEST[] = { W_SHAFT, W_BATHS, W_VOID, W_NURSERY, W_CHAPEL, W_WARD, W_STATIC, W_DINNER, W_WOMB, W_CITY, W_MORGUE, W_THEATRE, W_FAIR, W_LAKE, W_SCHOOL };
    int n = (int)(sizeof DEST / sizeof *DEST);
    WorldId to;
    do to = DEST[GetRandomValue(0, n - 1)]; while (to == L.id);
    audio_play_ex(SFX_SWELL, 0.6f, 1.2f);
    glitch = 0.4f;
    go(to, false);
}

static void write_diary(void) {
    int dv = 0, nd = 0;
    for (int w = W_SHAFT; w < W_END; w++) { nd++; dv += visited[w]; }
    char have[200] = "", want[200] = "";
    for (int f = 0; f < FX_COUNT; f++) {
        char *dst = (P.fx & (1u << f)) ? have : (FX_OUT & (1u << f)) ? want : NULL;
        if (!dst) continue;
        if (dst[0]) strcat(dst, ", ");
        strcat(dst, FX_NAME[f]);
    }
    snprintf(diary, sizeof diary, "MY DIARY. PRIVATE. KEEP OUT.\n\nI have been to %d of the %d places behind the doors. I have woken up %d times.\nI have read %d of the pages people left lying about.\nI have brought home: %s.\nThe door at the bottom of the stairs still wants: %s.\n\n%s\n%s",
             dv, nd, g_wakes, notesRead, have[0] ? have : "nothing yet", want[0] ? want : "nothing. it is open",
             g_rot >= 5 ? "Someone has been writing in here. It isn't my writing. Every page says STAY." :
             g_rot >= 3 ? "Mum says I talk in my sleep now. She says it isn't my voice." :
             g_rot >= 1 ? "The house was different when I came back. I don't know how. Something has been moved." : "Father A. says I must write everything down, so that he can read it.",
             g_launches > 3 ? "You came back again. I'm glad. It is lonely when you aren't dreaming me." : "");
    NOTES[NOTE_DIARY] = diary;
}
static void use_thing(Use *u) {
    switch (u->kind) {
    case USE_HIDE: if (hide.on) hide_out(); else hide_in(u); return;
    case USE_FORTUNE: say(FORTUNES[GetRandomValue(0, (int)(sizeof FORTUNES / sizeof *FORTUNES) - 1)], 6); audio_play_ex(SFX_SWELL, 0.4f, 1.4f); return;
    case USE_RIDE:
        if (!fair.ride.on) { fair.ride.on = true; fair.ride.t = 0; fair.ride.crowd = 0; say("you climb up onto one of the horses. the music starts.", 4); audio_play_ex(SFX_BELL, 0.5f, 1.8f); }
        return;
    case USE_HAMMER:
        if (fair.ham.t <= 0) {
            fair.ham.tries++;
            fair.ham.rang = fair.ham.tries >= 4 || GetRandomValue(0, 4) == 0;
            fair.ham.peak = fair.ham.rang ? 5.95f : frand_(0.6f, 4.8f);
            fair.ham.t = 1.2f; P.noise = 1;
            audio_play_ex(SFX_THUD, 1.0f, 1.3f);
        }
        return;
    case USE_DIARY: write_diary(); reading = NOTE_DIARY; audio_play_ex(SFX_CREAK, 0.25f, 1.6f); return;
    case USE_BED: {   // lie down, and dream somewhere you haven't been
        WorldId cand[W_COUNT]; int n = 0;
        for (int w = W_SHAFT; w < W_END; w++) if (!visited[w]) cand[n++] = (WorldId)w;
        if (!n) for (int w = W_SHAFT; w < W_END; w++) cand[n++] = (WorldId)w;
        say(g_rot >= 4 ? "you lie down. someone lies down behind you. you are asleep before you can turn round." : "you lie down, and you are asleep before your head is on the pillow.", 5);
        go(cand[GetRandomValue(0, n - 1)], false);
        return;
    }
    case USE_PIANO: {   // the lullaby from the music box, one note at a time
        static const float LULL[5] = { 440.0f, 523.3f, 493.9f, 392.0f, 415.3f };
        float f = LULL[house.note % 5] / 261.6f;
        if (g_rot >= 4 && house.note % 5 == 3) f *= 0.94f;   // that one has gone wrong
        audio_play_ex(SFX_PIANO, 0.9f, f);
        house.note++;
        if (house.note % 5 == 0) { house.humT = 1.6f; say(g_rot >= 3 ? "upstairs, close to the top of the stairs, a child laughs." : "upstairs, very quietly, someone hums the rest of it.", 4); }
        return;
    }
    case USE_RADIO: {
        house.station = house.station % 5 + 1;
        int st = house.station - 1;
        char b[320]; snprintf(b, sizeof b, "the radio, on %s: %s", STATION[st], RADIO_ST[st][GetRandomValue(0, 4)]);
        say(b, 9);
        play_from(st == 4 ? SFX_PRAYER : SFX_BREATH, u->pos, 0.3f, st == 4 ? 0.8f : 1.6f);
        return;
    }
    case USE_SCOPE: say(SCOPE[g_rot > 5 ? 5 : g_rot], 8); audio_play_ex(SFX_CREAK, 0.3f, 1.4f); return;
    case USE_FRIDGE:
        say(g_rot >= 5 ? "it is packed full of long black hair, pressed tight against the door, and it is wet." : FRIDGE[GetRandomValue(0, (int)(sizeof FRIDGE / sizeof *FRIDGE) - 1)], 6);
        audio_play_ex(SFX_CREAK, 0.4f, 0.8f);
        return;
    default: break;
    }
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
    if (u->kind == USE_VALVE) {   // it turns, screaming, and somewhere a pipe starts to knock
        play_from(SFX_SCREECH, u->pos, 0.8f, 0.7f);
        P.noise = 1.0f;
        int n = 0, all = 0;
        for (int i = 0; i < L.uses.size; i++) if (L.uses.data[i].kind == USE_VALVE) { all++; n += L.uses.data[i].done; }
        if (n < all) { char b[64]; snprintf(b, sizeof b, "%d of %d.", n, all); say(b, 3); }
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
            if (IsKeyPressed(KEY_Q) && (P.fx & (1u << FX_MATCHES)) && match.t <= 0 && !catchS.on) {
                if (match.left > 0) { match.left--; match.t = 7.0f; audio_play_ex(SFX_SCRAPE, 0.5f, 2.4f); if (match.left == 0) say("that was the last one.", 3); }
                else say("the box is empty. there will be more in the next dream.", 3);
            }
            if (match.t > 0) match.t -= frameDt;
            if (subT > 0) subT -= frameDt;
            if (hide.on) { P.vel = (Vector3){ 0 }; P.speedMeter = 0; P.noise = 0; }
            if (IsKeyPressed(KEY_R) && L.id != W_HUB) go(W_HUB, true);
            bool wasGrip = P.gripping;

            acc += frameDt;
            while (acc >= DT) {
                acc -= DT;
                if (!tr.on && dinner.sitT <= 0 && !catchS.on && !hide.on && !fair.ride.on) {
                    Input in = input_read();
                    if (bot) bot_input(&in, &P, &L, (float)clock);
                    player_update(&P, &L, &in, DT);
                    if (L.wrap > 0) { P.pos.x = wrapf(P.pos.x, L.wrap); P.pos.z = wrapf(P.pos.z, L.wrap); }   // off one edge, in at the other
                }
                level_step(&L, DT, P.pos);
            }
            static float mimicT; static Sfx mimicS;
            if (P.gripping && !wasGrip) { audio_play(SFX_GRAB); mimicS = SFX_GRAB; mimicT = 0.8f; }
            if (P.jumped) { audio_play(SFX_JUMP); P.jumped = false; mimicS = SFX_JUMP; mimicT = 0.8f; }
            if (mimicT > 0) { mimicT -= frameDt; if (mimicT <= 0 && L.id == W_SHAFT && climber.on) play_from(mimicS, climber.pos, 0.9f, 0.85f); }   // below you, something does the same
            if (P.grounded) {
                stepDist += Vector3Length((Vector3){ P.vel.x, 0, P.vel.z }) * frameDt;
                if (stepDist > 2.0f) {
                    stepDist = 0; audio_play_ex(SFX_STEP, 0.15f + 0.4f * P.noise, 0.85f + 0.2f * (GetRandomValue(0, 100) / 100.0f));
                    if (echo.on > 0 && L.id != W_HUB) echo.q = 0.26f;
                }
            }
            if (echo.on > 0) {   // someone walking behind you, exactly in step. when you stop, they take one more
                echo.on -= frameDt;
                bool moving = P.speedMeter > 0.5f;
                if (echo.moving && !moving) echo.extra = 0.8f;
                echo.moving = moving;
                if (echo.q > 0) { echo.q -= frameDt; if (echo.q <= 0) play_behind(SFX_STEP, 0.28f, 0.78f); }
                if (echo.extra > 0) { echo.extra -= frameDt; if (echo.extra <= 0) play_behind(SFX_STEP, 0.5f, 0.72f); }
            }

            if (catchS.on) {   // it has you
                float before = catchS.t, black = CATCH[catchS.style].black;
                catchS.t += frameDt;
                #define CUE(x) (before < (x) && catchS.t >= (x))
                float aim = CATCH[catchS.style].pitch;
                if (catchS.style == CS_TALL) aim *= 1.0f - Clamp(catchS.t / 1.55f, 0, 1);   // your eyes follow him down
                if (catchS.style == CS_HOST) aim *= 1.0f - Clamp(catchS.t / 1.3f, 0, 1);
                if (catchS.style == CS_SEEK) aim *= 1.0f - Clamp(catchS.t / 1.0f, 0, 1);
                P.pitch += (aim - P.pitch) * fminf(1.0f, frameDt * 6.0f);
                madness = catchS.style == CS_GREY ? 0.2f : 1.0f;
                switch (catchS.style) {
                case CS_POUNCE: if (CUE(0.32f)) audio_play_ex(SFX_ROAR, 1.0f, 1.0f); break;
                case CS_ARMS:   if (CUE(0.4f)) audio_play_ex(SFX_CREAK, 1.0f, 0.5f); if (CUE(0.75f)) audio_play_ex(SFX_SCREECH, 0.7f, 0.4f); break;
                case CS_MOTHER: if (CUE(0.95f)) { audio_play_ex(SFX_CRY, 1.0f, 0.6f); audio_play_ex(SFX_ROAR, 0.9f, 0.8f); } break;
                case CS_GIANT:  if (CUE(0.6f)) audio_play_ex(SFX_ROAR, 1.0f, 0.45f); if (CUE(0.25f)) audio_play_ex(SFX_GIANT, 1.0f, 0.6f); break;
                case CS_PRIEST: if (CUE(0.8f)) audio_play_ex(SFX_BELL, 1.0f, 0.8f); break;
                case CS_TALL:   if (CUE(0.9f)) audio_play_ex(SFX_SCREECH, 0.8f, 0.35f); break;
                case CS_HOST:   if (CUE(0.6f)) audio_play_ex(SFX_CHANT, 1.0f, 0.8f); break;
                case CS_SHEET:  if (CUE(0.55f)) audio_play_ex(SFX_SCREECH, 0.9f, 0.9f); break;
                case CS_FALL:   if (CUE(0.22f)) audio_play_ex(SFX_ROAR, 1.0f, 1.1f); if (CUE(0.6f)) audio_play_ex(SFX_WAKE, 0.8f, 0.6f); break;
                case CS_MIRROR: if (CUE(0.85f)) { audio_play_ex(SFX_SCREECH, 1.0f, 1.3f); audio_play_ex(SFX_ROAR, 0.7f, 1.5f); } break;
                case CS_ICE:    if (CUE(0.5f)) audio_play_ex(SFX_ROAR, 0.9f, 0.45f); if (CUE(1.1f)) audio_play_ex(SFX_ICE, 0.8f, 0.6f); break;
                case CS_SEEK:   if (CUE(1.05f)) { audio_play_ex(SFX_ROAR, 1.0f, 1.35f); audio_play_ex(SFX_CRY, 0.7f, 1.3f); } break;
                default: break;
                }
                #undef CUE
                if (catchS.t > black + 1.1f) { catchS.on = false; go(W_HUB, true); say(catchS.line, 4); }
            }
            if (!tr.on && !catchS.on) {
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
                static const char *HINT[] = { [USE_NOTE] = "E  read", [USE_VALVE] = "E  turn the valve", [USE_LINK] = "E  touch the screen", [USE_SIT] = "E  sit down", [USE_FACE] = "E  touch his face",
                                              [USE_HIDE] = "E  hide in here", [USE_FORTUNE] = "E  ask for your fortune", [USE_RIDE] = "E  ride the carousel", [USE_HAMMER] = "E  swing the hammer",
                                              [USE_DIARY] = "E  read his diary", [USE_BED] = "E  lie down and sleep", [USE_PIANO] = "E  play a note", [USE_RADIO] = "E  turn the dial",
                                              [USE_SCOPE] = "E  look through the telescope", [USE_FRIDGE] = "E  open the fridge" };
                if (fair.ride.on) near = NULL;
                useHint = near ? HINT[near->kind] : NULL;
                bool petCat = false;
                if (!near && (L.id == W_HUB || (cat.withYou && !cat.gone))) {   // the cat, if it is right in front of you
                    Vector3 eye = player_eye(&P), fwd = player_forward(&P), cp = { cat.pos.x, cat.pos.y + 0.25f, cat.pos.z };
                    Vector3 d = Vector3Subtract(cp, eye); float len = Vector3Length(d);
                    if (len < 2.3f && Vector3DotProduct(Vector3Scale(d, 1.0f / len), fwd) > 0.8f && cat.mode != 3) { petCat = true; useHint = cat.mode == 0 ? "E  wake the cat" : "E  stroke the cat"; }
                }
                if (hide.on) useHint = "E  come out";
                if (reading >= 0 && (IsKeyPressed(KEY_E) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || P.speedMeter > 4.5f)) reading = -1;
                else if (hide.on && IsKeyPressed(KEY_E)) hide_out();
                else if (petCat && IsKeyPressed(KEY_E)) {
                    if (cat.sulk > 0) say("the cat won't look at you.", 3);
                    else {
                        cat.petted++; cat.mode = cat.mode == 0 ? 1 : cat.mode;
                        audio_play_ex(SFX_PURR, 0.9f, 1.0f);
                        if (!cat.follow) { cat.follow = true; say("it purrs. it will follow you now, even through the doors. it knows things before you do.", 6); }
                        else say(PET[GetRandomValue(0, g_rot >= 3 ? 4 : 2)], 3);
                    }
                }
                else if (near && IsKeyPressed(KEY_E)) {
                    if (near->kind == USE_NOTE) { reading = near->arg; near->done = true; audio_play_ex(SFX_CREAK, 0.25f, 1.6f); if (!noteSeen[near->arg]) { noteSeen[near->arg] = true; notesRead++; } }
                    else use_thing(near);
                }
                // pickups
                for (int i = 0; i < L.pickups.size; i++) {
                    Pickup *pk = &L.pickups.data[i];
                    if (!pk->taken && pk->locked && Vector3Distance(Vector3Add(P.pos, (Vector3){ 0, 1, 0 }), pk->pos) < 1.6f) {
                        if (msgT < 0.5f) say(L.id == W_SCHOOL ? "the lost property cupboard is locked. it opens for children who are never found." : "it is at the bottom of the pool, under the water. somewhere, three valves.", 3);
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
                        if (pk->fx == FX_MATCHES) match.left = 5;
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
                            if (msgT < 0.5f) {
                                char b[200] = "the door won't open. it still wants: ";
                                for (int f = 0; f < FX_COUNT; f++) if ((pt->needs & (1 << f)) && !(P.fx & (1u << f))) { strcat(b, FX_NAME[f]); strcat(b, " "); }
                                say(b, 3);
                            }
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
                if (P.slipped) { P.slipped = false; audio_play(SFX_KNOCK); }
                if (morgue_update(frameDt, player_eye(&P), fwd)) caught(CS_SHEET, FIG_SHEET, (Color){ 0 }, 0, "it got down in the dark.");
                if (theatre_update(frameDt)) caught(CS_PUPPET, FIG_PUPPET, (Color){ 0 }, 0, "you left your seat in the dark.");
                if (below_update(frameDt)) caught(below.state == 2 ? CS_POUNCE : CS_ARMS, FIG_CRAWLER, (Color){ 0 }, 0, below.state == 2 ? "it was faster than you." : "they wanted to keep you.");
                if (baths_update(frameDt, player_eye(&P))) caught(swim.state == 0 ? CS_DROWN : CS_POUNCE, FIG_CRAWLER, (Color){ 140, 150, 146, 255 }, 0, swim.state == 0 ? "something under the water had you." : "it heard you on the tiles.");
                if (nursery_update(frameDt, player_eye(&P))) caught(CS_GIANT, FIG_MOTHER, (Color){ 0 }, 8.0f, "she found you.");
                if (city_update(frameDt, player_eye(&P), fwd)) caught(CS_TALL, FIG_TALL, (Color){ 0 }, 2.7f, "he was always that tall.");
                if (dinner_update(frameDt, player_eye(&P), fwd)) caught(CS_HOST, FIG_GARDENER, (Color){ 70, 30, 30, 255 }, 0, "it is rude to leave the table.");
                if (grey_update(frameDt)) caught(CS_GREY, FIG_PENITENT, (Color){ 104, 104, 104, 255 }, 0, "you forget something.");
                if (ward_update(frameDt, player_eye(&P))) caught(CS_MOTHER, FIG_MOTHER, (Color){ 0 }, 0, g_wardLoop >= 4 ? "you looked." : "she was always in the corridor.");
                if (update_mass(frameDt, player_eye(&P))) caught(CS_PRIEST, FIG_PRIEST, (Color){ 0 }, 0, "he counted one too many.");
                if (update_climber(player_eye(&P), fwd)) caught(CS_FALL, FIG_CLIMBER, (Color){ 0 }, 0, "it had your hands.");
                if (fair_update(frameDt, player_eye(&P), fwd)) caught(CS_MIRROR, FIG_SELF, (Color){ 0 }, 0, "it was only ever you.");
                if (lake_update(frameDt, player_eye(&P))) caught(CS_ICE, FIG_SELF, (Color){ 0 }, 0, "the ice opened.");
                if (school_update(frameDt, player_eye(&P))) caught(CS_SEEK, FIG_SEEKER, (Color){ 0 }, 0, seek.found ? "it heard the door." : "found you.");
                scare_update(frameDt);
                hub_update(frameDt);
                cat_update(frameDt);
                if (dead) go(W_HUB, true);
            }

            // transition
            if (tr.on) {
                tr.t += frameDt;
                if (tr.t >= 0.6f && !tr.loaded) { load_world(tr.to, tr.wake); tr.loaded = true; }
                if (tr.t >= 1.3f) tr.on = false;
            }

            if (L.sky.eye && !tr.on && !catchS.on && update_gaze(frameDt)) caught(CS_HAND, FIG_HAND, (Color){ 0 }, 1.4f, "it saw you, and it took you up.");
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
            Vector3 tp;
            if (L.id != W_HUB && L.id != W_SHAFT && threat(&tp)) { float d = Vector3Distance(tp, P.pos); if (d < 22) m += (22 - d) / 22.0f * 0.6f; }
            if (hide.on && seek.state >= 1) { float d = Vector3Distance(seek.pos, P.pos); if (d < 10) m += (10 - d) / 10.0f * 0.8f; }
            if (L.id == W_LAKE) m += lake.stress * 0.6f;
            if (m > 1) m = 1;
            madness += (m - madness) * fminf(1, frameDt * 3);
            tension = madness;
            float tone = L.id == W_HUB ? 1.0f : L.id == W_SHAFT ? 0.75f : L.id == W_BATHS ? 0.9f : L.id == W_VOID ? 1.4f : L.id == W_NURSERY ? 1.15f : L.id == W_CHAPEL ? 0.8f : L.id == W_WARD ? 1.25f : L.id == W_STATIC ? 0.6f : L.id == W_DINNER ? 0.85f : L.id == W_NURSERY ? 0.7f : L.id == W_WOMB ? 0.55f : L.id == W_CITY ? 0.7f : L.id == W_MORGUE ? 1.3f : L.id == W_THEATRE ? 0.9f : L.id == W_FAIR ? 1.05f : L.id == W_LAKE ? 0.62f : L.id == W_SCHOOL ? 1.2f : L.id == W_END ? 2.0f : 1.0f;
            float mus = (L.id == W_THEATRE && !show.dark) ? 0.6f : L.id == W_DINNER ? 0.6f : L.id == W_NURSERY ? 0.8f : L.id == W_VOID ? 0.5f : L.id == W_END ? 0.8f : L.id == W_HUB ? (house.station ? 0.7f : 0.4f) : L.id == W_FAIR ? (fair.ride.on ? 1.0f : 0.7f) : L.id == W_SCHOOL ? 0.2f : 0.0f;
            bool home = L.id == W_HUB;   // the house: a music box in tune, a low warm hum, nothing else
            if (home) tension = madness = 0;
            if (L.id == W_WOMB) tension = L.heart.y > -50 ? fmaxf(tension, 0.65f) : 0.0f;   // the heart is everywhere down here, until it stops
            audio_music(frozen ? 0.0f : mus, home ? fminf(0.6f, g_rot * 0.1f) : fminf(1.0f, madness * 0.9f + (blackout > 0 ? 0.4f : 0.0f) + (L.id == W_FAIR ? 0.25f : 0.0f)));
            float whisper = L.nearest < 18 ? 1.0f - L.nearest / 18.0f : 0.0f;
            if (L.id == W_STATIC) whisper = fmaxf(whisper, 0.35f);   // the hiss of the screens
            if (home) whisper = 0;
            bool hush = catchS.on && (catchS.t > CATCH[catchS.style].black || (catchS.style == CS_MOTHER && catchS.t < 0.95f));   // nothing at all
            if (catchS.on && catchS.style == CS_PUPPET && catchS.t < 1.0f) audio_music(1.0f, 0.0f);   // the music box, right in your ear
            if (catchS.on && catchS.style == CS_GREY) whisper = 0.8f;   // and the hiss of a screen with nothing on it
            if (hush) { audio_music(0, 0); whisper = 0; }
            if (!frozen) audio_set(tone, tension, whisper, hush ? 0.0f : tr.on ? 0.3f : L.id == W_STATIC ? 0.25f : home ? 0.45f : 0.9f);
            {   // the mass behind the walls gets louder the deeper you go; something breathes when one of them is close
                float choir = L.id == W_HUB ? 0.0f : (L.id == W_END || L.id == W_CHAPEL) ? 1.0f : L.id == W_STATIC ? 0.0f : 0.35f;
                float breath = 0, bpan = 0, bd = 1e9f;
                for (int i = 0; i < L.watchers.size; i++) {
                    float d = Vector3Distance(L.watchers.data[i].pos, P.pos);
                    if (d < bd) { bd = d; bpan = pan_of(L.watchers.data[i].pos); }
                }
                if (climber.on) { float d = Vector3Distance(climber.pos, P.pos); if (d < bd) { bd = d; bpan = pan_of(climber.pos); } }
                Vector3 tq;
                if (L.id != W_HUB && threat(&tq)) { float d = Vector3Distance(tq, P.pos); if (d < bd) { bd = d; bpan = pan_of(tq); } }
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
        gfx_fade_color((Color){ 0, 0, 0, 255 });
        if (catchS.on) {
            float b = CATCH[catchS.style].black, t = catchS.t, k;
            switch (catchS.style) {
            case CS_DROWN: k = (t - 0.3f) / (b - 0.3f); break;    // the water closes over you
            case CS_GREY:  k = t / b; break;                      // everything slowly goes grey
            case CS_SHEET: k = (t - 0.6f) / (b - 0.6f); break;    // a sheet over your face
            case CS_ICE:   k = (t - 0.5f) / (b - 0.5f); break;    // the black water
            case CS_MIRROR: k = (t - 1.0f) / (b - 1.0f); break;   // and then nothing but glass
            default:       k = (t - (b - 0.12f)) / 0.12f; break;  // then nothing
            }
            fade = fmaxf(fade, Clamp(k, 0, 1));
            gfx_fade_color(CATCH[catchS.style].fade);
        }

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
            text_c("WASD · SHIFT run · CTRL kneel · E use · LMB grip · F lamp · Q match · R wake up · [ ] mouse", RT_H - 18, 10, (Color){ 90, 85, 75, 255 });
        } else {
            Vector3 eye = player_eye(&P), fwd = player_forward(&P);
            Camera3D cam = { 0 };
            if (giant.shake > 0 && L.id == W_NURSERY) eye.y += sinf(time * 47.0f) * 0.05f * giant.shake;   // the floor shakes under her
            if (lake.shake > 0 && L.id == W_LAKE) eye.y += sinf(time * 53.0f) * 0.06f * lake.shake;     // and the ice, when it hits it
            cam.position = eye;
            cam.target = Vector3Add(eye, fwd);
            float roll = P.roll;
            if (catchS.on) {
                float t = catchS.t, k;
                switch (catchS.style) {
                case CS_DROWN: k = fminf(1, t / 0.9f); cam.position.y -= 1.6f * k; roll += 18 * k; break;
                case CS_ICE:   k = fminf(1, t / 0.6f); cam.position.y -= 1.9f * k * k; roll += 24 * k; break;
                case CS_HOST:  cam.position.y -= 0.7f * fminf(1, t / 0.3f); break;
                case CS_HAND:  k = fminf(1, t / 1.5f); cam.position.y += 8.0f * k * k * (3 - 2 * k); roll += sinf(t * 3) * 6; break;
                case CS_FALL:  if (t > 0.55f) { k = (t - 0.55f) / 0.9f; cam.position.y -= 10.0f * k * k; roll += (t - 0.55f) * 320; } break;
                case CS_GIANT: cam.position.y += sinf(t * 45) * 0.06f * fmaxf(0, 1 - t * 2); break;
                case CS_POUNCE: if (t > 0.45f) cam.position.y += sinf(t * 60) * 0.03f; break;
                default: break;
                }
                cam.target = Vector3Add(cam.position, fwd);
            }
            cam.up = Vector3RotateByAxisAngle((Vector3){ 0, 1, 0 }, fwd, roll * DEG2RAD);
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
        if (shotWorld == W_WARD && getenv("MURK_LAP")) { g_wardLoop = atoi(getenv("MURK_LAP")); level_free(&L); level_build(&L, W_WARD, dreams); mom.s = mom.shown = 40; }   // which lap of the ward
        P.pos = (Vector3){ sx, sy, sz }; P.yaw = syaw; P.pitch = spit;
        if (sfxmask & 1) lampOn = 1;
        if (getenv("MURK_CATCH")) { int cs = 0, ck = 0; float sc = 0; sscanf(getenv("MURK_CATCH"), "%d,%d,%f", &cs, &ck, &sc); caught((CatchStyle)cs, (FigKind)ck, (Color){ 0 }, sc, "dev"); }   // dev hook: be caught, at once
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
