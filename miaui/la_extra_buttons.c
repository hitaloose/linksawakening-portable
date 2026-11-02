/* Switch-style controls for Link's Awakening DX on MiaUI.
 *
 *   B        sword (fixed)            R / R2   shield, while held
 *   A        talk, read, lift, pull   L / L2   Pegasus Boots, while held
 *            (Power Bracelet, fixed)
 *   X / Y    the two item slots of the original (A and B on the GBC)
 *
 * How: the game keeps two button slots, wInventoryItems B ($DB00) and A
 * ($DB01), and many places check them directly (CheckItemsToUse, but also
 * ~26 liftable objects that look for the Power Bracelet in a slot together
 * with the matching button). So in the world the real slots hold the sword
 * (B) and the bracelet (A) and the game handles B and A exactly as it always
 * did. The X and Y items stay in the subscreen cells and are used through
 * extra runs of CheckItemsToUse (ROM0 $1173 in Rev 2) with, for that run
 * only, B = the item and the B button = X/Y's state; same for the shield
 * (R/R2) and the boots (L/L2). Everything is put back before the routine
 * returns, so the rest of the game never sees it.
 *
 * Two slot layouts, chosen from wGameplayType every frame:
 *   world      $DB00 = sword, $DB01 = bracelet (0 when not owned yet). The
 *              save is always written in this layout.
 *   inventory  $DB01 = X item, $DB00 = Y item, so the game's own subscreen
 *              equips them (X acts as its A, Y as its B), with its sounds,
 *              counts and the ocarina song menu. A and B do nothing there,
 *              and the fixed items (sword, shield, bracelet, boots) can't be
 *              equipped: their icons are just shown.
 * Switching is only swaps between a slot and a cell, so no item is ever
 * created or lost. In the world the status bar is drawn by the game's own
 * DrawInventorySlots, which we run with the X/Y items in the slots for the
 * duration of the call; la_overlay.c relabels its B/A as Y/X.
 * X/Y are remembered per save file in $LA_SAVE_DIR/xy_slot<n>.cfg.
 *
 * Also here, because they use the same dispatcher hooks:
 *  - Quieter dialogs: "You got a Piece of Power!" and "You've got a Guardian
 *    Acorn!" show the first time only, and "This looks pretty heavy" (rock,
 *    no Power Bracelet in a slot) shows once. What was already shown is kept
 *    in $DB59, the unused third byte of the death counter, inside the save
 *    data, so it follows each save file.
 *  - MENU: in the world it holds A+B+Start+Select for a few frames, which
 *    opens the game's own save screen; on the title and file screens it quits.
 *
 * The routines are reached through the trampoline dispatcher in the
 * generated rom.c (scripts/build_miaui.sh adds one line there that calls
 * la_hook_dispatch for the addresses set in la_hook_map).
 */
#include "gbrt.h"
#include "la_extra_buttons.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ROM (Rev 2) */
#define CHECK_ITEMS_TO_USE  0x1173          /* $1177 in zladx/LADX-Disassembly */
#define DRAW_INV_SLOTS      0x5CA8          /* bank $20: DrawInventorySlots */
#define DRAW_INV_SLOTS_BANK 0x20
#define XBTN_RETURN         0xFEED          /* unusable memory: never real code */
#define DRAW_RETURN         0xFEEE
#define OPEN_DIALOG_TBL0    0x237C          /* OpenDialogInTable0 (a = dialog index) */
#define GOT_ITEM_DIALOG_RET 0x105B          /* return address of its call in ApplyGotItem */
#define OPEN_DIALOG_CLR     0x7543          /* bank 2: OpenDialogInTable0AndClearIncrement */
#define ROCK_DIALOG_RET     0x7322          /* bank 2: return address of the "heavy rock" call */
#define DIALOG_PIECE_OF_POWER 0x08
#define DIALOG_ACORN          0xEC
#define DIALOG_HEAVY_ROCK     0x8D

