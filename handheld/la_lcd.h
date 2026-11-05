/* Reflective Game Boy Color LCD look for Link's Awakening DX on Linux handhelds. See la_lcd.c. */
#ifndef LA_LCD_H
#define LA_LCD_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Renderer;
struct SDL_Rect;

/* Before the frame is drawn: builds this frame's layers from the 160x144
 * picture (render targets; call it before anything is drawn to the screen).
 * Returns false when the effect is off or can't run. */
bool la_lcd_prepare(struct SDL_Renderer* r, const uint32_t* fb);
/* After the picture is on the screen at `dst`: lays the layers over it,
 * scaled the same way (linear = sharp-bilinear path, else nearest). */
void la_lcd_draw(struct SDL_Renderer* r, const struct SDL_Rect* dst, bool linear);

#ifdef __cplusplus
}
#endif
#endif
