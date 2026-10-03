# MURK

A first-person psychological horror game in C, in the spirit of Yume Nikki, LSD Dream Emulator and .flow.
You wake in a quiet house. Nothing follows you into it. Its doors open on dreams, and some of those dreams have
doors of their own that go deeper. Bring something back from each of the first five and find the way out.
Read what people left lying around.

    THE HOUSE (safe)
    ├─ the shaft ─────────── a door at the top ──> the city ──┬─ the hospital ──> the ward
    │                                                        └─ the underground ──> the static sea
    ├─ the drains ────────── a hatch in a dead end ──> the ward ── its last lap ──> below
    ├─ the steps ─────────── a lone door on the way ──> the static sea (its screens link anywhere)
    ├─ the orchard ───────── a door in the hedge ──> the dinner ── sit down ──> below
    ├─ the lower church ──── a low door behind the altar ──> below ── a door home
    └─ the way out (needs something from each of the five)

Each dream has its own thing with its own rules, and it takes its time arriving: something that climbs when you
climb, something blind that hears you, an eye that sees what moves, flowers that call the gardeners, a priest
who counts heads, a corridor that loops, a grey man, a host who moves when you aren't looking, a tall man at
the ends of streets.

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
exits (`MURK_DREAMS=n` sets how deep in you are, `MURK_LAP=n` which lap of the ward); `MURK_BOT=walk|jump|climb` drives the player and logs it.
