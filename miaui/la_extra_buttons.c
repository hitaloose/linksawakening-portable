/* Extra item buttons for Link's Awakening DX on MiaUI: X, Y, L and R.
 *
 * Assign: open the inventory (Start), put the cursor on an item and press
 *         X, Y, L or R. Pressing it on an empty cell clears that button.
 * Use:    press the button in the world. It works like a third/fourth/...
 *         item slot: the item is used for real, at the same time as A and B
 *         (shield on L held + sword on B works).
 *
 * How: the game handles item buttons in one routine, CheckItemsToUse
 * (ROM0 $1173 in Rev 2; $1177 in zladx/LADX-Disassembly). For each slot it
 * runs boots/shield while held, UseItem on a new press and the "held" handler
 * (sword charge, bracelet). It only knows slots A and B, so after the game's
 * own call we run the routine once more per active extra button, with, for
 * that pass only, B = the extra item, A = empty and the B bits of
 * hPressedButtonsMask/hJoypadState = that button's state. Everything is put
 * back before the routine returns to its caller, so nothing else in the game
 * ever sees the change and the save can't be affected.
 *
 * The passes are chained through the trampoline dispatcher in the generated
 * rom.c (scripts/build_miaui.sh adds one line there that calls
 * la_xbtn_dispatch for $1173 and for our private return address).
 *
 * The assignments are saved to $LA_SAVE_DIR/extra_buttons.cfg.
 *
 * Also here, because they use the same dispatcher hooks:
 *  - Quieter dialogs: "You got a Piece of Power!" and "You've got a Guardian
 *    Acorn!" show the first time only, and "This looks pretty heavy" (rock,
 *    Power Bracelet not on A/B) shows once, and never while the bracelet is on
 *    X/Y/L/R (the game only checks A and B). What was already shown is kept in
 *    $DB59, the unused third byte of the death counter, inside the save data,
 *    so it follows each save file (and is reverted by quitting without saving).
 *  - MENU: in the world it holds A+B+Start+Select for a few frames, which
 *    opens the game's own save screen; on the title and file screens it quits.
 */
#include "gbrt.h"
#include "la_extra_buttons.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ROM */
#define CHECK_ITEMS_TO_USE  0x1173          /* Rev 2 */
#define XBTN_RETURN         0xFEED          /* unusable memory: never real code */
#define OPEN_DIALOG_TBL0    0x237C          /* OpenDialogInTable0 (a = dialog index) */
#define GOT_ITEM_DIALOG_RET 0x105B          /* return address of its call in ApplyGotItem */
#define OPEN_DIALOG_CLR     0x7543          /* bank 2: OpenDialogInTable0AndClearIncrement */
#define ROCK_DIALOG_RET     0x7322          /* bank 2: return address of the "heavy rock" call */
#define DIALOG_PIECE_OF_POWER 0x08
#define DIALOG_ACORN          0xEC
#define DIALOG_HEAVY_ROCK     0x8D

#define W_SEEN_DIALOGS     0xDB59  /* wDeathCount+2: saved, never used by the game */
#define SEEN_POWER  0x01
#define SEEN_ACORN  0x02
#define SEEN_ROCK   0x04
#define INV_POWER_BRACELET 0x03

/* WRAM ($Dxxx lives in bank 1 during gameplay) */
#define W_INV_B            0xDB00  /* wInventoryItems: B, A, then the 10 subscreen cells */
#define W_INV_A            0xDB01
#define INV_SLOTS          12
#define W_INV_SELECTION    0xDBA3
#define W_GAMEPLAY_TYPE    0xDB95
#define W_GAMEPLAY_SUBTYPE 0xDB96
#define W_HAS_MIRROR_SHIELD 0xC15A
#define W_USING_SHIELD     0xC15B
#define W_FREE_MOVEMENT    0xC17B  /* non-zero makes the Color Dungeon floor helper return */
#define H_PRESSED_MASK     0xFFCB  /* held */
#define H_JOYPAD_STATE     0xFFCC  /* new presses this frame */
#define H_JINGLE           0xFFF2

#define J_A 0x10
#define J_B 0x20

#define GAMEPLAY_WORLD        0x0B
#define GAMEPLAY_INVENTORY    0x0C
#define INVENTORY_INTERACTIVE 0x08
#define JINGLE_VALIDATE       0x13

static const SDL_GameControllerButton k_pad[XB_COUNT] = {
    SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
};
static const char* const k_name[XB_COUNT] = { "X", "Y", "L", "R" };

