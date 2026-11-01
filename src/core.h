#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Lightgun state handed to the libretro core every emulated frame.
 * x/y use libretro's screen range, -0x7fff..0x7fff with (0,0) at the centre. */
typedef struct WgGun {
    int16_t x, y;
    bool offscreen, trigger, reload, aux_a, aux_b, start, select;
    /* Touch controllers as a gamepad, for games played without the gun: RetroPad buttons
     * (1 << RETRO_DEVICE_ID_JOYPAD_*) and the two sticks, -0x7fff..0x7fff with y down. */
    uint16_t pad;
    int16_t lx, ly, rx, ry;
} WgGun;

/* A key (RETROK_*) for the core, down or up. */
void wg_core_set_key(unsigned key, bool down);

/* Before wg_core_load: true plugs a gamepad in every port instead of the gun (PlayStation games
 * made for a pad). NES and SNES keep the pad in the first port and the gun in the second anyway. */
void wg_core_set_gamepad(bool on);

/* The Java VM and activity, handed to cores that need Android services (XenDroid: JNI threads,
 * its files folder); call once before the first wg_core_load. */
void wg_core_set_android(void *vm, void *activity);

bool wg_core_load(const char *core, const char *system_dir, const char *save_dir, const char *rom);
/* Ends the current game but keeps the core library loaded, ready for wg_core_load. */
void wg_core_close_game(void);
void wg_core_unload(void);
enum { WG_PLAYERS = 4 };
/* Runs one emulated frame with players 1 to 4 (guns[0..3]). */
void wg_core_run(const WgGun guns[WG_PLAYERS]);
/* Save states (0 when unsupported) and a CRC of the game RAM, for netplay sync checks. */
size_t wg_core_state_size(void);
bool wg_core_save_state(void *data, size_t size);
bool wg_core_load_state(const void *data, size_t size);
uint32_t wg_core_ram_crc(void);
/* Latest frame as RGBA8 (R in the low byte), w*h pixels, row pitch w, bottom row first. */
const uint32_t *wg_core_frame(unsigned *w, unsigned *h);
/* Stereo depth strength in percent, 0 for a flat picture; applied at once, also during a game.
 * LRPS2 and our N64 core make eyes. */
void wg_core_set_stereo(int percent);
/* True when that frame holds both eyes side by side: where each starts, and an eye's width. */
bool wg_core_views(unsigned *left_x, unsigned *right_x, unsigned *eye_w);
/* Layer of each pixel of that frame (same size and order: 0 backdrop, 1 background, 2 sprite
 * behind the background, 3 sprite, 4 foreground), from cores that report it (our NES core,
 * our FBNeo for some drivers such as Operation Wolf); else NULL. */
const uint8_t *wg_core_layers(void);
double wg_core_fps(void);
float wg_core_aspect(void);
unsigned wg_core_max_width(void);
unsigned wg_core_max_height(void);
/* Vulkan driver of the Xbox 360 core (XenDroid), as named in its xenia_vulkan_driver option: our
 * Turnip build (stock Turnips cannot allocate on Quest, see tools/turnip_quest_patch.py) or the
 * headset's Adreno driver. Read when Xenia starts. */
enum { WG_DRIVER_TURNIP, WG_DRIVER_SYSTEM, WG_DRIVER_COUNT };
/* Text asked by the game (Xbox 360: character name, save title). wg_core_text_submit answers it,
 * NULL cancels. Callable from any thread. */
bool wg_core_text_pending(char *title, size_t title_size, char *text, size_t text_size, unsigned *max_length);
void wg_core_text_submit(const char *text);
const char *wg_core_driver_name(int driver);
void wg_core_set_vulkan_driver(int driver);
/* Internal resolution of the GPU cores (Flycast, Dolphin, LRPS2, Mupen64Plus), as a multiple of the
 * console's own; 0 is the core's default. Call before wg_core_load: read when a game loads. */
void wg_core_set_internal_res(int multiple);
/* PPSSPP's ad hoc network: its server's IPv4 address, whether this headset runs that server (read
 * when a game boots), the relay on that server instead of direct links between players (read when the
 * game opens its ad hoc mode), and this headset's MAC address, 12 hex digits. */
void wg_core_set_adhoc(const char *server_ip, bool builtin_server, bool relay, const char *mac);
/* Shows or hides the core's lightgun crosshair; callable from any thread, before or after loading. */
void wg_core_set_crosshair(bool on);
