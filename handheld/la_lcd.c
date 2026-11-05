/* Reflective Game Boy Color LCD look for Link's Awakening DX on Linux handhelds.
 *
 * The GBC's screen has no backlight: light goes through the liquid crystal and
 * comes back from a reflective backing behind it. Where a pixel is light you
 * see that backing through it (a slightly lilac, grainy surface), and the dark
 * pixels cast a soft shadow on it, a little down and to the right, because
 * the backing sits behind the glass. The whole picture is also tinted by the
 * front polarizer. Inspired by mattakins' "Pixel Transparency" RetroArch
 * shader; this is our own implementation of the idea for SDL2 without shaders:
 *
 *   per pixel   a = 0.533 * luma            how much backing shows through
 *               S = 1 - luma, 3x3 blurred    the shadow a pixel casts
 *   out = lcd * (1 - a) + backing * shade(S moved 1 pixel down-right) * a
 *   out *= polarizer (0.94, 1.00, 0.865)
 *
 * All of it is plain SDL blending, so the GPU does the work at panel size and
 * the CPU only fills three small textures a frame (160x144 and 162x146):
 *   s_keep   (1 - a), copied 4x nearest into the target s_t_keep
 *   s_paper  the backing, generated once at 640x576, into the target s_t_back,
 *            then s_shadow (linear, offset) and s_alpha (a) multiplied in
 *   screen:  picture, then s_t_keep multiplied, s_t_back added, polarizer multiplied.
 * The 640x576 targets are scaled to the panel the same way as the picture
 * (linear after a 4x nearest = sharp-bilinear, or nearest), so they line up.
 *
 * GRID (independent of the above): the thin dark lines between the LCD's
 * pixels, a texture the size of the picture on the panel, multiplied over it.
 * Each panel pixel is darkened by how much of it a line covers (the line is
 * at the right / bottom edge of every Game Boy pixel, 1/n of it wide, n =
 * panel pixels per Game Boy pixel rounded): with PIXEL PERFECT (3x) that is
 * exactly every third column and row; with a fractional scale (KEEP is 3.33x)
 * the lines land between panel pixels and come out soft instead of uneven.
 */
#include "la_lcd.h"
#include "la_settings.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define GBW 160
#define GBH 144
#define K   4                      /* the targets are the picture at 4x */
#define TW  (GBW * K)
#define TH  (GBH * K)

static SDL_Texture *s_keep, *s_alpha, *s_shadow, *s_paper;
static SDL_Texture *s_t_keep, *s_t_back;
static bool s_failed, s_ready;
static SDL_Texture* s_grid;
static int s_grid_w, s_grid_h;
#define GRID_DARK 0.72f            /* a line lets this much light through */

/* the backing's colour (light lilac) */
static const float k_back[3] = { 0.675f, 0.643f, 0.672f };
#define BACK_LEVEL 0.48f           /* brightness of the backing before its tint */
#define ALPHA_K    0.533f          /* a = ALPHA_K * luma: white lets 53% of the backing through */
#define SHADOW_OP  0.5f            /* shadow opacity */

/* ---- textures made once ------------------------------------------------------ */
static uint32_t hash2(uint32_t x, uint32_t y) {
    uint32_t h = x * 0x8DA6B343u ^ y * 0xD8163841u ^ 0x5BD1E995u;
    h ^= h >> 13; h *= 0x5BD1E995u; h ^= h >> 15;
    return h;
}

static float value_noise(float x, float y, uint32_t seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float v00 = (hash2(xi + seed, yi) & 0xFFFF) / 65535.0f, v10 = (hash2(xi + 1 + seed, yi) & 0xFFFF) / 65535.0f;
    float v01 = (hash2(xi + seed, yi + 1) & 0xFFFF) / 65535.0f, v11 = (hash2(xi + 1 + seed, yi + 1) & 0xFFFF) / 65535.0f;
    return (v00 + (v10 - v00) * fx) + ((v01 + (v11 - v01) * fx) - (v00 + (v10 - v00) * fx)) * fy;
}

static int to8(float v) { return v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5f); }

/* The backing: grain in three octaves, then its lilac tint
 * (tint = backing colour + 2 * level - 1, so the grain shows in all channels). */
static bool make_paper(SDL_Renderer* r) {
    s_paper = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, TW, TH);
    if (!s_paper) return false;
    static uint32_t px[TW * TH];
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            float g = value_noise(x * 0.5f, y * 0.5f, 11) * 0.5f + value_noise(x * 1.0f, y * 1.0f, 23) * 0.25f +
                      value_noise(x * 2.0f, y * 2.0f, 37) * 0.125f;          /* 0 .. 0.875 */
            float lvl = BACK_LEVEL + (g - 0.4375f) * 0.065f;
            int c[3];
            for (int k = 0; k < 3; k++) c[k] = to8(k_back[k] + 2 * lvl - 1);
            px[y * TW + x] = 0xFF000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
        }
    SDL_UpdateTexture(s_paper, NULL, px, TW * 4);
    SDL_SetTextureBlendMode(s_paper, SDL_BLENDMODE_NONE);
    return true;
}