static uint8_t s_item[XB_COUNT];   /* assigned item ID, 0 = none */
static bool    s_loaded;
static int     s_enabled = -1;     /* -1 = not checked yet */
static bool    s_held[XB_COUNT];   /* sampled once per host frame */
static bool    s_held_prev[XB_COUNT]; /* as of the last real CheckItemsToUse */
static bool    s_inv_prev[XB_COUNT];  /* edge detection in the inventory */

/* Chained passes */
static bool     s_chain;
static uint16_t s_ret;                     /* the real caller's return address */
static int      s_pass;                    /* index into s_todo; -1 = the game's own pass */
static int      s_todo_n, s_todo[XB_COUNT];
static bool     s_todo_edge[XB_COUNT];
static uint8_t  s_sv_b, s_sv_a, s_sv_mask, s_sv_joy, s_sv_free, s_sv_shield, s_sv_mirror;

static uint8_t* wram(GBContext* c, uint16_t a) {
    return a < 0xD000 ? &c->wram[a - 0xC000] : &c->wram[0x1000 + (a - 0xD000)];
}
static uint8_t rd(GBContext* c, uint16_t a) { return *wram(c, a); }
static void    wr(GBContext* c, uint16_t a, uint8_t v) { *wram(c, a) = v; }

static void cfg_path(char* buf, size_t n) {
    const char* d = getenv("LA_SAVE_DIR");
    snprintf(buf, n, "%s/extra_buttons.cfg", (d && *d) ? d : ".");
}
static void cfg_load(void) {
    char p[600]; cfg_path(p, sizeof p);
    FILE* f = fopen(p, "rb");
    if (f) { if (fread(s_item, 1, XB_COUNT, f) != XB_COUNT) memset(s_item, 0, sizeof s_item); fclose(f); }
    fprintf(stderr, "[XBTN] loaded: X=%02X Y=%02X L=%02X R=%02X\n", s_item[0], s_item[1], s_item[2], s_item[3]);
}
static void cfg_save(void) {
    char p[600]; cfg_path(p, sizeof p);
    FILE* f = fopen(p, "wb");
    if (f) { fwrite(s_item, 1, XB_COUNT, f); fclose(f); }
}

uint8_t la_hook_map[0x10000 / 8];
static void hook(uint16_t a) { la_hook_map[a >> 3] |= (uint8_t)(1u << (a & 7)); }

static uint16_t stack16(GBContext* c, int off) {
    uint16_t sp = (uint16_t)(c->sp + off);
    return (uint16_t)(gb_read8(c, sp) | (gb_read8(c, (uint16_t)(sp + 1)) << 8));
}
/* Skip the routine being entered: behave as its `ret`. */
static void skip_routine(GBContext* c) {
    c->pc = stack16(c, 0);
    c->sp = (uint16_t)(c->sp + 2);
}

/* Only hook the ROM we know: CheckItemsToUse starts with ld a,[wBlockItemUsage]. */
static bool rom_ok(GBContext* c) {
    if (s_enabled < 0) {
        const uint8_t* r = c->rom;
        s_enabled = r && c->rom_size > 0x8000 + OPEN_DIALOG_CLR &&
                    r[CHECK_ITEMS_TO_USE] == 0xFA && r[CHECK_ITEMS_TO_USE + 1] == 0x0A &&
                    r[CHECK_ITEMS_TO_USE + 2] == 0xC5 &&
                    /* ApplyGotItem: ld a,[hl] / call OpenDialogInTable0 */
                    r[GOT_ITEM_DIALOG_RET - 4] == 0x7E && r[GOT_ITEM_DIALOG_RET - 3] == 0xCD &&
                    r[GOT_ITEM_DIALOG_RET - 2] == (OPEN_DIALOG_TBL0 & 0xFF) &&
                    r[GOT_ITEM_DIALOG_RET - 1] == (OPEN_DIALOG_TBL0 >> 8) &&
                    /* bank 2: call OpenDialogInTable0AndClearIncrement after ld e,$8D */
                    r[0x4000 + ROCK_DIALOG_RET - 3] == 0xCD &&
                    r[0x4000 + ROCK_DIALOG_RET - 2] == (OPEN_DIALOG_CLR & 0xFF) &&
                    r[0x4000 + ROCK_DIALOG_RET - 1] == (OPEN_DIALOG_CLR >> 8) &&
                    r[0x4000 + ROCK_DIALOG_RET - 16] == 0x1E && r[0x4000 + ROCK_DIALOG_RET - 15] == DIALOG_HEAVY_ROCK;
        if (s_enabled) { hook(CHECK_ITEMS_TO_USE); hook(XBTN_RETURN); hook(OPEN_DIALOG_TBL0); hook(OPEN_DIALOG_CLR); }
        fprintf(stderr, "[XBTN] CheckItemsToUse at $%04X: %s\n", CHECK_ITEMS_TO_USE,
                s_enabled ? "found, extra buttons on" : "NOT found (other ROM revision?), extra buttons off");
    }
    return s_enabled;
}

