#!/bin/bash
# Shared part of the Linux handheld builds (scripts/build_<target>.sh).
#
# Every handheld target gets the same game layer from handheld/:
#   - Switch-style controls (B sword, A talk/lift, R shield, L boots, X/Y items)
#   - the status bar relabelled Y/X, the screen at the largest 10:9 size
#   - Piece of Power / Acorn / heavy rock dialogs shown once per save
#   - MENU (SDL "guide") opens the save screen; on the title screen an options
#     screen (colour correction, scaling, quit)
#   - the battery save written to disk as soon as the game saves
# A target only says how to compile for its console and how to package it.
#
# Usage, from a build_<target>.sh (run inside its Docker image):
#
#   . "$(dirname "$0")/handheld.sh"
#   CC=...  CXX=...                 cross compilers
#   ARCH="-mcpu=..."                CPU flags, used for every file and the link
#   TARGET_DEFS="-DGBRT_PLATFORM_X -DGBRT_DISPLAY_HZ=60.0 -DGBRT_SCREEN_W=640 -DGBRT_SCREEN_H=480"
#   TARGET_INC="-I/usr/include/SDL2"
#   TARGET_LIBS="-lSDL2"
#   handheld_build "$OUT"           -> $OUT/obj/linksawakening.elf
#
# Then copy the .elf and the target's launcher (<target>/launch.sh) into the
# layout its frontend wants.
set -e
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

handheld_build() {
    local OUT="$1"
    local INC="-I$ROOT/handheld -I$GEN -I$RT/include -I$RT/src $TARGET_INC"
    local DEFS="-DGB_HAS_SDL2 -DGBRT_HANDHELD -D_REENTRANT $TARGET_DEFS $LA_DEFS"
    local CFLAGS="$ARCH -O2 -ffunction-sections -fdata-sections $DEFS $INC -Wall -Wno-unused-function -Wno-unused-variable"
    local ROMFLAGS="$ARCH ${ROM_OPT:--O1} $DEFS $INC -w"

    mkdir -p "$OUT/obj"
    cd "$OUT/obj"
    OBJS=()
    cc1()  { echo "  cc   $(basename $1)"; $CC  $CFLAGS -std=gnu11 -c "$1" -o "$2"; OBJS+=("$2"); }
    cxx1() { echo "  c++  $(basename $1)"; $CXX $CFLAGS -std=gnu++17 -c "$1" -o "$2"; OBJS+=("$2"); }

    echo '[1/4] Runtime + SDL2 backend + handheld layer'
    for s in gbrt ppu audio interpreter hwtrace menu_gui_stubs asset_viewer_stubs; do
        cc1 "$RT/src/$s.c" "$s.o"
    done
    cxx1 "$RT/src/platform_sdl.cpp" platform_sdl.o
    cc1  "$ROOT/handheld/la_extra_buttons.c" la_extra_buttons.o
    cc1  "$ROOT/handheld/la_overlay.c" la_overlay.o
    cc1  "$ROOT/handheld/la_settings.c" la_settings.o
    cc1  "$ROOT/handheld/la_lcd.c" la_lcd.o
    cc1  "$ROOT/handheld/la_music.c" la_music.o

    echo '[2/4] Entry point + ROM data'
    cc1 "$LA/rom_main.c" rom_main.o
    cc1 "$GEN/rom_rom.c" rom_rom.o

    echo '[3/4] Recompiled game: rom.c (~105 MB, slow)'
    if fresh rom.o && [ rom.o -nt "$ROOT/scripts/handheld.sh" ]; then echo '  rom.o is up to date (skipping)'; else
        echo "  cc   rom.c ($( echo $ROMFLAGS | grep -o -- '-O[0-9s]' ))"
        # Hook the trampoline dispatcher (handheld/la_extra_buttons.c): addresses with
        # their bit set in la_hook_map (CheckItemsToUse, dialog openers, ...) go to
        # la_hook_dispatch first, so new hooks don't need this file (and rom.o) rebuilt.
        awk 'BEGIN { done = 0 }
             /^void gb_dispatch\(GBContext\* ctx, uint16_t addr\) \{/ { print "int la_hook_dispatch(GBContext*, uint16_t); extern uint8_t la_hook_map[];"; ind = 1 }
             { print }
             ind && !done && /^        addr = ctx->pc;$/ {
                 print "        if (((la_hook_map[addr >> 3] >> (addr & 7)) & 1) && la_hook_dispatch(ctx, addr)) continue;"
                 done = 1
             }
             END { if (!done) { print "handheld.sh: dispatcher hook anchor not found" > "/dev/stderr"; exit 1 } }' \
            "$GEN/rom.c" > rom_hooked.c
        $CC $ROMFLAGS -std=gnu11 -c rom_hooked.c -o rom.o
        rm -f rom_hooked.c
    fi
    OBJS+=(rom.o)

    echo '[4/4] Link'
    $CXX $ARCH -Wl,--gc-sections "${OBJS[@]}" -static-libstdc++ -static-libgcc \
        $TARGET_LIBS -lpthread -lm -ldl -o linksawakening.elf
}
