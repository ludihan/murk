#include "player.h"
#include <float.h>
#include <math.h>

#define RADIUS 0.35f
#define HEIGHT 1.75f
#define EYE 1.58f
#define MAX_PLANES 32

static const b3Capsule CAP = { { 0, RADIUS, 0 }, { 0, HEIGHT - RADIUS, 0 }, RADIUS };

typedef struct {
    b3CollisionPlane planes[MAX_PLANES];
    int n;
    Vector3 vel;
} PlaneCtx;

static bool plane_cb(b3ShapeId sid, const b3PlaneResult *pr, int count, void *ctx) {
    PlaneCtx *c = ctx;
    b3BodyId body = b3Shape_GetBody(sid);
    for (int i = 0; i < count && c->n < MAX_PLANES; i++) {
        c->planes[c->n++] = (b3CollisionPlane){ pr[i].plane, FLT_MAX, 0.0f, true };
        // shove loose junk: dynamic bodies take an impulse opposite the contact normal
        if (b3Body_GetType(body) == b3_dynamicBody) {
            float sp = fminf(Vector3Length((Vector3){ c->vel.x, 0, c->vel.z }) + 1.0f, 7.0f);
            b3Vec3 imp = b3MulSV(-0.25f * b3Body_GetMass(body) * sp, pr[i].plane.normal);
            b3Body_ApplyLinearImpulse(body, imp, pr[i].point, true);
        }
    }
    return true;
}

Vector3 player_forward(const Player *p) {
    float y = p->yaw * DEG2RAD, pt = p->pitch * DEG2RAD;
    return (Vector3){ sinf(y) * cosf(pt), sinf(pt), -cosf(y) * cosf(pt) };
}
Vector3 player_eye(const Player *p) {
    float bob = sinf(p->bob) * 0.035f * (p->grounded ? 1 : 0);
    return (Vector3){ p->pos.x, p->pos.y + EYE + bob - p->landKick - p->crouch * 0.75f, p->pos.z };
}

void player_spawn(Player *p, const Level *L) {
    unsigned fx = p->fx;
    *p = (Player){ 0 };
    p->fx = fx;
    p->pos = L->spawn;
    p->yaw = L->spawnYaw;
    p->grip = 1;
}

// resolve a velocity against the world with Box3D's character mover: collide, solve planes, cast, repeat
static void move_and_slide(Player *p, Level *L, float dt) {
    b3QueryFilter qf = b3DefaultQueryFilter();
    Vector3 remaining = Vector3Scale(p->vel, dt);
    PlaneCtx ctx = { .n = 0, .vel = p->vel };
    for (int it = 0; it < 5; it++) {
        ctx.n = 0;
        b3World_CollideMover(L->phys, b3v(p->pos), &CAP, qf, plane_cb, &ctx);
        b3PlaneSolverResult r = b3SolvePlanes(b3v(remaining), ctx.planes, ctx.n);
        float frac = b3World_CastMover(L->phys, b3v(p->pos), &CAP, r.delta, qf, NULL, NULL);
        Vector3 d = rv(r.delta);
        p->pos = Vector3Add(p->pos, Vector3Scale(d, frac));
        if (frac >= 1.0f) break;
        remaining = Vector3Scale(d, 1.0f - frac);
    }
    // final slide of the velocity along whatever we're touching
    ctx.n = 0;
    b3World_CollideMover(L->phys, b3v(p->pos), &CAP, qf, plane_cb, &ctx);
    p->vel = rv(b3ClipVector(b3v(p->vel), ctx.planes, ctx.n));
}

static const Box *probe(Level *L, Vector3 o, Vector3 dir, float len, b3RayResult *out) {
    b3RayResult r = b3World_CastRayClosest(L->phys, b3v(o), b3v(Vector3Scale(dir, len)), b3DefaultQueryFilter());
    if (out) *out = r;
    return r.hit ? level_box_of_shape(L, r.shapeId) : NULL;
}

Input input_read(void) {
    Input in;
    in.mx = (IsKeyDown(KEY_D) ? 1 : 0) - (IsKeyDown(KEY_A) ? 1 : 0);
    in.mz = (IsKeyDown(KEY_W) ? 1 : 0) - (IsKeyDown(KEY_S) ? 1 : 0);
    in.sprint = IsKeyDown(KEY_LEFT_SHIFT);
    in.grip = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.glide = IsKeyDown(KEY_SPACE);
    in.crouch = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    return in;
}

