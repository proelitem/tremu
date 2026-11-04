#pragma once
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include "options.h"

enum { MENU_WIDTH = 800, MENU_HEIGHT = 960 };

typedef enum { MENU_NONE, MENU_RECENTER, MENU_CLOSE, MENU_BACK, MENU_APPLY_MULTI, MENU_CODE_ENTERED, MENU_TEXT_ENTERED, MENU_SAVE_STATE, MENU_LOAD_STATE, MENU_EXT_LEARNED, MENU_LANGUAGE, MENU_QUIT } MenuAction;

/* The menu draws its text through TremuActivity.renderMenu (Java). */
void tremu_menu_init(JavaVM *vm, jobject activity);
/* Left stick: up/down selects, left/right adjusts (with key repeat); confirm runs actions.
 * Sets *changed when an option value changed and should be saved. */
MenuAction tremu_menu_input(float stick_x, float stick_y, bool confirm, uint64_t now_ns, TremuOptions *o, bool *changed);
/* Returns RGBA pixels (bottom row first) when the image must be re-uploaded, else NULL.
 * net_status, when not empty, replaces the footer. */
const uint32_t *tremu_menu_render(const TremuOptions *o, bool gun_connected, const char *net_status);
/* Choices for the "Multijoueur" line (e.g. Solo, Host, Join X). The stick previews a choice;
 * A returns MENU_APPLY_MULTI when the previewed choice differs from current. */
void tremu_menu_set_multiplayer(const char choices[][64], int count, int current);
int tremu_menu_multiplayer_pick(void);
void tremu_menu_reset(void);
/* The menu was closed (cancels a button learning in progress). */
void tremu_menu_closed(void);
/* Party code entry replaces the option list: stick up/down changes a character, left/right
 * moves, A returns MENU_CODE_ENTERED with the code in tremu_menu_code(). */
void tremu_menu_begin_code(void);
bool tremu_menu_entering_code(void);
const char *tremu_menu_code(void);
/* Text entry for the game (same panel and controls as the code, with an empty place after the last
 * character to add one): A returns MENU_TEXT_ENTERED with the text in tremu_menu_text(). max_length 0
 * means the panel's own limit. tremu_menu_text_erase (Touch B) removes the character under the cursor. */
void tremu_menu_begin_text(const char *title, const char *text, unsigned max_length);
const char *tremu_menu_text(void);
void tremu_menu_text_erase(void);
/* Console the "console controls" page opens on (the game being played: TremuPadConsole). */
void tremu_menu_set_pad_console(int console);
/* Game the "optimisation" page may set apart (key "" and system -1: none, the carousel). */
void tremu_menu_set_perf_game(const char *key, const char *title, int system);
