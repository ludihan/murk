#pragma once
#include "common.h"

void gfx_init(void);
void gfx_shutdown(void);

void gfx_begin_scene(Camera3D cam, Color fog, float density, float flicker, float time);
void gfx_end_scene(void);

// draw an axis aligned (or quaternion rotated) textured box in the grime shader
void gfx_box(Vector3 c, Vector3 h, TexId tex, Color tint, float scale);
void gfx_box_rot(Vector3 c, Vector3 h, Quaternion q, TexId tex, Color tint, float scale);
void gfx_set_emit(bool on);   // flushes; while on, gfx_glow boxes are unlit and fog-free
// glowing box (call between gfx_set_emit(true/false))
void gfx_glow(Vector3 c, Vector3 h, Color col);
// scrolling horizontal slab (sludge/water)
void gfx_slab(float y, float half, TexId tex, Color tint, float time);

void gfx_update_static(float time, bool face);
// runtime text (blood on the walls). returns a TexId-compatible slot >= TEX_COUNT
int gfx_text_tex(const char *s, Color c);
float gfx_text_aspect(int id);
// flat quad facing +/- along the thin axis of h (dir = +1/-1 is which way it faces)
void gfx_decal(Vector3 c, Vector3 h, int id, float dir);
// final pass: low-res target -> window with the nasty post shader
void gfx_present(float time, float madness, float fade, float flash, float glitch);

extern RenderTexture2D gfx_rt;