void player_update(Player *p, Level *L, const Input *in, float dt) {
    const bool gloves = p->fx & (1u << FX_GLOVES), boots = p->fx & (1u << FX_BOOTS), feather = p->fx & (1u << FX_FEATHER);
    float yaw = p->yaw * DEG2RAD;
    Vector3 fwd = { sinf(yaw), 0, -cosf(yaw) }, right = { cosf(yaw), 0, sinf(yaw) };
    float mx = in->mx, mz = in->mz;
    bool kneel = in->crouch && !p->gripping;
    p->crouch += ((kneel ? 1.0f : 0.0f) - p->crouch) * fminf(1.0f, dt * 9.0f);
    bool sprint = in->sprint && p->crouch < 0.5f;
    bool jumpPressed = p->jumpBuf > 0;

    p->regrabLock = fmaxf(0, p->regrabLock - dt);
    p->jumpBuf = fmaxf(0, p->jumpBuf - dt);

    // ---- ground check
    float gf = b3World_CastMover(L->phys, b3v(p->pos), &CAP, (b3Vec3){ 0, -0.09f, 0 }, b3DefaultQueryFilter(), NULL, NULL);
    bool wasGrounded = p->grounded;
    p->grounded = gf < 1.0f && p->vel.y <= 0.5f;
    if (p->grounded) { p->coyote = 0.12f; p->airJumps = boots ? 1 : 0; }
    else p->coyote = fmaxf(0, p->coyote - dt);
    if (p->grounded && !wasGrounded && p->vel.y < -9) p->landKick = fminf(0.3f, -p->vel.y * 0.02f);
    if (p->grounded && !wasGrounded && p->vel.y < -6) p->noise = fmaxf(p->noise, fminf(1.0f, -p->vel.y * 0.08f));   // landings carry
    p->landKick = fmaxf(0, p->landKick - dt * 1.2f);

    // ---- grip: hold the left mouse button next to a rusty surface
    bool want = in->grip;
    Vector3 chest = { p->pos.x, p->pos.y + 1.1f, p->pos.z };
    b3RayResult hit = { 0 };
    const Box *wall = probe(L, chest, fwd, 1.0f, &hit);
    p->canGrip = wall && (wall->flags & F_GRIP) && fabsf(hit.normal.y) < 0.4f && p->grip > 0.02f;
    if (p->gripping && !(want && p->grip > 0.0f)) p->gripping = false;
    if (!p->gripping && want && p->canGrip && p->regrabLock <= 0) {
        p->gripping = true;
        p->wallN = rv(hit.normal);
    }
    if (p->gripping) {
        // keep the wall normal fresh while traversing, and let go if the wall ends under us
        if (wall && (wall->flags & F_GRIP)) p->wallN = rv(hit.normal);
        else p->gripping = false;
    }

    float drainMul = gloves ? 0.45f : 1.0f;
    if (p->gripping) {
        Vector3 n = p->wallN;
        Vector3 along = Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, n)); // wall tangent
        float climb = (gloves ? 3.2f : 2.4f) * (sprint ? 1.25f : 1.0f);
        Vector3 v = Vector3Scale(along, mx * climb * 0.8f);
        v.y = mz * climb;
        v = Vector3Add(v, Vector3Scale(n, -1.2f)); // press into the wall
        p->vel = v;
        if (p->grip < 0.18f && GetRandomValue(0, 10000) < (int)(dt * 18000)) {   // fingers give out before you decide to
            p->gripping = false; p->regrabLock = 0.4f; p->vel.y = -3.0f; p->slipped = true;
            p->grip = fmaxf(0, p->grip - 0.03f);
            return;
        }
        float effort = 0.07f + 0.08f * (fabsf(mx) + fabsf(mz)) + (sprint ? 0.05f : 0);
        p->grip = fmaxf(0, p->grip - effort * drainMul * dt);
        // mantle: if a flat ledge sits just above reach and we're pushing toward it, pop over it
        Vector3 dirs[2] = { Vector3Scale(n, -1), Vector3Scale(along, mx) };
        for (int i = 0; i < 2; i++) {
            Vector3 dir = dirs[i];
            if ((i == 0 && mz <= 0) || (i == 1 && mx == 0)) continue;
            Vector3 o = Vector3Add(Vector3Add(p->pos, Vector3Scale(dir, 0.75f)), (Vector3){ 0, 2.6f, 0 });
            b3RayResult lr;
            const Box *ledge = probe(L, o, (Vector3){ 0, -1, 0 }, 2.4f, &lr);
            if (ledge && lr.normal.y > 0.7f) {
                float rise = lr.point.y - p->pos.y;
                if (rise > 0.7f && rise < 2.4f) {
                    p->vel = Vector3Add(Vector3Scale(dir, 3.0f), (Vector3){ 0, sqrtf(2 * 20 * (rise + 0.45f)), 0 });
                    p->gripping = false; p->regrabLock = 0.45f;
                    break;
                }
            }
        }
        if (p->gripping && jumpPressed) {
            p->gripping = false; p->regrabLock = 0.14f; p->jumpBuf = 0;
            if (mz > 0) p->vel = (Vector3){ n.x * 1.0f, gloves ? 10.5f : 9.0f, n.z * 1.0f };  // lunge up the wall
            else p->vel = Vector3Add(Vector3Scale(n, 6.0f), (Vector3){ 0, 5.5f, 0 });         // kick off
            p->grip = fmaxf(0, p->grip - 0.04f);
        }
    } else {
        // ---- walking / falling
        float speed = (sprint ? 6.0f : 3.6f) * (p->gliding ? 1.35f : 1.0f) * (1.0f - 0.55f * p->crouch);
        Vector3 wish = Vector3Add(Vector3Scale(right, mx), Vector3Scale(fwd, mz));
        if (Vector3Length(wish) > 1) wish = Vector3Normalize(wish);
        p->gliding = feather && in->glide && !p->grounded && p->vel.y < 1.0f && p->jumpBuf <= 0;
        float k = p->grounded ? 14.0f : p->gliding ? 5.0f : 2.2f;
        float a = 1.0f - expf(-k * dt);
        p->vel.x += (wish.x * speed - p->vel.x) * a;
        p->vel.z += (wish.z * speed - p->vel.z) * a;
        if (p->grounded && p->vel.y < 0) p->vel.y = -1.0f;   // stick to the floor instead of accumulating fall speed
        else p->vel.y -= 20.0f * dt;
        if (p->gliding && p->vel.y < -2.0f) p->vel.y += (-2.0f - p->vel.y) * fminf(1.0f, dt * 9.0f);   // the feather catches the air
        if (p->vel.y < -45) p->vel.y = -45;
        if (jumpPressed) {
            if (p->coyote > 0) { p->vel.y = 7.4f; p->coyote = 0; p->grounded = false; p->jumpBuf = 0; p->jumped = true; }
            else if (p->airJumps > 0) { p->vel.y = 7.4f; p->airJumps--; p->jumpBuf = 0; p->jumped = true; }
        }
        if (p->grounded && !want) p->grip = fminf(1.0f, p->grip + 0.28f * dt);
        else if (!want) p->grip = fminf(1.0f, p->grip + 0.05f * dt);
        if (p->grounded && Vector3Length(wish) > 0.1f) p->bob += dt * (sprint ? 12.0f : 8.5f);
    }

    move_and_slide(p, L, dt);

    float horiz = Vector3Length((Vector3){ p->vel.x, 0, p->vel.z });
    p->speedMeter = horiz;
    // noise: running is loud, walking carries, kneeling and creeping is almost nothing
    float loud = !p->grounded ? 0.0f : horiz < 0.4f ? 0.0f : sprint ? 1.0f : 0.12f + 0.38f * (1.0f - p->crouch);
    if (p->gripping) loud = 0.15f;
    if ((p->fx & (1u << FX_VEIL)) && p->crouch > 0.5f) loud *= 0.25f;   // under the veil you are hardly there
    p->noise = fmaxf(loud, p->noise - dt * 0.8f);
    float rollTarget = p->gripping ? sinf(GetTime() * 2.3f) * 1.2f * (1.0f - p->grip) + (-mx) * 2.0f : -mx * 1.2f;
    p->roll += (rollTarget - p->roll) * fminf(1, dt * 8);
}
