#!/bin/bash
# Downloads the bundled screen overlays (GRID option, handheld/la_bezel.c)
# from their original repository into handheld/overlays/, at a fixed commit
# and checked against their SHA-256. They are not kept in this repository
# (ourigen/perfect_overlays has no license, so we don't redistribute them):
# like the ROM, each build fetches them itself. Credits:
# handheld/overlays/CREDITS.md.
#
# Run on the host by build-<target>.sh. Never fails the build: without them
# the game simply offers no bundled overlays (players can still add their own
# PNGs to Saves/LinksAwakening/overlays/).
cd "$(dirname "$0")/.."
DIR=handheld/overlays
BASE=https://raw.githubusercontent.com/ourigen/perfect_overlays/46eaa5ffc38bb7bcd4372b0c976a5430f5287825

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

# local name | path upstream | sha256
while IFS='|' read -r name path sum; do
    out="$DIR/$name"
    [ -f "$out" ] && [ "$(sha256 "$out")" = "$sum" ] && continue
    tmp="$out.part"
    if curl -fsSL --retry 2 -o "$tmp" "$BASE/$path" && [ "$(sha256 "$tmp")" = "$sum" ]; then
        mv "$tmp" "$out"
        echo "  $name"
    else
        rm -f "$tmp"
        echo "  $name: download failed or changed upstream (skipped)" >&2
    fi
done <<'LIST'
GBC_Mugwomp.png|GBC/mugwomp93/Perfect_GBC.png|08c5bf3e28227fe41b69a4462bf6d0e3b08a4b40374fae0d071c0bdde8d52886
GBC_Drkhrse.png|GBC/drkhrse/Perfect_GBC.png|4181450a43727825a97815400ce9baeb73aeb7aa69f5519cc99bdd9ab2a3acb2
LIST
exit 0
