# MURK

A grimy first-person dream-exploration game in C. Wake in a decaying room, step through doors into
other people's dreams, bring back an *effect* from each one, and find the way out.

    meson setup build && ninja -C build && ./build/murk

Needs meson, ninja, cmake, a C compiler and the usual X11/Wayland + GL dev headers. raylib, Box3D and
STC are fetched by Meson on first configure (`subprojects/*.wrap`).

## Controls
WASD move · Shift run · Space jump · **hold LMB at rusty plates to grip** (W climb, A/D shuffle,
Space lunge/kick off) · F lamp (once found) · R wake up · Esc quit

## Layout
- `src/gfx.c`    procedural grime textures, fog/object shader, low-res render target + CRT/VHS post pass (raylib)
- `src/world.c`  the five levels, maze generation, watcher AI (STC `vec` containers)
- `src/player.c` kinematic capsule mover on Box3D (collide -> solve planes -> cast), grip/climb/mantle
- `src/audio.c`  fully procedural drone, heartbeat and one-shot sounds
- Box3D also simulates the shoveable junk in the hub/drains and the weightless debris in the void.
