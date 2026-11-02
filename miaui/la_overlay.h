/* Screen layer ("modern HUD") for Link's Awakening DX on MiaUI. See la_overlay.c. */
#ifndef LA_OVERLAY_H
#define LA_OVERLAY_H
#include "gbrt.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Renderer;
struct SDL_Texture;

/* Draws the frame when the game is in the world and returns true; returns
 * false elsewhere so the caller draws the plain 160x144 screen. `game` is a
 * 160x144 texture holding `fb`. */
bool la_overlay_present(struct SDL_Renderer* r, struct SDL_Texture* game, GBContext* ctx,
                        const uint32_t* fb, bool held_a, bool held_b);
void la_overlay_snap_check(GBContext* ctx, const uint32_t* fb);

#ifdef __cplusplus
}
#endif
#endif
