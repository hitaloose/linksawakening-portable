#!/bin/sh
# MiaUI launcher for Link's Awakening DX: ROMS/Ports/Links Awakening DX.pak/
# MENU quits back to MiaUI. Saves go to Saves/LinksAwakening/.
PAK_PATH="$(cd "$(dirname "$0")" && pwd)"
cd "$PAK_PATH"

export LA_SAVE_DIR="${SAVES_PATH:-/userdata/Saves}/LinksAwakening"
mkdir -p "$LA_SAVE_DIR"

# Game Boy code is light: the game clock is plenty.
overclock.elf "${CPU_SPEED_GAME:-1296000}" 2>/dev/null

./linksawakening.elf > "$LA_SAVE_DIR/log.txt" 2>&1
