/* Screen layer for Link's Awakening DX on MiaUI ("modern HUD").
 *
 * In the world (wGameplayType = GAMEPLAY_WORLD) the game's status bar
 * (rows 128-143) is replaced by two bars of our own, so nothing covers the
 * play field (160x128, drawn 3x = 480x384 in the middle of the 640x480 panel):
 *   top     hearts (left) and rupees (right), copied live from the status bar;
 *   bottom  the six buttons in a row, L Y X B A R, each with its item at 3x;
 *           the held ones light up.
 * Both bars use the status bar's own colours and span the same width.
 * The inventory keeps the same layout: its status row (rows 0-15) feeds the
 * bars and rows 16-143 go in the play field's place, so nothing jumps when
 * it opens and the bottom bar shows what is on each button while assigning.
 * The save screen (rows 8-135 are its middle, the rest is brick wall) and
 * the world map (SELECT; full height, so it starts where the field starts
 * and covers the bottom bar) keep the top bar too. Everywhere else (title,
 * menus, cutscenes) the original 160x144 screen is shown at 3x.
 *
 * Item pictures (24x16: icon + level or count, as the game draws them) are
 * captured from the game's own screen: A and B from the status bar every
 * frame, every inventory cell while the inventory is open. They're kept in
 * $LA_SAVE_DIR/item_icons.bin. On X/Y/L/R the count (bombs, arrows, powder)
 * is drawn live from WRAM instead of the captured digits, and an item that is
 * not in the inventory (used up, or a save reloaded) is not shown.
 *
 * Debug: `adb shell touch /tmp/la_snap` saves the next 160x144 frame to
 * /tmp/la_snap.ppm (and logs the inventory and game mode);
 * `touch /tmp/la_snap_out` saves the composed 640x480 frame.
 */
#include "gbrt.h"
#include "la_overlay.h"
#include "la_extra_buttons.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GBW 160
#define GBH 144
#define FIELD_H 128            /* play field rows; 128-143 is the status bar */

#define W_INV_B            0xDB00
#define W_INV_A            0xDB01
#define W_INV_SELECTION    0xDBA3
#define W_GAMEPLAY_TYPE    0xDB95
#define W_GAMEPLAY_SUBTYPE 0xDB96
#define GAMEPLAY_FILE_SAVE    0x06
#define GAMEPLAY_WORLD_MAP    0x07
#define SAVE_MAP_DELAY1       0x02   /* FILE_SAVE / WORLD_MAP subtypes 0-1: the world fading out */
#define GAMEPLAY_WORLD        0x0B
#define GAMEPLAY_INVENTORY    0x0C
#define INVENTORY_INTERACTIVE 0x08
#define INVENTORY_DELAY1      0x02   /* from here to FADE_OUT the inventory is on screen */
#define INVENTORY_FADE_IN     0x07
#define INVENTORY_STATUS_OUT  0x0B
#define INVENTORY_FADE_OUT    0x0C

#define INV_BOMBS         0x02
#define INV_BOW           0x05
#define INV_MAGIC_POWDER  0x0C
#define W_ARROW_COUNT     0xDB45   /* BCD */
#define W_POWDER_COUNT    0xDB4C
#define W_BOMB_COUNT      0xDB4D

#define CELL_W 24
#define CELL_H 16
#define HUD_BG 0xFFFF8Cu       /* status bar background */
#define INV_BG 0xFFFFADu       /* inventory background */

/* ---- state ---------------------------------------------------------- */
static SDL_Texture* s_hud;     /* 160x16, status bar with the background keyed out */
static SDL_Texture* s_cells;   /* XB_COUNT cells side by side */
static int  s_frame;

static uint32_t s_icon[256][CELL_W * CELL_H];
static bool     s_icon_ok[256];
static bool     s_icons_loaded, s_icons_dirty;
static int      s_icons_dirty_since;
static uint8_t  s_cells_ids[XB_COUNT];
static bool     s_cells_valid;

static uint8_t rd(GBContext* c, uint16_t a) {
    return a < 0xD000 ? c->wram[a - 0xC000] : c->wram[0x1000 + (a - 0xD000)];
}

static bool owned(GBContext* c, uint8_t id) {
    if (!id) return false;
    for (int i = 0; i < 12; i++) if (rd(c, (uint16_t)(W_INV_B + i)) == id) return true;
    return false;
}
/* BCD count shown in the item's cell, or -1 */
static int item_count(GBContext* c, uint8_t id) {
    switch (id) {
    case INV_BOMBS:        return rd(c, W_BOMB_COUNT);
    case INV_BOW:          return rd(c, W_ARROW_COUNT);
    case INV_MAGIC_POWDER: return rd(c, W_POWDER_COUNT);
    default:               return -1;
    }
}

