#!/bin/sh
# Zero-downtime deploy: Caddy keeps running (it is only recreated if its own image changes);
# the game is rebuilt and its static files are swapped in underneath it.
set -eu
cd "$(dirname "$0")"
docker network inspect murk-web >/dev/null 2>&1 || docker network create murk-web
docker compose -p murk up -d --build --wait caddy
docker compose -p murk run --rm -T --build site
