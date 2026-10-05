#include "audio.h"
#include <raylib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE 22050
#define TAU 6.2831853f
static AudioStream drone;
static Sound sfx[SFX_COUNT];
static bool ready;
static volatile float g_music = 0.0f, g_sour = 0.0f;
static volatile float g_tone = 1.0f, g_tension = 0.0f, g_vol = 0.0f, g_whisper = 0.0f;
static volatile float g_choir = 0.0f, g_breath = 0.0f, g_breathPan = 0.0f;

static float frnd(void) { return (float)rand() / (float)RAND_MAX * 2.0f - 1.0f; }
static float urnd(void) { return (float)rand() / (float)RAND_MAX; }

// Chamberlin state variable filter: cheap resonant band-pass, fine below ~4 kHz at this rate
typedef struct { float lo, bd; } Svf;
static float svf_bp(Svf *s, float in, float fc, float q) {
    float f = 2.0f * sinf(3.14159265f * fc / RATE);
    s->lo += f * s->bd;
    float hi = in - s->lo - q * s->bd;
    s->bd += f * hi;
    return s->bd;
}
static float svf_lp(Svf *s, float in, float fc, float q) { svf_bp(s, in, fc, q); return s->lo; }

// ---------------------------------------------------------------- reverb: a stone room much bigger than the one you are in
#define NCOMB 4
static const int COMB_L[NCOMB] = { 778, 808, 745, 711 }, AP_L[2] = { 278, 220 };
static float combBuf[2][NCOMB][840], combLp[2][NCOMB], apBuf[2][2][300];
static int combPos[2][NCOMB], apPos[2][2];
static float reverb(int ch, float in) {
    float out = 0;
    for (int i = 0; i < NCOMB; i++) {
        int len = COMB_L[i] + ch * 23;
        float y = combBuf[ch][i][combPos[ch][i]];
        combLp[ch][i] += (y - combLp[ch][i]) * 0.45f;           // damping: the tail goes dark as it dies
        combBuf[ch][i][combPos[ch][i]] = in + combLp[ch][i] * 0.84f;
        if (++combPos[ch][i] >= len) combPos[ch][i] = 0;
        out += y;
    }
    out *= 0.25f;
    for (int i = 0; i < 2; i++) {
        int len = AP_L[i] + ch * 11;
        float b = apBuf[ch][i][apPos[ch][i]];
        float y = -out + b;
        apBuf[ch][i][apPos[ch][i]] = out + b * 0.5f;
        if (++apPos[ch][i] >= len) apPos[ch][i] = 0;
        out = y;
    }
    return out;
}

// ---------------------------------------------------------------- music box: A minor, wandering. `sour` drags it out of tune and slows it down
static float music_box(float amt, float sour) {
    static float nv[3], ph[3], seqT; static int step, note = 4;
    static const float SC[] = { 0, 3, 5, 7, 10, 12, 15, 17, 19 };
    seqT -= 1.0f / RATE;
    if (seqT <= 0) {
        seqT = (0.5f + 0.35f * sour) * ((step % 8 == 7) ? 2.2f : 1.0f);
        note += (rand() % 5) - 2; if (note < 0) note = 1; if (note > 8) note = 7;
        if (rand() % 6 == 0) note = (note + 4) % 9;
        float semi = SC[note] - (sour > 0.4f && rand() % 3 == 0 ? 1.0f : 0.0f) + (sour > 0.7f && rand() % 4 == 0 ? 6.0f : 0.0f);
        float f = 220.0f * powf(2.0f, (semi + frnd() * 0.5f * sour) / 12.0f);
        int v = step % 3; step++;
        if (sour > 0.6f && rand() % 5 == 0) f = 0;   // a tooth missing from the comb
        ph[v] = 0; nv[v] = f;
    }
    float out = 0;
    for (int v = 0; v < 3; v++) {
        if (nv[v] <= 0) continue;
        ph[v] += 1.0f / RATE;
        float e = expf(-ph[v] * 2.6f);
        if (e < 0.002f) { nv[v] = 0; continue; }
        float w = TAU * nv[v] * ph[v] * (1.0f - 0.004f * sour * ph[v]);   // the spring winds down mid-note
        out += (sinf(w) + 0.35f * sinf(w * 2.76f) * expf(-ph[v] * 9.0f) + 0.12f * sinf(w * 5.4f) * expf(-ph[v] * 18.0f)) * e;
    }
    return out * 0.14f * amt;
}

