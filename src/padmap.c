#include "padmap.h"
#include "libretro.h"

#define B RETRO_DEVICE_ID_JOYPAD_B
#define Y RETRO_DEVICE_ID_JOYPAD_Y
#define SELECT RETRO_DEVICE_ID_JOYPAD_SELECT
#define START RETRO_DEVICE_ID_JOYPAD_START
#define UP RETRO_DEVICE_ID_JOYPAD_UP
#define DOWN RETRO_DEVICE_ID_JOYPAD_DOWN
#define LEFT RETRO_DEVICE_ID_JOYPAD_LEFT
#define RIGHT RETRO_DEVICE_ID_JOYPAD_RIGHT
#define A RETRO_DEVICE_ID_JOYPAD_A
#define X RETRO_DEVICE_ID_JOYPAD_X
#define L RETRO_DEVICE_ID_JOYPAD_L
#define R RETRO_DEVICE_ID_JOYPAD_R
#define L2 RETRO_DEVICE_ID_JOYPAD_L2
#define R2 RETRO_DEVICE_ID_JOYPAD_R2
#define L3 RETRO_DEVICE_ID_JOYPAD_L3
#define R3 RETRO_DEVICE_ID_JOYPAD_R3
#define NO WFG_PAD_NOTHING
#define RA WFG_PAD_RIGHT_ANALOG

typedef struct Target {
    int id;
    const char *name;
} Target;

/* The buttons each console's pad has, under their own names (RetroPad ids as the cores read them:
 * Mupen64Plus takes the N64 A on RetroPad B, B on Y and Z on L2). */
