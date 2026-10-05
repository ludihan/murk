# MURK

A first-person psychological horror game in C, in the spirit of Yume Nikki, LSD Dream Emulator and .flow.
You wake in a quiet house. Nothing follows you into it. Its doors open on dreams, and some of those dreams have
doors of their own that go deeper. Bring something back from each of the first five and find the way out.
Read what people left lying around.

    THE HOUSE (safe, for now)
    ├─ the shaft ─────────── a door at the top ──> the city ──┬─ the hospital ──> the ward
    │                                                        ├─ the theatre ──> (backstage) the dinner
    │                                                        └─ the underground ──> the static sea
    ├─ the baths ─────────── down the slide ──> the ward ──┬─ the lift (from lap two) ──> the mortuary ── the drain ──> the baths
    │                                                      └─ its last lap ──> below
    ├─ the steps ─────────── a lone door on the way ──> the static sea (its screens link anywhere)
    ├─ the nursery ───────── the dollhouse door ──> the dinner ── sit down ──> below
    ├─ the lower church ──── a low door behind the altar ──> below ── a door home
    ├─ the wardrobe ──────── the school ── the school trip ──> the fairground
    ├─ the attic ─────────── the fairground ── the ghost train ──> the theatre
    ├─ the kitchen ───────── the back door ──> the frozen lake ── the fishing hole ──> the baths
    └─ the way out (needs something from each of the first five)

Most of it is dark: you see what is close to you, and the lamp throws a beam. Each dream has its own thing with
its own rules, and it does not wait long: something that climbs when you climb and copies your sounds, a starved
man on his hands and knees in a drained pool, his jaw hanging off and his eyes lit from inside, an eye and the
hands that rise when it opens, a mother sixteen metres tall, a priest who counts heads, a woman in the ward who
moves in the dark, bodies that sit up, puppets that are nearer each time the lights come up, a tall man at the
ends of streets. And now:

- **The school**, through the bedroom wardrobe. Hide and seek. Something nearly three metres tall in a school
  jumper counts to ten at the end of the corridor, banging its forehead on the wall at every number, then walks
  the school checking cupboards. Hide in one before ten (you watch it through the slats). It hears a door shut
  after ten, and it looks first where you hid last time. Last through two rounds and lost property opens.
- **The fairground**, up the new attic stairs. Every bulb lit, nobody there. Ride the carousel (more of them
  come to watch each time you go round), try the strength tester, and go through the hall of mirrors, where you
  are a sleepwalking boy in pyjamas with your eyes shut. Halfway along, one of you stops and opens its eyes; then
  it comes through the glass, and from then on it moves only when you move. The fortune teller is at the end.
- **The frozen lake**, out the kitchen's back door. The ice cracks under you if you stand still, the black
  patches are thin (kneel and crawl), and something with a face as wide as a door swims under the ice toward
  the sound of running. There are people frozen in it, looking up, and their eyes follow you.

The house is bigger and has things to do: an attic with a telescope, a kitchen with a radio (the weather in
the dreams, lost and found, requests), a fridge, a piano that plays the music box's lullaby, his diary (it
keeps count of where you have been), a bed that drops you into a dream you have not seen yet, and a mantel
that shows what you have brought home. There is a black cat. Stroke it and it follows you, even through the
doors, and in a dream it hisses at whatever is hunting you before you can see it, then bolts.

Nothing follows you into the house, but the house changes the more you bring back: someone standing in every
painting, nearer each time; someone in the armchair by the fire; knocking from the other side of the way out;
writing on the walls; things in the fridge.

The new dreams give new things: a **compass** that points at what you came for (and at what is coming for you),
**matches** (Q, five a dream) that make a small room of light in any dark, and **slippers** that quiet your feet.

    meson setup build && ninja -C build && ./build/murk

Needs meson, ninja, cmake, a C compiler and the usual X11/Wayland + GL dev headers. raylib, Box3D and STC are
fetched by Meson on first configure (`subprojects/*.wrap`).

## Controls
WASD move · Shift run · C kneel (Ctrl too, outside the browser) · E use / read / hide / stroke the cat · Space jump · **hold LMB at rusty
plates to grip** (W climb, A/D shuffle, Space lunge or kick off) · F lamp · Q strike a match (once found) ·
R wake up · [ ] mouse sensitivity · Esc quit

Headphones help: most of what matters is something you hear first, and where it comes from.

## Web version
The same source builds to WebAssembly with Emscripten:

    ./web/build.sh          # needs the Emscripten SDK (EMSDK=/path/to/emsdk, default ~/.local/share/emsdk)

That stages three static files in `dist/` (`index.html`, `murk.js`, `murk.wasm`, about 1.4 MB) that any web
server can host. Serve them over HTTPS: browsers only allow mouse capture and audio on secure pages.
Progress is kept in `localStorage` (the native build writes `murk.sav`). The page keeps the browser's own shortcuts out of the way
while you play: it swallows them (Ctrl+S, Ctrl+R, Ctrl+F, Space scrolling, Alt, the mouse's back button...), asks before
the tab closes, and in fullscreen (double-click) it locks the keyboard so that even Ctrl+W reaches the game where the
browser allows it (Chromium). Esc, F5, F11 and F12 still belong to the browser.

`docker compose up -d --build` builds the web version and serves it with Caddy (set the domain in
`web/Caddyfile`). There is also a `Jenkinsfile` for rebuilding on push.

## Layout
- `src/main.c`   game loop, the things that hunt you, the cat, the house going bad, notes, sound placement, HUD
- `src/world.c`  the house and the dreams, the maze and the blind thing's pathfinding (STC `vec` containers)
- `src/figure.c` the creatures (and the cat), built from tapered limbs and rounded heads
- `src/gfx.c`    procedural textures, fog shader, low-res render target and a light CRT pass (raylib)
- `src/player.c` kinematic capsule mover on Box3D (collide -> solve planes -> cast), grip/climb/mantle, kneeling, noise
- `src/audio.c`  fully procedural stereo sound: drone, distant choir, reverb, bells, breath, whispers
- `web/`         Emscripten cross file, HTML shell, build and publish scripts, Caddy config

Dev hooks: `MURK_SHOT="world,x,y,z,yaw,pitch,fxmask,frames,out.png"` renders one frame in a hidden window and
exits (`MURK_DREAMS=n` sets how deep in you are, `MURK_LAP=n` which lap of the ward); `MURK_FIG="kind,x,y,z,yaw,look"`
puts any figure in that frame; `MURK_BOT=walk|jump|climb` drives the player and logs it. World ids run in `WorldId`
order (`src/common.h`): the house is 0, the fairground 13, the lake 14, the school 15.