/* ---- icon cache ----------------------------------------------------- */
static void icons_path(char* b, size_t n) {
    const char* d = getenv("LA_SAVE_DIR");
    snprintf(b, n, "%s/item_icons.bin", (d && *d) ? d : ".");
}
static void icons_load(void) {
    char p[600]; icons_path(p, sizeof p);
    FILE* f = fopen(p, "rb");
    int n = 0;
    if (f) {
        uint8_t id;
        while (fread(&id, 1, 1, f) == 1 && fread(s_icon[id], sizeof s_icon[id], 1, f) == 1) { s_icon_ok[id] = true; n++; }
        fclose(f);
    }
    fprintf(stderr, "[HUD] %d item pictures loaded\n", n);
}
static void icons_save(void) {
    char p[600]; icons_path(p, sizeof p);
    FILE* f = fopen(p, "wb");
    if (!f) return;
    for (int id = 1; id < 256; id++) {
        if (!s_icon_ok[id]) continue;
        uint8_t b = (uint8_t)id;
        fwrite(&b, 1, 1, f);
        fwrite(s_icon[id], sizeof s_icon[id], 1, f);
    }
    fclose(f);
}

/* Copy the 24x16 cell at (x,y) as item `id` if the screen is settled there
 * (its top-right corner shows the expected background, i.e. no fade). */
static void capture(const uint32_t* fb, int x, int y, uint32_t bg, uint8_t id) {
    if (!id) return;
    if ((fb[y * GBW + x + CELL_W - 1] & 0xFFFFFF) != bg) return;
    uint32_t cell[CELL_W * CELL_H];
    for (int j = 0; j < CELL_H; j++)
        for (int i = 0; i < CELL_W; i++) {
            uint32_t c = fb[(y + j) * GBW + x + i] & 0xFFFFFF;
            cell[j * CELL_W + i] = (c == bg) ? 0 : (0xFF000000u | c);
        }
    if (s_icon_ok[id] && !memcmp(cell, s_icon[id], sizeof cell)) return;
    memcpy(s_icon[id], cell, sizeof cell);
    s_icon_ok[id] = true;
    if (!s_icons_dirty) s_icons_dirty_since = s_frame;
    s_icons_dirty = true;
    s_cells_valid = false;
}

static void capture_all(GBContext* c, const uint32_t* fb) {
    uint8_t type = rd(c, W_GAMEPLAY_TYPE);
    if (type == GAMEPLAY_WORLD) {
        capture(fb, 8, FIELD_H, HUD_BG, rd(c, W_INV_B));
        capture(fb, 48, FIELD_H, HUD_BG, rd(c, W_INV_A));
    } else if (type == GAMEPLAY_INVENTORY && rd(c, W_GAMEPLAY_SUBTYPE) == INVENTORY_INTERACTIVE) {
        uint8_t sel = rd(c, W_INV_SELECTION);
        for (int i = 0; i < 10; i++) {
            if (i == sel) continue;                   /* the cursor is drawn over it */
            capture(fb, 8 + (i & 1) * 32, 24 + (i >> 1) * 24, INV_BG, rd(c, (uint16_t)(W_INV_B + 2 + i)));
        }
    }
    if (s_icons_dirty && s_frame - s_icons_dirty_since > 120) { icons_save(); s_icons_dirty = false; }
}

/* ---- tiny font for the button labels (5x7) -------------------------- */
static const uint8_t k_glyph[16][7] = {
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* A */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* B */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, /* Y */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
#define GLYPH_DIGIT 6
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, /* 2 */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, /* 9 */
};
static void draw_glyph(SDL_Renderer* r, int g, int x, int y, int s) {
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 5; i++)
            if (k_glyph[g][j] & (0x10 >> i)) {
                SDL_Rect px = { x + i * s, y + j * s, s, s };
                SDL_RenderFillRect(r, &px);
            }
}

/* ---- drawing ---------------------------------------------------------- */
static bool ensure_textures(SDL_Renderer* r) {
    if (!s_hud) {
        s_hud = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, GBW, CELL_H);
        s_cells = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CELL_W * XB_COUNT, CELL_H);
        if (!s_hud || !s_cells) return false;
        SDL_SetTextureBlendMode(s_hud, SDL_BLENDMODE_BLEND);
        SDL_SetTextureBlendMode(s_cells, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(s_hud, SDL_ScaleModeNearest);
        SDL_SetTextureScaleMode(s_cells, SDL_ScaleModeNearest);
        fprintf(stderr, "[HUD] textures ready\n");
    }
    return true;
}