// ---------------------------------------------------------------- the bed of sound under everything
static void drone_cb(void *buffer, unsigned frames) {
    static float vol, brown, wph, ph[4], cph[4], swell, vlfo, hb;
    static Svf windF, f1, f2, heartF, whF, brF;
    static float wlfo, wenv, wtarget, wsmooth, wpan, wpanT, wform = 1800;
    static float bph, bsmooth, chsmooth;
    short *out = buffer;
    for (unsigned i = 0; i < frames; i++) {
        vol += (g_vol - vol) * 0.0005f;
        chsmooth += (g_choir - chsmooth) * 0.00005f;
        // wind moving through a big stone space: brown noise through a slowly wandering band
        brown += frnd() * 0.02f; brown *= 0.998f;
        wph += 0.07f / RATE; if (wph > 1) wph -= 1;
        float wind = svf_bp(&windF, brown * 3.0f, 260.0f + 160.0f * sinf(wph * TAU) + 60.0f * sinf(wph * TAU * 3.1f), 0.35f);
        // the drone sits on a tritone. it beats against itself, very slowly
        float f = 36.7f * g_tone;
        static const float RAT[4] = { 1.0f, 1.0041f, 1.4142f, 2.0f };
        float d = 0;
        for (int k = 0; k < 4; k++) {
            ph[k] += f * RAT[k] / RATE; if (ph[k] > 1) ph[k] -= 1;
            d += sinf(ph[k] * TAU) * (k == 3 ? 0.06f : 0.2f);
        }
        d *= 0.8f + 0.2f * sinf(wph * TAU * 2.0f);
        float s = d + wind * 0.9f;
        // a congregation, very far off: low voices on the same tritone through a vowel that slowly changes
        if (chsmooth > 0.001f) {
            vlfo += 1.0f / RATE;
            swell = 0.5f - 0.5f * cosf(vlfo * TAU / 13.0f);
            static const float CF[4] = { 73.4f, 103.8f, 146.8f, 73.6f };
            float v = 0;
            for (int k = 0; k < 4; k++) {
                float vib = 1.0f + 0.006f * sinf(vlfo * (4.7f + k * 0.6f));
                cph[k] += CF[k] * vib * (1.0f - 0.01f * swell) / RATE; if (cph[k] > 1) cph[k] -= 1;
                v += cph[k] * 2.0f - 1.0f;   // raw saw; the formants do the shaping
            }
            float vowel = 0.5f + 0.5f * sinf(vlfo * 0.21f);   // "oo" .. "ah"
            float c = svf_bp(&f1, v, 320.0f + 380.0f * vowel, 0.25f) + 0.6f * svf_bp(&f2, v, 850.0f + 250.0f * vowel, 0.3f);
            s += c * 0.07f * chsmooth * (0.25f + 0.75f * swell);
        }
        // heartbeat, felt more than heard
        hb += (0.9f + g_tension * 1.6f) / RATE; if (hb > 1) hb -= 1;
        float beat = expf(-hb * 26.0f) + 0.55f * expf(-fabsf(hb - 0.24f) * 44.0f);
        s += svf_lp(&heartF, sinf(hb * 300.0f) * beat, 90.0f, 0.6f) * 1.1f * g_tension * g_tension;
        s *= vol * 0.5f;
        float L = s, R = s;

        // whispers: breath shaped into syllables with a moving vowel, drifting from ear to ear
        wsmooth += (g_whisper - wsmooth) * 0.0003f;
        if (wsmooth > 0.002f) {
            wlfo -= 1.0f / RATE;
            if (wlfo <= 0) {
                wlfo = 0.06f + 0.18f * urnd();
                wtarget = (rand() % 4 == 0) ? 0.0f : 0.4f + 0.6f * urnd();
                wform = 900.0f + 2400.0f * urnd();
                if (rand() % 9 == 0) wpanT = frnd();   // sometimes it moves to the other side of your head
            }
            wenv += (wtarget - wenv) * 0.003f;
            wpan += (wpanT - wpan) * 0.00002f;
            float w = svf_bp(&whF, frnd(), wform, 0.18f) * wenv * wsmooth * 0.3f * vol;
            L += w * (1.0f - wpan) * 0.7f; R += w * (1.0f + wpan) * 0.7f;
        }
        // something breathing near you: slow in, slower out
        bsmooth += (g_breath - bsmooth) * 0.0002f;
        if (bsmooth > 0.002f) {
            bph += 1.0f / (RATE * 4.2f); if (bph > 1) bph -= 1;
            float env = bph < 0.4f ? sinf(bph / 0.4f * 3.14159f) * 0.6f : bph < 0.9f ? sinf((bph - 0.4f) / 0.5f * 3.14159f) : 0.0f;
            float b = svf_bp(&brF, frnd(), bph < 0.4f ? 1400.0f : 700.0f, 0.5f) * env * env * bsmooth * 0.45f * vol;
            float p = g_breathPan;
            L += b * (1.0f - p) * 0.8f; R += b * (1.0f + p) * 0.8f;
        }

        float mb = music_box(g_music, g_sour) * vol * 1.3f;
        L += mb; R += mb;
        float rl = reverb(0, L * 0.6f), rr = reverb(1, R * 0.6f);
        L += rl * 0.45f; R += rr * 0.45f;
        L = fmaxf(-1.0f, fminf(1.0f, L)); R = fmaxf(-1.0f, fminf(1.0f, R));
        out[i * 2] = (short)(L * 30000);
        out[i * 2 + 1] = (short)(R * 30000);
    }
}

