#!/bin/bash
# Build Link's Awakening for MiaUI (Anbernic RG35XX, ARMv7 hard-float, glibc 2.37).
# Normally run through ./build-miaui.sh at the repo root, which does the Docker
# and ROM steps; by hand:
#
#   docker build -t linksawakening-miaui -f docker/miaui.Dockerfile docker
#   docker run --rm -v <repo>:/src linksawakening-miaui bash /src/scripts/build_miaui.sh /src/build-miaui
#
# The game layer is shared with every handheld target (scripts/handheld.sh,
# handheld/); this file only has what's MiaUI's: the compiler (Debian bookworm
# armhf, glibc 2.36, links against the libSDL2 already on MiaUI), the panel
# rate, and the .pak layout.
#
# Output: "<out>/Links Awakening DX.pak/" (copy into Ports/ on the SD card) and a zip of it.
set -e
. "$(dirname "$0")/handheld.sh"
OUT="${1:-$ROOT/build-miaui}"

CC=arm-linux-gnueabihf-gcc
CXX=arm-linux-gnueabihf-g++
ARCH="-marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard"
TARGET_DEFS="-DGBRT_PLATFORM_MIAUI -DGBRT_DISPLAY_HZ=60.46 -DGBRT_SCREEN_W=640 -DGBRT_SCREEN_H=480"
TARGET_INC="-I/usr/include/SDL2 -I/usr/include/arm-linux-gnueabihf/SDL2"
TARGET_LIBS="-lSDL2"
handheld_build "$OUT"

PAK_NAME="Links Awakening DX.pak"
PAK="$OUT/$PAK_NAME"
mkdir -p "$PAK"
cp "$OUT/obj/linksawakening.elf" "$PAK/"
cp "$ROOT/miaui/launch.sh" "$PAK/"
chmod +x "$PAK/launch.sh" "$PAK/linksawakening.elf"
( cd "$OUT" && rm -f linksawakening-miaui.zip && mkdir -p zip/Ports && rm -rf "zip/Ports/$PAK_NAME" && cp -r "$PAK_NAME" zip/Ports/ && cd zip && zip -qr ../linksawakening-miaui.zip Ports )

echo
file "$PAK/linksawakening.elf"
echo 'glibc versions needed:'
arm-linux-gnueabihf-objdump -T "$PAK/linksawakening.elf" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 3
ls -lh "$PAK" "$OUT/linksawakening-miaui.zip"
