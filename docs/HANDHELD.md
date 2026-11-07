# Linux handhelds (MiaUI, and room for GarlicOS, Onion, PortMaster, ...)

A build for small Linux handhelds with a layer of changes for playing there.
Every handheld target shares that layer; a new target only says how to compile
for its console and how to package the result.

## What every handheld build gets (`handheld/`, `-DGBRT_HANDHELD`)

**Switch-style controls** (like the 2019 remake):

| Button | Does |
|---|---|
| B | Sword (fixed; hold for the spin attack) |
| A | Talk, read, lift, pull (Power Bracelet, fixed) |
| R / R2 | Shield, while held |
| L / L2 | Pegasus Boots, while held |
| X / Y | The two item slots of the original (its A and B) |
| MENU (SDL *guide*) | Save screen in game; options screen (colour, scaling, quit) on the title screen |

- In the subscreen, X/Y equip the item under the cursor. The fixed items
  (sword, shield, bracelet, boots) are shown but can't be equipped.
- The status bar reads `Y[ ] X[ ]`.
- The X/Y choice is kept per save file in `$LA_SAVE_DIR/xy_slot<n>.cfg`.

**Quality of life:**
- "You got a Piece of Power", "Guardian Acorn", "Secret Seashell", "Small
  Key" and the rock messages ("pretty heavy", "many cracks", "weird object",
  "key hole") show once per save file.
- The screen is drawn at the largest 10:9 size the panel allows, sharp-bilinear
  (the options screen's ASPECT also offers pixel perfect and full panel;
  LCD REFLECTIVE and GRID give it the Game Boy Color screen's look,
  `handheld/la_lcd.c`).
- Audio is resampled to the panel's refresh rate, so it doesn't pop.
- The battery save is written to disk, atomically, right after the game saves.

How it works is in the header comments of `handheld/la_extra_buttons.c` and
`handheld/la_overlay.c`.

## Building

You need git, Docker and your own ROM, *Link's Awakening DX (USA, Europe)
(Rev 2)*. The hooks use that revision's addresses, so other revisions turn
the layer off. Nothing derived from the game is in this repository or in any
download: the game is recompiled from your ROM on your machine.

```sh
git clone --recursive https://github.com/hitaloose/linksawakening-portable.git
cd linksawakening-portable
./build-miaui.sh path/to/your-rom.gbc
```

## Adding a console

Take MiaUI as the model. A target `foo` is four files:

| File | What's in it |
|---|---|
| `build-foo.sh` (repo root) | A copy of `build-miaui.sh` with `TARGET=foo`. |
| `docker/foo.Dockerfile` | A toolchain whose libc is no newer than the console's. It needs cmake, ninja, python3 and a native g++ too, because `scripts/recomp.sh` runs in it. |
| `scripts/build_foo.sh` | Sets `CC`, `CXX`, `ARCH`, `TARGET_DEFS`, `TARGET_INC` and `TARGET_LIBS`, calls `handheld_build "$OUT"`, then packages `$OUT/obj/linksawakening.elf` with the launcher. |
| `foo/launch.sh` | The frontend's launcher. It sets `LA_SAVE_DIR` and runs the `.elf`. |

In `TARGET_DEFS`:

- `-DGBRT_PLATFORM_FOO`: optional, for anything only that console needs.
- `-DGBRT_DISPLAY_HZ=<panel Hz>`: optional first guess (default 60). The real rate is measured while the game runs and the audio follows it.
- `-DGBRT_SCREEN_W=` and `-DGBRT_SCREEN_H=`: the panel size.

Things to check on the console:
- **Buttons.** The layer reads SDL GameController buttons: A, B, X, Y,
  shoulders, triggers as axes, and *guide* for MENU. If the frontend doesn't
  set `SDL_GAMECONTROLLERCONFIG` for its pad, set one in `launch.sh`.
- **Video.** The renderer needs render targets for the sharp-bilinear
  scaling. Without them it falls back to plain scaling on its own.
- **SDL.** The `.elf` links SDL2 dynamically. If the console has no SDL2,
  ship one in the package and set `LD_LIBRARY_PATH` in `launch.sh`.

Don't change `handheld/` for one console's sake. If a console needs
something different, put it behind `GBRT_PLATFORM_FOO`, so every target
keeps the same controls and dialogs.

## Never commit or ship

Your ROM, `LinksAwakening/gen/` (the game translated to C, with the ROM
bytes in it), built `.elf` files or anything extracted from the ROM. Share
the build scripts; everyone builds from their own copy.
