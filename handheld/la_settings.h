/* Options screen for Link's Awakening DX on Linux handhelds. See la_settings.c. */
#ifndef LA_SETTINGS_H
#define LA_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

union SDL_Event;

enum { LA_COLOR_OFF, LA_COLOR_GBC, LA_COLOR_MODERN, LA_COLOR_COUNT };

/* Colour correction of the 160x144 frame (ARGB8888 from the CGB's RGB555). */
void la_settings_apply_color(uint32_t* fb);
/* Screen option "PIXEL": plain nearest scaling instead of sharp-bilinear. */
bool la_settings_nearest(void);
/* Plain nearest scaling: SCREEN PIXEL, or ASPECT PIXEL PERFECT (whole multiples need no filter). */
bool la_settings_plain_scale(void);
/* Where the 160x144 picture goes on an ow x oh screen (ASPECT option). */
void la_settings_present_rect(int ow, int oh, int* x, int* y, int* w, int* h);

void la_settings_open(void);
bool la_settings_is_open(void);
/* One SDL event while open; returns true when the player chose QUIT GAME. */
bool la_settings_event(const union SDL_Event* e);
/* Draws the options box over the 160x144 frame (after colour correction). */
void la_settings_draw(uint32_t* fb);

#ifdef __cplusplus
}
#endif
#endif
