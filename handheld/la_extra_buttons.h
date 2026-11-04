/* Switch-style controls for Link's Awakening DX on Linux handhelds. See la_extra_buttons.c. */
#ifndef LA_EXTRA_BUTTONS_H
#define LA_EXTRA_BUTTONS_H
#include "gbrt.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _SDL_GameController;
/* Once per host frame, after the platform read the pad into the GB buttons. */
void    la_extra_buttons_update(GBContext* ctx, struct _SDL_GameController* pad,
                                uint8_t* gb_buttons, uint8_t* gb_dpad);
/* MENU pressed: opens the save screen in the world; returns true on the title/file
 * screens, where the platform opens the options screen (la_settings.c) instead. */
bool    la_menu_button(GBContext* ctx);

/* Hooks in rom.c's trampoline dispatcher: it calls la_hook_dispatch(ctx, addr)
 * for every address whose bit is set in la_hook_map (see scripts/handheld.sh). */
extern uint8_t la_hook_map[0x10000 / 8];
int     la_hook_dispatch(GBContext* ctx, uint16_t addr);

#ifdef __cplusplus
}
#endif
#endif