/* WRAM ($Dxxx lives in bank 1 during gameplay) */
#define W_INV_B            0xDB00  /* wInventoryItems: B, A, then the 10 subscreen cells */
#define W_INV_A            0xDB01
#define W_INV_CELLS        0xDB02
#define INV_CELLS          10
#define INV_SLOTS          12
#define W_INV_SELECTION    0xDBA3
#define W_SAVE_SLOT        0xDBA6
#define W_GAMEPLAY_TYPE    0xDB95
#define W_HAS_MIRROR_SHIELD 0xC15A
#define W_USING_SHIELD     0xC15B
#define W_FREE_MOVEMENT    0xC17B  /* non-zero makes the Color Dungeon floor helper return */
#define H_PRESSED_MASK     0xFFCB  /* held */
#define H_JOYPAD_STATE     0xFFCC  /* new presses this frame */
#define W_SEEN_DIALOGS     0xDB59  /* wDeathCount+2: saved, never used by the game */
#define SEEN_POWER  0x01
#define SEEN_ACORN  0x02
#define SEEN_ROCK   0x04

#define J_A 0x10
#define J_B 0x20

#define GAMEPLAY_FILE_SCREENS_END 0x05     /* intro/title, credits, file select/new/delete/copy */
#define GAMEPLAY_FILE_SAVE    0x06         /* from here on a save file is loaded */
#define GAMEPLAY_WORLD        0x0B
#define GAMEPLAY_INVENTORY    0x0C

/* Items */
#define INV_SWORD          0x01
#define INV_POWER_BRACELET 0x03
#define INV_SHIELD         0x04
#define INV_PEGASUS_BOOTS  0x08
#define INV_MAGIC_POWDER   0x0C
#define W_POWDER_COUNT     0xDB4C  /* BCD */
#define W_HAS_TOADSTOOL    0xDB4B  /* the toadstool is item 0C too, with no powder count */

/* Pad, logical: the two item slots and the two fixed held items */
enum { PB_X, PB_Y, PB_SHIELD, PB_BOOTS, PB_COUNT };

static int     s_enabled = -1;     /* -1 = not checked yet */
static uint8_t s_xy[2];            /* X and Y items, 0 = none */
static int     s_cfg_slot = -1;    /* save slot s_xy belongs to */
static bool    s_menu_layout;      /* slots currently in the inventory layout */
static bool    s_held[PB_COUNT];   /* sampled once per host frame */
static bool    s_held_prev[PB_COUNT]; /* as of the last real CheckItemsToUse */
static bool    s_toad_press;       /* the toadstool's X/Y is held: it's in the real B slot */

/* Chained passes */
static bool     s_chain;
static uint16_t s_ret;                     /* the real caller's return address */
static int      s_pass;                    /* index into s_todo; -1 = the game's own pass */
static int      s_todo_n;
static uint8_t  s_todo_item[PB_COUNT];
static bool     s_todo_edge[PB_COUNT];
static uint8_t  s_sv_b, s_sv_a, s_sv_mask, s_sv_joy, s_sv_free, s_sv_shield, s_sv_mirror;

/* DrawInventorySlots with the X/Y items shown */
static bool     s_draw_active;
static uint16_t s_draw_ret;
static uint8_t  s_draw_b, s_draw_a;

static uint8_t* wram(GBContext* c, uint16_t a) {
    return a < 0xD000 ? &c->wram[a - 0xC000] : &c->wram[0x1000 + (a - 0xD000)];
}
static uint8_t rd(GBContext* c, uint16_t a) { return *wram(c, a); }
static void    wr(GBContext* c, uint16_t a, uint8_t v) { *wram(c, a) = v; }

uint8_t la_hook_map[0x10000 / 8];
static void hook(uint16_t a) { la_hook_map[a >> 3] |= (uint8_t)(1u << (a & 7)); }

static uint16_t stack16(GBContext* c, int off) {
    uint16_t sp = (uint16_t)(c->sp + off);
    return (uint16_t)(gb_read8(c, sp) | (gb_read8(c, (uint16_t)(sp + 1)) << 8));
}
static void set_stack16(GBContext* c, int off, uint16_t v) {
    uint16_t sp = (uint16_t)(c->sp + off);
    gb_write8(c, sp, v & 0xFF);
    gb_write8(c, (uint16_t)(sp + 1), v >> 8);
}
/* Skip the routine being entered: behave as its `ret`. */
static void skip_routine(GBContext* c) {
    c->pc = stack16(c, 0);
    c->sp = (uint16_t)(c->sp + 2);
}

