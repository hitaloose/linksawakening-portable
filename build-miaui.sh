#!/bin/bash
# Link's Awakening DX for MiaUI (Anbernic RG35XX), built from your own ROM.
#
#   ./build-miaui.sh [your ROM: a .gbc, a .zip holding one, or a folder]
#
# Needs git and Docker; nothing else is installed on your machine. The ROM
# (Link's Awakening DX (USA, Europe) (Rev 2)) is only read, never shipped:
# the game is recompiled from it here, on your computer.
#
# Output: build-miaui/linksawakening-miaui.zip -> unzip at the root of the SD
# card (it holds Ports/Links Awakening DX.pak/).
#
# Other consoles: copy this file to build-<target>.sh with its own Docker
# image and scripts/build_<target>.sh (docs/HANDHELD.md).
set -e
TARGET=miaui
IMAGE=linksawakening-$TARGET
cd "$(dirname "$0")"
REPO="$PWD"

command -v docker >/dev/null || { echo "Docker is needed: https://docs.docker.com/get-docker/" >&2; exit 1; }

if [ ! -f LinksAwakening/gbrecomp/runtime/src/gbrt.c ]; then
    echo '== Fetching submodules'
    git submodule update --init --recursive
fi

echo "== Toolchain image ($IMAGE)"
docker build -q -t "$IMAGE" -f "docker/$TARGET.Dockerfile" docker >/dev/null

echo '== ROM -> C'
if [ -f LinksAwakening/gen/rom.c ] && [ -z "$1" ]; then
    echo '  LinksAwakening/gen/ already there (pass your ROM again to redo it)'
else
    ROM="${1:-.}"
    [ -e "$ROM" ] || { echo "No such file or folder: $ROM" >&2; exit 1; }
    if [ -d "$ROM" ]; then ROMDIR="$(cd "$ROM" && pwd)"; else
        ROMDIR="$(mktemp -d)"; cp "$ROM" "$ROMDIR/"; trap 'rm -rf "$ROMDIR"' EXIT
    fi
    rm -f LinksAwakening/rom.gbc          # take the one given now
    docker run --rm -v "$REPO:/src" -v "$ROMDIR:/rom:ro" "$IMAGE" bash /src/scripts/recomp.sh
fi

echo "== Build ($TARGET)"
docker run --rm -v "$REPO:/src" "$IMAGE" bash "/src/scripts/build_$TARGET.sh" "/src/build-$TARGET"

echo
echo "Done: build-$TARGET/linksawakening-$TARGET.zip (unzip at the root of the SD card)"
