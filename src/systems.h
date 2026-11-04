#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Game systems and the libretro core that runs each one. A game's system comes from its file
 * extension or, for a .zip, from the files inside (no console file = an arcade set). */

/* SYS_NAOMI: Sega Naomi / Atomiswave arcade sets (.zip), run by Flycast like the Dreamcast. */
/* SYS_PS2: PlayStation 2 discs, run by our LRPS2 build with its GunCon 2 (needs the PS2 BIOS).
 * SYS_WII and SYS_GAMECUBE: Dolphin. SYS_NAMCO246: Namco's PS2-based arcade boards (System
 * 246/256: Time Crisis 3 and 4, Vampire Night...), run by Play!. SYS_N64: Mupen64Plus-Next (GLES3).
 * SYS_MEGADRIVE: Genesis Plus GX (Mega Drive / Genesis, Master System, Game Gear, Mega-CD discs).
 * SYS_MAME: arcade sets in a "mame" folder (zip or unzipped folder), run by recent MAME.
 * SYS_SATURN: Beetle Saturn (the only Saturn core with the Virtua Gun / Stunner; needs the BIOS).
 * SYS_X360: Xbox 360 (.iso with an XDVDFS volume, .xex, .zar), run by our XenDroid (Xenia) build.
 * SYS_XBOX: original Xbox (.iso with default.xbe), run by our xemu build; needs the console's files.
 * SYS_WIIU: Download/Roms/wiiu, listed by WiiuLibrary.java and run by our Cemu app, no core here.
 * SYS_PS3: Download/Roms/ps3, listed by Ps3Library.java and run by the ARMSX3 app, no core here.
 * SYS_VITA: Download/Roms/vita, listed by VitaLibrary.java and run by our Vita3K app, no core here.
 * SYS_PSP: PPSSPP; several headsets play through the game's own ad hoc mode, not lockstep. */
typedef enum {
    SYS_ARCADE, SYS_NES, SYS_SNES, SYS_PS1, SYS_DREAMCAST, SYS_NAOMI, SYS_WII, SYS_GAMECUBE, SYS_NAMCO246, SYS_PS2, SYS_N64, SYS_MEGADRIVE, SYS_SATURN, SYS_MAME, SYS_X360, SYS_XBOX, SYS_WIIU, SYS_PS3, SYS_PSP, SYS_VITA, SYS_COUNT
} TremuSystem;

typedef struct TremuSystemInfo {
    const char *label;       /* shown after console titles, e.g. "NES" */
    const char *core;        /* core library inside the APK */
    const char *extensions;  /* "|nes|fds|": game files, lower case */
    const char *thumbnails;  /* libretro-thumbnails folder for posters, NULL for arcade (own table) */
} TremuSystemInfo;

const TremuSystemInfo *tremu_system_info(TremuSystem system);
/* Title of a known Naomi / Atomiswave set ("hotd2" -> "The House of the Dead 2"), or NULL. */
const char *tremu_naomi_title(const char *set);
/* Title of a known Namco System 246/256 gun set ("timecrs3" -> "Time Crisis 3"), or NULL. Its
 * disc image sits in a folder of that name next to the .zip (MAME layout), never listed alone. */
const char *tremu_namco_title(const char *set);
/* System of a game file, or -1 when the file is not a game (CD track .bin, BIOS, picture...). */
int tremu_system_detect(const char *path);
/* For a zipped console game: unpacks the game file into dir (kept as a cache, reused while the
 * same size) and writes its path to out. False when the archive holds no game file. */
bool tremu_system_unzip(const char *zip, TremuSystem system, const char *dir, char *out, size_t size);
/* True when a console game file is a lightgun game (by name): it gets the gun, any other game
 * of a console with both is played with the Touch controllers as a gamepad. */
bool tremu_system_xbox_gun_game(const char *path);
bool tremu_system_gun_game(const char *path);
/* True when path sits directly in a folder named "mame" (sets for MAME rather than FBNeo). */
bool tremu_in_mame_folder(const char *path);
