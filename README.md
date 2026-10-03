# MURK

A first-person psychological horror game in C, in the spirit of Yume Nikki, LSD Dream Emulator and .flow.
You wake in a quiet house. Nothing follows you into it. Its doors open on dreams, and some of those dreams have
doors of their own that go deeper. Bring something back from each of the first five and find the way out.
Read what people left lying around.

    THE HOUSE (safe)
    ├─ the shaft ─────────── a door at the top ──> the city ──┬─ the hospital ──> the ward
    │                                                        ├─ the theatre ──> (backstage) the dinner
    │                                                        └─ the underground ──> the static sea
    ├─ the baths ─────────── down the slide ──> the ward ──┬─ the lift (from lap two) ──> the mortuary ── the drain ──> the baths
    │                                                      └─ its last lap ──> below
    ├─ the steps ─────────── a lone door on the way ──> the static sea (its screens link anywhere)
    ├─ the nursery ───────── the dollhouse door ──> the dinner ── sit down ──> below
    ├─ the lower church ──── a low door behind the altar ──> below ── a door home
    └─ the way out (needs something from each of the five)

Most of it is dark: you see what is close to you, and the lamp throws a beam. Each dream has its own thing with
its own rules, and it takes its time arriving: something that climbs when you climb and copies your sounds,
something blind in a drained pool, an eye and the hands that rise when it opens, a mother sixteen metres tall,
a priest who counts heads, a woman in the ward who moves in the dark, bodies that sit up, puppets that are
nearer each time the lights come up, a tall man at the ends of streets.

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
