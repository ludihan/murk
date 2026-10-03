#pragma once
#include "common.h"
#include "world.h"

typedef struct Player {
    Vector3 pos;            // feet
    Vector3 vel;
    float yaw, pitch;       // degrees
    bool grounded, gripping, canGrip;
    float grip;             // stamina 0..1
    float jumpBuf; bool jumped, mantled, slipped;
    float regrabLock, coyote, bob, roll, landKick;
    int airJumps;
    Vector3 wallN;
    unsigned fx;            // collected effect bitmask
    float speedMeter;
    bool gliding;
    float crouch;           // 0 standing .. 1 kneeling
    float noise;            // how loud you are right now, 0..1. the blind ones listen for this
} Player;

void player_spawn(Player *p, const Level *L);
typedef struct Input { float mx, mz; bool sprint, grip, glide, crouch; } Input;
Input input_read(void);
void player_update(Player *p, Level *L, const Input *in, float dt);
Vector3 player_eye(const Player *p);
Vector3 player_forward(const Player *p);
