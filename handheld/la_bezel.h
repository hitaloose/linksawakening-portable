/* Screen overlays (bezel PNGs) for Link's Awakening DX on Linux handhelds. See la_bezel.c. */
#ifndef LA_BEZEL_H
#define LA_BEZEL_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Renderer;

/* The PNGs found in ./overlays and $LA_SAVE_DIR/overlays, sorted. */
int         la_bezel_count(void);
/* For the options screen: "GBC_Mugwomp.png" -> "GBC MUGWOMP". */
const char* la_bezel_name(int i);
/* The file name, as kept in settings.cfg. */
const char* la_bezel_file(int i);
/* Index of a file name, or -1. */
int         la_bezel_find(const char* file);
/* Shows overlay i (-1: none). Returns false when it can't be loaded. */
bool        la_bezel_select(int i);
/* Where the 160x144 picture goes on an ow x oh screen: the overlay's
 * transparent hole. False when no overlay is shown. */
bool        la_bezel_rect(int ow, int oh, int* x, int* y, int* w, int* h);
/* Lays the overlay over everything (after the picture and the LCD layers). */
void        la_bezel_draw(struct SDL_Renderer* r);

#ifdef __cplusplus
}
#endif
#endif
