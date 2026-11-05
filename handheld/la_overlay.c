/* Screen layer for Link's Awakening DX on Linux handhelds (MiaUI, ...).
 *
 * The game's own screen, by default as large as the panel allows with its 10:9
 * aspect (533x480 on a 640x480 panel, centred; ASPECT in la_settings.c picks
 * the size), scaled "sharp-bilinear": nearest 4x into a
 * render target, then a linear copy down to the panel, so pixels are even
 * without the blur of plain bilinear.
 *
 * The status bar letters follow the Switch-style controls (la_extra_buttons.c):
 * "B[" becomes "Y[" and "A[" becomes "X[", in the world (rows 128-143) and at
 * the top of the inventory (rows 0-15). A letter is replaced only when the
 * pixels there are exactly the game's "B"/"A" shape in two colours (any
 * two, so fades work too), so nothing else on screen is ever touched.
 *
 * Debug: `adb shell touch /tmp/la_snap` saves the next 160x144 frame to
 * /tmp/la_snap.ppm (and logs the inventory and game mode);
 * `touch /tmp/la_snap_out` saves the composed panel frame.
 */
#include "gbrt.h"
#include "la_overlay.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GBW 160
#define GBH 144

static int s_frame;

/* ---- status bar letters ------------------------------------------------ */
/* 4x7, bit 3 = leftmost pixel: the game's B and A, and our Y and X */
static const uint8_t k_B[7] = { 0xE, 0x9, 0x9, 0xE, 0x9, 0x9, 0xE };
static const uint8_t k_A[7] = { 0x6, 0x9, 0x9, 0xF, 0x9, 0x9, 0x9 };
static const uint8_t k_Y[7] = { 0x9, 0x9, 0x9, 0x6, 0x6, 0x6, 0x6 };
static const uint8_t k_X[7] = { 0x9, 0x9, 0x6, 0x6, 0x6, 0x9, 0x9 };

/* Exact shape match in two colours (any two, so it also works while the
 * screen fades in or out); then redraw with the same two colours. */
static void swap_letter(uint32_t* fb, int x0, int y0, const uint8_t* from, const uint8_t* to) {
    uint32_t ink = 0, bg = 0;
    bool have_ink = false, have_bg = false;
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 4; i++) {
            uint32_t c = fb[(y0 + j) * GBW + x0 + i] & 0xFFFFFF;
            if (from[j] & (8 >> i)) {
                if (!have_ink) { ink = c; have_ink = true; } else if (c != ink) return;
            } else {
                if (!have_bg) { bg = c; have_bg = true; } else if (c != bg) return;
            }
        }
    if (!have_ink || !have_bg || ink == bg) return;
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 4; i++)
            fb[(y0 + j) * GBW + x0 + i] = 0xFF000000u | ((to[j] & (8 >> i)) ? ink : bg);
}

void la_overlay_patch_fb(GBContext* ctx, uint32_t* fb) {
    (void)ctx;
    if (!fb) return;
    static const int rows[2] = { 129, 1 };   /* world status bar, inventory top */
    for (int k = 0; k < 2; k++) {
        swap_letter(fb, 1, rows[k], k_B, k_Y);
        swap_letter(fb, 41, rows[k], k_A, k_X);
    }
}

/* ---- presenting ---------------------------------------------------------- */
#define PRESCALE 4
static SDL_Texture* s_target;
static bool         s_target_failed;

void la_overlay_snap_out(SDL_Renderer* r) {
    if (s_frame % 15 || access("/tmp/la_snap_out", F_OK) != 0) return;
    unlink("/tmp/la_snap_out");
    int w = 640, h = 480;
    SDL_GetRendererOutputSize(r, &w, &h);
    uint32_t* out = (uint32_t*)malloc((size_t)w * h * 4);
    if (!out) return;
    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, out, w * 4) == 0) {
        FILE* f = fopen("/tmp/la_snap_out.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; i++) {
                unsigned char px[3] = { (unsigned char)(out[i] >> 16), (unsigned char)(out[i] >> 8), (unsigned char)out[i] };
                fwrite(px, 1, 3, f);
            }
            fclose(f);
        }
    } else {
        fprintf(stderr, "[SCREEN] RenderReadPixels failed: %s\n", SDL_GetError());
    }
    free(out);
}

bool la_overlay_present(SDL_Renderer* r, SDL_Texture* game, const SDL_Rect* dst) {
    if (s_target_failed || !dst) return false;
    if (!s_target) {
        s_target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                     GBW * PRESCALE, GBH * PRESCALE);
        if (!s_target) {
            fprintf(stderr, "[SCREEN] no render target (%s): plain scaling\n", SDL_GetError());
            s_target_failed = true;
            return false;
        }
        SDL_SetTextureScaleMode(s_target, SDL_ScaleModeLinear);
        fprintf(stderr, "[SCREEN] sharp-bilinear %dx%d -> %dx%d at %d,%d\n",
                GBW * PRESCALE, GBH * PRESCALE, dst->w, dst->h, dst->x, dst->y);
    }
    SDL_SetTextureScaleMode(game, SDL_ScaleModeNearest);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    if (SDL_SetRenderTarget(r, s_target) != 0) {
        fprintf(stderr, "[SCREEN] SetRenderTarget failed (%s): plain scaling\n", SDL_GetError());
        s_target_failed = true;
        return false;
    }
    SDL_RenderCopy(r, game, NULL, NULL);
    SDL_SetRenderTarget(r, NULL);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, s_target, NULL, dst);
    return true;
}

void la_overlay_snap_check(GBContext* ctx, const uint32_t* fb) {
    if (++s_frame % 15) return;
    if (access("/tmp/la_snap", F_OK) != 0) return;
    unlink("/tmp/la_snap");
    FILE* f = fopen("/tmp/la_snap.ppm", "wb");
    if (!f) return;
    fprintf(f, "P6\n160 144\n255\n");
    for (int i = 0; i < GBW * GBH; i++) {
        uint32_t c = fb[i];
        unsigned char px[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    if (ctx && ctx->wram) {
        const uint8_t* w1 = ctx->wram + 0x1000;   /* bank 1, $D000 */
        fprintf(stderr, "[SNAP] type=%02X sub=%02X sel=%02X inv:", w1[0xB95], w1[0xB96], w1[0xBA3]);
        for (int i = 0; i < 12; i++) fprintf(stderr, " %02X", w1[0xB00 + i]);
        fprintf(stderr, "\n");
    }
}