/* ---- per-save X/Y ------------------------------------------------------- */
static void cfg_path(char* buf, size_t n, int slot) {
    const char* d = getenv("LA_SAVE_DIR");
    snprintf(buf, n, "%s/xy_slot%d.cfg", (d && *d) ? d : ".", slot);
}
static void cfg_load(int slot) {
    char p[600]; cfg_path(p, sizeof p, slot);
    memset(s_xy, 0, sizeof s_xy);
    FILE* f = fopen(p, "rb");
    if (f) { if (fread(s_xy, 1, 2, f) != 2) memset(s_xy, 0, sizeof s_xy); fclose(f); }
    s_cfg_slot = slot;
    fprintf(stderr, "[XBTN] save %d: X=%02X Y=%02X\n", slot, s_xy[0], s_xy[1]);
}
static void cfg_save(void) {
    if (s_cfg_slot < 0) return;
    char p[600]; cfg_path(p, sizeof p, s_cfg_slot);
    FILE* f = fopen(p, "wb");
    if (f) { fwrite(s_xy, 1, 2, f); fclose(f); }
}
static void set_xy(int k, uint8_t id) {
    if (s_xy[k] == id) return;
    s_xy[k] = id;
    cfg_save();
    fprintf(stderr, "[XBTN] %s = item %02X\n", k ? "Y" : "X", id);
}

/* ---- ROM check ---------------------------------------------------------- */
static bool rom_ok(GBContext* c) {
    if (s_enabled < 0) {
        const uint8_t* r = c->rom;
        const uint32_t draw = DRAW_INV_SLOTS_BANK * 0x4000u + (DRAW_INV_SLOTS - 0x4000u);
        static const uint8_t k_draw[9] = { 0xD5, 0xC5, 0x21, 0x00, 0xDB, 0x09, 0x7E, 0xE0, 0xD8 };
        s_enabled = r && c->rom_size > draw + sizeof k_draw &&
                    /* CheckItemsToUse: ld a,[wBlockItemUsage] */
                    r[CHECK_ITEMS_TO_USE] == 0xFA && r[CHECK_ITEMS_TO_USE + 1] == 0x0A &&
                    r[CHECK_ITEMS_TO_USE + 2] == 0xC5 &&
                    /* DrawInventorySlots: push de / push bc / ld hl,wInventoryItems / add hl,bc ... */
                    !memcmp(r + draw, k_draw, sizeof k_draw) &&
                    /* ApplyGotItem: ld a,[hl] / call OpenDialogInTable0 */
                    r[GOT_ITEM_DIALOG_RET - 4] == 0x7E && r[GOT_ITEM_DIALOG_RET - 3] == 0xCD &&
                    r[GOT_ITEM_DIALOG_RET - 2] == (OPEN_DIALOG_TBL0 & 0xFF) &&
                    r[GOT_ITEM_DIALOG_RET - 1] == (OPEN_DIALOG_TBL0 >> 8) &&
                    /* bank 2: call OpenDialogInTable0AndClearIncrement after ld e,$8D */
                    r[0x4000 + ROCK_DIALOG_RET - 3] == 0xCD &&
                    r[0x4000 + ROCK_DIALOG_RET - 2] == (OPEN_DIALOG_CLR & 0xFF) &&
                    r[0x4000 + ROCK_DIALOG_RET - 1] == (OPEN_DIALOG_CLR >> 8) &&
                    r[0x4000 + ROCK_DIALOG_RET - 16] == 0x1E && r[0x4000 + ROCK_DIALOG_RET - 15] == DIALOG_HEAVY_ROCK;
        if (s_enabled) {
            hook(CHECK_ITEMS_TO_USE); hook(XBTN_RETURN);
            hook(DRAW_INV_SLOTS); hook(DRAW_RETURN);
            hook(OPEN_DIALOG_TBL0); hook(OPEN_DIALOG_CLR);
        }
        fprintf(stderr, "[XBTN] ROM: %s\n", s_enabled ? "Rev 2 found, Switch-style controls on"
                                                      : "unknown revision, controls left as they are");
    }
    return s_enabled;
}

