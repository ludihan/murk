#!/usr/bin/env sh
# Build the browser version and stage it in ./dist (three static files you can drop on any web server).
#   needs: meson, ninja, cmake, and the Emscripten SDK (EMSDK=/path/to/emsdk, default ~/.local/share/emsdk)
set -e
cd "$(dirname "$0")/.."
EMSDK="${EMSDK:-$HOME/.local/share/emsdk}"
. "$EMSDK/emsdk_env.sh" >/dev/null 2>&1

if [ -d build-web ]; then meson setup build-web --reconfigure >/dev/null; else meson setup build-web --cross-file web/emscripten.cross; fi
ninja -C build-web murk.html

rm -rf dist && mkdir dist
cp build-web/murk.html dist/index.html
cp build-web/murk.js build-web/murk.wasm dist/
echo "staged: $(du -sh dist | cut -f1) in ./dist  (index.html, murk.js, murk.wasm)"
