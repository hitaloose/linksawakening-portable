/* Screen overlays for Link's Awakening DX on Linux handhelds (MiaUI, ...).
 *
 * A screen overlay is a PNG the size of the panel: an opaque bezel around a
 * transparent hole where the game shows, often with a translucent LCD grid
 * in the hole (RetroArch "perfect overlays", handheld/overlays/CREDITS.md).
 * They are offered as extra values of the options screen's GRID
 * (la_settings.c): OFF, ON, then one per PNG found in
 *   ./overlays/              next to the executable (the bundled ones)
 *   $LA_SAVE_DIR/overlays/   the player's own
 *
 * No .cfg is read: the hole is measured from the PNG. From the centre we
 * grow the run of columns that have a see-through pixel (alpha < HOLE_ALPHA), then
 * the run of rows (within those columns) the same way. It has to be the run
 * around the centre: the drkhrse GBC overlay has a stray clear column at
 * x = 0, so a box around every clear pixel would be wrong.
 *
 * The PNG is shown 1:1, centred, when it fits the panel, else scaled down
 * keeping its shape; the game picture goes in the hole (la_settings.c asks
 * la_bezel_rect), and the PNG is drawn over everything last.
 */
#include "la_bezel.h"
#include <SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define MAX_BEZELS 16
#define HOLE_ALPHA 255            /* any see-through pixel; 200 cut a grid line of drkhrse's */

typedef struct {
    char path[512];
    char file[128];
    char name[32];
} Bezel;

static Bezel s_list[MAX_BEZELS];
static int   s_count = -1;              /* -1: not scanned yet */

static int            s_sel = -1;
static unsigned char* s_pixels;         /* RGBA, until the texture is made */
static int            s_w, s_h;
static SDL_Rect       s_hole;           /* in the PNG */
static SDL_Rect       s_dst;            /* the PNG on the panel (la_bezel_rect) */
static bool           s_dst_ok;
static SDL_Texture*   s_tex;

/* ---- finding them ------------------------------------------------------- */
static bool is_png(const char* f) {
    size_t n = strlen(f);
    return n > 4 && f[0] != '.' && strcasecmp(f + n - 4, ".png") == 0;
}

static void scan_dir(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && s_count < MAX_BEZELS) {
        if (!is_png(e->d_name) || strlen(e->d_name) >= sizeof(s_list[0].file)) continue;
        if (la_bezel_find(e->d_name) >= 0) continue;     /* first folder wins */
        Bezel* b = &s_list[s_count++];
        snprintf(b->path, sizeof(b->path), "%s/%s", dir, e->d_name);
        snprintf(b->file, sizeof(b->file), "%s", e->d_name);
        size_t n = strlen(e->d_name) - 4, k = 0;
        for (size_t i = 0; i < n && k < sizeof(b->name) - 1; i++) {
            char c = e->d_name[i];
            b->name[k++] = (c == '_' || c == '.') ? ' ' : (char)toupper((unsigned char)c);
        }
        b->name[k] = 0;
    }
    closedir(d);
}

static int by_name(const void* a, const void* b) {
    return strcmp(((const Bezel*)a)->name, ((const Bezel*)b)->name);
}

static void scan(void) {
    if (s_count >= 0) return;
    s_count = 0;
    scan_dir("overlays");
    const char* sd = getenv("LA_SAVE_DIR");
    if (sd && *sd) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/overlays", sd);
        scan_dir(dir);
    }
    qsort(s_list, s_count, sizeof(Bezel), by_name);
    fprintf(stderr, "[BEZEL] %d overlay(s)\n", s_count);
}

int la_bezel_count(void) { scan(); return s_count; }

const char* la_bezel_name(int i) { scan(); return i >= 0 && i < s_count ? s_list[i].name : ""; }

const char* la_bezel_file(int i) { scan(); return i >= 0 && i < s_count ? s_list[i].file : ""; }

int la_bezel_find(const char* file) {
    if (s_count < 0) scan();
    for (int i = 0; i < s_count; i++)
        if (strcmp(s_list[i].file, file) == 0) return i;
    return -1;
}

/* ---- loading -------------------------------------------------------------- */
static bool col_clear(const unsigned char* p, int w, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++)
        if (p[((size_t)y * w + x) * 4 + 3] < HOLE_ALPHA) return true;
    return false;
}

