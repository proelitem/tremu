#pragma once

/* The Touch controllers as a console pad: which pad button each Touch input presses, per console,
 * set in the "console controls" menu. The left stick is always the movement (the cross on NES,
 * SNES, PS1 and Mega Drive, the analog stick elsewhere; the Wii keeps the right controller's aim
 * as the Wiimote pointer, and its right trigger includes the gun's trigger). */
typedef enum { WFG_PAD_NES, WFG_PAD_SNES, WFG_PAD_N64, WFG_PAD_PS1, WFG_PAD_PS2, WFG_PAD_MEGADRIVE,
               WFG_PAD_DREAMCAST, WFG_PAD_GAMECUBE, WFG_PAD_WII, WFG_PAD_SATURN, WFG_PAD_X360, WFG_PAD_XBOX, WFG_PAD_PSP, WFG_PAD_CONSOLES } WfgPadConsole;

typedef enum {
    TI_A, TI_B, TI_X, TI_Y, TI_LGRIP, TI_RGRIP, TI_LTRIGGER, TI_RTRIGGER,
    TI_RS_UP, TI_RS_DOWN, TI_RS_LEFT, TI_RS_RIGHT, TI_LCLICK, TI_RCLICK, TI_COUNT
} WfgTouchInput;

/* Targets: a RetroPad button (RETRO_DEVICE_ID_JOYPAD_*), nothing, or the pad's right analog stick
 * (for the right stick's directions: N64 C buttons, PS2 camera). */
enum { WFG_PAD_NOTHING = -1, WFG_PAD_RIGHT_ANALOG = 100 };

void wfg_padmap_defaults(WfgPadConsole console, int map[TI_COUNT]);
const char *wfg_pad_console_name(WfgPadConsole console);
const char *wfg_pad_config_key(WfgPadConsole console);  /* "snes", for the settings file */
/* Name of a Touch input, in French (translate with T()). */
const char *wfg_touch_input_name(WfgTouchInput input);
/* Name of a target on that console's pad ("Z", "\xE2\x96\xA1"); French words go through T(). */
const char *wfg_pad_target_name(WfgPadConsole console, int target);
/* The target after (step 1) or before (-1) this one in the console's list. */
int wfg_pad_target_next(WfgPadConsole console, WfgTouchInput input, int target, int step);