/* The status row starts at `row` (128 in the world, 0 in the inventory). */
static void upload_hud(const uint32_t* fb, int row) {
    void* pixels; int pitch;
    if (SDL_LockTexture(s_hud, NULL, &pixels, &pitch) != 0) return;
    uint32_t key = fb[(row + CELL_H - 1) * GBW] & 0xFFFFFF;   /* its bottom-left pixel is background */
    for (int j = 0; j < CELL_H; j++) {
        uint32_t* d = (uint32_t*)((uint8_t*)pixels + j * pitch);
        const uint32_t* s = fb + (row + j) * GBW;
        for (int i = 0; i < GBW; i++) {
            uint32_t c = s[i] & 0xFFFFFF;
            d[i] = (c == key) ? 0 : (0xFF000000u | c);
        }
    }
    SDL_UnlockTexture(s_hud);
}

static void upload_cells(void) {
    uint8_t ids[XB_COUNT];
    for (int k = 0; k < XB_COUNT; k++) ids[k] = la_extra_buttons_item(k);
    if (s_cells_valid && !memcmp(ids, s_cells_ids, sizeof ids)) return;
    void* pixels; int pitch;
    if (SDL_LockTexture(s_cells, NULL, &pixels, &pitch) != 0) return;
    for (int j = 0; j < CELL_H; j++) {
        uint32_t* d = (uint32_t*)((uint8_t*)pixels + j * pitch);
        for (int k = 0; k < XB_COUNT; k++)
            for (int i = 0; i < CELL_W; i++) {
                bool digits = i >= 8 && j >= 8 &&          /* count area, drawn live instead */
                              (ids[k] == INV_BOMBS || ids[k] == INV_BOW || ids[k] == INV_MAGIC_POWDER);
                d[k * CELL_W + i] = (ids[k] && s_icon_ok[ids[k]] && !digits) ? s_icon[ids[k]][j * CELL_W + i] : 0;
            }
    }
    SDL_UnlockTexture(s_cells);
    memcpy(s_cells_ids, ids, sizeof ids);
    s_cells_valid = true;
}

/* Layout (640x480): play field 3x in the middle, a bar above and a bar below,
 * both in the status bar's own colours so the game's black outlines show. */
#define SCALE    3
#define FIELD_X  80
#define FIELD_Y  48
#define BAR_H    (CELL_H * SCALE)          /* 48 */
#define TOP_Y    0
#define BOT_Y    (FIELD_Y + FIELD_H * SCALE) /* 432 */

