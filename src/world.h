#pragma once
#include "common.h"

typedef struct Sky {
    bool on;
    Color zenith, horizon, ground;
    float stars;            // 0..1 density
    bool moon; Color moonCol; Vector3 moonDir;
    Color aurora; float aurAmt;
    bool eye;               // a vast eye that watches from the sky
    float eyeAmt;           // how wide open it is
} Sky;

typedef struct Level {
    WorldId id;
    const char *name;
    b3WorldId phys;
    Boxes boxes;
    Pickups pickups;
    Portals portals;
    Watchers watchers;
    Motes motes;
    Props props;
    Effigies effigies;
    Uses uses;
    Blooms blooms;
    Sky sky;
    Color moteCol;          // dust / pollen / embers
    Color gradeLo, gradeHi; // colour grading of the whole dream
    bool water; float waterY, waterHalf; Vector3 waterC;
    bool moteGlow;
    bool rain;              // the motes are rain, falling hard
    bool creepers;          // watchers that only move when unseen (open levels)

    Color fog;
    float fogDensity;
    float light;            // base brightness multiplier
    float ambient;          // how much of the dream you can see without a light of your own (1 = all of it)
    Vector3 spawn;
    float spawnYaw;
    float killY;            // fall below this and you wake up

    bool sludge;            // rising sludge hazard
    float sludgeY, sludgeSpeed;
    bool sludgeArmed;
    float shaftW;           // half width of the shaft
    float wrap;             // > 0: the dream repeats every `wrap` metres in x and z, and walking off one edge brings you in at the other

    // drains maze
    int mazeN;
    float cell;
    uint8_t *open;          // per-cell bitmask of open sides (1=N 2=E 4=S 8=W)
    int *dist;              // BFS distance to player
    int distCell;

    int dreamNo;            // how many dreams deep we are; everything gets worse
    bool sawWatcher;        // set when a watcher first comes into view (main consumes it)
    float nearest;          // distance to the closest watcher
    Vector3 bed;            // ending trigger
    Vector3 heart;          // the womb: where the heart is
    Vector3 wakePos; float wakeYaw;   // where you come to after waking (the hub's bed)
    float t;
} Level;

extern int g_launches, g_wakes;   // persisted between runs: the game remembers you
extern int g_secret;
extern int g_wardLoop;            // how many times you have walked the ward corridor this visit              // you knelt in the circle and were told who is asleep
// the pages left lying around. 0 .. NOTE_COUNT-1
enum { NOTE_VIGIL, NOTE_DAY9, NOTE_DOORS, NOTE_GARDEN, NOTE_DRAINS, NOTE_CLIMB, NOTE_EYE, NOTE_FLOWERS, NOTE_CANDLES, NOTE_FAMILY, NOTE_AWAKE, NOTE_BELL, NOTE_SLEEPER, NOTE_WARD, NOTE_STATIC, NOTE_WOMB, NOTE_CITY, NOTE_COUNT };
void level_build(Level *L, WorldId id, int seed);
void level_free(Level *L);
void level_step(Level *L, float dt, Vector3 player);
// the blind things in the drains: they go where they last heard you. true if one caught the player this frame
bool level_watchers(Level *L, Vector3 eye, Vector3 fwd, Vector3 feet, float dt, float noise);
bool level_seen(Level *L, Vector3 eye, Vector3 fwd, Vector3 pos);
const Box *level_box_of_shape(const Level *L, b3ShapeId s);