static bool owned(GBContext* c, uint8_t id) {
    for (int i = 0; i < INV_SLOTS; i++) if (rd(c, W_INV_B + i) == id) return true;
    return false;
}

uint8_t la_extra_buttons_item(int xb) { return (xb >= 0 && xb < XB_COUNT) ? s_item[xb] : 0; }
bool    la_extra_buttons_held(int xb) { return xb >= 0 && xb < XB_COUNT && s_held[xb]; }

static bool on_extra_button(uint8_t id) {
    for (int k = 0; k < XB_COUNT; k++) if (s_item[k] == id) return true;
    return false;
}

/* ---- MENU -> save screen ----------------------------------------------- */
#define GAMEPLAY_FILE_SCREENS_END 0x05     /* intro/title, credits, file select/new/delete/copy */
static int s_menu_frames;                  /* frames left holding A+B+Start+Select */

bool la_menu_button(GBContext* c) {
    if (!c || !c->wram) return true;
    uint8_t type = rd(c, W_GAMEPLAY_TYPE);
    if (type <= GAMEPLAY_FILE_SCREENS_END) return true;
    if (type == GAMEPLAY_WORLD) { s_menu_frames = 4; fprintf(stderr, "[XBTN] MENU: save screen\n"); }
    return false;
}

/* Once per host frame, from gb_platform_poll_events. */
void la_extra_buttons_update(GBContext* c, SDL_GameController* pad, uint8_t* gb_buttons, uint8_t* gb_dpad) {
    if (s_menu_frames > 0) {               /* exactly A+B+Start+Select, no direction */
        s_menu_frames--;
        if (gb_buttons) *gb_buttons &= 0xF0;
        if (gb_dpad) *gb_dpad = 0xFF;
    }
    if (!c || !c->wram || !rom_ok(c)) return;
    if (!s_loaded) { cfg_load(); s_loaded = true; }

    for (int i = 0; i < XB_COUNT; i++) s_held[i] = pad && SDL_GameControllerGetButton(pad, k_pad[i]);

    /* Assign in the inventory screen */
    if (rd(c, W_GAMEPLAY_TYPE) == GAMEPLAY_INVENTORY && rd(c, W_GAMEPLAY_SUBTYPE) == INVENTORY_INTERACTIVE) {
        for (int i = 0; i < XB_COUNT; i++) {
            if (s_held[i] && !s_inv_prev[i]) {
                uint8_t sel = rd(c, W_INV_SELECTION);
                if (sel < INV_SLOTS - 2) {
                    s_item[i] = rd(c, W_INV_B + 2 + sel);
                    cfg_save();
                    gb_write8(c, H_JINGLE, JINGLE_VALIDATE);
                    fprintf(stderr, "[XBTN] %s = item %02X (cursor %u)\n", k_name[i], s_item[i], sel);
                }
            }
        }
    }
    memcpy(s_inv_prev, s_held, sizeof s_held);
}

