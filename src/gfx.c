#include "gfx.h"
#include <rlgl.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

RenderTexture2D gfx_rt;
#define MAX_DECAL 32
static Texture2D decalTex[MAX_DECAL];
static float decalAsp[MAX_DECAL];
static char decalStr[MAX_DECAL][96];
static uint32_t decalCol[MAX_DECAL];
static int decalN;
static Texture2D tex[TEX_COUNT];
static Shader obj, post;
static int u_fog, u_dens, u_flick, u_emit;
static int p_time, p_mad, p_fade, p_flash, p_res, p_glitch;

// ---------------------------------------------------------------- procedural textures
static uint32_t hash2(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float rnd(int x, int y, int s) { return (hash2(x, y, s) & 0xffff) / 65535.0f; }

// tileable value noise, period p cells
static float vnoise(float x, float y, int p, int seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    int x0 = ((xi % p) + p) % p, x1 = (x0 + 1) % p, y0 = ((yi % p) + p) % p, y1 = (y0 + 1) % p;
    float a = rnd(x0, y0, seed), b = rnd(x1, y0, seed), c = rnd(x0, y1, seed), d = rnd(x1, y1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
static float fbm(float u, float v, int base, int seed) { // u,v in [0,1)
    float s = 0, amp = 0.5f, tot = 0;
    for (int o = 0; o < 5; o++) {
        int p = base << o;
        s += amp * vnoise(u * p, v * p, p, seed + o);
        tot += amp; amp *= 0.5f;
    }
    return s / tot;
}
static Color mix(Color a, Color b, float t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return (Color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 255 };
}
static Color shade(Color a, float k) {
    float r = a.r * k, g = a.g * k, b = a.b * k;
    return (Color){ r > 255 ? 255 : r < 0 ? 0 : r, g > 255 ? 255 : g < 0 ? 0 : g, b > 255 ? 255 : b < 0 ? 0 : b, 255 };
}

#define TS 128
static Color texel(TexId id, int x, int y) {
    float u = (x + 0.5f) / TS, v = (y + 0.5f) / TS;
    float n = fbm(u, v, 4, id * 17);
    float grain = rnd(x, y, 99 + id);
    switch (id) {
    case TEX_CONCRETE: {
        Color c = mix((Color){70, 74, 66, 255}, (Color){118, 116, 102, 255}, n);
        float drip = vnoise(u * 24, v * 2, 24, 5);                 // vertical streaks
        c = shade(c, 1.0f - 0.55f * powf(drip, 3.0f) - 0.3f * fbm(u, v, 2, 40));
        if (grain > 0.97f) c = shade(c, 0.5f);
        if (grain < 0.02f) c = shade(c, 1.4f);
        return shade(c, 0.9f + 0.2f * grain);
    }
    case TEX_RUST: {
        Color c = mix((Color){70, 34, 20, 255}, (Color){168, 92, 40, 255}, n);
        c = mix(c, (Color){40, 38, 36, 255}, powf(fbm(u, v, 8, 77), 3.0f) * 1.6f);
        // hand holds: a lattice of pale worn nubs so grippable surfaces read at a glance
        int gx = x % 32, gy = y % 32;
        float dx = gx - 16, dy = gy - 16;
        if (dx * dx + dy * dy < 30) c = mix(c, (Color){210, 190, 120, 255}, 0.75f);
        if (gx < 2 || gy < 2) c = shade(c, 0.55f);                 // plate seams
        return shade(c, 0.85f + 0.3f * grain);
    }
    case TEX_TILE: {
        int tx = x / 32, ty = y / 32;
        Color base = mix((Color){122, 128, 104, 255}, (Color){160, 156, 128, 255}, rnd(tx, ty, 3));
        Color c = mix(base, (Color){54, 46, 30, 255}, powf(fbm(u, v, 4, 31), 2.0f) * 0.9f);
        if ((x % 32) < 2 || (y % 32) < 2) c = (Color){28, 26, 20, 255};
        float blood = fbm(u + 0.3f, v, 2, 123);
        if (blood > 0.62f) c = mix(c, (Color){90, 18, 14, 255}, (blood - 0.62f) * 5.0f);
        return shade(c, 0.88f + 0.24f * grain);
    }
    case TEX_FLESH: {
        float w = fbm(u + 0.15f * fbm(v, u, 4, 9), v, 4, 55);
        Color c = mix((Color){90, 22, 30, 255}, (Color){196, 98, 104, 255}, w);
        float vein = fabsf(fbm(u, v, 6, 66) - 0.5f);
        if (vein < 0.035f) c = mix(c, (Color){54, 10, 40, 255}, 0.85f);
        if (grain > 0.985f) c = mix(c, (Color){240, 220, 170, 255}, 0.5f);
        return shade(c, 0.85f + 0.3f * grain);
    }
    case TEX_SLUDGE: {
        Color c = mix((Color){22, 30, 14, 255}, (Color){72, 80, 30, 255}, n);
        float b = fbm(u, v, 8, 14);
        if (b > 0.66f) c = mix(c, (Color){150, 170, 70, 255}, (b - 0.66f) * 4.0f);
        return shade(c, 0.9f + 0.2f * grain);
    }
    case TEX_WOOD: {
        int plank = y / 16;
        Color c = mix((Color){54, 38, 24, 255}, (Color){104, 76, 46, 255}, rnd(plank, 0, 8) * 0.7f + 0.3f * n);
        float g = vnoise(u * 3, v * 40, 40, 71);
        c = shade(c, 0.75f + 0.4f * g);
        if (y % 16 == 0) c = shade(c, 0.35f);
        if (((x + plank * 37) % 128) == 0) c = shade(c, 0.4f);
        return shade(c, 0.9f + 0.2f * grain);
    }
    default: {
        uint8_t g = (uint8_t)(rnd(x, y, 1000) * 255);
        return (Color){ g, g, g, 255 };
    }
    }
}

static Texture2D make_tex(TexId id) {
    Color *px = malloc(TS * TS * sizeof(Color));
    for (int y = 0; y < TS; y++) for (int x = 0; x < TS; x++) px[y * TS + x] = texel(id, x, y);
    Image img = { px, TS, TS, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D t = LoadTextureFromImage(img);
    free(px);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

// ---------------------------------------------------------------- shaders
static const char *OBJ_VS =
"#version 330\n"
"in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;\n"
"uniform mat4 mvp; out vec2 fragTexCoord; out vec4 fragColor;\n"
"void main(){ fragTexCoord=vertexTexCoord; fragColor=vertexColor;\n"
"  gl_Position=mvp*vec4(vertexPosition,1.0); }\n";

static const char *OBJ_FS =
"#version 330\n"
"in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
"uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
"uniform vec3 fogColor; uniform float fogDensity; uniform float flicker; uniform float emit;\n"
"void main(){\n"
"  vec4 t = texture(texture0, fragTexCoord) * fragColor * colDiffuse;\n"
"  if (t.a < 0.3) discard;\n"
"  float d = 1.0/gl_FragCoord.w;\n"
"  float f = 1.0 - exp(-pow(d*fogDensity, 1.5));\n"
"  vec3 lit = mix(t.rgb * flicker * 2.3, fragColor.rgb * colDiffuse.rgb, emit);\n"
"  vec3 col = mix(lit, fogColor, clamp(f,0.0,1.0)*(1.0-emit));\n"
"  finalColor = vec4(col, 1.0); }\n";

static const char *POST_FS =
"#version 330\n"
"in vec2 fragTexCoord; out vec4 finalColor;\n"
"uniform sampler2D texture0;\n"
"uniform float time; uniform float madness; uniform float fade; uniform float flash; uniform float glitch; uniform vec2 res;\n"
"float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233))) * 43758.5453); }\n"
"void main(){\n"
"  vec2 uv = fragTexCoord; vec2 c = uv - 0.5; float r2 = dot(c,c);\n"
"  uv = 0.5 + c*(1.0 + 0.10*r2);\n"
"  uv.x += madness*0.010*sin(uv.y*26.0 + time*3.0);\n"
"  uv.y += madness*0.006*sin(uv.x*17.0 - time*2.3);\n"
"  float band = floor(uv.y*28.0 + floor(time*24.0)); float gr = hash(vec2(band, floor(time*24.0)));\n"
"  if (gr < glitch*0.55) uv.x += (gr-0.25)*0.25*glitch;\n"
"  float ca = 0.0012 + madness*0.008 + glitch*0.02;\n"
"  vec3 col = vec3(texture(texture0, uv+vec2(ca,0)).r, texture(texture0, uv).g, texture(texture0, uv-vec2(ca,0)).b);\n"
"  float lum = dot(col, vec3(0.3,0.5,0.2));\n"
"  col = mix(vec3(lum)*vec3(1.0,1.05,0.85), col, 1.0 - 0.45*madness);\n"
"  col += (hash(uv*res + fract(time)*91.0) - 0.5) * 0.13;\n"
"  col *= 0.93 + 0.07*sin(uv.y*res.y*3.14159);\n"
"  float vig = smoothstep(0.95, 0.25, length(c)*(1.15+0.5*madness));\n"
"  col *= vig;\n"
"  float q = 20.0;\n"
"  col = floor(col*q + hash(floor(uv*res)+time*0.0)*0.9)/q;\n"
"  col = mix(col, vec3(0.7,0.05,0.04), flash*0.6);\n"
"  col = mix(col, 1.0-col, step(0.9, hash(vec2(floor(time*18.0), 7.0)))*glitch);\n"
"  col *= (1.0 - fade);\n"
"  finalColor = vec4(col, 1.0); }\n";

void gfx_init(void) {
    for (int i = 0; i < TEX_COUNT; i++) tex[i] = make_tex((TexId)i);
    gfx_rt = LoadRenderTexture(RT_W, RT_H);
    SetTextureFilter(gfx_rt.texture, TEXTURE_FILTER_POINT);
    obj = LoadShaderFromMemory(OBJ_VS, OBJ_FS);
    u_fog = GetShaderLocation(obj, "fogColor");
    u_dens = GetShaderLocation(obj, "fogDensity");
    u_flick = GetShaderLocation(obj, "flicker");
    u_emit = GetShaderLocation(obj, "emit");
    post = LoadShaderFromMemory(NULL, POST_FS);
    p_time = GetShaderLocation(post, "time");
    p_mad = GetShaderLocation(post, "madness");
    p_fade = GetShaderLocation(post, "fade");
    p_flash = GetShaderLocation(post, "flash");
    p_res = GetShaderLocation(post, "res");
    p_glitch = GetShaderLocation(post, "glitch");
    float res[2] = { RT_W, RT_H };
    SetShaderValue(post, p_res, res, SHADER_UNIFORM_VEC2);
}

void gfx_shutdown(void) {
    for (int i = 0; i < TEX_COUNT; i++) UnloadTexture(tex[i]);
    for (int i = 0; i < decalN; i++) UnloadTexture(decalTex[i]);
    UnloadRenderTexture(gfx_rt);
    UnloadShader(obj); UnloadShader(post);
}

void gfx_update_static(float time, bool face) {
    (void)time;
    static Color px[64 * 64];
    for (int i = 0; i < 64 * 64; i++) { uint8_t g = GetRandomValue(30, 255); px[i] = (Color){ g, g, g, 255 }; }
    if (face) {   // something is looking out of the television
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            bool eye = (y >= 20 && y < 30) && ((x >= 14 && x < 24) || (x >= 40 && x < 50));
            bool mouth = (y >= 42 && y < 52) && (x >= 22 && x < 42) && ((x + y) % 7 != 0);
            if (eye || mouth) px[y * 64 + x] = (Color){ 4, 4, 4, 255 };
        }
    }
    UpdateTexture(tex[TEX_STATIC], px);
}


int gfx_text_tex(const char *s, Color c) {
    uint32_t cc = (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a;
    for (int i = 0; i < decalN; i++) if (decalCol[i] == cc && !strcmp(decalStr[i], s)) return TEX_COUNT + i;
    if (decalN >= MAX_DECAL) return TEX_COUNT + decalN - 1;
    int fs = 20, w = MeasureText(s, fs) + 16, h = fs + 26;
    Image img = GenImageColor(w, h, BLANK);
    ImageDrawText(&img, s, 9, 4, fs, (Color){ c.r / 2, c.g / 2, c.b / 2, 255 });
    ImageDrawText(&img, s, 8, 4, fs, c);
    // let the paint run
    for (int x = 8; x < w - 8; x++) {
        int low = -1;
        for (int y = 0; y < fs + 6; y++) if (GetImageColor(img, x, y).a > 200) low = y;
        if (low >= 0 && GetRandomValue(0, 100) < 14) {
            int len = GetRandomValue(3, 20);
            for (int k = 1; k <= len; k++) ImageDrawPixel(&img, x, low + k, (Color){ c.r, c.g, c.b, (unsigned char)(255 - k * 255 / (len + 2)) });
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    decalTex[decalN] = t; decalAsp[decalN] = (float)w / h;
    snprintf(decalStr[decalN], sizeof decalStr[0], "%s", s);
    decalCol[decalN] = cc;
    return TEX_COUNT + decalN++;
}
float gfx_text_aspect(int id) { return decalAsp[id - TEX_COUNT]; }

void gfx_decal(Vector3 c, Vector3 h, int id, float dir) {
    Texture2D t = decalTex[id - TEX_COUNT];
    rlCheckRenderBatchLimit(8);
    rlSetTexture(t.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    Vector3 r, u = { 0, h.y, 0 };
    if (h.z < h.x) r = (Vector3){ h.x * (dir > 0 ? 1 : -1), 0, 0 };
    else           r = (Vector3){ 0, 0, h.z * (dir > 0 ? -1 : 1) };
    Vector3 bl = Vector3Subtract(Vector3Subtract(c, r), u), br = Vector3Subtract(Vector3Add(c, r), u);
    Vector3 tr = Vector3Add(Vector3Add(c, r), u), tl = Vector3Add(Vector3Subtract(c, r), u);
    rlTexCoord2f(0, 1); rlVertex3f(bl.x, bl.y, bl.z);
    rlTexCoord2f(1, 1); rlVertex3f(br.x, br.y, br.z);
    rlTexCoord2f(1, 0); rlVertex3f(tr.x, tr.y, tr.z);
    rlTexCoord2f(0, 0); rlVertex3f(tl.x, tl.y, tl.z);
    rlEnd();
    rlSetTexture(0);
}

// ---------------------------------------------------------------- drawing
static void set_emit(float e) { rlDrawRenderBatchActive(); SetShaderValue(obj, u_emit, &e, SHADER_UNIFORM_FLOAT); }
void gfx_set_emit(bool on) { set_emit(on ? 1.0f : 0.0f); }

void gfx_begin_scene(Camera3D cam, Color fog, float density, float flicker, float time) {
    (void)time;
    ClearBackground(fog);
    BeginMode3D(cam);
    rlDisableBackfaceCulling();
    BeginShaderMode(obj);
    float f[3] = { fog.r / 255.f, fog.g / 255.f, fog.b / 255.f };
    SetShaderValue(obj, u_fog, f, SHADER_UNIFORM_VEC3);
    SetShaderValue(obj, u_dens, &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(obj, u_flick, &flicker, SHADER_UNIFORM_FLOAT);
    float e = 0; SetShaderValue(obj, u_emit, &e, SHADER_UNIFORM_FLOAT);
}
void gfx_end_scene(void) {
    EndShaderMode();
    rlEnableBackfaceCulling();
    EndMode3D();
}

static void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float su, float sv, float k, Color t) {
    // a,b is the bottom edge, c,d the top edge; vertical gradient darkens the grime line
    Color lo = shade(t, k * 0.55f), hi = shade(t, k);
    lo.a = hi.a = 255;
    rlColor4ub(lo.r, lo.g, lo.b, 255); rlTexCoord2f(0, sv); rlVertex3f(a.x, a.y, a.z);
    rlColor4ub(lo.r, lo.g, lo.b, 255); rlTexCoord2f(su, sv); rlVertex3f(b.x, b.y, b.z);
    rlColor4ub(hi.r, hi.g, hi.b, 255); rlTexCoord2f(su, 0); rlVertex3f(c.x, c.y, c.z);
    rlColor4ub(hi.r, hi.g, hi.b, 255); rlTexCoord2f(0, 0); rlVertex3f(d.x, d.y, d.z);
}

static void box_faces(Vector3 h, Color t, float s) {
    float x = h.x, y = h.y, z = h.z;
    float ux = 2 * x / s, uy = 2 * y / s, uz = 2 * z / s;
    // +Z / -Z
    quad((Vector3){-x,-y, z}, (Vector3){ x,-y, z}, (Vector3){ x, y, z}, (Vector3){-x, y, z}, ux, uy, 0.62f, t);
    quad((Vector3){ x,-y,-z}, (Vector3){-x,-y,-z}, (Vector3){-x, y,-z}, (Vector3){ x, y,-z}, ux, uy, 0.58f, t);
    // +X / -X
    quad((Vector3){ x,-y, z}, (Vector3){ x,-y,-z}, (Vector3){ x, y,-z}, (Vector3){ x, y, z}, uz, uy, 0.78f, t);
    quad((Vector3){-x,-y,-z}, (Vector3){-x,-y, z}, (Vector3){-x, y, z}, (Vector3){-x, y,-z}, uz, uy, 0.74f, t);
    // top (brighter, no vertical gradient meaning) / bottom
    rlColor4ub(t.r, t.g, t.b, 255);
    Color tc = shade(t, 1.0f), bc = shade(t, 0.6f);
    rlColor4ub(tc.r, tc.g, tc.b, 255);
    rlTexCoord2f(0, 0);   rlVertex3f(-x, y, z);
    rlTexCoord2f(ux, 0);  rlVertex3f( x, y, z);
    rlTexCoord2f(ux, uz); rlVertex3f( x, y,-z);
    rlTexCoord2f(0, uz);  rlVertex3f(-x, y,-z);
    rlColor4ub(bc.r, bc.g, bc.b, 255);
    rlTexCoord2f(0, 0);   rlVertex3f(-x,-y,-z);
    rlTexCoord2f(ux, 0);  rlVertex3f( x,-y,-z);
    rlTexCoord2f(ux, uz); rlVertex3f( x,-y, z);
    rlTexCoord2f(0, uz);  rlVertex3f(-x,-y, z);
}

void gfx_box_rot(Vector3 c, Vector3 h, Quaternion q, TexId id, Color tint, float scale) {
    rlCheckRenderBatchLimit(36);
    rlPushMatrix();
    rlTranslatef(c.x, c.y, c.z);
    Matrix m = QuaternionToMatrix(q);
    rlMultMatrixf(MatrixToFloat(m));
    rlSetTexture(tex[id].id);
    rlBegin(RL_QUADS);
    box_faces(h, tint, scale);
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

void gfx_box(Vector3 c, Vector3 h, TexId id, Color tint, float scale) {
    rlCheckRenderBatchLimit(36);
    rlPushMatrix();
    rlTranslatef(c.x, c.y, c.z);
    rlSetTexture(tex[id].id);
    rlBegin(RL_QUADS);
    box_faces(h, tint, scale);
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

void gfx_glow(Vector3 c, Vector3 h, Color col) {
    gfx_box(c, h, TEX_CONCRETE, col, 100.0f);
}

void gfx_slab(float y, float half, TexId id, Color tint, float time) {
    rlCheckRenderBatchLimit(8);
    rlSetTexture(tex[id].id);
    rlBegin(RL_QUADS);
    float s = half / 3.0f, o = time * 0.05f;
    rlColor4ub(tint.r, tint.g, tint.b, 255);
    rlTexCoord2f(o, o);         rlVertex3f(-half, y, half);
    rlTexCoord2f(o + s, o);     rlVertex3f( half, y, half);
    rlTexCoord2f(o + s, o + s); rlVertex3f( half, y,-half);
    rlTexCoord2f(o, o + s);     rlVertex3f(-half, y,-half);
    rlEnd();
    rlSetTexture(0);
}

void gfx_present(float time, float madness, float fade, float flash, float glitch) {
    BeginDrawing();
    ClearBackground(BLACK);
    BeginShaderMode(post);
    SetShaderValue(post, p_time, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, p_mad, &madness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, p_fade, &fade, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, p_flash, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post, p_glitch, &glitch, SHADER_UNIFORM_FLOAT);
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    float k = fminf(sw / RT_W, sh / RT_H);
    Rectangle dst = { (sw - RT_W * k) / 2, (sh - RT_H * k) / 2, RT_W * k, RT_H * k };
    DrawTexturePro(gfx_rt.texture, (Rectangle){ 0, 0, RT_W, -RT_H }, dst, (Vector2){ 0, 0 }, 0, WHITE);
    EndShaderMode();
    EndDrawing();
}