/* How much a shadow of strength s (0 .. SHADOW_OP) darkens the tinted backing,
 * per channel: the shadow darkens the level before the tint. */
static uint8_t s_shade[256][3];
static void make_shade_lut(void) {
    for (int i = 0; i < 256; i++) {
        float s = i / 255.0f * SHADOW_OP;
        float dz = 0.1f * SHADOW_OP;                                  /* faint shadows fade out */
        float t = s / dz; if (t > 1) t = 1;
        s *= t * t * (3 - 2 * t);
        for (int k = 0; k < 3; k++) {
            float lit = k_back[k] + 2 * BACK_LEVEL - 1;
            float dark = k_back[k] + 2 * BACK_LEVEL * (1 - 0.8f * s) - 1;
            s_shade[i][k] = (uint8_t)to8(dark > 0 ? dark / lit : 0);
        }
    }
}

static SDL_Texture* make_stream(SDL_Renderer* r, int w, int h, SDL_BlendMode mode, SDL_ScaleMode scale) {
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (t) { SDL_SetTextureBlendMode(t, mode); SDL_SetTextureScaleMode(t, scale); }
    return t;
}

static SDL_Texture* make_target(SDL_Renderer* r) {
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, TW, TH);
    return t;
}

static bool setup(SDL_Renderer* r) {
    if (s_ready) return true;
    if (s_failed) return false;
    s_keep = make_stream(r, GBW, GBH, SDL_BLENDMODE_NONE, SDL_ScaleModeNearest);
    s_alpha = make_stream(r, GBW, GBH, SDL_BLENDMODE_MOD, SDL_ScaleModeNearest);
    s_shadow = make_stream(r, GBW + 2, GBH + 2, SDL_BLENDMODE_MOD, SDL_ScaleModeLinear);
    s_t_keep = make_target(r);
    s_t_back = make_target(r);
    if (!s_keep || !s_alpha || !s_shadow || !s_t_keep || !s_t_back || !make_paper(r)) {
        fprintf(stderr, "[LCD] can't set up (%s): effect off\n", SDL_GetError());
        s_failed = true;
        return false;
    }
    SDL_SetTextureBlendMode(s_t_keep, SDL_BLENDMODE_MOD);
    SDL_SetTextureBlendMode(s_t_back, SDL_BLENDMODE_ADD);
    make_shade_lut();
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(r, &info) == 0) fprintf(stderr, "[LCD] reflective LCD on (%s)\n", info.name);
    s_ready = true;
    return true;
}

/* ---- per frame ----------------------------------------------------------------- */
static bool s_drawn;               /* this frame's layers are ready */

bool la_lcd_prepare(SDL_Renderer* r, const uint32_t* fb) {
    s_drawn = false;
    if (!la_settings_lcd() || !fb || !setup(r)) return false;

    static uint8_t luma[GBH][GBW];
    static uint8_t dark[GBH + 4][GBW + 4];   /* 1 - luma, with a 2-pixel border of full dark (the bezel) */
    for (int y = 0; y < GBH; y++)
        for (int x = 0; x < GBW; x++) {
            uint32_t c = fb[y * GBW + x];
            int l = (54 * ((c >> 16) & 255) + 183 * ((c >> 8) & 255) + 19 * (c & 255)) >> 8;   /* BT.709 */
            luma[y][x] = (uint8_t)l;
        }
    memset(dark, 255, sizeof(dark));
    for (int y = 0; y < GBH; y++)
        for (int x = 0; x < GBW; x++) dark[y + 2][x + 2] = (uint8_t)(255 - luma[y][x]);

    void* p; int pitch;
    /* (1 - a) and a */
    if (SDL_LockTexture(s_keep, NULL, &p, &pitch) == 0) {
        for (int y = 0; y < GBH; y++) {
            uint32_t* row = (uint32_t*)((uint8_t*)p + y * pitch);
            for (int x = 0; x < GBW; x++) {
                uint32_t v = 255 - (uint32_t)(luma[y][x] * ALPHA_K + 0.5f);
                row[x] = 0xFF000000u | v << 16 | v << 8 | v;
            }
        }
        SDL_UnlockTexture(s_keep);
    }
    if (SDL_LockTexture(s_alpha, NULL, &p, &pitch) == 0) {
        for (int y = 0; y < GBH; y++) {
            uint32_t* row = (uint32_t*)((uint8_t*)p + y * pitch);
            for (int x = 0; x < GBW; x++) {
                uint32_t v = (uint32_t)(luma[y][x] * ALPHA_K + 0.5f);
                row[x] = 0xFF000000u | v << 16 | v << 8 | v;
            }
        }
        SDL_UnlockTexture(s_alpha);
    }
    /* the shadow: cell (i, j) is picture pixel (i - 1, j - 1), blurred 3x3 (4/2/1) */
    if (SDL_LockTexture(s_shadow, NULL, &p, &pitch) == 0) {
        for (int j = 0; j < GBH + 2; j++) {
            uint32_t* row = (uint32_t*)((uint8_t*)p + j * pitch);
            for (int i = 0; i < GBW + 2; i++) {
                const int cx = i + 1, cy = j + 1;    /* in `dark` */
                int s = 4 * dark[cy][cx] +
                        2 * (dark[cy - 1][cx] + dark[cy + 1][cx] + dark[cy][cx - 1] + dark[cy][cx + 1]) +
                        dark[cy - 1][cx - 1] + dark[cy - 1][cx + 1] + dark[cy + 1][cx - 1] + dark[cy + 1][cx + 1];
                const uint8_t* f = s_shade[s >> 4];
                row[i] = 0xFF000000u | (uint32_t)f[0] << 16 | (uint32_t)f[1] << 8 | f[2];
            }
        }
        SDL_UnlockTexture(s_shadow);
    }

    if (SDL_SetRenderTarget(r, s_t_keep) != 0) {
        fprintf(stderr, "[LCD] SetRenderTarget failed (%s): effect off\n", SDL_GetError());
        s_failed = true; s_ready = false;
        return false;
    }
    SDL_RenderCopy(r, s_keep, NULL, NULL);
    SDL_SetRenderTarget(r, s_t_back);
    SDL_RenderCopy(r, s_paper, NULL, NULL);
    /* cell i covers picture pixel i-1; one pixel (K) down-right: cell i starts at i*K */
    SDL_Rect sh = { 0, 0, (GBW + 2) * K, (GBH + 2) * K };
    SDL_RenderCopy(r, s_shadow, NULL, &sh);
    SDL_RenderCopy(r, s_alpha, NULL, NULL);
    SDL_SetRenderTarget(r, NULL);
    s_drawn = true;
    return true;
}