/* ---- slots ---------------------------------------------------------------- */
static bool fixed_item(uint8_t id) {
    return id == INV_SWORD || id == INV_POWER_BRACELET || id == INV_SHIELD || id == INV_PEGASUS_BOOTS;
}
static int find_slot(GBContext* c, uint8_t id) {     /* index into wInventoryItems, -1 */
    if (!id) return -1;
    for (int i = 0; i < INV_SLOTS; i++) if (rd(c, (uint16_t)(W_INV_B + i)) == id) return i;
    return -1;
}
static int find_cell(GBContext* c, uint8_t id) {     /* index into wInventoryItems (2..11), -1 */
    for (int i = 2; i < INV_SLOTS; i++) if (rd(c, (uint16_t)(W_INV_B + i)) == id) return i;
    return -1;
}
static bool owned(GBContext* c, uint8_t id) { return find_slot(c, id) >= 0; }
static void swap_inv(GBContext* c, int i, int j) {
    uint8_t a = rd(c, (uint16_t)(W_INV_B + i)), b = rd(c, (uint16_t)(W_INV_B + j));
    wr(c, (uint16_t)(W_INV_B + i), b);
    wr(c, (uint16_t)(W_INV_B + j), a);
}
/* Put `want` (or nothing, if 0 / not in a cell) in slot `s` (0 = B, 1 = A),
 * swapping with the cell that holds it or with an empty one. */
static void put_in_slot(GBContext* c, int s, uint8_t want) {
    uint8_t cur = rd(c, (uint16_t)(W_INV_B + s));
    if (cur == want) return;
    int from = want ? find_cell(c, want) : -1;
    if (from < 0) {
        if (want && find_slot(c, want) >= 0) return;   /* it's in the other slot: leave it */
        if (!cur) return;
        from = find_cell(c, 0);
        if (from < 0) return;             /* no empty cell (can't happen: 12 slot items max) */
    }
    swap_inv(c, s, from);
}

/* A real inventory never holds the same item twice; drop any copy (repairs
 * saves hit by an earlier bug that duplicated items into free cells). */
static void dedupe(GBContext* c) {
    for (int i = 0; i < INV_SLOTS; i++) {
        uint8_t id = rd(c, (uint16_t)(W_INV_B + i));
        if (!id) continue;
        for (int j = i + 1; j < INV_SLOTS; j++)
            if (rd(c, (uint16_t)(W_INV_B + j)) == id) {
                wr(c, (uint16_t)(W_INV_B + j), 0);
                fprintf(stderr, "[XBTN] removed a duplicate of item %02X\n", id);
            }
    }
}

/* World layout: sword in B, bracelet in A. Whatever else was there goes to a
 * cell and, if X/Y has room, onto X (from A) / Y (from B), like the game's
 * own auto-equip of a new item. */
static void world_layout(GBContext* c, bool toad_in_b) {
    uint8_t b = rd(c, W_INV_B), a = rd(c, W_INV_A);
    if (s_menu_layout) {                  /* leaving the inventory: its slots are X and Y */
        set_xy(0, fixed_item(a) ? 0 : a);
        set_xy(1, fixed_item(b) ? 0 : b);
        s_menu_layout = false;
    } else {
        if (a && !fixed_item(a) && !s_xy[0] && s_xy[1] != a) set_xy(0, a);
        if (b && !fixed_item(b) && !s_xy[1] && s_xy[0] != b) set_xy(1, b);
    }
    /* The game drops the powder from its slot when it runs out; do the same from X/Y's cell
     * (not the toadstool: same item, but it never has a count). */
    if (!rd(c, W_POWDER_COUNT) && !rd(c, W_HAS_TOADSTOOL)) {
        int i = find_cell(c, INV_MAGIC_POWDER);
        if (i >= 0 && (s_xy[0] == INV_MAGIC_POWDER || s_xy[1] == INV_MAGIC_POWDER)) {
            wr(c, (uint16_t)(W_INV_B + i), 0);
            fprintf(stderr, "[XBTN] magic powder used up\n");
        }
    }
    put_in_slot(c, 0, toad_in_b ? INV_MAGIC_POWDER : owned(c, INV_SWORD) ? INV_SWORD : 0);
    put_in_slot(c, 1, owned(c, INV_POWER_BRACELET) ? INV_POWER_BRACELET : 0);
}

/* Inventory layout: X item in A, Y item in B. */
static void menu_layout(GBContext* c) {
    if (s_menu_layout) return;
    /* empty both slots first, so X/Y can move in whichever slot they come from */
    put_in_slot(c, 0, 0);
    put_in_slot(c, 1, 0);
    if (owned(c, s_xy[0])) put_in_slot(c, 1, s_xy[0]);
    if (owned(c, s_xy[1])) put_in_slot(c, 0, s_xy[1]);
    s_menu_layout = true;
}

