/* Custom music for Link's Awakening DX on Linux handhelds. See la_music.c. */
#ifndef LA_MUSIC_H
#define LA_MUSIC_H
#include "gbrt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Once per frame: follows the game's music track, starts / stops the
 * replacement and picks the channels to silence. */
void la_music_frame(GBContext* ctx);

#ifdef __cplusplus
}
#endif
#endif
