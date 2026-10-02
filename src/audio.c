#include "audio.h"
#include <raylib.h>
#include <math.h>
#include <stdlib.h>

#define RATE 22050
static AudioStream drone;
static Sound sfx[SFX_COUNT];
static bool ready;
static volatile float g_music = 0.0f, g_sour = 0.0f;
static volatile float g_tone = 1.0f, g_tension = 0.0f, g_vol = 0.0f, g_whisper = 0.0f;

#define DLY 5200
static float dly[DLY];
static int dpos;

// a music box in A minor that wanders; `sour` drags it out of tune and slows it down
static float music_box(float amt, float sour) {
    static float t, nv[3], ph[3], seqT; static int step, note = 4;
    static const float SC[] = { 0, 3, 5, 7, 10, 12, 15, 17, 19 };   // minor pentatonic-ish, semitones above A3
    seqT -= 1.0f / RATE;
    if (seqT <= 0) {
        seqT = (0.42f + 0.2f * sour) * ((step % 8 == 7) ? 1.8f : 1.0f);
        note += (rand() % 5) - 2; if (note < 0) note = 1; if (note > 8) note = 7;
        if (rand() % 6 == 0) note = (note + 4) % 9;
        float semi = SC[note] - (sour > 0.5f && rand() % 3 == 0 ? 1.0f : 0.0f);
        float f = 220.0f * powf(2.0f, (semi + (rand() % 100 - 50) * 0.002f * sour * 10.0f) / 12.0f);
        int v = step % 3; step++;
        ph[v] = 0; nv[v] = f;
        t = 0;
    }
    float out = 0;
    for (int v = 0; v < 3; v++) {
        if (nv[v] <= 0) continue;
        ph[v] += 1.0f / RATE;
        float e = expf(-ph[v] * 3.2f);
        if (e < 0.002f) { nv[v] = 0; continue; }
        float w = 2 * 3.14159265f * nv[v] * ph[v];
        out += (sinf(w) + 0.35f * sinf(w * 2.76f) * expf(-ph[v] * 9.0f) + 0.12f * sinf(w * 5.4f) * expf(-ph[v] * 18.0f)) * e;
    }
    (void)t;
    return out * 0.16f * amt;
}

