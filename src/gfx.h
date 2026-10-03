#pragma once
#include "common.h"
#include "world.h"

void gfx_init(void);
void gfx_shutdown(void);

void gfx_begin_scene(Camera3D cam, Color fog, float density, float flicker, float time);
void gfx_end_scene(void);
// how much light there is: the dream's own, a beam from where you are looking (the lamp), and the little you carry
void gfx_set_light(float ambient, float torch, float beamR, float reach, float carried);

// draw an axis aligned (or quaternion rotated) textured box in the grime shader
void gfx_box(Vector3 c, Vector3 h, TexId tex, Color tint, float scale);
void gfx_box_rot(Vector3 c, Vector3 h, Quaternion q, TexId tex, Color tint, float scale);
void gfx_set_emit(bool on);   // flushes; while on, gfx_glow boxes are unlit and fog-free
// a tapered six-sided limb from a (radius ra) to b (radius rb), shaded by a fixed key light
void gfx_limb(Vector3 a, Vector3 b, float ra, float rb, TexId tex, Color tint);
// ellipsoid with three (scaled) semi-axes
void gfx_ellipsoid(Vector3 c, Vector3 ax, Vector3 ay, Vector3 az, TexId tex, Color tint);
// glowing box (call between gfx_set_emit(true/false))
void gfx_glow(Vector3 c, Vector3 h, Color col);
// scrolling horizontal slab (sludge/water)
void gfx_slab(float y, float half, TexId tex, Color tint, float time);

void gfx_update_static(float time, bool face);
// runtime text (blood on the walls). returns a TexId-compatible slot >= TEX_COUNT
int gfx_text_tex(const char *s, Color c);
float gfx_text_aspect(int id);
// a ritual circle (decal texture, square)
int gfx_sigil_tex(Color c, int seed);
// flat quad facing +/- along the thin axis of h (dir = +1/-1 is which way it faces)
void gfx_decal(Vector3 c, Vector3 h, int id, float dir);
// final pass: low-res target -> window with the nasty post shader
void gfx_present(float time, float madness, float fade, float flash, float glitch);

// additive glow: soft billboard blobs (bulbs, doors, eyes). bracket with begin/end
void gfx_begin_glow(void);
void gfx_halo(Vector3 p, float size, Color col, float intensity);
void gfx_end_glow(void);
// sky dome (gradient, stars, aurora, moon, the eye). draw first, after gfx_begin_scene
void gfx_sky(const Sky *sk, float time, Vector3 playerPos);
// colour grade of the post pass: 128 = neutral
void gfx_grade(Color lo, Color hi);

extern RenderTexture2D gfx_rt;
