#!/bin/bash
# Build Link's Awakening for MiaUI (Anbernic RG35XX, ARMv7 hard-float, glibc 2.37).
#
# Reuses the SDL2 backend (platform_sdl.cpp) without ImGui or multiplayer, and
# links against the libSDL2-2.0.so.0 already on MiaUI. Runs in the image from
# docker/miaui.Dockerfile (Debian bookworm, glibc 2.36, so it loads on MiaUI):
#
#   docker build -t linksawakening-miaui -f docker/miaui.Dockerfile docker
#   docker run --rm -v <repo>:/src linksawakening-miaui bash /src/scripts/build_miaui.sh /src/build-miaui
#
# Output: "<out>/Links Awakening DX.pak/" (copy into Ports/ on the SD card) and a zip of it.
set -e
. "$(dirname "$0")/env.sh"
OUT="${1:-$ROOT/build-miaui}"
CC=arm-linux-gnueabihf-gcc
CXX=arm-linux-gnueabihf-g++
ARCH="-marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard"
INC="-I$ROOT/miaui -I$GEN -I$RT/include -I$RT/src -I/usr/include/SDL2 -I/usr/include/arm-linux-gnueabihf/SDL2"
DEFS="-DGB_HAS_SDL2 -DGBRT_PLATFORM_MIAUI -D_REENTRANT $LA_DEFS"
CFLAGS="$ARCH -O2 -ffunction-sections -fdata-sections $DEFS $INC -Wall -Wno-unused-function -Wno-unused-variable"
ROMFLAGS="$ARCH ${ROM_OPT:--O1} $DEFS $INC -w"

mkdir -p "$OUT/obj"
cd "$OUT/obj"
OBJS=()
cc1()  { echo "  cc   $(basename $1)"; $CC  $CFLAGS -std=gnu11 -c "$1" -o "$2"; OBJS+=("$2"); }
cxx1() { echo "  c++  $(basename $1)"; $CXX $CFLAGS -std=gnu++17 -c "$1" -o "$2"; OBJS+=("$2"); }

echo '[1/4] Runtime + SDL2 backend'
for s in gbrt ppu audio interpreter hwtrace menu_gui_stubs asset_viewer_stubs; do
    cc1 "$RT/src/$s.c" "$s.o"
done
cxx1 "$RT/src/platform_sdl.cpp" platform_sdl.o
cc1  "$ROOT/miaui/la_extra_buttons.c" la_extra_buttons.o
cc1  "$ROOT/miaui/la_overlay.c" la_overlay.o

echo '[2/4] Entry point + ROM data'
cc1 "$LA/rom_main.c" rom_main.o
cc1 "$GEN/rom_rom.c" rom_rom.o

echo '[3/4] Recompiled game: rom.c (~105 MB, slow)'
if fresh rom.o && [ rom.o -nt "$ROOT/scripts/build_miaui.sh" ]; then echo '  rom.o is up to date (skipping)'; else
    echo "  cc   rom.c ($( echo $ROMFLAGS | grep -o -- '-O[0-9s]' ))"
    # Hook the trampoline dispatcher (miaui/la_extra_buttons.c): addresses with their bit
    # set in la_hook_map (CheckItemsToUse, dialog openers, ...) go to la_hook_dispatch
    # first, so new hooks don't need this file (and rom.o) rebuilt.
    awk 'BEGIN { done = 0 }
         /^void gb_dispatch\(GBContext\* ctx, uint16_t addr\) \{/ { print "int la_hook_dispatch(GBContext*, uint16_t); extern uint8_t la_hook_map[];"; ind = 1 }
         { print }
         ind && !done && /^        addr = ctx->pc;$/ {
             print "        if (((la_hook_map[addr >> 3] >> (addr & 7)) & 1) && la_hook_dispatch(ctx, addr)) continue;"
             done = 1
         }
         END { if (!done) { print "build_miaui: dispatcher hook anchor not found" > "/dev/stderr"; exit 1 } }' \
        "$GEN/rom.c" > rom_miaui.c
    $CC $ROMFLAGS -std=gnu11 -c rom_miaui.c -o rom.o
    rm -f rom_miaui.c
fi
OBJS+=(rom.o)

echo '[4/4] Link'
$CXX $ARCH -Wl,--gc-sections "${OBJS[@]}" -static-libstdc++ -static-libgcc \
    -lSDL2 -lpthread -lm -ldl -o linksawakening.elf

PAK_NAME="Links Awakening DX.pak"
PAK="$OUT/$PAK_NAME"
mkdir -p "$PAK"
cp linksawakening.elf "$PAK/"
cp "$ROOT/miaui/launch.sh" "$PAK/"
chmod +x "$PAK/launch.sh" "$PAK/linksawakening.elf"
( cd "$OUT" && rm -f linksawakening-miaui.zip && mkdir -p zip/Ports && rm -rf "zip/Ports/$PAK_NAME" && cp -r "$PAK_NAME" zip/Ports/ && cd zip && zip -qr ../linksawakening-miaui.zip Ports )

echo
file "$PAK/linksawakening.elf"
echo 'glibc versions needed:'
arm-linux-gnueabihf-objdump -T "$PAK/linksawakening.elf" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 3
ls -lh "$PAK" "$OUT/linksawakening-miaui.zip"
