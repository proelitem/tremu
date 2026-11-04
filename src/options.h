#pragma once
#include <stdbool.h>
#include "padmap.h"

enum { TREMU_MAX_GUN_PROFILES = 8, TREMU_MAX_PERF_GAMES = 64, TREMU_PERF_KEY_SIZE = 56 };

/* "Optimisation" page. A profile other than custom decides the values (perf.c). */
enum { TREMU_PERF_AUTO, TREMU_PERF_BATTERY, TREMU_PERF_BALANCED, TREMU_PERF_PERFORMANCE, TREMU_PERF_QUALITY, TREMU_PERF_CUSTOM, TREMU_PERF_PROFILES };
enum { TREMU_LEVEL_SAVE, TREMU_LEVEL_LOW, TREMU_LEVEL_HIGH, TREMU_LEVEL_MAX, TREMU_PERF_LEVELS };
typedef struct TremuPerf {
    int profile;
    int cpu, gpu;   /* TREMU_LEVEL_* */
    int refresh;    /* Hz, 0 follows the game */
    int sharpen;    /* 0 none, 1 normal, 2 high */
    int res;        /* internal resolution of the GPU cores, as a multiple; 0 = the core's default */
} TremuPerf;

typedef struct TremuGamePerf {
    char game[TREMU_PERF_KEY_SIZE];
    TremuPerf perf;
} TremuGamePerf;

/* Gun mounting of one WonderFitter gun, remembered by its Bluetooth name. */
typedef struct TremuGunProfile {
    char name[32];
    int pitch, drop, back;
} TremuGunProfile;

/* Saved player settings. Gun values follow time-crisis-vr: the angle and the
 * down/back offsets correct a Touch controller mounted on top of a physical gun. */
typedef struct TremuOptions {
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
    int language;         /* TremuLang, -1 follows the headset language */
    int background;       /* TREMU_BACKGROUND_*: black, the room, the dimmed room (passthrough) */
    int x360_driver;      /* Xbox 360 Vulkan driver, TREMU_DRIVER_* (core.h); used when Xenia starts */
    char adhoc_mac[13];   /* this headset's address in PSP ad hoc games, 12 hex digits; made once */
    int pad_map[TREMU_PAD_CONSOLES][TI_COUNT];  /* Touch input -> pad button, per console (padmap.h) */
    char game[64];        /* ROM archive name of the last game played, e.g. "term2.zip" */
    int ext_buttons[5];   /* Bluetooth/USB gun: trigger, grenade, coin, start, action (0 = defaults) */
    char gun[32];         /* profile in use: name of the last gun connected, empty before any */
    TremuGunProfile profiles[TREMU_MAX_GUN_PROFILES];
    int profile_count;
    TremuPerf perf;         /* every game without its own settings, and the carousel */
    TremuGamePerf perf_games[TREMU_MAX_PERF_GAMES];
    int perf_game_count;
} TremuOptions;

void tremu_options_defaults(TremuOptions *o);
void tremu_options_load(const char *path, TremuOptions *o);
bool tremu_options_save(const char *path, const TremuOptions *o);
/* A gun connected: its saved mounting replaces the current one (a new gun keeps the
 * current values as its starting point). Returns true when the values changed. */
bool tremu_options_use_gun(TremuOptions *o, const char *name);
/* Copies the current mounting into the profile in use (after the menu changed it). */
void tremu_options_store_gun(TremuOptions *o);
/* The settings of this game: its own, otherwise the general ones. */
const TremuPerf *tremu_options_perf(const TremuOptions *o, const char *game);
/* This game's own settings, created from the general ones if needed; NULL when the table is full. */
TremuPerf *tremu_options_perf_own(TremuOptions *o, const char *game);
bool tremu_options_perf_has_own(const TremuOptions *o, const char *game);
void tremu_options_perf_forget(TremuOptions *o, const char *game);