static float frnd(void) { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

// brown-noise rumble + two beating low sines + a heartbeat that speeds up with tension
static void drone_cb(void *buffer, unsigned frames) {
    static float brown, ph1, ph2, hb, lp, vol, wh1, wh2, wlfo, wenv, wtarget, wsmooth;
    short *out = buffer;
    for (unsigned i = 0; i < frames; i++) {
        vol += (g_vol - vol) * 0.0005f;
        brown += frnd() * 0.02f; brown *= 0.998f;
        float f = 41.0f * g_tone;
        ph1 += f / RATE; ph2 += (f * 1.012f) / RATE;
        if (ph1 > 1) { ph1 -= 1; }
        if (ph2 > 1) { ph2 -= 1; }
        float s = sinf(ph1 * 6.2831853f) * 0.25f + sinf(ph2 * 6.2831853f) * 0.25f + brown * 1.4f;
        lp += (frnd() * 0.5f - lp) * 0.08f;
        s += lp * 0.05f * (0.4f + g_tension);
        hb += (1.1f + g_tension * 2.2f) / RATE;
        if (hb > 1) hb -= 1;
        float beat = expf(-hb * 22.0f) + 0.6f * expf(-fabsf(hb - 0.22f) * 40.0f);
        s += sinf(hb * 70.0f) * beat * 0.7f * g_tension;
        // whispers: band-limited noise chopped into syllables, louder as something gets close
        wlfo -= 1.0f / RATE;
        if (wlfo <= 0) { wlfo = 0.07f + 0.2f * (rand() / (float)RAND_MAX); wtarget = (rand() % 3 == 0) ? 0.0f : 0.4f + 0.6f * (rand() / (float)RAND_MAX); }
        wenv += (wtarget - wenv) * 0.002f;
        float n = frnd();
        wh1 += (n - wh1) * 0.28f; wh2 += (wh1 - wh2) * 0.28f;
        wsmooth += (g_whisper - wsmooth) * 0.0003f;
        s += (wh1 - wh2) * 5.0f * wenv * wsmooth * 0.35f;
        s *= vol * 0.5f;
        float mb = music_box(g_music, g_sour) * vol * 1.4f;
        // a long soft echo makes every sound feel like it happens in a bigger room
        float dl = dly[dpos];
        dly[dpos] = (s + mb) * 0.9f + dl * 0.46f;
        if (++dpos >= DLY) dpos = 0;
        s += mb + dl * 0.38f;
        s = fmaxf(-1.0f, fminf(1.0f, s));
        out[i] = (short)(s * 30000);
    }
}

static float s_sting(float t, float d) { (void)d; // a dissonant cluster that dies slowly, with a noise slap on the front
    float e = expf(-t * 2.2f);
    float f[5] = { 233, 247, 349, 370, 523 };
    float v = 0;
    for (int i = 0; i < 5; i++) v += sinf(t * 2 * 3.14159f * f[i] * (1.0f - 0.05f * t)) * 0.18f;
    return (v * e + frnd() * expf(-t * 30) * 0.8f) * (t < 0.01f ? t * 100 : 1);
}
static float s_knock(float t, float d) { (void)d; return (sinf(t * 2 * 3.14159f * 55) * expf(-t * 22) + frnd() * expf(-t * 80) * 0.5f) * 0.9f + ((t > 0.28f) ? (sinf((t - 0.28f) * 2 * 3.14159f * 50) * expf(-(t - 0.28f) * 22) * 0.8f) : 0.0f); }
static float s_scream(float t, float d) {
    float f = 700 + 500 * sinf(t * 9) - 400 * t / d;
    return (sinf(t * 2 * 3.14159f * f) + 0.7f * (sinf(t * 2 * 3.14159f * f * 1.41f) > 0 ? 1.0f : -1.0f) + frnd()) * 0.3f * (t < 0.02f ? t * 50 : 1) * (1 - t / d);
}
typedef float (*SynthFn)(float t, float dur);
static float s_step(float t, float d)   { (void)d; return frnd() * expf(-t * 40.0f) * 0.5f + sinf(t * 2 * 3.14159f * 70) * expf(-t * 30) * 0.5f; }
static float s_jump(float t, float d)   { return sinf(t * 2 * 3.14159f * (180 - 90 * t / d)) * (1 - t / d) * 0.4f; }
static float s_grab(float t, float d)   { (void)d; return frnd() * expf(-t * 25) * 0.4f + sinf(t * 2 * 3.14159f * 120) * expf(-t * 18) * 0.5f; }
static float s_pick(float t, float d)   { (void)d; return (sinf(t * 2 * 3.14159f * 440) + sinf(t * 2 * 3.14159f * 660 * (1 + 0.003f * sinf(t * 30))) * 0.7f) * expf(-t * 3.0f) * 0.35f; }
static float s_wake(float t, float d)   { return (frnd() * 0.4f + sinf(t * 2 * 3.14159f * (300 - 270 * t / d))) * (1 - t / d) * 0.5f; }
static float s_door(float t, float d)   { return sinf(t * 2 * 3.14159f * (60 + 40 * t / d)) * (1 - t / d) * 0.6f + frnd() * 0.1f * (1 - t / d); }

static Sound synth(SynthFn fn, float dur) {
    int n = (int)(RATE * dur);
    short *data = malloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float v = fn((float)i / RATE, dur);
        v = fmaxf(-1.0f, fminf(1.0f, v));
        data[i] = (short)(v * 28000);
    }
    Wave w = { (unsigned)n, RATE, 16, 1, data };
    Sound s = LoadSoundFromWave(w);
    free(data);
    return s;
}

void audio_init(void) {
    InitAudioDevice();
    ready = IsAudioDeviceReady();
    if (!ready) return;
    SetAudioStreamBufferSizeDefault(2048);
    drone = LoadAudioStream(RATE, 16, 1);
    SetAudioStreamCallback(drone, drone_cb);
    PlayAudioStream(drone);
    sfx[SFX_STEP] = synth(s_step, 0.16f);
    sfx[SFX_JUMP] = synth(s_jump, 0.25f);
    sfx[SFX_GRAB] = synth(s_grab, 0.2f);
    sfx[SFX_PICKUP] = synth(s_pick, 1.4f);
    sfx[SFX_WAKE] = synth(s_wake, 1.0f);
    sfx[SFX_DOOR] = synth(s_door, 0.8f);
    sfx[SFX_STINGER] = synth(s_sting, 2.4f);
    sfx[SFX_KNOCK] = synth(s_knock, 0.7f);
    sfx[SFX_SCREAM] = synth(s_scream, 1.1f);
}

void audio_shutdown(void) {
    if (!ready) return;
    for (int i = 0; i < SFX_COUNT; i++) UnloadSound(sfx[i]);
    UnloadAudioStream(drone);
    CloseAudioDevice();
}

void audio_set(float tone, float tension, float whisper, float volume) { g_tone = tone; g_tension = tension; g_whisper = whisper; g_vol = volume; }

void audio_music(float amount, float sour) { g_music = amount; g_sour = sour; }

void audio_play_ex(Sfx s, float vol, float pitch) {
    if (!ready) return;
    SetSoundPitch(sfx[s], pitch); SetSoundVolume(sfx[s], vol); PlaySound(sfx[s]);
}

void audio_play(Sfx s) {
    if (!ready) return;
    SetSoundPitch(sfx[s], 0.85f + 0.3f * (rand() / (float)RAND_MAX));
    SetSoundVolume(sfx[s], s == SFX_STEP ? 0.5f : 0.8f);
    PlaySound(sfx[s]);
}
