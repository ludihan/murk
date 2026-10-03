#pragma once
#include <raylib.h>
#include <raymath.h>
#include <box3d/box3d.h>
#include <stdint.h>
#include <stdbool.h>

// ---- shared types -------------------------------------------------------

typedef enum {
    TEX_CONCRETE, TEX_RUST, TEX_TILE, TEX_FLESH, TEX_SLUDGE, TEX_WOOD, TEX_STATIC, TEX_GRASS, TEX_MOSAIC, TEX_EYES, TEX_WATER, TEX_SKIN, TEX_CLOTH, TEX_COUNT
} TexId;

enum { F_GRIP = 1, F_EMIT = 2, F_NOCOLLIDE = 4, F_DECAL = 8 };

typedef struct Box {
    Vector3 c, h;       // center, half extents
    TexId tex;
    Color tint;
    float scale;        // meters per texture tile
    uint8_t flags;
} Box;

typedef enum { FX_LAMP, FX_GLOVES, FX_BOOTS, FX_FEATHER, FX_COUNT } EffectId;
#define FX_ALL ((1 << FX_COUNT) - 1)

typedef struct Pickup  { Vector3 pos; EffectId fx; bool taken, locked; } Pickup;
typedef enum { W_HUB, W_SHAFT, W_DRAINS, W_VOID, W_GARDEN, W_END, W_COUNT } WorldId;
typedef struct Portal  { Vector3 pos; float radius; WorldId to; int needs; Color col; const char *label; } Portal;
typedef struct Watcher { Vector3 pos, goal; float phase, stride, timer, sense; int state; bool seen; } Watcher;
typedef struct Mote    { Vector3 pos, vel; float life; } Mote;
typedef struct Bloom   { Vector3 pos; Color col; float size, yaw; } Bloom;   // a flower that watches you
typedef struct Prop    { b3BodyId body; Vector3 h; TexId tex; Color tint; } Prop;
typedef enum { USE_NOTE, USE_CANDLE, USE_LILY } UseKind;
typedef struct Use     { Vector3 pos; int kind, arg; bool done; } Use;   // something you can press E on
typedef struct Effigy  { int kind; Vector3 pos; float yaw, tilt, look; bool seen; } Effigy;   // a figure that stays where it was put (mostly)

#define i_type Boxes,    Box
#include <stc/vec.h>
#define i_type Pickups,  Pickup
#include <stc/vec.h>
#define i_type Portals,  Portal
#include <stc/vec.h>
#define i_type Watchers, Watcher
#include <stc/vec.h>
#define i_type Motes,    Mote
#include <stc/vec.h>
#define i_type Blooms,   Bloom
#include <stc/vec.h>
#define i_type Props,    Prop
#include <stc/vec.h>
#define i_type Effigies, Effigy
#include <stc/vec.h>
#define i_type Uses,     Use
#include <stc/vec.h>

#define RT_W 480
#define RT_H 270

static inline b3Vec3 b3v(Vector3 v) { return (b3Vec3){ v.x, v.y, v.z }; }
static inline Vector3 rv(b3Vec3 v)  { return (Vector3){ v.x, v.y, v.z }; }
