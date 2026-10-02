# MURK

A grimy first-person dream-exploration game in C. Wake in a decaying room, step through doors into
other people's dreams (a rusted shaft, the drains, a void with an eye in the sky, a moonlit orchard
of flowers that watch you), bring back an *effect* from each one, and find the way out.

    meson setup build && ninja -C build && ./build/murk

Needs meson, ninja, cmake, a C compiler and the usual X11/Wayland + GL dev headers. raylib, Box3D and
STC are fetched by Meson on first configure (`subprojects/*.wrap`).

## Web version (host it on your VPS)

The same C source builds to WebAssembly with Emscripten, through the same Meson project:

    ./web/build.sh          # needs the Emscripten SDK (EMSDK=/path/to/emsdk, default ~/.local/share/emsdk)

That stages `dist/` (about 1.2 MB: `index.html`, `murk.js`, `murk.wasm`). Copy it to your server, e.g.

    rsync -av dist/ you@your-vps:/var/www/murk/

nginx (HTTPS recommended; browsers are stricter with audio and mouse capture on plain http):

    server {
        server_name murk.example.com;
        root /var/www/murk;
        index index.html;
        gzip on;
        gzip_types application/wasm application/javascript text/html;
        types { application/wasm wasm; }
    }

In the browser the game asks for a name once and keeps your launches and wake-ups in `localStorage`
(the native build uses `$USER` and a `murk.sav` file). Click to start; Esc releases the mouse, click to take it back.

## Docker (murk.ludihan.xyz on its own VPS)

Point an `A` record for `murk` at the VPS (the apex domain can stay wherever it is), open ports 80 and 443, then:

    docker compose up -d --build

Caddy inside the container serves the game and fetches the HTTPS certificate for `murk.ludihan.xyz` by itself
(browsers need HTTPS for mouse capture and audio). To use another name, edit `web/Caddyfile`.
Update later with `git pull && docker compose up -d --build`.

## Controls
WASD move · Shift run · Space jump · **hold LMB at rusty plates to grip** (W climb, A/D shuffle,
Space lunge/kick off) · [ ] mouse sensitivity (remembered) · hold Space in the air to glide (feather) · F lamp (once found) · R wake up · Esc quit

## Layout
- `src/gfx.c`    procedural grime textures, fog/object shader, low-res render target + CRT/VHS post pass (raylib)
- `src/world.c`  the five levels, maze generation, watcher AI (STC `vec` containers)
- `src/player.c` kinematic capsule mover on Box3D (collide -> solve planes -> cast), grip/climb/mantle
- `web/`         Emscripten cross file, HTML shell and build script
- `src/audio.c`  fully procedural drone, heartbeat and one-shot sounds
- Box3D also simulates the shoveable junk in the hub/drains and the weightless debris in the void.