// ---------------------------------------------------------------- one-shots
typedef float (*SynthFn)(float t, float dur);

// partials of a cast bell: hum, prime, minor tierce, quint, nominal and a few out of the way
static const float BELL_R[8] = { 0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.51f, 2.66f, 3.01f };
static const float BELL_A[8] = { 0.9f, 0.7f, 0.55f, 0.3f, 0.45f, 0.18f, 0.15f, 0.1f };
static const float BELL_D[8] = { 0.35f, 0.6f, 0.8f, 1.1f, 1.3f, 2.2f, 2.6f, 3.0f };
static float bell(float t, float f, float decay, float bright) {
    float v = 0;
    for (int k = 0; k < 8; k++) v += sinf(t * TAU * f * BELL_R[k]) * BELL_A[k] * expf(-t * BELL_D[k] * decay) * (k < 4 ? 1.0f : bright);
    return v;
}

static float s_step(float t, float d)  { (void)d; return frnd() * expf(-t * 40.0f) * 0.5f + sinf(t * TAU * 70) * expf(-t * 30) * 0.5f; }
static float s_jump(float t, float d)  { return frnd() * 0.5f * sinf(t / d * 3.14159f) * expf(-t * 6); }
static float s_grab(float t, float d)  { (void)d; return frnd() * expf(-t * 25) * 0.4f + sinf(t * TAU * 120) * expf(-t * 18) * 0.5f; }
static float s_pick(float t, float d)  { (void)d; return bell(t, 196.0f, 0.9f, 0.6f) * 0.28f * (t < 0.004f ? t * 250 : 1); }
static float s_bell(float t, float d)  { (void)d; return bell(t, 98.0f, 0.55f, 0.25f) * 0.3f * (t < 0.01f ? t * 100 : 1); }

