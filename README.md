# linksawakening-portable — handheld edition

*The Legend of Zelda: Link's Awakening DX*, statically recompiled to C and
running natively on the cheap Chinese Linux handhelds (Anbernic, Miyoo,
TrimUI and friends), with Switch-style controls and a few quality-of-life
changes. No emulator: the game's code was turned into C ahead of time from
your own ROM and compiled for the console.

> **Status: the only console done is the Anbernic RG35XX (the original "OG"
> model) running [MiaUI](https://github.com/hitaloose/MiaUI-GPU-RG35XX).**
> The game layer is shared, so other consoles and systems (GarlicOS,
> Onion OS, PortMaster, ...) only need their own build script. See
> [Adding a console](#adding-a-console).

This is a fork of
[sp00nznet/linksawakening-portable](https://github.com/sp00nznet/linksawakening-portable),
which builds the same game for PlayStation, Nintendo, Xbox, Dreamcast,
Android and the browser. Those targets are still in the tree, untouched and
untested here. Use upstream for them.

You supply your own ROM; no game code is distributed here.

## What's different

**Controls, like the 2019 Switch remake:**

| Button | Does |
|---|---|
| B | Sword. Fixed; hold it for the spin attack. |
| A | Talk, read, lift, pull. This is the Power Bracelet, with no slot of its own. |
| R / R2 | Shield, while held |
| L / L2 | Pegasus Boots, while held |
| X / Y | The two item slots. On the Game Boy these were A and B. |
| MENU | Opens the save screen in game; the options screen on the title screen |
| Start / Select | Subscreen and map, as in the original |

- In the subscreen, X and Y equip the item under the cursor. The sword,
  shield, bracelet and boots are always on their buttons, so they're shown
  there but can't be equipped.
- The status bar reads `Y[ ] X[ ]`.
- What's on X and Y is remembered for each save file.

**Quality of life:**
- "You got a Piece of Power!", "You've got a Guardian Acorn!" and "This looks
  pretty heavy" show the first time only. This is kept per save file.
- The screen is the largest 10:9 picture the panel allows (533×480 on the
  RG35XX), with sharp-bilinear scaling; the options screen also has pixel
  perfect (3×, 480×432) and full screen (640×480).
- The audio follows the panel's real refresh rate (60.46 Hz), so it doesn't pop.
- The save goes to the SD card as soon as the game saves, written atomically.

**Known limits:**
- **Only one ROM revision works:** *Link's Awakening DX (USA, Europe) (Rev 2)*,
  SHA-1 `1c091225688d966928cc74336dbef2e07d12a47c`. With any other ROM the
  game still runs, but with the original controls.

## How it works

The ROM is recompiled unchanged. The port changes the game from the outside,
in two ways:
- **Hooks:** the recompiled code goes through a dispatcher for calls and
  jumps. The build adds one line to it, so that a few chosen ROM addresses
  run our C code first (`la_hook_dispatch` in `handheld/la_extra_buttons.c`).
- **Memory:** game RAM is read and written once per frame.

At startup the port checks the bytes at every address it hooks. With another
ROM revision the layer turns itself off.

### Controls

The game has only two button slots, B (`$DB00`) and A (`$DB01`). A lot of
code reads them directly. `CheckItemsToUse` does, and so do about 26
liftable objects (pots, cuccos, statues), which look for the Power Bracelet
in a slot plus the matching button. So the port doesn't try to rewrite
those checks:

- **B and A stay real.** In the world, the B slot holds the sword and the A
  slot the bracelet, so B and A behave exactly as on the Game Boy: spin
  attack, talking, lifting, reading.
- **X, Y, R and L are extra item passes.** While one of them is held, the
  port lets the game's own `CheckItemsToUse` (`$1173`) run, then runs it
  again once per held button. For each extra run it puts that item in the B
  slot and that button's state on B (held, and newly pressed). Afterwards it
  restores everything, before the rest of the frame sees it. The shield (R)
  and boots (L) work this way too, so you can hold the shield and swing the
  sword at once.
- **Subscreen.** While it's open, the port swaps the two slots to the X and Y
  items, and physical X/Y become the game's A/B there. That way the game's
  own equip code still works, with its sounds, counts and the ocarina's song
  menu. On a fixed item the press is ignored. The swaps only exchange a slot
  with a subscreen cell, so no item is ever created or lost. The save is
  always written in the world layout, so the save file stays a normal one.
- **Status bar.** `DrawInventorySlots` (bank `$20`, `$5CA8`) draws the
  status bar. The port runs it with the X/Y items in the slots for the
  duration of the call. After the frame is drawn, the B/A letters are
  redrawn as Y/X, but only where the pixels exactly match the letter shape,
  in any two colours, so they also survive the fades.
- **Toadstool.** The witch and a few other places check the toadstool in a
  real slot together with that slot's button, so its X/Y works differently.
  While held, it takes the sword's slot for real and presses B.
- **Goriya.** He trades whatever is in the B slot, which in the world is
  the sword. When his trade routine runs (`$19:4690`), the port puts the Y
  item in that slot, or the X item if he refuses Y's. The boomerang he gives
  lands on that same button, and so does your item when you trade back.
- **Bomb arrows.** Hold the bow's button and the bombs' button together:
  both extra passes run in the same frame, which is what the game checks for.
- **MENU.** In the world it holds A+B+Start+Select for 4 frames, the game's
  own save shortcut. On the title and file screens it opens the options
  screen (game and sound paused): COLOR correction (OFF, GBC LCD as the Game
  Boy Color's screen showed it, or a lighter MODERN), ASPECT (PIXEL PERFECT
  3×, KEEP 10:9 as large as fits, or FULL 640×480), SCREEN (SMOOTH
  sharp-bilinear or PIXEL nearest; pixel perfect needs neither), LCD
  (REFLECTIVE: the GBC screen's backing showing through light pixels, with
  the shadows dark pixels cast on it), GRID (the thin lines between the
  LCD's pixels), MUSIC (CUSTOM plays your own Ogg files in place of the
  game's tracks, see [docs/MUSIC.md](docs/MUSIC.md)), RESUME and QUIT GAME
  (back to MiaUI).
  D-pad and A change things, B or MENU closes; the choices are kept in
  `Saves/LinksAwakening/settings.cfg`.

### Messages shown once

Dialogs open through `OpenDialogInTable0` (`$237C`) and, for the heavy rock,
bank 2's `$7543`. The port recognises the Piece of Power, Guardian Acorn and
heavy rock calls by their return address and dialog number. The second time,
it returns from the routine without opening the box. What was already shown
is kept in `$DB59`, an unused byte of the death counter. That byte lies
inside the area the game saves, so the setting belongs to each save file.

### Screen, audio and saves

- **Screen.** The 160×144 frame is scaled 4× with nearest neighbour into a
  640×576 render target, then copied linearly to 533×480 (or 640×480 with
  ASPECT FULL). Pixels come out even, with only the slight softening of a
  downscale. ASPECT PIXEL PERFECT draws 3× with plain nearest instead.
- **Audio.** The game is paced by the panel's vsync, at 60.46 Hz, while the
  Game Boy runs at 59.73 Hz. So the game makes about 1.2% more sound than
  the audio device plays, and samples used to be dropped, which you hear as
  pops. Now the audio is resampled by 59.73/60.46, nudged by how full the
  buffer is, so the buffer stays half full.
- **Saves.** The runtime used to copy the cartridge RAM to disk every
  5 seconds, and skipped the copy on exit while the game had that RAM
  locked. The game locks it right after every save, so a quick "save & quit"
  was lost. Now the save is written as soon as the game finishes writing
  it, and again on exit. The write goes to a temporary file, is flushed and
  then renamed over the old save, so a power cut can't corrupt it.

## Install on an RG35XX with MiaUI

You need git, [Docker](https://docs.docker.com/get-docker/) (Docker Desktop on
macOS and Windows) and your ROM. Nothing else is installed on your machine.

```sh
git clone --recursive https://github.com/hitaloose/linksawakening-portable.git
cd linksawakening-portable
./build-miaui.sh path/to/your-rom.gbc
```

The first run builds the toolchain image and compiles the game, about
10 to 20 minutes. Reruns skip what's already done.

The output is `build-miaui/linksawakening-miaui.zip`. Unzip it at the root of
the SD card; it holds `Ports/Links Awakening DX.pak/`. The game then shows up
in MiaUI under Ports.

Saves and a log of the last run are in `Saves/LinksAwakening/` on the card.

## Adding a console

Everything above, except packaging, lives in a shared handheld layer:
`handheld/`, built with `-DGBRT_HANDHELD`. A new console adds four files:

- `build-<console>.sh`
- a Docker toolchain
- `scripts/build_<console>.sh`
- a launcher

The controls and messages stay the same on every console. The how-to is in
[docs/HANDHELD.md](docs/HANDHELD.md). Pull requests for GarlicOS, Onion OS,
PortMaster and others are welcome.

```
.
├── build-miaui.sh    one command: ROM -> MiaUI package
├── handheld/         shared layer: controls, status bar, dialogs, screen
├── miaui/            MiaUI launcher
├── scripts/          handheld.sh (shared build), build_miaui.sh, recomp.sh, upstream targets
├── docker/           toolchain images
├── docs/             HANDHELD.md, and upstream's building and porting notes
└── LinksAwakening/   submodule: entry point, gen/ (your ROM's code), gbrecomp/ (runtime)
```

## Credits

- Original port and runtime: [sp00nznet/linksawakening-portable](https://github.com/sp00nznet/linksawakening-portable),
  [sp00nznet/LinksAwakening](https://github.com/sp00nznet/LinksAwakening)
- Recompiler: [arcanite24/gb-recompiled](https://github.com/arcanite24/gb-recompiled),
  with sp00nznet's and this fork's changes in
  [hitaloose/gb-recompiled](https://github.com/hitaloose/gb-recompiled)
- Game knowledge: the [LADX-Disassembly](https://github.com/zladx/LADX-Disassembly) contributors
- [SDL2](https://github.com/libsdl-org/SDL), and the MiaUI / MinUI projects

## License

The code in this repository is [MIT](LICENSE).

That covers the build scripts, packaging and documentation. It does not and
cannot cover *The Legend of Zelda: Link's Awakening DX*. The game's code, art,
audio and data belong to Nintendo, are not distributed here, and you need your
own legitimate copy. The C generated from your ROM is a derivative of the game
and is never committed. Don't share built packages either: they contain it.
This project is not affiliated with or endorsed by Nintendo.