static bool row_clear(const unsigned char* p, int w, int y, int x0, int x1) {
    for (int x = x0; x <= x1; x++)
        if (p[((size_t)y * w + x) * 4 + 3] < HOLE_ALPHA) return true;
    return false;
}

static bool find_hole(const unsigned char* p, int w, int h, SDL_Rect* out) {
    int cx = w / 2, cy = h / 2;
    if (!col_clear(p, w, cx, 0, h - 1)) return false;
    int x0 = cx, x1 = cx;
    while (x0 > 0 && col_clear(p, w, x0 - 1, 0, h - 1)) x0--;
    while (x1 < w - 1 && col_clear(p, w, x1 + 1, 0, h - 1)) x1++;
    if (!row_clear(p, w, cy, x0, x1)) return false;
    int y0 = cy, y1 = cy;
    while (y0 > 0 && row_clear(p, w, y0 - 1, x0, x1)) y0--;
    while (y1 < h - 1 && row_clear(p, w, y1 + 1, x0, x1)) y1++;
    out->x = x0; out->y = y0; out->w = x1 - x0 + 1; out->h = y1 - y0 + 1;
    return true;
}

static void unload(void) {
    if (s_tex) { SDL_DestroyTexture(s_tex); s_tex = NULL; }
    if (s_pixels) { stbi_image_free(s_pixels); s_pixels = NULL; }
    s_sel = -1;
    s_dst_ok = false;
}

bool la_bezel_select(int i) {
    scan();
    if (i == s_sel) return i < 0 || s_pixels || s_tex;
    unload();
    if (i < 0 || i >= s_count) return i < 0;
    int n;
    unsigned char* p = stbi_load(s_list[i].path, &s_w, &s_h, &n, 4);
    if (!p) {
        fprintf(stderr, "[BEZEL] can't load %s: %s\n", s_list[i].path, stbi_failure_reason());
        return false;
    }
    if (!find_hole(p, s_w, s_h, &s_hole)) {
        fprintf(stderr, "[BEZEL] %s: no transparent hole in the middle\n", s_list[i].file);
        stbi_image_free(p);
        return false;
    }
    s_pixels = p;
    s_sel = i;
    fprintf(stderr, "[BEZEL] %s %dx%d hole %d,%d,%d,%d\n", s_list[i].name, s_w, s_h,
            s_hole.x, s_hole.y, s_hole.w, s_hole.h);
    return true;
}

/* ---- showing ---------------------------------------------------------------- */
bool la_bezel_rect(int ow, int oh, int* x, int* y, int* w, int* h) {
    if (s_sel < 0 || ow <= 0 || oh <= 0) { s_dst_ok = false; return false; }
    double k = 1.0;
    if (s_w > ow || s_h > oh) {
        double kx = (double)ow / s_w, ky = (double)oh / s_h;
        k = kx < ky ? kx : ky;
    }
    s_dst.w = (int)(s_w * k + 0.5);
    s_dst.h = (int)(s_h * k + 0.5);
    s_dst.x = (ow - s_dst.w) / 2;
    s_dst.y = (oh - s_dst.h) / 2;
    s_dst_ok = true;
    *x = s_dst.x + (int)(s_hole.x * k + 0.5);
    *y = s_dst.y + (int)(s_hole.y * k + 0.5);
    *w = (int)(s_hole.w * k + 0.5);
    *h = (int)(s_hole.h * k + 0.5);
    return true;
}

void la_bezel_draw(SDL_Renderer* r) {
    if (s_sel < 0 || !s_dst_ok) return;
    if (!s_tex && s_pixels) {
        s_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, s_w, s_h);
        if (s_tex) {
            SDL_UpdateTexture(s_tex, NULL, s_pixels, s_w * 4);
            SDL_SetTextureBlendMode(s_tex, SDL_BLENDMODE_BLEND);
        } else {
            fprintf(stderr, "[BEZEL] texture: %s\n", SDL_GetError());
        }
        stbi_image_free(s_pixels);
        s_pixels = NULL;
    }
    if (!s_tex) return;
    SDL_SetTextureScaleMode(s_tex, s_dst.w == s_w && s_dst.h == s_h ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    SDL_RenderCopy(r, s_tex, NULL, &s_dst);
}