static Svf sv_a, sv_b;   // filter state for the one-shot that is being rendered (they render one at a time)
static float s_wake(float t, float d)  {   // a gasp, like coming up from water
    float e = t < 0.25f ? t / 0.25f : expf(-(t - 0.25f) * 5.0f);
    return svf_bp(&sv_a, frnd(), 900.0f + 1600.0f * fminf(1.0f, t / 0.25f), 0.4f) * e * 0.45f * (1 - t / d);
}
static float s_door(float t, float d)  {   // a heavy door dragging on its hinge: stick and slip
    static float next, ph;
    if (t == 0) { next = 0; ph = 0; }
    float out = 0;
    if (t >= next) { next = t + 0.006f + 0.02f * urnd() * (1.0f - t / d); ph = 0; }
    ph += 1.0f / RATE;
    out = svf_bp(&sv_a, (ph < 0.0008f ? 1.0f : 0.0f) + frnd() * 0.05f, 180.0f + 120.0f * t / d, 0.08f);
    return out * 0.5f * sinf(t / d * 3.14159f);
}
static float s_swell(float t, float d) {   // a bell played backwards: rises out of nothing and is cut off
    float r = d - t;
    float v = bell(r, 82.0f, 0.8f, 0.4f) * 0.4f + svf_bp(&sv_a, frnd(), 400.0f, 0.3f) * 0.2f * powf(t / d, 2.0f);
    float cut = r < 0.02f ? r / 0.02f : 1.0f;
    return v * powf(t / d, 1.6f) * cut;
}
static float s_knock(float t, float d) {   // three, slow
    (void)d;
    float v = 0;
    for (int k = 0; k < 3; k++) {
        float tt = t - k * 0.55f;
        if (tt < 0) continue;
        v += (sinf(tt * TAU * 95) * expf(-tt * 28) + sinf(tt * TAU * 210) * expf(-tt * 50) * 0.5f + frnd() * expf(-tt * 120) * 0.6f) * (k == 2 ? 1.1f : 0.85f);
    }
    return v * 0.5f;
}
static float s_breath(float t, float d) {  // out, long and slow, with something in the throat
    float e = sinf(fminf(1.0f, t / d) * 3.14159f);
    e = e * e;
    float air = svf_bp(&sv_a, frnd(), 650.0f - 200.0f * t / d, 0.45f);
    float rasp = svf_bp(&sv_b, (fmodf(t * 63.0f, 1.0f) < 0.15f ? 1.0f : 0.0f) * frnd(), 300.0f, 0.5f);
    return (air * 0.8f + rasp * 0.25f) * e * 0.8f;
}
static float s_chant(float t, float d) {   // three held notes from far away: D, the tritone above, D again
    static Svf a, b; static float ph[4];
    (void)d;
    if (t == 0) { memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(ph, 0, sizeof ph); }
    int n = t < 1.3f ? 0 : t < 2.6f ? 1 : 2;
    float nt = t - n * 1.3f;
    static const float ROOT[3] = { 146.8f, 207.6f, 138.6f };
    float v = 0;
    for (int k = 0; k < 4; k++) {
        float f = ROOT[n] * (k == 3 ? 0.5f : 1.0f) * (1.0f + (k - 1.5f) * 0.004f) * (1.0f + 0.005f * sinf(t * (5.0f + k)));
        ph[k] += f / RATE; if (ph[k] > 1) ph[k] -= 1;
        v += ph[k] * 2.0f - 1.0f;
    }
    float vowel = n == 1 ? 0.8f : 0.2f;
    float c = svf_bp(&a, v, 350.0f + 380.0f * vowel, 0.25f) + 0.6f * svf_bp(&b, v, 800.0f + 300.0f * vowel, 0.3f);
    float env = fminf(1.0f, nt / 0.35f) * (nt > 1.0f ? fmaxf(0.0f, 1.0f - (nt - 1.0f) / (n == 2 ? 1.2f : 0.3f)) : 1.0f);
    return c * env * 0.13f;
}
static float s_scrape(float t, float d) {  // stone on stone
    static float g;
    if (t == 0) g = 0;
    if (urnd() < 0.004f) g = urnd();
    float grit = svf_bp(&sv_a, frnd(), 520.0f + 300.0f * g, 0.3f);
    float body = svf_lp(&sv_b, frnd(), 120.0f, 0.5f) * 3.0f;
    return (grit * (0.4f + 0.6f * g) + body) * sinf(t / d * 3.14159f) * 0.5f;
}
static float s_hum(float t, float d) {     // a child, humming the tune from the music box, not quite in tune
    static const float N[5] = { 440.0f, 523.3f, 493.9f, 392.0f, 415.3f };
    static float ph;
    if (t == 0) ph = 0;
    int n = (int)(t / (d / 5)); if (n > 4) n = 4;
    float nt = t - n * d / 5;
    float f = N[n] * (1.0f + 0.01f * sinf(t * 31.0f)) * (n == 4 ? 1.0f - 0.03f * nt : 1.0f);
    ph += f / RATE; if (ph > 1) ph -= 1;
    float v = sinf(ph * TAU) + 0.3f * sinf(ph * TAU * 2) + 0.1f * sinf(ph * TAU * 3);
    float env = fminf(1.0f, nt / 0.08f) * fminf(1.0f, (d / 5 - nt) / 0.1f);
    return svf_lp(&sv_a, v, 1200.0f, 0.7f) * env * 0.25f;
}
static float s_click(float t, float d) {   // a burst of wet clicks
    static float next, ct, f;
    if (t == 0) { next = 0; }
    if (t >= next && t < d - 0.05f) { next = t + 0.035f + 0.07f * urnd(); ct = 0; f = 1300.0f + 900.0f * urnd(); }
    ct += 1.0f / RATE;
    return svf_bp(&sv_a, ct < 0.0015f ? frnd() * 3.0f : 0.0f, f, 0.12f) * expf(-ct * 60.0f) * 0.22f;
}
static float s_thud(float t, float d) { (void)d; return (sinf(t * TAU * (48 - 16 * t)) * expf(-t * 9) + svf_lp(&sv_a, frnd(), 200.0f, 0.7f) * expf(-t * 20) * 2.0f) * 0.5f; }
static float s_prayer(float t, float d) {  // syllables with their envelopes reversed: words, backwards
    static float sylT, sylLen, form;
    if (t == 0) { sylT = 0; sylLen = 0; }
    if (t >= sylT + sylLen) { sylT = t; sylLen = 0.12f + 0.2f * urnd(); form = 800.0f + 2200.0f * urnd(); }
    float p = (t - sylT) / sylLen;
    float env = powf(p, 2.5f) * (p > 0.92f ? (1.0f - p) / 0.08f : 1.0f);
    return svf_bp(&sv_a, frnd(), form, 0.2f) * env * 0.55f * sinf(fminf(1.0f, t / d) * 3.14159f);
}
static float s_creak(float t, float d) {
    float f = 70.0f + 40.0f * sinf(t / d * 3.14159f);
    float saw = fmodf(t * f, 1.0f) * 2.0f - 1.0f;
    return svf_bp(&sv_a, saw, 600.0f + 300.0f * t / d, 0.15f) * sinf(t / d * 3.14159f) * 0.35f;
}

