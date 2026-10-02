# syntax=docker/dockerfile:1
# Stage 1: compile the game to WebAssembly with Emscripten (same Meson project as the native build)
FROM emscripten/emsdk:3.1.74 AS build
# apt's meson is too old for the STC wrap (needs >= 1.1), so take meson from pip
RUN apt-get update \
 && apt-get install -y --no-install-recommends ninja-build cmake git ca-certificates python3 python3-pip \
 && rm -rf /var/lib/apt/lists/* \
 && (pip3 install --no-cache-dir "meson>=1.3" || pip3 install --no-cache-dir --break-system-packages "meson>=1.3") \
 && meson --version
WORKDIR /src
COPY . .
ENV EMSDK=/emsdk
RUN sh web/build.sh

# Stage 2: serve the three static files (index.html, murk.js, murk.wasm) with Caddy, which also handles HTTPS
FROM caddy:2-alpine
COPY web/Caddyfile /etc/caddy/Caddyfile
COPY --from=build /src/dist/ /srv/
EXPOSE 80 443