/* Set up WRAM/HRAM so that one run of CheckItemsToUse sees extra button k as B. */
static void pass_begin(GBContext* c, int k, bool edge) {
    s_sv_b = rd(c, W_INV_B);  s_sv_a = rd(c, W_INV_A);
    s_sv_mask = gb_read8(c, H_PRESSED_MASK);  s_sv_joy = gb_read8(c, H_JOYPAD_STATE);
    s_sv_free = rd(c, W_FREE_MOVEMENT);
    s_sv_shield = rd(c, W_USING_SHIELD);  s_sv_mirror = rd(c, W_HAS_MIRROR_SHIELD);

    wr(c, W_INV_B, s_item[k]);
    wr(c, W_INV_A, 0);
    gb_write8(c, H_PRESSED_MASK, (uint8_t)((s_sv_mask & ~(J_A | J_B)) | (s_held[k] ? J_B : 0)));
    gb_write8(c, H_JOYPAD_STATE, (uint8_t)((s_sv_joy & ~(J_A | J_B)) | (edge ? J_B : 0)));
    if (!s_sv_free) wr(c, W_FREE_MOVEMENT, 1);
}
static void pass_end(GBContext* c) {
    wr(c, W_INV_B, s_sv_b);  wr(c, W_INV_A, s_sv_a);
    gb_write8(c, H_PRESSED_MASK, s_sv_mask);  gb_write8(c, H_JOYPAD_STATE, s_sv_joy);
    wr(c, W_FREE_MOVEMENT, s_sv_free);
    /* Each run starts by lowering the shield; keep it up if an earlier pass raised it. */
    if (s_sv_shield) wr(c, W_USING_SHIELD, s_sv_shield);
    if (s_sv_mirror) wr(c, W_HAS_MIRROR_SHIELD, s_sv_mirror);
}

/* Dialogs shown once per save file. Returns 1 if it skipped the call. */
static int quiet_dialogs(GBContext* c, uint16_t addr) {
    uint8_t bit = 0;
    if (addr == OPEN_DIALOG_TBL0 && stack16(c, 0) == GOT_ITEM_DIALOG_RET) {
        if (c->a == DIALOG_PIECE_OF_POWER) bit = SEEN_POWER;
        else if (c->a == DIALOG_ACORN)     bit = SEEN_ACORN;
    } else if (addr == OPEN_DIALOG_CLR && c->rom_bank == 2 && stack16(c, 0) == ROCK_DIALOG_RET &&
               c->a == DIALOG_HEAVY_ROCK) {
        /* The game skips this dialog when the bracelet is on A or B; do the same for X/Y/L/R. */
        if (owned(c, INV_POWER_BRACELET) && on_extra_button(INV_POWER_BRACELET)) { skip_routine(c); return 1; }
        bit = SEEN_ROCK;
    }
    if (!bit) return 0;
    uint8_t seen = rd(c, W_SEEN_DIALOGS);
    if (seen & bit) { skip_routine(c); return 1; }
    wr(c, W_SEEN_DIALOGS, seen | bit);
    fprintf(stderr, "[XBTN] dialog %02X shown once, quiet from now on\n", c->a);
    return 0;
}

static int la_xbtn_dispatch(GBContext* c, uint16_t addr);
int la_hook_dispatch(GBContext* c, uint16_t addr) {
    if (addr == OPEN_DIALOG_TBL0 || addr == OPEN_DIALOG_CLR) return quiet_dialogs(c, addr);
    return la_xbtn_dispatch(c, addr);
}

/* From the dispatcher, for $1173 and XBTN_RETURN. Returns 1 when it set ctx->pc itself. */
static int la_xbtn_dispatch(GBContext* c, uint16_t addr) {
    if (addr == CHECK_ITEMS_TO_USE) {
        if (s_chain || !s_enabled || !s_loaded || rd(c, W_GAMEPLAY_TYPE) != GAMEPLAY_WORLD) return 0;
        s_todo_n = 0;
        for (int k = 0; k < XB_COUNT; k++) {
            bool edge = s_held[k] && !s_held_prev[k];
            s_held_prev[k] = s_held[k];
            if (s_item[k] && s_held[k] && owned(c, s_item[k])) {
                s_todo_edge[s_todo_n] = edge;
                s_todo[s_todo_n++] = k;
            }
        }
        if (!s_todo_n) return 0;
        /* Let the game's own pass run, but make it return to us. */
        s_ret = (uint16_t)(gb_read8(c, c->sp) | (gb_read8(c, (uint16_t)(c->sp + 1)) << 8));
        gb_write8(c, c->sp, XBTN_RETURN & 0xFF);
        gb_write8(c, (uint16_t)(c->sp + 1), XBTN_RETURN >> 8);
        s_chain = true;
        s_pass = -1;
        return 0;
    }
    if (addr == XBTN_RETURN && s_chain) {
        if (s_pass >= 0) pass_end(c);
        if (++s_pass < s_todo_n) {
            pass_begin(c, s_todo[s_pass], s_todo_edge[s_pass]);
            gb_push16(c, XBTN_RETURN);
            c->pc = CHECK_ITEMS_TO_USE;
            return 1;
        }
        s_chain = false;
        c->pc = s_ret;
        return 1;
    }
    return 0;
}