static float s_ring(float t, float d) {   // two bursts of an old bell telephone
    (void)d;
    float on = (fmodf(t, 3.0f) < 0.4f || (fmodf(t, 3.0f) > 0.6f && fmodf(t, 3.0f) < 1.0f)) ? 1.0f : 0.0f;
    float clap = fmodf(t * 20.0f, 1.0f) < 0.5f ? 1.0f : 0.6f;
    return (sinf(t * TAU * 820) * 0.5f + sinf(t * TAU * 1240) * 0.3f + sinf(t * TAU * 1650) * 0.15f) * on * clap * 0.45f;
}

static float s_roar(float t, float d) {    // a throat far too big, saturated and torn
    static float ph, ph2;
    if (t == 0) { ph = ph2 = 0; }
    float f = 62.0f - 22.0f * t / d + 6.0f * sinf(t * 31.0f);
    ph += f / RATE; ph2 += f * 1.49f / RATE;
    float saw = (fmodf(ph, 1.0f) * 2 - 1) + 0.6f * (fmodf(ph2, 1.0f) * 2 - 1) + frnd() * 0.9f;
    float v = svf_bp(&sv_a, saw, 520.0f + 200.0f * sinf(t * 5.0f), 0.3f) * 2.5f + svf_lp(&sv_b, saw, 180.0f, 0.6f) * 1.5f;
    v = tanhf(v * 2.5f);
    float env = fminf(1.0f, t / 0.03f) * (1.0f - powf(t / d, 3.0f));
    return v * env * 0.9f;
}
static float s_cry(float t, float d) {     // short, hitching sobs, then a long wail
    (void)d;
    static float ph;
    if (t == 0) ph = 0;
    float seg = fmodf(t, 0.9f), on = seg < 0.65f ? sinf(seg / 0.65f * 3.14159f) : 0.0f;
    float f = 420.0f + 80.0f * sinf(seg * 6.0f) + 15.0f * sinf(t * 37.0f);
    ph += f / RATE;
    float src = fmodf(ph, 1.0f) * 2 - 1;
    float v = svf_bp(&sv_a, src, 1100.0f + 400.0f * on, 0.25f) + 0.5f * svf_bp(&sv_b, src, 2600.0f, 0.3f);
    return v * on * 0.35f;
}
static float s_bang(float t, float d) {    // three blows on a door, the last one hardest, and the frame rattling
    (void)d;
    float v = 0;
    for (int k = 0; k < 3; k++) {
        float tt = t - k * 0.32f;
        if (tt < 0) continue;
        float a = k == 2 ? 1.3f : 0.8f;
        v += a * (sinf(tt * TAU * 48) * expf(-tt * 14) + frnd() * expf(-tt * 40) * 0.8f + sinf(tt * TAU * 230) * expf(-tt * 30) * 0.4f);
    }
    if (t > 0.64f) v += frnd() * 0.25f * expf(-(t - 0.64f) * 6) * (fmodf(t * 31, 1.0f) < 0.4f ? 1 : 0);
    return tanhf(v * 1.2f) * 0.9f;
}
static float s_run(float t, float d) {     // bare feet slapping on something wet, fast
    (void)d;
    float st = fmodf(t, 0.17f);
    return (svf_bp(&sv_a, frnd(), 900.0f, 0.4f) * expf(-st * 50) * 1.2f + sinf(st * TAU * 80) * expf(-st * 40) * 0.5f) * 0.6f;
}
static float s_screech(float t, float d) { // metal on metal
    float f = 1800.0f + 600.0f * sinf(t * 3.0f) + 300.0f * sinf(t * 47.0f);
    float v = sinf(t * TAU * f + 3.0f * sinf(t * TAU * f * 0.51f)) + svf_bp(&sv_a, frnd(), 3000.0f, 0.2f) * 0.6f;
    return v * sinf(t / d * 3.14159f) * 0.3f;
}
static float s_giant(float t, float d) {   // something enormous putting its weight down
    (void)d;
    return tanhf((sinf(t * TAU * (28 - 10 * t)) * expf(-t * 4) * 1.6f + svf_lp(&sv_a, frnd(), 120.0f, 0.6f) * expf(-t * 7) * 4.0f) * 1.3f) * 0.95f;
}