/* ---- pad ------------------------------------------------------------------ */
static bool trigger(SDL_GameController* pad, SDL_GameControllerAxis ax) {
    return SDL_GameControllerGetAxis(pad, ax) > 16000;
}

/* ---- MENU -> save screen ----------------------------------------------- */
static int s_menu_frames;                  /* frames left holding A+B+Start+Select */

bool la_menu_button(GBContext* c) {
    if (!c || !c->wram) return true;
    uint8_t type = rd(c, W_GAMEPLAY_TYPE);
    if (type <= GAMEPLAY_FILE_SCREENS_END) return true;
    if (type == GAMEPLAY_WORLD) { s_menu_frames = 4; fprintf(stderr, "[XBTN] MENU: save screen\n"); }
    return false;
}

/* Once per host frame, from gb_platform_poll_events, after the pad was read. */
void la_extra_buttons_update(GBContext* c, SDL_GameController* pad, uint8_t* gb_buttons, uint8_t* gb_dpad) {
    if (c && c->wram && rom_ok(c)) {
        s_held[PB_X] = pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X);
        s_held[PB_Y] = pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y);
        s_held[PB_SHIELD] = pad && (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ||
                                    trigger(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        s_held[PB_BOOTS]  = pad && (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ||
                                    trigger(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));

        uint8_t type = rd(c, W_GAMEPLAY_TYPE);
        /* This can run in the middle of a game frame. While the status bar is
         * being drawn or an extra pass runs, the slots hold borrowed items:
         * leave them alone until the next frame. */
        bool busy = s_draw_active || s_chain;
        if (type >= GAMEPLAY_FILE_SAVE && !busy) {   /* a save file is loaded */
            int slot = rd(c, W_SAVE_SLOT);
            if (slot != s_cfg_slot) cfg_load(slot);
            dedupe(c);
            if (type == GAMEPLAY_INVENTORY) {
                menu_layout(c);
                /* X/Y are the subscreen's A/B; A/B and the fixed items do nothing */
                uint8_t sel = rd(c, W_INV_SELECTION);
                uint8_t cell = sel < INV_CELLS ? rd(c, (uint16_t)(W_INV_CELLS + sel)) : 0;
                bool blocked = fixed_item(cell);
                if (gb_buttons) {
                    *gb_buttons |= 0x03;
                    if (s_held[PB_X] && !blocked) *gb_buttons &= (uint8_t)~0x01;
                    if (s_held[PB_Y] && !blocked) *gb_buttons &= (uint8_t)~0x02;
                }
            } else {
                /* The witch (and the toadstool's other uses) look for it in a
                 * real slot together with that slot's button, outside
                 * CheckItemsToUse: while its X/Y is held it takes the B slot
                 * (the sword's) and, from the next frame on, B is pressed. */
                bool toad = type == GAMEPLAY_WORLD && rd(c, W_HAS_TOADSTOOL) &&
                            owned(c, INV_MAGIC_POWDER) &&
                            ((s_xy[0] == INV_MAGIC_POWDER && s_held[PB_X]) ||
                             (s_xy[1] == INV_MAGIC_POWDER && s_held[PB_Y]));
                if (toad && rd(c, W_INV_B) == INV_MAGIC_POWDER && gb_buttons)
                    *gb_buttons &= (uint8_t)~0x02;
                s_toad_press = toad;
                world_layout(c, toad);
            }
        }
    }
    if (s_menu_frames > 0) {               /* exactly A+B+Start+Select, no direction */
        s_menu_frames--;
        if (gb_buttons) *gb_buttons &= 0xF0;
        if (gb_dpad) *gb_dpad = 0xFF;
    }
}