static void fill(SDL_Renderer* r, int x, int y, int w, int h, uint32_t rgb) {
    SDL_Rect rc = { x, y, w, h };
    SDL_SetRenderDrawColor(r, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
    SDL_RenderFillRect(r, &rc);
}

/* One button: [label][item 3x], lit while held. glyph: 0=A 1=B 2=X 3=Y 4=L 5=R */
#define BTN_W (4 + 10 + 4 + CELL_W * SCALE + 4)   /* 94 */
static void button(SDL_Renderer* r, int x, int glyph, SDL_Texture* src, int sx, bool has, bool lit, int count) {
    fill(r, x, BOT_Y, BTN_W, BAR_H, lit ? 0xFFC040 : HUD_BG);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    draw_glyph(r, glyph, x + 4, BOT_Y + (BAR_H - 14) / 2, 2);
    if (has) {
        SDL_Rect s = { sx, 0, CELL_W, CELL_H }, d = { x + 18, BOT_Y, CELL_W * SCALE, CELL_H * SCALE };
        SDL_RenderCopy(r, src, &s, &d);
        if (count >= 0) {   /* two BCD digits in the cell's bottom-right 16x8, at 3x */
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            int dx = x + 18 + 8 * SCALE + 3, dy = BOT_Y + 8 * SCALE + 1;
            draw_glyph(r, GLYPH_DIGIT + ((count >> 4) % 10), dx, dy, 3);
            draw_glyph(r, GLYPH_DIGIT + ((count & 15) % 10), dx + 6 * 3, dy, 3);
        }
    }
}
static void xbutton(SDL_Renderer* r, GBContext* c, int x, int glyph, int xb) {
    uint8_t id = la_extra_buttons_item(xb);
    bool has = owned(c, id) && s_icon_ok[id];
    button(r, x, glyph, s_cells, xb * CELL_W, has, la_extra_buttons_held(xb), has ? item_count(c, id) : -1);
}

bool la_overlay_present(SDL_Renderer* r, SDL_Texture* game, GBContext* c, const uint32_t* fb,
                        bool held_a, bool held_b) {
    if (!c || !c->wram || !fb) return false;
    if (!s_icons_loaded) { icons_load(); s_icons_loaded = true; }
    capture_all(c, fb);
    /* Where the status row and the field are in this frame */
    uint8_t type = rd(c, W_GAMEPLAY_TYPE), sub = rd(c, W_GAMEPLAY_SUBTYPE);
    int field_row, hud_row, field_h = FIELD_H;
    bool bottom_bar = true;
    bool from_world = (type == GAMEPLAY_INVENTORY && sub < INVENTORY_DELAY1) ||
                      ((type == GAMEPLAY_FILE_SAVE || type == GAMEPLAY_WORLD_MAP) && sub < SAVE_MAP_DELAY1);
    if (type == GAMEPLAY_WORLD || from_world) {
        field_row = 0; hud_row = FIELD_H;                  /* world, or fading out to the inventory */
    } else if (type == GAMEPLAY_INVENTORY && sub <= INVENTORY_FADE_OUT) {
        field_row = CELL_H;                                /* status row on top */
        /* keep the last bars while the inventory fades in or out */
        hud_row = (sub >= INVENTORY_INTERACTIVE && sub <= INVENTORY_STATUS_OUT) ? 0 : -1;
    } else if (type == GAMEPLAY_FILE_SAVE) {
        field_row = 8; hud_row = -1;                       /* the middle of the brick wall */
    } else if (type == GAMEPLAY_WORLD_MAP) {
        field_row = 0; hud_row = -1; field_h = GBH; bottom_bar = false;
    } else {
        return false;
    }
    if (!ensure_textures(r)) return false;
    if (hud_row >= 0) upload_hud(fb, hud_row);
    upload_cells();

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    /* Play field, 3x */
    SDL_Rect src = { 0, field_row, GBW, field_h };
    SDL_Rect dst = { FIELD_X, FIELD_Y, GBW * SCALE, field_h * SCALE };
    SDL_RenderCopy(r, game, &src, &dst);

    /* Both bars span the six buttons' width */
    const int gap = 8, n = 6;
    const int bar_w = n * BTN_W + (n - 1) * gap, bar_x = (640 - bar_w) / 2;

    /* Top bar: hearts (status bar x 104-159) on the left, rupees (x 80-103) on the right */
    fill(r, bar_x, TOP_Y, bar_w, BAR_H, HUD_BG);
    SDL_Rect hs = { 104, 0, 56, CELL_H }, hd = { bar_x + 12, TOP_Y, 56 * SCALE, BAR_H };
    SDL_Rect rs = { 80, 0, 24, CELL_H },  rpd = { bar_x + bar_w - 12 - 24 * SCALE, TOP_Y, 24 * SCALE, BAR_H };
    SDL_RenderCopy(r, s_hud, &hs, &hd);
    SDL_RenderCopy(r, s_hud, &rs, &rpd);

    /* Bottom bar: L Y X B A R */
    int x = bar_x;
    if (bottom_bar) {
    xbutton(r, c, x, 4, XB_L); x += BTN_W + gap;
    xbutton(r, c, x, 3, XB_Y); x += BTN_W + gap;
    xbutton(r, c, x, 2, XB_X); x += BTN_W + gap;
    button(r, x, 1, s_hud, 8, true, held_b, -1);  x += BTN_W + gap;   /* B: status bar x 8-31 */
    button(r, x, 0, s_hud, 48, true, held_a, -1); x += BTN_W + gap;   /* A: status bar x 48-71 */
    xbutton(r, c, x, 5, XB_R);
    }

    /* Debug: `touch /tmp/la_snap_out` saves this composed 640x480 frame */
    if (s_frame % 15 == 0 && access("/tmp/la_snap_out", F_OK) == 0) {
        unlink("/tmp/la_snap_out");
        static uint32_t out[640 * 480];
        SDL_Rect all = { 0, 0, 640, 480 };
        if (SDL_RenderReadPixels(r, &all, SDL_PIXELFORMAT_ARGB8888, out, 640 * 4) == 0) {
            FILE* f = fopen("/tmp/la_snap_out.ppm", "wb");
            if (f) {
                fprintf(f, "P6\n640 480\n255\n");
                for (int i = 0; i < 640 * 480; i++) {
                    unsigned char px[3] = { (unsigned char)(out[i] >> 16), (unsigned char)(out[i] >> 8), (unsigned char)out[i] };
                    fwrite(px, 1, 3, f);
                }
                fclose(f);
            }
        } else {
            fprintf(stderr, "[HUD] RenderReadPixels failed: %s\n", SDL_GetError());
        }
    }
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
