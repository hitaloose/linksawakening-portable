/* Screen layer for Link's Awakening DX on Linux handhelds. See la_overlay.c. */
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
struct SDL_Rect;

/* Relabels the status bar's B/A as Y/X in the 160x144 frame (before upload). */
void la_overlay_patch_fb(GBContext* ctx, uint32_t* fb);
/* Draws `game` (160x144) into `dst` sharp-bilinear; false if the renderer
 * can't, so the caller does a plain copy. */
bool la_overlay_present(struct SDL_Renderer* r, struct SDL_Texture* game, const struct SDL_Rect* dst);
/* Debug: saves the composed panel frame when /tmp/la_snap_out exists (call after drawing). */
void la_overlay_snap_out(struct SDL_Renderer* r);
void la_overlay_snap_check(GBContext* ctx, const uint32_t* fb);

#ifdef __cplusplus
}
#endif
#endif
