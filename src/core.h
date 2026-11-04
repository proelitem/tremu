#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Lightgun state handed to the libretro core every emulated frame.
 * x/y use libretro's screen range, -0x7fff..0x7fff with (0,0) at the centre. */
typedef struct TremuGun {
    int16_t x, y;
    bool offscreen, trigger, reload, aux_a, aux_b, start, select;
    /* Touch controllers as a gamepad, for games played without the gun: RetroPad buttons
     * (1 << RETRO_DEVICE_ID_JOYPAD_*) and the two sticks, -0x7fff..0x7fff with y down. */
    uint16_t pad;
    int16_t lx, ly, rx, ry;
} TremuGun;

/* A key (RETROK_*) for the core, down or up. */
void tremu_core_set_key(unsigned key, bool down);

/* Before tremu_core_load: true plugs a gamepad in every port instead of the gun (PlayStation games
 * made for a pad). NES and SNES keep the pad in the first port and the gun in the second anyway. */
void tremu_core_set_gamepad(bool on);

/* The Java VM and activity, handed to cores that need Android services (XenDroid: JNI threads,
 * its files folder); call once before the first tremu_core_load. */
void tremu_core_set_android(void *vm, void *activity);

bool tremu_core_load(const char *core, const char *system_dir, const char *save_dir, const char *rom);
/* Ends the current game but keeps the core library loaded, ready for tremu_core_load. */
void tremu_core_close_game(void);
void tremu_core_unload(void);
enum { TREMU_PLAYERS = 4 };
/* Runs one emulated frame with players 1 to 4 (guns[0..3]). */
void tremu_core_run(const TremuGun guns[TREMU_PLAYERS]);
/* Save states (0 when unsupported) and a CRC of the game RAM, for netplay sync checks. */
size_t tremu_core_state_size(void);
bool tremu_core_save_state(void *data, size_t size);
bool tremu_core_load_state(const void *data, size_t size);
uint32_t tremu_core_ram_crc(void);
/* Latest frame as RGBA8 (R in the low byte), w*h pixels, row pitch w, bottom row first. */
const uint32_t *tremu_core_frame(unsigned *w, unsigned *h);
/* Stereo depth strength in percent, 0 for a flat picture; applied at once, also during a game.
 * LRPS2 and our N64 core make eyes. */
void tremu_core_set_stereo(int percent);
/* True when that frame holds both eyes side by side: where each starts, and an eye's width. */
bool tremu_core_views(unsigned *left_x, unsigned *right_x, unsigned *eye_w);
/* Layer of each pixel of that frame (same size and order: 0 backdrop, 1 background, 2 sprite
 * behind the background, 3 sprite, 4 foreground), from cores that report it (our NES core,
 * our FBNeo for some drivers such as Operation Wolf); else NULL. */
const uint8_t *tremu_core_layers(void);
double tremu_core_fps(void);
float tremu_core_aspect(void);
unsigned tremu_core_max_width(void);
unsigned tremu_core_max_height(void);
/* Vulkan driver of the Xbox 360 core (XenDroid), as named in its xenia_vulkan_driver option: our
 * Turnip build (stock Turnips cannot allocate on Quest, see tools/turnip_quest_patch.py) or the
 * headset's Adreno driver. Read when Xenia starts. */
enum { TREMU_DRIVER_TURNIP, TREMU_DRIVER_SYSTEM, TREMU_DRIVER_COUNT };
/* Text asked by the game (Xbox 360: character name, save title). tremu_core_text_submit answers it,
 * NULL cancels. Callable from any thread. */
bool tremu_core_text_pending(char *title, size_t title_size, char *text, size_t text_size, unsigned *max_length);
void tremu_core_text_submit(const char *text);
const char *tremu_core_driver_name(int driver);
void tremu_core_set_vulkan_driver(int driver);
/* Internal resolution of the GPU cores (Flycast, Dolphin, LRPS2, Mupen64Plus), as a multiple of the
 * console's own; 0 is the core's default. Call before tremu_core_load: read when a game loads. */
void tremu_core_set_internal_res(int multiple);
/* PPSSPP's ad hoc network: its server's IPv4 address, whether this headset runs that server (read
 * when a game boots), the relay on that server instead of direct links between players (read when the
 * game opens its ad hoc mode), and this headset's MAC address, 12 hex digits. */
void tremu_core_set_adhoc(const char *server_ip, bool builtin_server, bool relay, const char *mac);
/* Shows or hides the core's lightgun crosshair; callable from any thread, before or after loading. */
void tremu_core_set_crosshair(bool on);
