#include "gfx.h"
#include <rlgl.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

RenderTexture2D gfx_rt;
#define MAX_DECAL 96
static Texture2D decalTex[MAX_DECAL];
static float decalAsp[MAX_DECAL];
static char decalStr[MAX_DECAL][96];
static uint32_t decalCol[MAX_DECAL];
static int decalN;
static Texture2D tex[TEX_COUNT];
static Shader obj, post;
static Texture2D haloTex, moonTex, scleraTex, irisTex;
static Vector3 camPos, camR, camU, camF;
static Color gradeLo = { 128, 128, 128, 255 }, gradeHi = { 128, 128, 128, 255 };
static int p_glo, p_ghi;
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
    case TEX_GRASS: {
        Color c = mix((Color){22, 26, 20, 255}, (Color){62, 72, 52, 255}, n);   // long grass gone to seed, grey in the moonlight
        float blade = vnoise(u * 48, v * 6, 48, 91);
        c = shade(c, 0.7f + 0.7f * blade);
        if (grain > 0.994f) c = mix(c, (Color){170, 160, 140, 255}, 0.7f);       // seed heads
        else if (grain < 0.003f) c = mix(c, (Color){90, 20, 20, 255}, 0.8f);
        return shade(c, 0.9f + 0.2f * grain);
    }
    case TEX_MOSAIC: {
        static const Color PAL[6] = { { 140, 30, 80, 255 }, { 30, 90, 130, 255 }, { 220, 170, 60, 255 }, { 50, 140, 110, 255 }, { 100, 50, 150, 255 }, { 220, 90, 60, 255 } };
        int a = (x + y) / 16, b = (x - y + 128) / 16;
        Color c = PAL[hash2(a, b, 5) % 6];
        int fa = (x + y) % 16, fb = (x - y + 128) % 16;
        if (fa < 1 || fb < 1) c = (Color){ 24, 18, 26, 255 };
        else if (fa > 6 && fa < 9 && fb > 6 && fb < 9) c = shade(c, 1.5f);   // a gem in each tile
        c = shade(c, 0.75f + 0.5f * n);
        return shade(c, 0.9f + 0.2f * grain);
    }
    case TEX_EYES: {
        // wallpaper of closed-lidded eyes in rows. some of them are open.
        int cx = x % 32, cy = y % 32, row = y / 32, col = (x + (row & 1) * 16) / 32;
        float dx = ((x + (row & 1) * 16) % 32) - 16.0f, dy = cy - 16.0f;
        Color c = mix((Color){ 54, 14, 30, 255 }, (Color){ 96, 26, 52, 255 }, n);
        if (((cx + cy) % 16) < 1) c = shade(c, 0.6f);                              // damask lattice
        float e = (dx * dx) / (11.0f * 11.0f) + (dy * dy) / (5.0f * 5.0f);
        bool open = hash2(col, row, 77) % 3 == 0;
        if (e < 1.0f) {
            c = open ? (Color){ 226, 214, 176, 255 } : (Color){ 20, 6, 14, 255 };
            if (open && dx * dx + dy * dy < 14) c = (Color){ 12, 4, 6, 255 };
            else if (open && dx * dx + dy * dy < 30) c = (Color){ 190, 60, 40, 255 };
        } else if (e < 1.35f) c = (Color){ 18, 6, 10, 255 };
        return shade(c, 0.85f + 0.3f * grain);
        (void)cx;
    }
    case TEX_WATER: {
        Color c = mix((Color){ 6, 14, 40, 255 }, (Color){ 24, 60, 100, 255 }, n);
        float k = fabsf(fbm(u, v, 4, 140) - 0.5f), k2 = fabsf(fbm(v, u, 6, 150) - 0.5f);
        if (k < 0.03f || k2 < 0.025f) c = mix(c, (Color){ 140, 230, 255, 255 }, 0.8f);
        return shade(c, 0.9f + 0.2f * grain);
    }
    case TEX_SKIN: {   // skin that has not seen the sun: grey, mottled, bruised, blue at the veins
        float m = fbm(u + 0.2f * fbm(v, u, 3, 301), v, 4, 302);
        Color c = mix((Color){ 120, 112, 108, 255 }, (Color){ 214, 206, 196, 255 }, m);
        float bruise = fbm(u, v, 2, 303);
        if (bruise > 0.6f) c = mix(c, (Color){ 96, 74, 96, 255 }, (bruise - 0.6f) * 2.5f);
        float vein = fabsf(fbm(u, v, 5, 304) - 0.5f);
        if (vein < 0.02f) c = mix(c, (Color){ 70, 84, 120, 255 }, 0.6f);
        return shade(c, 0.92f + 0.16f * grain);
    }
    case TEX_CLOTH: {  // coarse wool, hanging in folds, stiff with something at the hem
        float fold = 0.5f + 0.5f * sinf(u * 6.2831853f * 5.0f + 2.0f * fbm(u, v, 2, 311));
        Color c = mix((Color){ 40, 36, 38, 255 }, (Color){ 96, 90, 88, 255 }, 0.35f * n + 0.65f * fold);
        if (((x + y) & 3) == 0 || ((x - y) & 3) == 0) c = shade(c, 0.85f);   // weave
        float stain = fbm(u, v, 3, 312) * (v * 1.4f);
        if (stain > 0.55f) c = mix(c, (Color){ 30, 12, 10, 255 }, (stain - 0.55f) * 2.0f);
        return shade(c, 0.9f + 0.2f * grain);
    }
    case TEX_PAPER: {  // the house's wallpaper: soft stripes and small flowers, a little faded
        Color c = mix((Color){ 150, 132, 104, 255 }, (Color){ 186, 168, 136, 255 }, 0.5f + 0.5f * n);
        if ((x % 32) < 3) c = shade(c, 0.9f);
        int fx = (x + 16) % 32 - 16, fy = (y + (x / 32 % 2) * 16) % 32 - 16;
        if (fx * fx + fy * fy < 10) c = mix(c, (Color){ 150, 90, 80, 255 }, 0.5f);
        else if (fx * fx + fy * fy < 18 && fy > 0) c = mix(c, (Color){ 100, 120, 80, 255 }, 0.35f);
        return shade(c, 0.94f + 0.12f * grain);
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
#ifdef __EMSCRIPTEN__
#define GLSL_HEAD "#version 300 es\nprecision highp float;\n"
#else
#define GLSL_HEAD "#version 330\n"
#endif

static const char *OBJ_VS =
GLSL_HEAD
"in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;\n"
"uniform mat4 mvp; out vec2 fragTexCoord; out vec4 fragColor;\n"
"void main(){ fragTexCoord=vertexTexCoord; fragColor=vertexColor;\n"
"  gl_Position=mvp*vec4(vertexPosition,1.0); }\n";

static const char *OBJ_FS =
GLSL_HEAD
"in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
"uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
"uniform vec3 fogColor; uniform float fogDensity; uniform float flicker; uniform float emit;\n"
"void main(){\n"
"  vec4 tx = texture(texture0, fragTexCoord);\n"
"  if (tx.a < 0.3) discard;\n"
"  vec4 t = tx * fragColor * colDiffuse;\n"
"  float d = 1.0/gl_FragCoord.w;\n"
"  float f = 1.0 - exp(-pow(d*fogDensity, 1.5));\n"
"  float e = min(emit, 1.0); float te = step(1.5, emit);\n"
"  vec3 flat_ = fragColor.rgb * colDiffuse.rgb * mix(vec3(1.0), tx.rgb, te);\n"
"  vec3 lit = mix(t.rgb * flicker * 2.3, flat_, e);\n"
"  vec3 col = mix(lit, fogColor, clamp(f,0.0,1.0)*(1.0-e));\n"
"  finalColor = vec4(col, mix(1.0, fragColor.a * colDiffuse.a, e)); }\n";

static const char *POST_FS =
GLSL_HEAD
"in vec2 fragTexCoord; out vec4 finalColor;\n"
"uniform sampler2D texture0;\n"
"uniform float time; uniform float madness; uniform float fade; uniform float flash; uniform float glitch; uniform vec2 res; uniform vec3 gLo; uniform vec3 gHi;\n"
"float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233))) * 43758.5453); }\n"
"void main(){\n"
"  vec2 uv = fragTexCoord; vec2 c = uv - 0.5; float r2 = dot(c,c);\n"
"  uv = 0.5 + c*(1.0 + 0.03*r2);\n"
"  uv.x += madness*0.0025*sin(uv.y*9.0 + time*0.7);\n"
"  float band = floor(uv.y*20.0 + floor(time*12.0)); float gr = hash(vec2(band, floor(time*12.0)));\n"
"  if (gr < glitch*0.3) uv.x += (gr-0.15)*0.05*glitch;\n"
"  float ca = 0.0004 + madness*0.0018 + glitch*0.004;\n"
"  vec3 col = vec3(texture(texture0, uv+vec2(ca,0)).r, texture(texture0, uv).g, texture(texture0, uv-vec2(ca,0)).b);\n"
"  vec3 bl = vec3(0.0);\n"
"  for (int i = 0; i < 10; i++) { float a = float(i) * 0.6283; vec2 o = vec2(cos(a), sin(a));\n"
"    bl += max(texture(texture0, uv + o*3.0/res).rgb - 0.5, 0.0) + max(texture(texture0, uv + o*8.0/res).rgb - 0.5, 0.0) * 0.8; }\n"
"  col += bl * 0.08;\n"
"  float lum = dot(col, vec3(0.3,0.5,0.2));\n"
"  col *= mix(gLo, gHi, smoothstep(0.0, 0.7, lum));\n"
"  col = mix(vec3(lum)*vec3(1.0,1.02,0.95), col, 1.0 - 0.35*madness);\n"
"  col += (hash(uv*res + fract(time)*91.0) - 0.5) * 0.04;\n"
"  col *= 0.985 + 0.015*sin(uv.y*res.y*3.14159);\n"
"  float vig = smoothstep(1.0, 0.3, length(c)*(1.1+0.35*madness));\n"
"  col *= vig;\n"
"  float q = 64.0;\n"
"  col = floor(col*q + hash(floor(uv*res))*0.9)/q;\n"
"  col = mix(col, vec3(0.0), flash*0.6);\n"
"  col *= (1.0 - fade);\n"
"  finalColor = vec4(col, 1.0); }\n";

static Texture2D sprite(int w, int h, Color (*fn)(int, int, int, int)) {
    Color *px = malloc(w * h * sizeof(Color));
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[y * w + x] = fn(x, y, w, h);
    Image img = { px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D t = LoadTextureFromImage(img);
    free(px);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    return t;
}
static Color sp_halo(int x, int y, int w, int h) {
    float dx = (x + 0.5f) / w * 2 - 1, dy = (y + 0.5f) / h * 2 - 1, r = sqrtf(dx * dx + dy * dy);
    float k = r >= 1 ? 0 : powf(1 - r, 2.4f);
    uint8_t g = (uint8_t)(k * 255);
    return (Color){ g, g, g, 255 };
}
static Color sp_moon(int x, int y, int w, int h) {
    float dx = (x + 0.5f) / w * 2 - 1, dy = (y + 0.5f) / h * 2 - 1, r = sqrtf(dx * dx + dy * dy);
    if (r > 1) return (Color){ 0, 0, 0, 0 };
    float n = fbm((x + 0.5f) / w, (y + 0.5f) / h, 4, 600);
    float c = powf(vnoise(x * 0.18f, y * 0.18f, 64, 33), 2.0f);
    float k = 0.78f + 0.3f * n - 0.35f * c - 0.25f * powf(r, 3.0f);
    float lit = 0.6f + 0.4f * (-dx * 0.6f - dy * 0.4f + 0.5f);          // terminator
    k *= lit;
    return (Color){ (uint8_t)fminf(255, 250 * k), (uint8_t)fminf(255, 240 * k), (uint8_t)fminf(255, 222 * k), 255 };
}
static Color sp_sclera(int x, int y, int w, int h) {
    float dx = (x + 0.5f) / w * 2 - 1, dy = (y + 0.5f) / h * 2 - 1;
    float lid = 1.0f - dx * dx;                                  // almond: height shrinks toward the corners
    if (lid <= 0 || fabsf(dy) > lid) return (Color){ 0, 0, 0, 0 };
    float e = fabsf(dy) / lid;
    Color c = mix((Color){ 232, 214, 190, 255 }, (Color){ 150, 40, 44, 255 }, powf(e, 3.0f));
    float vein = fabsf(fbm((x + 0.5f) / w, (y + 0.5f) / h, 6, 710) - 0.5f);
    if (vein < 0.03f) c = mix(c, (Color){ 140, 20, 30, 255 }, 0.8f);
    if (e > 0.9f) c = (Color){ 30, 8, 14, 255 };
    return c;
}
static Color sp_iris(int x, int y, int w, int h) {
    float dx = (x + 0.5f) / w * 2 - 1, dy = (y + 0.5f) / h * 2 - 1, r = sqrtf(dx * dx + dy * dy);
    if (r > 1) return (Color){ 0, 0, 0, 0 };
    if (r < 0.34f) return (Color){ 3, 2, 4, 255 };
    float a = atan2f(dy, dx), f = 0.5f + 0.5f * sinf(a * 17.0f + r * 6.0f);
    Color c = mix((Color){ 40, 150, 130, 255 }, (Color){ 220, 200, 70, 255 }, f * (1 - r));
    if (r > 0.9f) c = (Color){ 6, 20, 24, 255 };
    return c;
}
static void make_sprites(void) {
    haloTex = sprite(64, 64, sp_halo);
    moonTex = sprite(96, 96, sp_moon);
    scleraTex = sprite(192, 96, sp_sclera);
    irisTex = sprite(64, 64, sp_iris);
}

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
    p_glo = GetShaderLocation(post, "gLo");
    p_ghi = GetShaderLocation(post, "gHi");
    make_sprites();
    float res[2] = { RT_W, RT_H };
    SetShaderValue(post, p_res, res, SHADER_UNIFORM_VEC2);
}

void gfx_shutdown(void) {
    for (int i = 0; i < TEX_COUNT; i++) UnloadTexture(tex[i]);
    for (int i = 0; i < decalN; i++) UnloadTexture(decalTex[i]);
    UnloadTexture(haloTex); UnloadTexture(moonTex); UnloadTexture(scleraTex); UnloadTexture(irisTex);
    UnloadRenderTexture(gfx_rt);
    UnloadShader(obj); UnloadShader(post);
}

void gfx_update_static(float time, bool face) {
    (void)time;
    static Color px[TS * TS];
    for (int i = 0; i < TS * TS; i++) { uint8_t g = GetRandomValue(30, 255); px[i] = (Color){ g, g, g, 255 }; }
    if (face) {   // something is looking out of the television
        for (int y = 0; y < TS; y++) for (int x = 0; x < TS; x++) {
            int u = x / 2, v = y / 2;
            bool eye = (v >= 20 && v < 30) && ((u >= 14 && u < 24) || (u >= 40 && u < 50));
            bool mouth = (v >= 42 && v < 52) && (u >= 22 && u < 42) && ((u + v) % 7 != 0);
            if (eye || mouth) px[y * TS + x] = (Color){ 4, 4, 4, 255 };
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

// a circle drawn on the floor in chalk or worse: two rings, a star with one point down, marks between the rings
int gfx_sigil_tex(Color c, int seed) {
    uint32_t cc = (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a;
    char key[32]; snprintf(key, sizeof key, "\x01sigil%d", seed);
    for (int i = 0; i < decalN; i++) if (decalCol[i] == cc && !strcmp(decalStr[i], key)) return TEX_COUNT + i;
    if (decalN >= MAX_DECAL) return TEX_COUNT + decalN - 1;
    const int S = 256;
    Image img = GenImageColor(S, S, BLANK);
    Vector2 o = { S / 2.0f, S / 2.0f };
    for (int k = 0; k < 3; k++) { ImageDrawCircleLinesV(&img, o, 120 - k, c); ImageDrawCircleLinesV(&img, o, 96 - k, c); }
    Vector2 pt[5];
    for (int k = 0; k < 5; k++) { float a = 1.5708f + k * 1.2566f; pt[k] = (Vector2){ o.x + cosf(a) * 94, o.y + sinf(a) * 94 }; }
    for (int k = 0; k < 5; k++) ImageDrawLineEx(&img, pt[k], pt[(k + 2) % 5], 3, c);
    for (int k = 0; k < 28; k++) {   // letters nobody taught you, between the rings
        float a = k * 6.2831853f / 28 + rnd(k, seed, 7) * 0.1f, r0 = 100, r1 = 116;
        Vector2 p0 = { o.x + cosf(a) * r0, o.y + sinf(a) * r0 }, p1 = { o.x + cosf(a + 0.12f * (rnd(k, seed, 8) - 0.5f)) * r1, o.y + sinf(a + 0.1f) * r1 };
        ImageDrawLineEx(&img, p0, p1, 2, c);
        if (rnd(k, seed, 9) > 0.5f) ImageDrawLineEx(&img, p1, (Vector2){ p1.x + cosf(a + 1.6f) * 6, p1.y + sinf(a + 1.6f) * 6 }, 2, c);
    }
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {   // smudged: the chalk has been walked through
        Color p = GetImageColor(img, x, y);
        if (p.a && rnd(x, y, seed + 5) < 0.25f + 0.5f * fbm(x / (float)S, y / (float)S, 3, seed)) ImageDrawPixel(&img, x, y, (Color){ c.r, c.g, c.b, (unsigned char)(p.a * 0.35f) });
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    decalTex[decalN] = t; decalAsp[decalN] = 1.0f;
    snprintf(decalStr[decalN], sizeof decalStr[0], "%s", key);
    decalCol[decalN] = cc;
    return TEX_COUNT + decalN++;
}

void gfx_decal(Vector3 c, Vector3 h, int id, float dir) {
    Texture2D t = decalTex[id - TEX_COUNT];
    rlCheckRenderBatchLimit(8);
    rlSetTexture(t.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    Vector3 r, u = { 0, h.y, 0 };
    if (h.y < h.x && h.y < h.z) { r = (Vector3){ h.x, 0, 0 }; u = (Vector3){ 0, 0, -h.z }; }   // lying on the floor
    else if (h.z < h.x) r = (Vector3){ h.x * (dir > 0 ? 1 : -1), 0, 0 };
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
    camPos = cam.position;
    camF = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    camR = Vector3Normalize(Vector3CrossProduct(camF, cam.up));
    camU = Vector3CrossProduct(camR, camF);
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

// ---------------------------------------------------------------- organic shapes for the things that live here
static const Vector3 LIGHT = { 0.32f, 0.86f, 0.40f };
static void lit_vertex(Vector3 p, Vector3 n, float u, float v, Color t, float k) {
    float l = (0.42f + 0.58f * fmaxf(0.0f, Vector3DotProduct(n, LIGHT))) * k;
    Color c = shade(t, l);
    rlColor4ub(c.r, c.g, c.b, 255); rlTexCoord2f(u, v); rlVertex3f(p.x, p.y, p.z);
}
static void basis_of(Vector3 u, Vector3 *p, Vector3 *q) {
    Vector3 ref = fabsf(u.y) < 0.9f ? (Vector3){ 0, 1, 0 } : (Vector3){ 1, 0, 0 };
    *p = Vector3Normalize(Vector3CrossProduct(u, ref));
    *q = Vector3CrossProduct(u, *p);
}
void gfx_limb(Vector3 a, Vector3 b, float ra, float rb, TexId id, Color tint) {
    Vector3 ax = Vector3Subtract(b, a);
    float len = Vector3Length(ax);
    if (len < 1e-4f) return;
    Vector3 u = Vector3Scale(ax, 1.0f / len), p, q;
    basis_of(u, &p, &q);
    const int N = 6;
    rlCheckRenderBatchLimit(N * 4 * 3 + 8);
    rlSetTexture(tex[id].id);
    rlBegin(RL_QUADS);
    float slope = (ra - rb) / len;
    for (int i = 0; i < N; i++) {
        float t0 = i * 6.2831853f / N, t1 = (i + 1) * 6.2831853f / N;
        Vector3 d0 = Vector3Add(Vector3Scale(p, cosf(t0)), Vector3Scale(q, sinf(t0)));
        Vector3 d1 = Vector3Add(Vector3Scale(p, cosf(t1)), Vector3Scale(q, sinf(t1)));
        Vector3 n0 = Vector3Normalize(Vector3Add(d0, Vector3Scale(u, slope))), n1 = Vector3Normalize(Vector3Add(d1, Vector3Scale(u, slope)));
        float u0 = (float)i / N, u1 = (float)(i + 1) / N, vl = len * 1.5f;
        lit_vertex(Vector3Add(a, Vector3Scale(d0, ra)), n0, u0, 0, tint, 1.0f);
        lit_vertex(Vector3Add(a, Vector3Scale(d1, ra)), n1, u1, 0, tint, 1.0f);
        lit_vertex(Vector3Add(b, Vector3Scale(d1, rb)), n1, u1, vl, tint, 1.0f);
        lit_vertex(Vector3Add(b, Vector3Scale(d0, rb)), n0, u0, vl, tint, 1.0f);
    }
    for (int e = 0; e < 2; e++) {   // end caps as fans of quads
        Vector3 c = e ? b : a, n = e ? u : Vector3Negate(u);
        float r = e ? rb : ra;
        for (int i = 0; i < N; i += 2) {
            Vector3 v[3];
            for (int k = 0; k < 3; k++) { float t = (i + k) * 6.2831853f / N; v[k] = Vector3Add(c, Vector3Add(Vector3Scale(p, cosf(t) * r), Vector3Scale(q, sinf(t) * r))); }
            lit_vertex(c, n, 0.5f, 0.5f, tint, 0.9f);
            lit_vertex(v[e ? 0 : 2], n, 0, 0, tint, 0.9f);
            lit_vertex(v[1], n, 1, 0, tint, 0.9f);
            lit_vertex(v[e ? 2 : 0], n, 1, 1, tint, 0.9f);
        }
    }
    rlEnd();
    rlSetTexture(0);
}
void gfx_ellipsoid(Vector3 c, Vector3 ax, Vector3 ay, Vector3 az, TexId id, Color tint) {
    const int RINGS = 6, SEG = 8;
    rlCheckRenderBatchLimit(RINGS * SEG * 4 + 8);
    rlSetTexture(tex[id].id);
    rlBegin(RL_QUADS);
    float lx = Vector3Length(ax), ly = Vector3Length(ay), lz = Vector3Length(az);
    for (int r = 0; r < RINGS; r++) for (int sgi = 0; sgi < SEG; sgi++) {
        int idx[4][2] = { { r, sgi }, { r, sgi + 1 }, { r + 1, sgi + 1 }, { r + 1, sgi } };
        for (int k = 3; k >= 0; k--) {
            float th = idx[k][0] * 3.14159265f / RINGS, ph = idx[k][1] * 6.2831853f / SEG;
            float sx = sinf(th) * cosf(ph), sy = -cosf(th), sz = sinf(th) * sinf(ph);
            Vector3 pnt = Vector3Add(c, Vector3Add(Vector3Scale(ax, sx), Vector3Add(Vector3Scale(ay, sy), Vector3Scale(az, sz))));
            // normal of an ellipsoid: divide by the squared radii along each axis
            Vector3 n = Vector3Normalize(Vector3Add(Vector3Scale(ax, sx / (lx * lx + 1e-6f)), Vector3Add(Vector3Scale(ay, sy / (ly * ly + 1e-6f)), Vector3Scale(az, sz / (lz * lz + 1e-6f)))));
            lit_vertex(pnt, n, (float)idx[k][1] / SEG, (float)idx[k][0] / RINGS, tint, 1.0f);
        }
    }
    rlEnd();
    rlSetTexture(0);
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
    float lo[3] = { gradeLo.r / 128.f, gradeLo.g / 128.f, gradeLo.b / 128.f }, hi[3] = { gradeHi.r / 128.f, gradeHi.g / 128.f, gradeHi.b / 128.f };
    SetShaderValue(post, p_glo, lo, SHADER_UNIFORM_VEC3);
    SetShaderValue(post, p_ghi, hi, SHADER_UNIFORM_VEC3);
    float sw = GetScreenWidth(), sh = GetScreenHeight();
    float k = fminf(sw / RT_W, sh / RT_H);
    Rectangle dst = { (sw - RT_W * k) / 2, (sh - RT_H * k) / 2, RT_W * k, RT_H * k };
    DrawTexturePro(gfx_rt.texture, (Rectangle){ 0, 0, RT_W, -RT_H }, dst, (Vector2){ 0, 0 }, 0, WHITE);
    EndShaderMode();
    EndDrawing();
}


void gfx_grade(Color lo, Color hi) { gradeLo = lo; gradeHi = hi; }

// ---------------------------------------------------------------- glow, sky
void gfx_begin_glow(void) {
    set_emit(2.0f);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
}
void gfx_end_glow(void) {
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    set_emit(0.0f);
}
static void sprite_quad(Texture2D t, Vector3 c, Vector3 r, Vector3 u, Color col) {
    rlCheckRenderBatchLimit(8);
    rlSetTexture(t.id);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    Vector3 a = Vector3Subtract(Vector3Subtract(c, r), u), b = Vector3Subtract(Vector3Add(c, r), u);
    Vector3 d = Vector3Add(Vector3Add(c, r), u), e = Vector3Add(Vector3Subtract(c, r), u);
    rlTexCoord2f(0, 1); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(1, 1); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(1, 0); rlVertex3f(d.x, d.y, d.z);
    rlTexCoord2f(0, 0); rlVertex3f(e.x, e.y, e.z);
    rlEnd();
    rlSetTexture(0);
}
// soft additive blob facing the camera. call between gfx_begin_glow / gfx_end_glow
void gfx_halo(Vector3 p, float size, Color col, float intensity) {
    if (intensity <= 0) return;
    Vector3 to = Vector3Subtract(p, camPos);
    if (Vector3DotProduct(to, camF) < 0) return;
    col.a = (unsigned char)(fminf(1.0f, intensity) * 255);
    sprite_quad(haloTex, p, Vector3Scale(camR, size), Vector3Scale(camU, size), col);
}

static float hashf(int a, int b) { return (hash2(a, b, 4242) & 0xffff) / 65535.0f; }

static Vector3 dome(float az, float el, float R) {
    float ce = cosf(el);
    return (Vector3){ camPos.x + R * ce * sinf(az), camPos.y + R * sinf(el), camPos.z - R * ce * cosf(az) };
}
static void dome_basis(Vector3 d, Vector3 *r, Vector3 *u) {
    Vector3 dir = Vector3Normalize(Vector3Subtract(d, camPos));
    *r = Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, dir));
    *r = Vector3Scale(*r, -1);
    *u = Vector3CrossProduct(*r, dir); *u = Vector3Scale(*u, -1);
}

void gfx_sky(const Sky *sk, float time, Vector3 playerPos) {
    if (!sk->on) return;
    const float R = 300.0f;
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    set_emit(2.0f);
    // gradient dome
    static const float EL[] = { -90, -25, -4, 0, 5, 12, 24, 40, 62, 90 };
    int rows = (int)(sizeof EL / sizeof *EL), seg = 24;
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    for (int i = 0; i + 1 < rows; i++) for (int j = 0; j < seg; j++) {
        float a0 = j * 6.2831853f / seg, a1 = (j + 1) * 6.2831853f / seg;
        Color c[2];
        for (int k = 0; k < 2; k++) {
            float el = EL[i + k];
            if (el <= 0) c[k] = mix(sk->ground, sk->horizon, el < -4 ? 0.0f : (el + 4) / 4.0f);
            else c[k] = mix(sk->horizon, sk->zenith, powf(el / 90.0f, 0.55f));
        }
        Vector3 v0 = dome(a0, EL[i] * DEG2RAD, R), v1 = dome(a1, EL[i] * DEG2RAD, R), v2 = dome(a1, EL[i + 1] * DEG2RAD, R), v3 = dome(a0, EL[i + 1] * DEG2RAD, R);
        rlColor4ub(c[0].r, c[0].g, c[0].b, 255); rlVertex3f(v0.x, v0.y, v0.z); rlVertex3f(v1.x, v1.y, v1.z);
        rlColor4ub(c[1].r, c[1].g, c[1].b, 255); rlVertex3f(v2.x, v2.y, v2.z); rlVertex3f(v3.x, v3.y, v3.z);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    // stars
    int ns = (int)(320 * sk->stars);
    for (int i = 0; i < ns; i++) {
        float az = hashf(i, 1) * 6.2831853f, el = asinf(0.03f + 0.97f * hashf(i, 2));
        float tw = 0.55f + 0.45f * sinf(time * (1.0f + hashf(i, 3) * 3.0f) + i);
        float sz = 0.35f + hashf(i, 4) * 0.8f, b = (0.5f + 0.5f * hashf(i, 5)) * tw;
        Vector3 p = dome(az, el, R), r, u;
        dome_basis(p, &r, &u);
        Color col = hashf(i, 6) < 0.15f ? (Color){ 255, 190, 170, 255 } : hashf(i, 6) < 0.3f ? (Color){ 170, 200, 255, 255 } : (Color){ 255, 250, 230, 255 };
        col = (Color){ col.r * b, col.g * b, col.b * b, 255 };
        sprite_quad(haloTex, p, Vector3Scale(r, sz * 3), Vector3Scale(u, sz * 3), col);   // soft little points (drawn additively below)
    }
    // aurora ribbons
    if (sk->aurAmt > 0) {
        rlSetTexture(0);
        for (int k = 0; k < 3; k++) {
            rlBegin(RL_QUADS);
            int n = 48;
            for (int j = 0; j < n; j++) {
                float a0 = j * 6.2831853f / n, a1 = (j + 1) * 6.2831853f / n;
                float b0 = (18 + k * 9) * DEG2RAD + sinf(a0 * 3 + time * 0.25f + k * 2) * 0.09f, b1 = (18 + k * 9) * DEG2RAD + sinf(a1 * 3 + time * 0.25f + k * 2) * 0.09f;
                float h = (11 + 3 * sinf(a0 * 5 - time * 0.4f)) * DEG2RAD;
                float amt = sk->aurAmt * (0.5f + 0.5f * sinf(a0 * 2 + time * 0.3f + k * 1.7f));
                Color c = sk->aurora; Color lo = { c.r, c.g, c.b, 0 }, mid = { c.r, c.g, c.b, (unsigned char)(amt * 110) };
                Vector3 p0 = dome(a0, b0, R), p1 = dome(a1, b1, R), p2 = dome(a1, b1 + h, R), p3 = dome(a0, b0 + h, R);
                rlColor4ub(mid.r, mid.g, mid.b, mid.a); rlVertex3f(p0.x, p0.y, p0.z); rlVertex3f(p1.x, p1.y, p1.z);
                rlColor4ub(lo.r, lo.g, lo.b, 0);       rlVertex3f(p2.x, p2.y, p2.z); rlVertex3f(p3.x, p3.y, p3.z);
            }
            rlEnd();
        }
    }
    // shooting star
    {
        float per = 9.0f; int slot = (int)floorf(time / per); float ph = (time - slot * per) / 1.1f;
        if (sk->stars > 0 && ph < 1 && hashf(slot, 9) > 0.3f) {
            float az = hashf(slot, 10) * 6.2831853f, el = (25 + 30 * hashf(slot, 11)) * DEG2RAD;
            float dir = hashf(slot, 12) * 6.2831853f;
            Vector3 head = dome(az + cosf(dir) * ph * 0.5f, el + sinf(dir) * ph * 0.3f, R);
            Vector3 tail = dome(az + cosf(dir) * (ph - 0.25f) * 0.5f, el + sinf(dir) * (ph - 0.25f) * 0.3f, R);
            Vector3 r, u; dome_basis(head, &r, &u);
            Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(Vector3Subtract(head, tail), Vector3Subtract(head, camPos))), 0.8f);
            float f = sinf(ph * 3.14159f);
            rlSetTexture(0);
            rlBegin(RL_QUADS);
            rlColor4ub(255, 255, 255, (unsigned char)(255 * f));
            rlVertex3f(head.x + side.x, head.y + side.y, head.z + side.z); rlVertex3f(head.x - side.x, head.y - side.y, head.z - side.z);
            rlColor4ub(255, 255, 255, 0);
            rlVertex3f(tail.x - side.x, tail.y - side.y, tail.z - side.z); rlVertex3f(tail.x + side.x, tail.y + side.y, tail.z + side.z);
            rlEnd();
        }
    }
    if (sk->moon) {
        Vector3 m = Vector3Add(camPos, Vector3Scale(Vector3Normalize(sk->moonDir), R));
        Color mc = sk->moonCol; mc.a = 150;
        Vector3 r, u; dome_basis(m, &r, &u);
        sprite_quad(haloTex, m, Vector3Scale(r, 70), Vector3Scale(u, 70), mc);
        sprite_quad(haloTex, m, Vector3Scale(r, 170), Vector3Scale(u, 170), (Color){ mc.r / 3, mc.g / 3, mc.b / 3, 90 });
    }
    rlDrawRenderBatchActive();
    EndBlendMode();
    if (sk->moon) {
        Vector3 m = Vector3Add(camPos, Vector3Scale(Vector3Normalize(sk->moonDir), R)), r, u;
        dome_basis(m, &r, &u);
        sprite_quad(moonTex, m, Vector3Scale(r, 34), Vector3Scale(u, 34), sk->moonCol);
    }
    if (sk->eye && sk->eyeAmt > 0.01f) {
        float az = 200 * DEG2RAD, el = 30 * DEG2RAD;
        Vector3 c = dome(az, el, R * 0.95f), r, u;
        dome_basis(c, &r, &u);
        Vector3 d = Vector3Normalize(Vector3Subtract(c, camPos));
        float open = sk->eyeAmt;
        float facing = fmaxf(0.0f, (Vector3DotProduct(camF, d) - 0.7f) / 0.3f);
        float wx = sinf(time * 0.37f) * 0.35f * (1 - facing), wy = sinf(time * 0.53f) * 0.15f;
        // the eye tracks you: iris slides toward the way you are standing relative to it
        Vector3 toP = Vector3Subtract(playerPos, camPos);
        (void)toP;
        float W = 78, H = 40 * open;
        sprite_quad(scleraTex, c, Vector3Scale(r, W), Vector3Scale(u, fmaxf(0.5f, H)), (Color){ 255, 255, 255, 255 });
        Vector3 ic = Vector3Add(Vector3Add(c, Vector3Scale(r, wx * W * 0.5f)), Vector3Add(Vector3Scale(u, wy * H * 0.4f), Vector3Scale(d, -2.0f)));
        float is = 30.0f;
        sprite_quad(irisTex, ic, Vector3Scale(r, is), Vector3Scale(u, fminf(is, H * 0.95f)), (Color){ 255, 255, 255, 255 });
        gfx_begin_glow();
        sprite_quad(haloTex, c, Vector3Scale(r, 160), Vector3Scale(u, 90), (Color){ 160, 30, 40, (unsigned char)(120 * open) });
        gfx_end_glow();
        set_emit(2.0f);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    set_emit(0.0f);
}
