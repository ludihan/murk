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

## Continuous deploy (Jenkins)

The `Jenkinsfile` rebuilds and restarts the site (`docker compose -p murk up -d --build --wait`) on every push to `main`.
Jenkins runs on the same server and uses the host's Docker through the mounted socket (no SSH, no keys). GitHub's
push webhook reaches it through the site's own Caddy (`https://murk.ludihan.xyz/github-webhook/`); the Jenkins UI is
never exposed publicly.

### Step by step

1. **DNS and firewall.** `A` record for `murk.ludihan.xyz` -> the VPS; ports 80 and 443 open. Docker and the compose plugin installed.
2. **Get the code and a shared network** (on the VPS):

       git clone https://github.com/ludihan/murk.git && cd murk
       docker network create murk-web

3. **Start the site** (first deploy by hand; Jenkins takes over afterwards):

       docker compose -p murk up -d --build

4. **Start Jenkins:**

       docker compose -f ci/docker-compose.jenkins.yml up -d --build
       docker compose -f ci/docker-compose.jenkins.yml exec jenkins cat /var/jenkins_home/secrets/initialAdminPassword

5. **Open Jenkins** (it only listens on the VPS's localhost): from your PC run `ssh -L 8080:localhost:8080 you@your-vps`,
   then browse http://localhost:8080. Paste the password, *Install suggested plugins* (this includes the GitHub plugin), create your admin user.
6. **Create the job:** New Item -> name `murk` -> *Pipeline* -> Pipeline definition *Pipeline script from SCM* -> SCM *Git* ->
   URL `https://github.com/ludihan/murk.git` -> branch `*/main` -> script path `Jenkinsfile` -> Save.
7. **Run it once** with *Build Now*. This first run is also what registers the push trigger from the `Jenkinsfile`.
8. **Add the GitHub webhook:** repo -> Settings -> Webhooks -> Add webhook -> payload URL
   `https://murk.ludihan.xyz/github-webhook/`, content type `application/json`, *Just the push event*.
   GitHub's "recent deliveries" should show a green 200.
9. **Test:** push a commit; a build appears in Jenkins within seconds and the site updates. A 10-minute poll acts as a
   fallback if a webhook is ever missed.

Notes: anything that can run jobs in Jenkins has root-equivalent access to the host, so keep the login protected and
don't expose port 8080. After step 3, later rebuilds are done by Jenkins, not by hand.

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
