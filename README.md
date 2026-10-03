# MURK

A first-person psychological horror game in C. You wake in a house where a congregation keeps a vigil over
someone who must not wake. Its doors lead into dreams: a shaft you climb while something climbs after you,
drains where a blind thing hunts by sound, steps under an eye that sees whatever moves, an orchard whose
flowers are watching, and a church where everyone kneels on the third bell. Bring something back from each
one and find the way out. Read what people left lying around.

    meson setup build && ninja -C build && ./build/murk

Needs meson, ninja, cmake, a C compiler and the usual X11/Wayland + GL dev headers. raylib, Box3D and STC are
fetched by Meson on first configure (`subprojects/*.wrap`).

## Controls
WASD move · Shift run · Ctrl kneel · E use / read · Space jump · **hold LMB at rusty plates to grip**
(W climb, A/D shuffle, Space lunge or kick off) · F lamp (once found) · R wake up · [ ] mouse sensitivity · Esc quit

Headphones help: most of what matters is something you hear first, and where it comes from.

## Web version
The same source builds to WebAssembly with Emscripten:

    ./web/build.sh          # needs the Emscripten SDK (EMSDK=/path/to/emsdk, default ~/.local/share/emsdk)

That stages three static files in `dist/` (`index.html`, `murk.js`, `murk.wasm`, about 1.4 MB) that any web
server can host. Serve them over HTTPS: browsers only allow mouse capture and audio on secure pages.
Progress is kept in `localStorage` (the native build writes `murk.sav`).

`docker compose up -d --build` builds the web version and serves it with Caddy (set the domain in
`web/Caddyfile`). There is also a `Jenkinsfile` for rebuilding on push.

## Layout
- `src/main.c`   game loop, the things that hunt you, notes, sound placement, HUD
- `src/world.c`  the house and the dreams, the maze and the blind thing's pathfinding (STC `vec` containers)
- `src/figure.c` the creatures, built from tapered limbs and rounded heads
- `src/gfx.c`    procedural textures, fog shader, low-res render target and a light CRT pass (raylib)
- `src/player.c` kinematic capsule mover on Box3D (collide -> solve planes -> cast), grip/climb/mantle, kneeling, noise
- `src/audio.c`  fully procedural stereo sound: drone, distant choir, reverb, bells, breath, whispers
- `web/`         Emscripten cross file, HTML shell, build and publish scripts, Caddy config

Dev hooks: `MURK_SHOT="world,x,y,z,yaw,pitch,fxmask,frames,out.png"` renders one frame in a hidden window and
exits (`MURK_DREAMS=n` sets how deep in you are); `MURK_BOT=walk|jump|climb` drives the player and logs it.
