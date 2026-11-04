#pragma once
#include <jni.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { CAROUSEL_WIDTH = 1280, CAROUSEL_HEIGHT = 720, SPLASH_WIDTH = 1200, SPLASH_HEIGHT = 400 };

/* A carousel tile: a ROM archive run by the core, or another installed app (e.g. Time Crisis VR). */
typedef struct TremuEntry {
    char title[64];
    char rom[512];      /* full path of the game archive, empty for apps */
    char package[96];   /* Android package to launch, empty for games */
    char banner[512];   /* poster image, empty if none was found */
    char join_ip[64];   /* set for "join" tiles: the host announcing this game on the network */
    char join_key[64];  /* game archive the host plays (rom is empty if we do not have it) */
    int system;         /* TremuSystem of a game (arcade, NES...) */
    char detail[64];    /* shown in the footer when selected: "Mise à jour v208 + DLC" */
} TremuEntry;

void tremu_carousel_init(JavaVM *vm, jobject activity);
/* Lists games from each roms folder and its subfolders (BIOS/device archives and CD tracks
 * skipped; the system of each game comes from systems.h) plus known installed apps.
 * Posters are looked up in banner_dir as <rom or app name>.png/.jpg/.jpeg/.webp. */
int tremu_carousel_scan(const char *const *rom_dirs, int rom_dir_count, const char *banner_dir);
/* Missing posters are downloaded in the background; call each frame to pick them up. */
void tremu_carousel_poll_posters(void);
/* Address and code of the "add games" web page ("http://...:8080   code 1234"); false if none. */
bool tremu_library_info(char *out, size_t size);
/* True once after a file was added through that page (the carousel should rescan). */
bool tremu_library_changed(void);
int tremu_carousel_count(void);
const TremuEntry *tremu_carousel_entry(int i);
/* The chosen entry, -1 when the current tab and search show none. */
int tremu_carousel_selected(void);
/* Also clears the tab and search if they hide this entry. */
void tremu_carousel_select(int i);
/* Left stick left/right moves the selection, with key repeat; up goes to the bar of console tabs and
 * search, down back to the games. confirmed is a new press of A or the trigger. */
typedef enum { CAROUSEL_NONE, CAROUSEL_LAUNCH, CAROUSEL_SEARCH } CarouselAction;
CarouselAction tremu_carousel_input(float stick_x, float stick_y, bool confirmed, uint64_t now_ns);
/* Only titles containing text are shown (any case); "" shows them all again. */
void tremu_carousel_set_search(const char *text);
const char *tremu_carousel_search(void);
/* The stick is on the bar of tabs, not on the games. */
bool tremu_carousel_in_bar(void);
/* Animates towards the selection; returns RGBA pixels (bottom row first) when the image changed, else NULL. */
const uint32_t *tremu_carousel_render(float dt, const char *footer);
/* Splash logo from the APK assets, RGBA bottom row first; NULL on failure. */
const uint32_t *tremu_load_logo(void);
/* The picture shown while a game loads, in place of the black screen: the game's poster and title,
 * a turning wheel at angle (degrees) and status. RGBA bottom row first (the carousel's size), NULL
 * on failure. Leaves the carousel to redraw itself next time. */
const uint32_t *tremu_loading_render(const char *key, const char *title, const char *status, float angle);
bool tremu_launch_package(const char *package);
/* Opens a Wii U game in our Cemu app; false when the app is not installed. */
bool tremu_launch_wiiu(const char *path);
/* Opens a PS3 game (disc image or folder dump) in the ARMSX3 app; false when it is not installed. */
bool tremu_launch_ps3(const char *path);
bool tremu_launch_vita(const char *path);
/* Starts downloading the depth network into path (Java, background); safe from any thread. */
void tremu_fetch_depth_model(const char *path);
/* Unpacks a zip bundled in the APK assets into dir, once per APK version; blocking. Used for
 * Dolphin's Sys folder and Play!'s arcade definitions. */
bool tremu_unpack_asset(const char *asset, const char *dir);
/* Game path given with "am start ... --es tremu_game <path>" (tests), once; false when none. */
bool tremu_intent_game(char *path, size_t size);
/* The app reopens itself on the carousel once this process has ended. */
void tremu_restart_after_exit(void);
/* Packs an unzipped arcade set folder into zip (cached until a file changes); blocking. */
bool tremu_zip_folder(const char *folder, const char *zip);
/* Replaces the "join" tiles with the games announced by hosts on the local network. */
struct NetHost;
void tremu_carousel_set_joinable(const struct NetHost *hosts, int count);
/* Index of the local game whose archive is named key (e.g. "term2.zip"), or -1. */
int tremu_carousel_find_game(const char *key);
/* File name used for a game in the saved options, e.g. "term2.zip". */
const char *tremu_entry_key(const TremuEntry *e);
