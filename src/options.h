#pragma once
#include <stdbool.h>
#include "padmap.h"

enum { WFG_MAX_GUN_PROFILES = 8, WFG_MAX_PERF_GAMES = 64, WFG_PERF_KEY_SIZE = 56 };

/* "Optimisation" page. A profile other than custom decides the values (perf.c). */
enum { WFG_PERF_AUTO, WFG_PERF_BATTERY, WFG_PERF_BALANCED, WFG_PERF_PERFORMANCE, WFG_PERF_QUALITY, WFG_PERF_CUSTOM, WFG_PERF_PROFILES };
enum { WFG_LEVEL_SAVE, WFG_LEVEL_LOW, WFG_LEVEL_HIGH, WFG_LEVEL_MAX, WFG_PERF_LEVELS };
typedef struct WfgPerf {
    int profile;
    int cpu, gpu;   /* WFG_LEVEL_* */
    int refresh;    /* Hz, 0 follows the game */
    int sharpen;    /* 0 none, 1 normal, 2 high */
    int res;        /* internal resolution of the GPU cores, as a multiple; 0 = the core's default */
} WfgPerf;

typedef struct WfgGamePerf {
    char game[WFG_PERF_KEY_SIZE];
    WfgPerf perf;
} WfgGamePerf;

/* Gun mounting of one WonderFitter gun, remembered by its Bluetooth name. */
typedef struct WfgGunProfile {
    char name[32];
    int pitch, drop, back;
} WfgGunProfile;

/* Saved player settings. Gun values follow time-crisis-vr: the angle and the
 * down/back offsets correct a Touch controller mounted on top of a physical gun. */
typedef struct WfgOptions {
    int gun_pitch;        /* degrees, negative lowers the barrel */
    int gun_drop;         /* cm below the controller, along the angled gun */
    int gun_back;         /* cm towards the stock */
    int screen_distance;  /* cm */
    int screen_width;     /* cm */
    int screen_height;    /* cm above (+) or below (-) eye level */
    bool crosshair;
    bool vibration;       /* Touch controller pulse on each shot and reload */
    bool ai_depth;        /* games without layers: depth guessed by a neural network */
    bool voxels;          /* games that report layers (NES, Operation Wolf) in 3D voxels */
    int stereo;           /* PS2: one picture per eye from the game's depth, in percent; 0 flat */
    bool local_p2;        /* player 2 on the left Touch controller (same headset) */
    bool ext_aim;         /* the external pointer (Wii Remote via DolphinBar, mouse gun) aims for player 1 */
    int language;         /* WfgLang, -1 follows the headset language */
    int background;       /* WFG_BACKGROUND_*: black, the room, the dimmed room (passthrough) */
    int x360_driver;      /* Xbox 360 Vulkan driver, WG_DRIVER_* (core.h); used when Xenia starts */
    char adhoc_mac[13];   /* this headset's address in PSP ad hoc games, 12 hex digits; made once */
    int pad_map[WFG_PAD_CONSOLES][TI_COUNT];  /* Touch input -> pad button, per console (padmap.h) */
    char game[64];        /* ROM archive name of the last game played, e.g. "term2.zip" */
    int ext_buttons[5];   /* Bluetooth/USB gun: trigger, grenade, coin, start, action (0 = defaults) */
    char gun[32];         /* profile in use: name of the last gun connected, empty before any */
    WfgGunProfile profiles[WFG_MAX_GUN_PROFILES];
    int profile_count;
    WfgPerf perf;         /* every game without its own settings, and the carousel */
    WfgGamePerf perf_games[WFG_MAX_PERF_GAMES];
    int perf_game_count;
} WfgOptions;

void wfg_options_defaults(WfgOptions *o);
void wfg_options_load(const char *path, WfgOptions *o);
bool wfg_options_save(const char *path, const WfgOptions *o);
/* A gun connected: its saved mounting replaces the current one (a new gun keeps the
 * current values as its starting point). Returns true when the values changed. */
bool wfg_options_use_gun(WfgOptions *o, const char *name);
/* Copies the current mounting into the profile in use (after the menu changed it). */
void wfg_options_store_gun(WfgOptions *o);
/* The settings of this game: its own, otherwise the general ones. */
const WfgPerf *wfg_options_perf(const WfgOptions *o, const char *game);
/* This game's own settings, created from the general ones if needed; NULL when the table is full. */
WfgPerf *wfg_options_perf_own(WfgOptions *o, const char *game);
bool wfg_options_perf_has_own(const WfgOptions *o, const char *game);
void wfg_options_perf_forget(WfgOptions *o, const char *game);
