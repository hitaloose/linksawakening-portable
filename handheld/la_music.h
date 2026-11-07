/* Custom music and sound effects for Link's Awakening DX on Linux handhelds.
 * See la_music.c. */
#ifndef LA_MUSIC_H
#define LA_MUSIC_H
#include "gbrt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Once per frame: follows the game's music track, starts / stops the
 * replacement and picks the channels to silence. */
void la_music_frame(GBContext* ctx);

/* The sound driver's PlaySfx (bank $1F): $4006 is its entry, $401E the body
 * it jumps to. la_hook_dispatch (la_extra_buttons.c) sends both here. */
#define LA_MUSIC_HOOK_BANK      0x1F
#define LA_MUSIC_HOOK_PLAY_SFX  0x4006
#define LA_MUSIC_HOOK_PLAY_SFX2 0x401E
int la_music_sfx_hook(GBContext* ctx, uint16_t addr);

#ifdef __cplusplus
}
#endif
#endif