static float s_ice(float t, float d) {     // a crack, and then the whole sheet rings: a chirp falling through the dispersion of the ice
    static float ph;
    if (t == 0) ph = 0;
    float f = 2600.0f * expf(-t * 5.5f) + 160.0f;
    ph += f / RATE;
    float ring = sinf(ph * TAU) * expf(-t * 2.2f) * 0.6f + sinf(ph * TAU * 1.51f) * expf(-t * 4.0f) * 0.2f;
    float crack = svf_bp(&sv_a, frnd(), 1800.0f, 0.3f) * expf(-t * 40.0f) * 1.4f;
    return (ring + crack) * 0.5f * (1 - t / d);
}
static float s_purr(float t, float d) {    // twenty-five little catches a second, in and out
    float in = fmodf(t, 1.6f) < 0.8f ? 1.0f : 0.75f;
    float pulse = powf(0.5f + 0.5f * sinf(t * TAU * 25.0f), 3.0f);
    return svf_lp(&sv_a, frnd(), 240.0f, 0.5f) * pulse * in * 1.6f * sinf(fminf(1.0f, t / d) * 3.14159f);
}
static float s_hiss(float t, float d) {
    float e = fminf(1.0f, t / 0.05f) * (1.0f - t / d);
    return (svf_bp(&sv_a, frnd(), 4200.0f, 0.5f) + 0.4f * svf_bp(&sv_b, frnd(), 2200.0f, 0.4f)) * e * 0.5f;
}
static float s_piano(float t, float d) {   // three strings to a note, never quite agreeing
    (void)d;
    float f = 261.6f, v = 0;
    for (int k = 1; k <= 5; k++) {
        float a = 1.0f / (k * k * 0.7f + 0.3f);
        v += a * (sinf(t * TAU * f * k) + sinf(t * TAU * f * k * 1.0031f) + sinf(t * TAU * f * k * 0.9974f)) * expf(-t * (0.9f + k * 0.7f));
    }
    float hammer = frnd() * expf(-t * 200.0f) * 0.4f;
    return (v * 0.09f + hammer) * (t < 0.003f ? t / 0.003f : 1.0f);
}
static float s_giggle(float t, float d) {  // hee hee hee, falling, muffled by a hand
    (void)d;
    static float ph;
    if (t == 0) ph = 0;
    int n = (int)(t / 0.16f);
    float nt = t - n * 0.16f, on = nt < 0.1f ? sinf(nt / 0.1f * 3.14159f) : 0.0f;
    if (n > 5) on = 0;
    float f = 620.0f - n * 35.0f + 40.0f * sinf(nt * 60.0f);
    ph += f / RATE;
    float src = (fmodf(ph, 1.0f) * 2 - 1) * 0.6f + frnd() * 0.5f;
    return (svf_bp(&sv_a, src, 2700.0f, 0.2f) + 0.6f * svf_bp(&sv_b, src, 900.0f, 0.3f)) * on * 0.3f;
}