static const Target NES[] = {{NO, "Aucun"}, {A, "A"}, {B, "B"}, {SELECT, "Select"}, {START, "Start"},
                             {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"}, {RIGHT, "Droite"}};
static const Target SNES[] = {{NO, "Aucun"}, {A, "A"}, {B, "B"}, {X, "X"}, {Y, "Y"}, {L, "L"}, {R, "R"},
                              {SELECT, "Select"}, {START, "Start"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"},
                              {RIGHT, "Droite"}};
static const Target N64[] = {{NO, "Aucun"}, {B, "A"}, {Y, "B"}, {L2, "Z"}, {L, "L"}, {R, "R"}, {START, "Start"},
                             {RA, "C (stick droit)"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"}, {RIGHT, "Droite"}};
static const Target PS1[] = {{NO, "Aucun"}, {B, "\xE2\x9C\x95"}, {A, "\xE2\x97\x8B"}, {Y, "\xE2\x96\xA1"},
                             {X, "\xE2\x96\xB3"}, {L, "L1"}, {R, "R1"}, {L2, "L2"}, {R2, "R2"}, {L3, "L3"}, {R3, "R3"},
                             {SELECT, "Select"}, {START, "Start"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"},
                             {RIGHT, "Droite"}};
static const Target PS2[] = {{NO, "Aucun"}, {B, "\xE2\x9C\x95"}, {A, "\xE2\x97\x8B"}, {Y, "\xE2\x96\xA1"},
                             {X, "\xE2\x96\xB3"}, {L, "L1"}, {R, "R1"}, {L2, "L2"}, {R2, "R2"}, {L3, "L3"}, {R3, "R3"},
                             {SELECT, "Select"}, {START, "Start"}, {RA, "Stick droit"}, {UP, "Haut"}, {DOWN, "Bas"},
                             {LEFT, "Gauche"}, {RIGHT, "Droite"}};

/* Genesis Plus GX: Y A, B B, A C, L X, X Y, R Z, Select Mode. */
static const Target MD[] = {{NO, "Aucun"}, {Y, "A"}, {B, "B"}, {A, "C"}, {L, "X"}, {X, "Y"}, {R, "Z"},
                            {START, "Start"}, {SELECT, "Mode"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"},
                            {RIGHT, "Droite"}};
/* Flycast: B A, A B, Y X, X Y, L2 / R2 the analog triggers. */
static const Target DC[] = {{NO, "Aucun"}, {B, "A"}, {A, "B"}, {Y, "X"}, {X, "Y"}, {L2, "L"}, {R2, "R"},
                            {START, "Start"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"}, {RIGHT, "Droite"}};
/* Dolphin, GameCube controller: R is Z, L2 / R2 the analog L and R, the right analog stick C. */
static const Target GC[] = {{NO, "Aucun"}, {A, "A"}, {B, "B"}, {X, "X"}, {Y, "Y"}, {R, "Z"}, {L2, "L"}, {R2, "R"},
                            {START, "Start"}, {RA, "C (stick droit)"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"},
                            {RIGHT, "Droite"}};
/* Dolphin, Wiimote (upright), aimed with the right controller. */
static const Target WII[] = {{NO, "Aucun"}, {B, "B"}, {A, "A"}, {X, "1"}, {Y, "2"}, {START, "+"}, {SELECT, "\xE2\x88\x92"},
                             {R2, "Secouer"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"}, {RIGHT, "Droite"}};

/* Beetle Saturn: Y A, B B, A C, L X, X Y, R Z, L2 / R2 the shoulder buttons. */
static const Target SAT[] = {{NO, "Aucun"}, {Y, "A"}, {B, "B"}, {A, "C"}, {L, "X"}, {X, "Y"}, {R, "Z"}, {L2, "L"},
                             {R2, "R"}, {START, "Start"}, {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"}, {RIGHT, "Droite"}};

/* Xenia (wfg_libretro.cpp): RetroPad B A, A B, Y X, X Y, L / R the bumpers, L2 / R2 the triggers. */
static const Target X360[] = {{NO, "Aucun"}, {B, "A"}, {A, "B"}, {Y, "X"}, {X, "Y"}, {L, "LB"}, {R, "RB"},
                              {L2, "LT"}, {R2, "RT"}, {L3, "Clic stick gauche"}, {R3, "Clic stick droit"},
                              {SELECT, "Back"}, {START, "Start"}, {RA, "Stick droit"}, {UP, "Haut"}, {DOWN, "Bas"},
                              {LEFT, "Gauche"}, {RIGHT, "Droite"}};

/* xemu (wfg_xemu.cpp): RetroPad B A, A B, Y X, X Y, L / R white and black, L2 / R2 the triggers. */
static const Target XBOX[] = {{NO, "Aucun"}, {B, "A"}, {A, "B"}, {Y, "X"}, {X, "Y"}, {L, "White"}, {R, "Black"},
                              {L2, "LT"}, {R2, "RT"}, {L3, "Clic stick gauche"}, {R3, "Clic stick droit"},
                              {SELECT, "Back"}, {START, "Start"}, {RA, "Stick droit"}, {UP, "Haut"}, {DOWN, "Bas"},
                              {LEFT, "Gauche"}, {RIGHT, "Droite"}};

/* PPSSPP: the PlayStation buttons, one shoulder button per side, the left stick is the PSP's only one. */
static const Target PSP[] = {{NO, "Aucun"}, {B, "\xE2\x9C\x95"}, {A, "\xE2\x97\x8B"}, {Y, "\xE2\x96\xA1"},
                             {X, "\xE2\x96\xB3"}, {L, "L"}, {R, "R"}, {SELECT, "Select"}, {START, "Start"},
                             {UP, "Haut"}, {DOWN, "Bas"}, {LEFT, "Gauche"}, {RIGHT, "Droite"}};

static const struct {
    const char *name, *key;
    const Target *targets;
    int count;
    int defaults[TI_COUNT];  /* A, B, X, Y, grips L R, triggers L R, right stick up down left right, clicks L R */
} CONSOLES[WFG_PAD_CONSOLES] = {
    [WFG_PAD_NES] = {"NES", "nes", NES, sizeof NES / sizeof NES[0], {A, B, START, SELECT, NO, NO, NO, NO, NO, NO, NO, NO, NO, NO}},
    [WFG_PAD_SNES] = {"SNES", "snes", SNES, sizeof SNES / sizeof SNES[0],
                      {Y, B, X, A, L, R, NO, NO, SELECT, START, NO, NO, NO, NO}},
    [WFG_PAD_N64] = {"Nintendo 64", "n64", N64, sizeof N64 / sizeof N64[0],
                     {B, Y, START, NO, L, R, L2, L2, RA, RA, RA, RA, NO, NO}},
    [WFG_PAD_PS1] = {"PlayStation", "ps1", PS1, sizeof PS1 / sizeof PS1[0],
                     {A, B, X, Y, L, R, L2, R2, SELECT, START, L2, R2, L3, R3}},
    [WFG_PAD_PS2] = {"PlayStation 2", "ps2", PS2, sizeof PS2 / sizeof PS2[0],
                     {A, B, X, Y, L, R, L2, R2, SELECT, START, L2, R2, L3, R3}},
    [WFG_PAD_MEGADRIVE] = {"Mega Drive", "md", MD, sizeof MD / sizeof MD[0],
                           {B, A, Y, START, L, R, X, NO, SELECT, START, NO, NO, NO, NO}},
    [WFG_PAD_DREAMCAST] = {"Dreamcast", "dc", DC, sizeof DC / sizeof DC[0],
                           {B, A, Y, X, L2, R2, L2, R2, NO, START, NO, NO, NO, NO}},
    [WFG_PAD_GAMECUBE] = {"GameCube", "gc", GC, sizeof GC / sizeof GC[0],
                          {A, B, X, Y, START, R, L2, R2, RA, RA, RA, RA, NO, NO}},
    [WFG_PAD_WII] = {"Wii", "wii", WII, sizeof WII / sizeof WII[0],
                     {SELECT, START, R2, X, Y, A, NO, B, UP, DOWN, LEFT, RIGHT, NO, NO}},
    [WFG_PAD_SATURN] = {"Saturn", "saturn", SAT, sizeof SAT / sizeof SAT[0],
                        {B, A, Y, START, L2, R2, L, R, X, NO, NO, NO, NO, NO}},
    /* The menu button belongs to Tremu: Start and Back go on the stick clicks. */
    [WFG_PAD_X360] = {"Xbox 360", "x360", X360, sizeof X360 / sizeof X360[0],
                      {B, A, Y, X, L, R, L2, R2, RA, RA, RA, RA, START, SELECT}},
    [WFG_PAD_XBOX] = {"Xbox", "xbox", XBOX, sizeof XBOX / sizeof XBOX[0],
                      {B, A, Y, X, L, R, L2, R2, RA, RA, RA, RA, START, SELECT}},
    /* The left stick is the analog nub, so the right stick gives the cross. */
    [WFG_PAD_PSP] = {"PSP", "psp", PSP, sizeof PSP / sizeof PSP[0],
                     {B, A, Y, X, L, R, L, R, UP, DOWN, LEFT, RIGHT, START, SELECT}},
};

void wfg_padmap_defaults(WfgPadConsole console, int map[TI_COUNT]) {
    for (int i = 0; i < TI_COUNT; i++) map[i] = CONSOLES[console].defaults[i];
}

const char *wfg_pad_console_name(WfgPadConsole console) { return CONSOLES[console].name; }
const char *wfg_pad_config_key(WfgPadConsole console) { return CONSOLES[console].key; }

const char *wfg_touch_input_name(WfgTouchInput input) {
    static const char *const names[TI_COUNT] = {
        "Bouton A", "Bouton B", "Bouton X", "Bouton Y", "Grip gauche", "Grip droit", "G\xC3\xA2" "chette gauche",
        "G\xC3\xA2" "chette droite", "Stick droit haut", "Stick droit bas", "Stick droit gauche", "Stick droit droite",
        "Clic stick gauche", "Clic stick droit"};
    return names[input];
}

const char *wfg_pad_target_name(WfgPadConsole console, int target) {
    for (int i = 0; i < CONSOLES[console].count; i++)
        if (CONSOLES[console].targets[i].id == target) return CONSOLES[console].targets[i].name;
    return "?";
}

int wfg_pad_target_next(WfgPadConsole console, WfgTouchInput input, int target, int step) {
    int n = CONSOLES[console].count, at = 0;
    for (int i = 0; i < n; i++)
        if (CONSOLES[console].targets[i].id == target) at = i;
    for (int k = 0; k < n; k++) {
        at = (at + step + n) % n;
        int id = CONSOLES[console].targets[at].id;
        /* The right analog stick only makes sense on the right stick's directions. */
        if (id != RA || (input >= TI_RS_UP && input <= TI_RS_RIGHT)) return id;
    }
    return target;
}
