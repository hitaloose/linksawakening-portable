/* Options screen for Link's Awakening DX on Linux handhelds (MiaUI, ...).
 *
 * MENU on the title / file screens opens it (in the world MENU still opens
 * the save screen, la_extra_buttons.c). The game is paused while it is open
 * (platform_sdl.cpp keeps presenting the frozen frame and pauses the audio).
 *
 *   COLOR   OFF      the CGB colours as they are (bright, saturated)
 *           GBC LCD  as the Game Boy Color's screen showed them: mixed
 *                    channels, softer and darker (Gambatte's formula)
 *           MODERN   a lighter touch: green leans to blue a little, same
 *                    brightness, slightly less saturated
 *   SCREEN  SMOOTH   sharp-bilinear (even pixels, la_overlay.c)
 *           PIXEL    plain nearest (crisper, pixels of uneven width)
 *   RESUME / QUIT GAME
 *
 * D-pad moves / changes, A changes or picks, B or MENU closes. Choices are
 * kept in $LA_SAVE_DIR/settings.cfg ("color=1" lines).
 *
 * Everything is drawn into the 160x144 frame before it is scaled, with a
 * 5x7 font of our own, so it looks like part of the game screen.
 */
#include "la_settings.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GBW 160
#define GBH 144

static int  s_color = LA_COLOR_OFF;
static int  s_screen;              /* 0 smooth, 1 pixel */
static bool s_loaded, s_open, s_dirty;
static int  s_sel;

/* ---- settings file ------------------------------------------------------ */
static void cfg_path(char* buf, size_t n) {
    const char* d = getenv("LA_SAVE_DIR");
    snprintf(buf, n, "%s/settings.cfg", d && *d ? d : ".");
}

static void cfg_load(void) {
    if (s_loaded) return;
    s_loaded = true;
    char path[512], line[128];
    cfg_path(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return;
    int v;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "color=%d", &v) == 1 && v >= 0 && v < LA_COLOR_COUNT) s_color = v;
        if (sscanf(line, "screen=%d", &v) == 1 && v >= 0 && v <= 1) s_screen = v;
    }
    fclose(f);
    fprintf(stderr, "[OPTIONS] loaded color=%d screen=%d\n", s_color, s_screen);
}

static void cfg_save(void) {
    char path[512], tmp[520];
    cfg_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "w");
    if (!f) { fprintf(stderr, "[OPTIONS] can't write %s\n", tmp); return; }
    fprintf(f, "color=%d\nscreen=%d\n", s_color, s_screen);
    fflush(f);
    fclose(f);
    rename(tmp, path);
    fprintf(stderr, "[OPTIONS] saved color=%d screen=%d\n", s_color, s_screen);
}

/* ---- colour correction --------------------------------------------------- */
static uint32_t* s_lut;            /* RGB555 -> ARGB8888 for s_lut_mode */
static int       s_lut_mode = -1;

static int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }

static void build_lut(int mode) {
    if (!s_lut) s_lut = (uint32_t*)malloc(32768 * sizeof(uint32_t));
    if (!s_lut) return;
    for (int i = 0; i < 32768; i++) {
        int r5 = i & 31, g5 = (i >> 5) & 31, b5 = (i >> 10) & 31;
        int r, g, b;
        if (mode == LA_COLOR_GBC) {
            /* Gambatte's GBC LCD colours (max 248) */
            r = (r5 * 13 + g5 * 2 + b5) >> 1;
            g = (g5 * 3 + b5) << 1;
            b = (r5 * 3 + g5 * 2 + b5 * 11) >> 1;
        } else {
            double R = r5 * 255.0 / 31, G = g5 * 255.0 / 31, B = b5 * 255.0 / 31;
            double nr = R, ng = (G * 3 + B) / 4, nb = B;
            double om = R > G ? (R > B ? R : B) : (G > B ? G : B);
            double nm = nr > ng ? (nr > nb ? nr : nb) : (ng > nb ? ng : nb);
            if (nm > 0) { nr *= om / nm; ng *= om / nm; nb *= om / nm; }
            double l = 0.299 * nr + 0.587 * ng + 0.114 * nb;
            r = clamp255(nr + (l - nr) * 0.12);
            g = clamp255(ng + (l - ng) * 0.12);
            b = clamp255(nb + (l - nb) * 0.12);
        }
        s_lut[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    s_lut_mode = mode;
}

void la_settings_apply_color(uint32_t* fb) {
    cfg_load();
    if (!fb || s_color == LA_COLOR_OFF) return;
    if (s_lut_mode != s_color) build_lut(s_color);
    if (!s_lut) return;
    /* the PPU expands 5 bits as (x << 3) | (x >> 2), so >> 3 gets them back */
    for (int i = 0; i < GBW * GBH; i++) {
        uint32_t c = fb[i];
        fb[i] = s_lut[((c >> 19) & 0x1F) | ((c >> 6) & 0x3E0) | ((c << 7) & 0x7C00)];
    }
}

bool la_settings_nearest(void) { cfg_load(); return s_screen == 1; }

/* ---- font: 5x7, '#' = ink ------------------------------------------------ */
typedef struct { char ch; const char* rows[7]; } Glyph;
static const Glyph k_font[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
    {':', {".....", "..#..", "..#..", ".....", "..#..", "..#..", "....."}},
};

static const Glyph* glyph(char c) {
    for (size_t i = 0; i < sizeof(k_font) / sizeof(k_font[0]); i++)
        if (k_font[i].ch == c) return &k_font[i];
    return NULL;
}

/* Text with a 1-pixel drop shadow; 6 pixels per character. */
static void text(uint32_t* fb, int x, int y, const char* s, uint32_t ink) {
    for (; *s; s++, x += 6) {
        const Glyph* g = glyph(*s);
        if (!g) continue;
        for (int pass = 0; pass < 2; pass++) {
            uint32_t c = pass ? ink : 0xFF000000u;
            int o = pass ? 0 : 1;
            for (int j = 0; j < 7; j++)
                for (int i = 0; i < 5; i++) {
                    int px = x + i + o, py = y + j + o;
                    if (g->rows[j][i] == '#' && px >= 0 && px < GBW && py >= 0 && py < GBH)
                        fb[py * GBW + px] = c;
                }
        }
    }
}

static int text_w(const char* s) { return (int)strlen(s) * 6 - 1; }

/* ---- the screen ----------------------------------------------------------- */
enum { ITEM_COLOR, ITEM_SCREEN, ITEM_RESUME, ITEM_QUIT, ITEM_COUNT };
static const char* k_color_names[LA_COLOR_COUNT] = { "OFF", "GBC LCD", "MODERN" };
static const char* k_screen_names[2] = { "SMOOTH", "PIXEL" };

void la_settings_open(void) {
    cfg_load();
    s_open = true;
    s_sel = ITEM_COLOR;
    s_dirty = false;
    fprintf(stderr, "[OPTIONS] open\n");
}

bool la_settings_is_open(void) { return s_open; }

static void close_menu(void) {
    s_open = false;
    if (s_dirty) cfg_save();
    fprintf(stderr, "[OPTIONS] closed\n");
}

static void change(int dir) {
    if (s_sel == ITEM_COLOR) {
        s_color = (s_color + dir + LA_COLOR_COUNT) % LA_COLOR_COUNT;
        s_dirty = true;
    } else if (s_sel == ITEM_SCREEN) {
        s_screen ^= 1;
        s_dirty = true;
    }
}

bool la_settings_event(const SDL_Event* e) {
    if (!s_open || e->type != SDL_CONTROLLERBUTTONDOWN) return false;
    switch (e->cbutton.button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    s_sel = (s_sel + ITEM_COUNT - 1) % ITEM_COUNT; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  s_sel = (s_sel + 1) % ITEM_COUNT; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  change(-1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: change(+1); break;
    case SDL_CONTROLLER_BUTTON_A:
        if (s_sel == ITEM_RESUME) close_menu();
        else if (s_sel == ITEM_QUIT) { close_menu(); return true; }
        else change(+1);
        break;
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_GUIDE:
        close_menu();
        break;
    default: break;
    }
    return false;
}

void la_settings_draw(uint32_t* fb) {
    if (!s_open || !fb) return;
    const int x0 = 8, y0 = 30, x1 = 152, y1 = 118;
    for (int y = y0; y < y1; y++)               /* darkened box with a thin frame */
        for (int x = x0; x < x1; x++) {
            uint32_t* p = &fb[y * GBW + x];
            bool edge = y == y0 || y == y1 - 1 || x == x0 || x == x1 - 1;
            *p = edge ? 0xFFB8B8B8u : 0xFF000000u | ((*p >> 2) & 0x3F3F3Fu);
        }
    const uint32_t white = 0xFFF8F8F8u, sel = 0xFFF8D030u, dim = 0xFFA0A0A0u;
    text(fb, (GBW - text_w("OPTIONS")) / 2, y0 + 6, "OPTIONS", white);

    static const char* labels[ITEM_COUNT] = { "COLOR", "SCREEN", "RESUME", "QUIT GAME" };
    for (int i = 0; i < ITEM_COUNT; i++) {
        int y = y0 + 22 + i * 13;
        uint32_t c = i == s_sel ? sel : white;
        if (i == s_sel) text(fb, x0 + 5, y, ">", sel);
        text(fb, x0 + 14, y, labels[i], c);
        const char* v = i == ITEM_COLOR ? k_color_names[s_color]
                      : i == ITEM_SCREEN ? k_screen_names[s_screen] : NULL;
        if (v) {                                /* "< VALUE >", centred in its column */
            const int vx0 = 72, vx1 = x1 - 6;
            text(fb, vx0, y, "<", i == s_sel ? sel : dim);
            text(fb, vx1 - 5, y, ">", i == s_sel ? sel : dim);
            text(fb, vx0 + 6 + (vx1 - vx0 - 11 - text_w(v)) / 2, y, v, c);
        }
    }
}
