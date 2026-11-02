/* Extra item buttons (X/Y/L/R) for Link's Awakening DX on MiaUI. See la_extra_buttons.c. */
#ifndef LA_EXTRA_BUTTONS_H
#define LA_EXTRA_BUTTONS_H
#include "gbrt.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { XB_X, XB_Y, XB_L, XB_R, XB_COUNT };

struct _SDL_GameController;
void    la_extra_buttons_update(GBContext* ctx, struct _SDL_GameController* pad,
                                uint8_t* gb_buttons, uint8_t* gb_dpad);
/* MENU pressed: opens the save screen in the world; returns true when the app should quit instead. */
bool    la_menu_button(GBContext* ctx);

/* Hooks in rom.c's trampoline dispatcher: it calls la_hook_dispatch(ctx, addr)
 * for every address whose bit is set in la_hook_map (see scripts/build_miaui.sh). */
extern uint8_t la_hook_map[0x10000 / 8];
int     la_hook_dispatch(GBContext* ctx, uint16_t addr);
uint8_t la_extra_buttons_item(int xb);
bool    la_extra_buttons_held(int xb);

#ifdef __cplusplus
}
#endif
#endif
