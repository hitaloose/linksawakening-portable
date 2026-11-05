# Custom music (handhelds)

With **MUSIC: CUSTOM** on the options screen, the port plays your own Ogg
Vorbis files in place of the game's tracks (`handheld/la_music.c`).

- Put them in `Ports/Links Awakening DX.pak/music/`.
- Name each one after the game's track number, two hex digits: `05.ogg`
  replaces the overworld theme. Upper or lower case both work.
- Loop points: Vorbis comments `LOOPSTART` and `LOOPLENGTH` (or `LOOPEND`), in
  samples. Without them the whole file loops.
- Any sample rate, mono or stereo. Tracks without a file keep the Game Boy music.
- Sound effects still play: the music's channels are only silenced while no
  effect is using them.

To make a file with loop points from a WAV (sox built with Vorbis):

    sox in.wav --comment "LOOPSTART=123456" --add-comment "LOOPLENGTH=2345678" -C 5 05.ogg

## Track numbers

From the zladx disassembly (`src/constants/sfx.asm`, `MUSIC_*`).

| File | Track | File | Track |
|---|---|---|---|
| `01.ogg` | Title screen | `32.ogg` | Mr. Write's house |
| `02.ogg` | Trendy Game (minigame) | `33.ogg` | Ulrira |
| `03.ogg` | Game over | `34.ogg` | Tarin and the bees |
| `04.ogg` | Mabe Village | `35.ogg` | Mamu's frog song |
| `05.ogg` | Overworld | `36.ogg` | Monkeys building the bridge |
| `06.ogg` | Tal Tal Mountain Range | `37.ogg` | Christine's house |
| `07.ogg` | Shop | `39.ogg` | Turtle Rock entrance boss |
| `08.ogg` | Rapids raft ride | `3A.ogg` | Fisherman under the bridge |
| `09.ogg` | Mysterious Forest | `3C.ogg` | File select (Totaka) |
| `0A.ogg` | Inside a house | `3D.ogg` | Ending |
| `0B.ogg` | Animal Village | `3E.ogg` | Moblin hideout |
| `0C.ogg` | Fairy fountain | `3F.ogg` | The island disappears |
| `0D.ogg` | Title screen (no intro) | `40.ogg` | Richard's villa |
| `0E.ogg` | BowWow kidnapped | `41.ogg` | Ballad of the Wind Fish |
| `0F.ogg` | Got the sword | `42.ogg`–`47.ogg` | Ballad, as the instruments join |
| `10.ogg` | Got an item | `48.ogg` | Ghost's house |
| `11.ogg` | File select | `49.ogg` | Piece of Power active |
| `12.ogg` | Lost Woods egg maze | `4A.ogg` | Learning the ballad |
| `13.ogg` | Kanalet Castle | `4B.ogg` | Catfish's Maw |
| `14.ogg` | Tail Cave | `4C.ogg` | Angler's Tunnel opens |
| `15.ogg` | Bottle Grotto | `4D.ogg` | Marin on the beach |
| `16.ogg` | Key Cavern | `4E.ogg` | Talking with Marin on the beach |
| `17.ogg` | Angler's Tunnel | `50.ogg` | Miniboss |
| `18.ogg` | After a boss | `51.ogg` | Kanalet Castle (copy) |
| `19.ogg` | Boss | `52.ogg` | Tail Cave (copy) |
| `1A.ogg` | Title cutscene | `53.ogg` | Dream Shrine dream |
| `1B.ogg` | Got an instrument | `54.ogg` | Evil Eagle, before the fight |
| `1C.ogg` | Waking up (intro) | `55.ogg` | Flying Rooster revived |
| `1D.ogg` | Overworld, before the sword | `56.ogg` | Level 2 sword |
| `1E.ogg` | Dream Shrine, sleeping | `57.ogg` | Henhouse |
| `1F.ogg` | Southern Face Shrine | `58.ogg` | Face Shrine |
| `20.ogg` | Full Moon Cello | `59.ogg` | The Wind Fish |
| `21.ogg` | 2D underground | `5A.ogg` | Turtle Rock |
| `22.ogg` | The owl | `5B.ogg` | Eagle's Tower |
| `23.ogg` | Final boss | `5C.ogg` | Evil Eagle fight |
| `24.ogg` | Dream Shrine bed | `5D.ogg` | Final boss intro |
| `25.ogg` | Heart container | `5E.ogg` | Boss defeated |
| `26.ogg` | Cave | `5F.ogg` | Final boss defeated |
| `27.ogg` | Got a power-up | `60.ogg` | File select (Zelda name) |
| `28.ogg` | Conch Horn | `61.ogg` | Color Dungeon |
| `29.ogg` | Sea Lily's Bell | | |
| `2A.ogg` | Surf Harp | | |
| `2B.ogg` | Wind Marimba | | |
| `2C.ogg` | Coral Triangle | | |
| `2D.ogg` | Organ of Evening Calm | | |
| `2E.ogg` | Thunder Drum | | |
| `2F.ogg` | Marin singing | | |
| `30.ogg` | Manbo's Mambo | | |
| `31.ogg` | Overworld intro | | |

Unused in the game: `38`, `3B`, `4F`. To see which track plays where, the log
(`Saves/LinksAwakening/log.txt`) has a `[MUSIC] game track XX -> YY` line at
every change.