/* ---- extra passes of CheckItemsToUse ------------------------------------ */
/* Set up WRAM/HRAM so that one run of CheckItemsToUse sees `item` on B. */
static void pass_begin(GBContext* c, uint8_t item, bool edge) {
    s_sv_b = rd(c, W_INV_B);  s_sv_a = rd(c, W_INV_A);
    s_sv_mask = gb_read8(c, H_PRESSED_MASK);  s_sv_joy = gb_read8(c, H_JOYPAD_STATE);
    s_sv_free = rd(c, W_FREE_MOVEMENT);
    s_sv_shield = rd(c, W_USING_SHIELD);  s_sv_mirror = rd(c, W_HAS_MIRROR_SHIELD);

    wr(c, W_INV_B, item);
    wr(c, W_INV_A, 0);
    gb_write8(c, H_PRESSED_MASK, (uint8_t)((s_sv_mask & ~(J_A | J_B)) | J_B));
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

static int xbtn_dispatch(GBContext* c, uint16_t addr) {
    if (addr == CHECK_ITEMS_TO_USE) {
        if (s_chain || rd(c, W_GAMEPLAY_TYPE) != GAMEPLAY_WORLD || s_menu_layout) return 0;
        static const uint8_t fixed[PB_COUNT] = { 0, 0, INV_SHIELD, INV_PEGASUS_BOOTS };
        s_todo_n = 0;
        for (int k = 0; k < PB_COUNT; k++) {
            bool edge = s_held[k] && !s_held_prev[k];
            s_held_prev[k] = s_held[k];
            uint8_t item = k < 2 ? s_xy[k] : fixed[k];
            if (s_toad_press && item == INV_MAGIC_POWDER) continue;   /* pressed for real */
            if (item && s_held[k] && owned(c, item)) {
                s_todo_item[s_todo_n] = item;
                s_todo_edge[s_todo_n] = edge;
                s_todo_n++;
            }
        }
        if (!s_todo_n) return 0;
        /* Let the game's own pass run, but make it return to us. */
        s_ret = stack16(c, 0);
        set_stack16(c, 0, XBTN_RETURN);
        s_chain = true;
        s_pass = -1;
        return 0;
    }
    if (addr == XBTN_RETURN && s_chain) {
        if (s_pass >= 0) pass_end(c);
        if (++s_pass < s_todo_n) {
            pass_begin(c, s_todo_item[s_pass], s_todo_edge[s_pass]);
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

/* ---- status bar: draw the X/Y items in the slots' places ----------------- */
static int draw_dispatch(GBContext* c, uint16_t addr) {
    if (addr == DRAW_INV_SLOTS) {
        /* in the inventory the slots already hold X/Y; the routine re-enters itself with jp */
        if (s_draw_active || s_menu_layout || c->rom_bank != DRAW_INV_SLOTS_BANK) return 0;
        s_draw_b = rd(c, W_INV_B);  s_draw_a = rd(c, W_INV_A);
        wr(c, W_INV_A, owned(c, s_xy[0]) ? s_xy[0] : 0);
        wr(c, W_INV_B, owned(c, s_xy[1]) ? s_xy[1] : 0);
        s_draw_ret = stack16(c, 0);
        set_stack16(c, 0, DRAW_RETURN);
        s_draw_active = true;
        return 0;
    }
    if (addr == DRAW_RETURN && s_draw_active) {
        wr(c, W_INV_B, s_draw_b);  wr(c, W_INV_A, s_draw_a);
        s_draw_active = false;
        c->pc = s_draw_ret;
        return 1;
    }
    return 0;
}

/* ---- dialogs shown once per save file ------------------------------------ */
static int quiet_dialogs(GBContext* c, uint16_t addr) {
    uint8_t bit = 0;
    if (addr == OPEN_DIALOG_TBL0 && stack16(c, 0) == GOT_ITEM_DIALOG_RET) {
        if (c->a == DIALOG_PIECE_OF_POWER) bit = SEEN_POWER;
        else if (c->a == DIALOG_ACORN)     bit = SEEN_ACORN;
    } else if (addr == OPEN_DIALOG_CLR && c->rom_bank == 2 && stack16(c, 0) == ROCK_DIALOG_RET &&
               c->a == DIALOG_HEAVY_ROCK) {
        bit = SEEN_ROCK;
    }
    if (!bit) return 0;
    uint8_t seen = rd(c, W_SEEN_DIALOGS);
    if (seen & bit) { skip_routine(c); return 1; }
    wr(c, W_SEEN_DIALOGS, seen | bit);
    fprintf(stderr, "[XBTN] dialog %02X shown once, quiet from now on\n", c->a);
    return 0;
}

int la_hook_dispatch(GBContext* c, uint16_t addr) {
    switch (addr) {
    case OPEN_DIALOG_TBL0: case OPEN_DIALOG_CLR: return quiet_dialogs(c, addr);
    case DRAW_INV_SLOTS:   case DRAW_RETURN:     return draw_dispatch(c, addr);
    default:                                     return xbtn_dispatch(c, addr);
    }
}
