#!/bin/sh
# Publish the built game into the shared volume as a new release, then switch Caddy to it atomically.
# Caddy serves /srv/current (a symlink), so visitors never see a half-copied site and nothing restarts.
set -eu
cd /site
rel="releases/$(date +%Y%m%d%H%M%S)"
mkdir -p "$rel"
cp /dist/* "$rel"/
ln -sfn "$rel" current.new
mv -T current.new current
# keep the last three releases around for a quick rollback
ls -1d releases/* | head -n -3 | xargs -r rm -rf
echo "published $rel"
