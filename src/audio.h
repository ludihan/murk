#pragma once
typedef enum {
    SFX_STEP, SFX_JUMP, SFX_GRAB, SFX_PICKUP, SFX_WAKE, SFX_DOOR,
    SFX_SWELL,      // a reversed bell that swells up out of nothing and stops dead
    SFX_KNOCK,      // three slow knocks on wood
    SFX_BREATH,     // a long exhale, close
    SFX_BELL,       // a funeral bell somewhere far off
    SFX_CHANT,      // a congregation singing three notes behind a wall
    SFX_SCRAPE,     // something heavy dragged over stone
    SFX_HUM,        // a child humming the music box tune
    SFX_CLICK,      // wet clicking, like a tongue against teeth
    SFX_THUD,       // a heavy footfall
    SFX_PRAYER,     // whispered words, played backwards
    SFX_CREAK,      // a floorboard taking weight
    SFX_COUNT
} Sfx;
void audio_init(void);
void audio_shutdown(void);
void audio_set(float tone, float tension, float whisper, float volume); // drone pitch, heartbeat/dread 0..1, master
void audio_atmos(float choir, float breath, float breathPan);           // a distant mass behind the walls; something breathing near you (-1 left .. 1 right)
void audio_play(Sfx s);
void audio_play_ex(Sfx s, float vol, float pitch);
void audio_play_at(Sfx s, float vol, float pitch, float pan);           // pan -1 left .. 1 right
void audio_music(float amount, float sour); // music box volume 0..1, how out of tune it is 0..1
