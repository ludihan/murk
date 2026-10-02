#pragma once
typedef enum { SFX_STEP, SFX_JUMP, SFX_GRAB, SFX_PICKUP, SFX_WAKE, SFX_DOOR, SFX_STINGER, SFX_KNOCK, SFX_SCREAM, SFX_COUNT } Sfx;
void audio_init(void);
void audio_shutdown(void);
void audio_set(float tone, float tension, float whisper, float volume); // drone pitch, heartbeat/dread 0..1, master
void audio_play(Sfx s);
void audio_play_ex(Sfx s, float vol, float pitch);