static Sound synth(SynthFn fn, float dur) {
    int n = (int)(RATE * dur);
    short *data = malloc(n * sizeof(short));
    memset(&sv_a, 0, sizeof sv_a); memset(&sv_b, 0, sizeof sv_b);
    for (int i = 0; i < n; i++) {
        float v = fn((float)i / RATE, dur);
        float tail = (float)(n - i) / (RATE * 0.01f);   // never end on a click
        if (tail < 1) v *= tail;
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
    drone = LoadAudioStream(RATE, 16, 2);
    SetAudioStreamCallback(drone, drone_cb);
    PlayAudioStream(drone);
    static const struct { SynthFn fn; float dur; } DEF[SFX_COUNT] = {
        [SFX_STEP] = { s_step, 0.16f },   [SFX_JUMP] = { s_jump, 0.3f },     [SFX_GRAB] = { s_grab, 0.2f },
        [SFX_PICKUP] = { s_pick, 3.0f },  [SFX_WAKE] = { s_wake, 1.2f },     [SFX_DOOR] = { s_door, 1.3f },
        [SFX_SWELL] = { s_swell, 2.4f },  [SFX_KNOCK] = { s_knock, 1.5f },   [SFX_BREATH] = { s_breath, 2.2f },
        [SFX_BELL] = { s_bell, 6.0f },    [SFX_CHANT] = { s_chant, 4.0f },   [SFX_SCRAPE] = { s_scrape, 1.8f },
        [SFX_HUM] = { s_hum, 3.4f },      [SFX_CLICK] = { s_click, 0.7f },   [SFX_THUD] = { s_thud, 0.6f },
        [SFX_PRAYER] = { s_prayer, 2.6f }, [SFX_CREAK] = { s_creak, 0.8f },   [SFX_RING] = { s_ring, 1.1f },
        [SFX_ROAR] = { s_roar, 1.3f },    [SFX_CRY] = { s_cry, 2.7f },       [SFX_BANG] = { s_bang, 1.3f },
        [SFX_RUN] = { s_run, 1.4f },      [SFX_SCREECH] = { s_screech, 1.6f }, [SFX_GIANT] = { s_giant, 1.4f },
        [SFX_ICE] = { s_ice, 1.8f },      [SFX_PURR] = { s_purr, 3.2f },     [SFX_HISS] = { s_hiss, 0.9f },
        [SFX_PIANO] = { s_piano, 2.6f },  [SFX_GIGGLE] = { s_giggle, 1.1f },
    };
    for (int i = 0; i < SFX_COUNT; i++) sfx[i] = synth(DEF[i].fn, DEF[i].dur);
}

void audio_shutdown(void) {
    if (!ready) return;
    for (int i = 0; i < SFX_COUNT; i++) UnloadSound(sfx[i]);
    UnloadAudioStream(drone);
    CloseAudioDevice();
}

void audio_set(float tone, float tension, float whisper, float volume) { g_tone = tone; g_tension = tension; g_whisper = whisper; g_vol = volume; }
void audio_atmos(float choir, float breath, float breathPan) { g_choir = choir; g_breath = breath; g_breathPan = breathPan; }
void audio_music(float amount, float sour) { g_music = amount; g_sour = sour; }

void audio_play_at(Sfx s, float vol, float pitch, float pan) {
    if (!ready) return;
    pan = fmaxf(-1.0f, fminf(1.0f, pan));
    SetSoundPitch(sfx[s], pitch); SetSoundVolume(sfx[s], vol); SetSoundPan(sfx[s], 0.5f - 0.5f * pan);   // raylib: 1.0 is hard left
    PlaySound(sfx[s]);
}
void audio_play_ex(Sfx s, float vol, float pitch) { audio_play_at(s, vol, pitch, 0.0f); }
void audio_play(Sfx s) { audio_play_at(s, s == SFX_STEP ? 0.5f : 0.8f, 0.92f + 0.16f * urnd(), 0.0f); }