/* ---- grid ----------------------------------------------------------------------- */
/* How much of panel pixel p (of `out` across `gb` Game Boy pixels) the lines cover. */
static float line_cover(int p, int out, int gb) {
    float k = (float)out / gb;               /* panel pixels per GB pixel */
    int n = (int)(k + 0.5f); if (n < 1) n = 1;
    float lw = 1.0f / n;                     /* line width, in GB pixels */
    float a = p / k, b = (p + 1) / k, cover = 0;   /* this panel pixel, in GB pixels */
    for (int c = (int)a; c <= (int)b + 1; c++) { /* the line at the end of GB pixel c: [c+1-lw, c+1] */
        float lo = c + 1 - lw, hi = (float)(c + 1);
        float o = (b < hi ? b : hi) - (a > lo ? a : lo);
        if (o > 0) cover += o;
    }
    cover *= k;                              /* fraction of the panel pixel */
    return cover > 1 ? 1 : cover;
}

static void draw_grid(SDL_Renderer* r, const SDL_Rect* dst) {
    if (!dst || dst->w <= 0 || dst->h <= 0) return;
    if (!s_grid || s_grid_w != dst->w || s_grid_h != dst->h) {
        if (s_grid) SDL_DestroyTexture(s_grid);
        s_grid = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, dst->w, dst->h);
        if (!s_grid) return;
        s_grid_w = dst->w; s_grid_h = dst->h;
        float* cx = (float*)malloc(sizeof(float) * dst->w);
        uint32_t* px = (uint32_t*)malloc(sizeof(uint32_t) * dst->w * dst->h);
        if (!cx || !px) { free(cx); free(px); SDL_DestroyTexture(s_grid); s_grid = NULL; return; }
        for (int x = 0; x < dst->w; x++) cx[x] = 1 - (1 - GRID_DARK) * line_cover(x, dst->w, GBW);
        for (int y = 0; y < dst->h; y++) {
            float fy = 1 - (1 - GRID_DARK) * line_cover(y, dst->h, GBH);
            for (int x = 0; x < dst->w; x++) {
                uint32_t v = (uint32_t)to8(cx[x] * fy);
                px[y * dst->w + x] = 0xFF000000u | v << 16 | v << 8 | v;
            }
        }
        SDL_UpdateTexture(s_grid, NULL, px, dst->w * 4);
        free(cx); free(px);
        SDL_SetTextureBlendMode(s_grid, SDL_BLENDMODE_MOD);
        SDL_SetTextureScaleMode(s_grid, SDL_ScaleModeNearest);
        fprintf(stderr, "[LCD] grid %dx%d\n", dst->w, dst->h);
    }
    SDL_RenderCopy(r, s_grid, NULL, dst);
}

void la_lcd_draw(SDL_Renderer* r, const SDL_Rect* dst, bool linear) {
    if (la_settings_grid()) draw_grid(r, dst);
    if (!s_drawn) return;
    SDL_ScaleMode m = linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest;
    SDL_SetTextureScaleMode(s_t_keep, m);
    SDL_SetTextureScaleMode(s_t_back, m);
    SDL_RenderCopy(r, s_t_keep, NULL, dst);
    SDL_RenderCopy(r, s_t_back, NULL, dst);
    /* the front polarizer */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_MOD);
    SDL_SetRenderDrawColor(r, 240, 255, 221, 255);
    SDL_RenderFillRect(r, dst);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
}
