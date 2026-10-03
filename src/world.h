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
    Blooms blooms;
    Sky sky;
    Color moteCol;          // dust / pollen / embers
    Color gradeLo, gradeHi; // colour grading of the whole dream
    bool water; float waterY, waterHalf; Vector3 waterC;
    bool moteGlow;
    bool creepers;          // watchers that only move when unseen (open levels)

    Color fog;
    float fogDensity;
    float light;            // base brightness multiplier
    Vector3 spawn;
    float spawnYaw;
    float killY;            // fall below this and you wake up

    bool sludge;            // rising sludge hazard
    float sludgeY, sludgeSpeed;
    bool sludgeArmed;

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
    float t;
} Level;

extern int g_launches, g_wakes;   // persisted between runs: the game remembers you
void level_build(Level *L, WorldId id, int seed);
void level_free(Level *L);
void level_step(Level *L, float dt, Vector3 player);
// true if a watcher caught the player this frame
bool level_watchers(Level *L, Vector3 eye, Vector3 fwd, Vector3 feet, float dt, bool blind);
bool level_seen(Level *L, Vector3 eye, Vector3 fwd, Vector3 pos);
const Box *level_box_of_shape(const Level *L, b3ShapeId s);
