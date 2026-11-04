#pragma once

/* The Touch controllers as a console pad: which pad button each Touch input presses, per console,
 * set in the "console controls" menu. The left stick is always the movement (the cross on NES,
 * SNES, PS1 and Mega Drive, the analog stick elsewhere; the Wii keeps the right controller's aim
 * as the Wiimote pointer, and its right trigger includes the gun's trigger). */
typedef enum { TREMU_PAD_NES, TREMU_PAD_SNES, TREMU_PAD_N64, TREMU_PAD_PS1, TREMU_PAD_PS2, TREMU_PAD_MEGADRIVE,
               TREMU_PAD_DREAMCAST, TREMU_PAD_GAMECUBE, TREMU_PAD_WII, TREMU_PAD_SATURN, TREMU_PAD_X360, TREMU_PAD_XBOX, TREMU_PAD_PSP, TREMU_PAD_VITA, TREMU_PAD_CONSOLES } TremuPadConsole;

typedef enum {
    TI_A, TI_B, TI_X, TI_Y, TI_LGRIP, TI_RGRIP, TI_LTRIGGER, TI_RTRIGGER,
    TI_RS_UP, TI_RS_DOWN, TI_RS_LEFT, TI_RS_RIGHT, TI_LCLICK, TI_RCLICK, TI_COUNT
} TremuTouchInput;

/* Targets: a RetroPad button (RETRO_DEVICE_ID_JOYPAD_*), nothing, or the pad's right analog stick
 * (for the right stick's directions: N64 C buttons, PS2 camera). */
/* The PS Vita also has a finger on its screens, where the right controller points. */
enum { TREMU_PAD_NOTHING = -1, TREMU_PAD_RIGHT_ANALOG = 100, TREMU_PAD_TOUCH_FRONT = 101, TREMU_PAD_TOUCH_REAR = 102 };

void tremu_padmap_defaults(TremuPadConsole console, int map[TI_COUNT]);
const char *tremu_pad_console_name(TremuPadConsole console);
const char *tremu_pad_config_key(TremuPadConsole console);  /* "snes", for the settings file */
/* Name of a Touch input, in French (translate with T()). */
const char *tremu_touch_input_name(TremuTouchInput input);
/* Name of a target on that console's pad ("Z", "\xE2\x96\xA1"); French words go through T(). */
const char *tremu_pad_target_name(TremuPadConsole console, int target);
/* The target after (step 1) or before (-1) this one in the console's list. */
int tremu_pad_target_next(TremuPadConsole console, TremuTouchInput input, int target, int step);
