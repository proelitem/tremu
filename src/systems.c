#include "systems.h"
#include <android/log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <zlib.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

static const TremuSystemInfo systems[SYS_COUNT] = {
    [SYS_ARCADE] = {"Arcade", "libtremu_core.so", "|zip|", NULL},
    [SYS_NES] = {"NES", "libtremu_fceumm.so", "|nes|fds|unf|unif|", "Nintendo - Nintendo Entertainment System"},
    [SYS_SNES] = {"SNES", "libtremu_snes9x.so", "|sfc|smc|swc|fig|bs|", "Nintendo - Super Nintendo Entertainment System"},
    /* CD images: the .cue (or .m3u) is the game, the .bin tracks next to it are not listed. */
    [SYS_PS1] = {"PS1", "libtremu_pcsx.so", "|cue|chd|pbp|m3u|iso|cbn|mdf|toc|", "Sony - PlayStation"},
    /* .chd and .cue can also be Dreamcast discs: tremu_system_detect looks inside. */
    [SYS_DREAMCAST] = {"Dreamcast", "libtremu_flycast.so", "|gdi|cdi|", "Sega - Dreamcast"},
    [SYS_NAOMI] = {"Naomi", "libtremu_flycast.so", "||", NULL},
    /* .iso can also be a PlayStation disc: tremu_system_detect reads the disc header. */
    [SYS_WII] = {"Wii", "libtremu_dolphin.so", "|wbfs|rvz|wia|gcz|ciso|wad|", "Nintendo - Wii"},
    [SYS_GAMECUBE] = {"GameCube", "libtremu_dolphin.so", "|gcm|", "Nintendo - GameCube"},
    [SYS_NAMCO246] = {"Namco", "libtremu_play.so", "||", NULL},
    /* .iso and .chd are told apart from CDs by their size or CHD tag (tremu_system_detect). */
    [SYS_PS2] = {"PS2", "libtremu_pcsx2.so", "||", "Sony - PlayStation 2"},
    [SYS_N64] = {"N64", "libtremu_n64.so", "|z64|n64|v64|", "Nintendo - Nintendo 64"},
    [SYS_MEGADRIVE] = {"Mega Drive", "libtremu_genesis.so", "|md|gen|smd|sms|gg|", "Sega - Mega Drive - Genesis"},
    /* Saturn and Mega-CD discs are .cue files: tremu_system_detect reads the first track's header. */
    [SYS_SATURN] = {"Saturn", "libtremu_saturn.so", "||", "Sega - Saturn"},
    [SYS_MAME] = {"MAME", "libtremu_mame.so", "||", NULL},
    /* .iso discs are recognised by their XDVDFS volume (tremu_system_detect). */
    [SYS_X360] = {"Xbox 360", "libtremu_xenia.so", "|xex|zar|", "Microsoft - Xbox 360"},
    [SYS_XBOX] = {"Xbox", "libtremu_xemu.so", "||", "Microsoft - Xbox"},
    [SYS_WIIU] = {"Wii U", "", "||", "Nintendo - Wii U"},
    [SYS_PS3] = {"PS3", "", "||", "Sony - PlayStation 3"},
    /* .iso and .pbp are told apart by their content, anything in a "psp" folder is PSP (tremu_system_detect). */
    [SYS_PSP] = {"PSP", "libtremu_ppsspp.so", "|cso|", "Sony - PlayStation Portable"},
    [SYS_VITA] = {"PS Vita", "", "||", "Sony - PlayStation Vita"},
};

static const struct { const char *set, *title; } namco_sets[] = {
    {"timecrs3", "Time Crisis 3"}, {"timecrs3e", "Time Crisis 3"}, {"timecrs3u", "Time Crisis 3"},
    {"timecrs4", "Time Crisis 4"}, {"timecrs4j", "Time Crisis 4"}, {"vnight", "Vampire Night"},
    {"cobrata", "Cobra The Arcade"},
};

const char *tremu_namco_title(const char *set) {
    for (size_t i = 0; i < sizeof namco_sets / sizeof namco_sets[0]; i++)
        if (!strcasecmp(set, namco_sets[i].set)) return namco_sets[i].title;
    return NULL;
}

/* Naomi / Atomiswave gun games (MAME set names, as Flycast expects). Any other set placed in a
 * Roms/naomi or Roms/atomiswave folder is run by Flycast too. */
static const struct { const char *set, *title; } naomi_sets[] = {
    {"hotd2", "The House of the Dead 2"}, {"hotd2o", "The House of the Dead 2"}, {"hotd2p", "The House of the Dead 2"},
    {"hotd2e", "The House of the Dead 2"}, {"confmiss", "Confidential Mission"}, {"mok", "The Maze of the Kings"},
    {"lupinsho", "Lupin the Third - The Shooting"}, {"deathcox", "Death Crimson OX"}, {"ninjaslt", "Ninja Assault"},
    {"ninjaslta", "Ninja Assault"}, {"ninjasltj", "Ninja Assault"}, {"gunsur2", "Gun Survivor 2"},
    {"gunsur2e", "Gun Survivor 2"}, {"sprtshot", "Sports Shooting USA"}, {"xtrmhunt", "Extreme Hunting"},
    {"xtrmhnt2", "Extreme Hunting 2"}, {"rangrmsn", "Ranger Mission"},
};

const char *tremu_naomi_title(const char *set) {
    for (size_t i = 0; i < sizeof naomi_sets / sizeof naomi_sets[0]; i++)
        if (!strcasecmp(set, naomi_sets[i].set)) return naomi_sets[i].title;
    return NULL;
}

const TremuSystemInfo *tremu_system_info(TremuSystem system) { return &systems[system < SYS_COUNT ? system : SYS_ARCADE]; }

static int system_of_name(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || strlen(dot) > 6) return -1;
    char ext[10];
    snprintf(ext, sizeof ext, "|%s|", dot + 1);
    for (char *c = ext; *c; c++) *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
    for (int s = SYS_NES; s < SYS_COUNT; s++)
        if (strstr(systems[s].extensions, ext)) return s;
    return -1;
}

static uint64_t be64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}

/* A CHD's metadata says GD-ROM (Dreamcast) with a "CHGD" track tag, CD with "CHT2"/"CHTR",
 * DVD (PS2) with "DVD ". Returns the system, SYS_PS1 for a CD. */
static int chd_system(FILE *f) {
    uint8_t h[124];
    if (fread(h, 1, sizeof h, f) < 64 || memcmp(h, "MComprHD", 8)) return SYS_PS1;
    unsigned version = (unsigned)h[12] << 24 | h[13] << 16 | h[14] << 8 | h[15];
    uint64_t meta = version >= 5 ? be64(h + 48) : version == 4 || version == 3 ? be64(h + 36) : 0;
    for (int i = 0; meta && i < 256; i++) {
        uint8_t m[16];
        if (fseek(f, (long)meta, SEEK_SET) || fread(m, 1, 16, f) != 16) return SYS_PS1;
        if (!memcmp(m, "CHGD", 4)) return SYS_DREAMCAST;
        if (!memcmp(m, "DVD ", 4)) return SYS_PS2;
        if (!memcmp(m, "CHT2", 4) || !memcmp(m, "CHTR", 4)) return SYS_PS1;
        meta = be64(m + 8);
    }
    return SYS_PS1;
}

/* Nintendo disc headers: Wii magic 5D1C9EA3 at 0x18, GameCube magic C2339F3D at 0x1C. */
static int nintendo_disc(FILE *f) {
    uint8_t h[32];
    if (fread(h, 1, sizeof h, f) != sizeof h) return -1;
    if (!memcmp(h + 0x18, "\x5D\x1C\x9E\xA3", 4)) return SYS_WII;
    if (!memcmp(h + 0x1C, "\xC2\x33\x9F\x3D", 4)) return SYS_GAMECUBE;
    return -1;
}

/* Xbox discs: "MICROSOFT*XBOX*MEDIA" at sector 32 of the game partition, which starts at 0 (rebuilt
 * XISO), 0xFD90000 (Xbox 360 XGD2 dump), 0x2080000 (XGD3) or 0x18300000 (original Xbox dump). The root
 * directory, located right after the magic, tells the console: default.xbe or default.xex.
 * Returns SYS_XBOX, SYS_X360 or -1. */
static int xbox_disc(FILE *f) {
    static const long partitions[] = {0, 0xFD90000, 0x2080000, 0x18300000};
    for (size_t i = 0; i < sizeof partitions / sizeof partitions[0]; i++) {
        uint8_t volume[28];
        if (fseek(f, partitions[i] + 0x10000, SEEK_SET) || fread(volume, 1, sizeof volume, f) != sizeof volume ||
            memcmp(volume, "MICROSOFT*XBOX*MEDIA", 20))
            continue;
        uint32_t sector = volume[20] | volume[21] << 8 | volume[22] << 16 | (uint32_t)volume[23] << 24;
        uint32_t size = volume[24] | volume[25] << 8 | volume[26] << 16 | (uint32_t)volume[27] << 24;
        if (size > 1 << 20) size = 1 << 20;
        char *dir = size ? malloc(size) : NULL;
        int system = SYS_X360;  /* as before when the directory cannot be read */
        if (dir && !fseek(f, partitions[i] + (long)sector * 2048, SEEK_SET) && fread(dir, 1, size, f) == size) {
            for (uint32_t n = 0; n < size; n++) dir[n] = (char)(dir[n] >= 'A' && dir[n] <= 'Z' ? dir[n] + 32 : dir[n]);
            if (memmem(dir, size, "default.xbe", 11)) system = SYS_XBOX;
        }
        free(dir);
        return system;
    }
    return -1;
}

/* System of a .cue: Redump's Dreamcast .cue files describe the GD-ROM "HIGH-DENSITY AREA"; Saturn
 * and Mega-CD discs start with "SEGA SEGASATURN" / "SEGADISCSYSTEM" in the first track (after
 * the 16-byte sync and header of a raw 2352-byte sector); anything else is a PlayStation CD. */
static int cue_system(FILE *f, const char *cue_path) {
    char text[4097];
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    if (strcasestr(text, "HIGH-DENSITY")) return SYS_DREAMCAST;
    const char *file = strcasestr(text, "FILE \"");
    const char *end = file ? strchr(file + 6, '"') : NULL;
    if (!end) return SYS_PS1;
    char track[1024];
    const char *slash = strrchr(cue_path, '/');
    snprintf(track, sizeof track, "%.*s%.*s", slash ? (int)(slash + 1 - cue_path) : 0, cue_path, (int)(end - file - 6), file + 6);
    FILE *t = fopen(track, "rb");
    uint8_t h[48] = {0};
    size_t got = t ? fread(h, 1, sizeof h, t) : 0;
    if (t) fclose(t);
    for (size_t at = 0; at <= 16 && at + 15 <= got; at += 16) {
        if (!memcmp(h + at, "SEGA SEGASATURN", 15)) return SYS_SATURN;
        if (!memcmp(h + at, "SEGADISCSYSTEM", 14)) return SYS_MEGADRIVE;
    }
    return SYS_PS1;
}

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* The central directory of a .zip (malloc'd, caller frees), or NULL. */
static uint8_t *zip_directory(FILE *f, uint32_t *size, uint16_t *count) {
    if (fseek(f, 0, SEEK_END)) return NULL;
    long length = ftell(f);
    long tail = length < 65557 ? length : 65557;  /* end record + longest comment */
    uint8_t *buf = malloc((size_t)tail);
    if (!buf || fseek(f, length - tail, SEEK_SET) || fread(buf, 1, (size_t)tail, f) != (size_t)tail) { free(buf); return NULL; }
    long end = -1;
    for (long i = tail - 22; i >= 0 && end < 0; i--)
        if (u32(buf + i) == 0x06054b50) end = i;
    if (end < 0) { free(buf); return NULL; }
    *count = u16(buf + end + 10);
    *size = u32(buf + end + 12);
    uint32_t offset = u32(buf + end + 16);
    free(buf);
    if (*size > 16u << 20) return NULL;
    uint8_t *dir = malloc(*size ? *size : 1);
    if (!dir || fseek(f, offset, SEEK_SET) || fread(dir, 1, *size, f) != *size) { free(dir); return NULL; }
    return dir;
}

/* Walks the directory entries until match(name) is true; returns that entry, or NULL. */
static const uint8_t *zip_find(const uint8_t *dir, uint32_t size, uint16_t count, bool (*match)(const char *, void *),
                               void *ctx, char *name, size_t name_size) {
    uint32_t p = 0;
    for (uint16_t i = 0; i < count && p + 46 <= size && u32(dir + p) == 0x02014b50; i++) {
        uint16_t n = u16(dir + p + 28), extra = u16(dir + p + 30), comment = u16(dir + p + 32);
        if (p + 46 + n > size) break;
        snprintf(name, name_size, "%.*s", (int)n, (const char *)dir + p + 46);
        if (match(name, ctx)) return dir + p;
        p += 46 + n + extra + comment;
    }
    return NULL;
}

static bool any_console_file(const char *name, void *ctx) {
    int s = system_of_name(name);
    if (s < 0) return false;
    *(int *)ctx = s;
    return true;
}

static bool file_of_system(const char *name, void *ctx) { return system_of_name(name) == *(int *)ctx; }

/* True when path sits directly in a folder named "mame": the player's sets for MAME. */
bool tremu_in_mame_folder(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return false;
    const char *parent = slash - 1;
    while (parent > path && parent[-1] != '/') parent--;
    return !strncasecmp(parent, "mame/", 5);
}

/* A UMD image: its ISO 9660 volume says "PSP GAME" as the system id. */
static bool psp_disc(FILE *f) {
    char id[8];
    return !fseek(f, 0x8008, SEEK_SET) && fread(id, 1, sizeof id, f) == sizeof id && !memcmp(id, "PSP GAME", 8);
}

/* An EBOOT.PBP holds a PS1 disc ("PSISOIMG" or "PSTITLEI" where its DATA.PSAR starts) or a PSP program. */
static bool pbp_is_psp(FILE *f) {
    uint8_t h[40], psar[8];
    if (fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "\0PBP", 4)) return false;
    if (fseek(f, u32(h + 36), SEEK_SET) || fread(psar, 1, sizeof psar, f) != sizeof psar) return true;
    return memcmp(psar, "PSISOIMG", 8) && memcmp(psar, "PSTITLEI", 8);
}

int tremu_system_detect(const char *path) {
    struct stat st;
    if (!stat(path, &st) && S_ISDIR(st.st_mode)) return tremu_in_mame_folder(path) ? SYS_MAME : SYS_ARCADE;  /* an unzipped set */
    const char *dot = strrchr(path, '.');
    if (!dot || strchr(dot, '/')) return -1;
    /* The "psp" folder settles the formats other consoles share, and adds homebrew programs. */
    if (strcasestr(path, "/psp/"))
        for (const char *const *e = (const char *const[]){".iso", ".cso", ".chd", ".pbp", ".elf", ".prx", NULL}; *e; e++)
            if (!strcasecmp(dot, *e)) return SYS_PSP;
    if (!strcasecmp(dot, ".pbp")) {
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        bool psp = pbp_is_psp(f);
        fclose(f);
        return psp ? SYS_PSP : SYS_PS1;
    }
    if (!strcasecmp(dot, ".iso")) {
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        bool psp = psp_disc(f);
        rewind(f);
        int nintendo = psp ? -1 : nintendo_disc(f);
        int xbox = !psp && nintendo < 0 ? xbox_disc(f) : -1;
        fclose(f);
        if (psp) return SYS_PSP;
        if (xbox >= 0) return xbox;
        /* Not Nintendo: a PlayStation CD (up to 700 MB) or a PS2 DVD. */
        return nintendo >= 0 ? nintendo : st.st_size > (off_t)900 << 20 ? SYS_PS2 : SYS_PS1;
    }
    if (!strcasecmp(dot, ".chd") || !strcasecmp(dot, ".cue")) {
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        int system = !strcasecmp(dot, ".chd") ? chd_system(f) : cue_system(f, path);
        fclose(f);
        return system;
    }
    if (strcasecmp(dot, ".zip")) return system_of_name(path);
    /* Naomi: a known gun game, or any set in a "naomi" / "atomiswave" folder. */
    const char *slash = strrchr(path, '/');
    char set[64];
    snprintf(set, sizeof set, "%.*s", (int)(dot - (slash ? slash + 1 : path)), slash ? slash + 1 : path);
    if (tremu_in_mame_folder(path)) return SYS_MAME;
    if (tremu_naomi_title(set)) return SYS_NAOMI;
    if (tremu_namco_title(set)) return SYS_NAMCO246;
    if (slash) {
        const char *parent = slash - 1;
        while (parent > path && parent[-1] != '/') parent--;
        if (!strncasecmp(parent, "naomi/", 6) || !strncasecmp(parent, "atomiswave/", 11)) return SYS_NAOMI;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t size;
    uint16_t count;
    uint8_t *dir = zip_directory(f, &size, &count);
    fclose(f);
    int system = SYS_ARCADE;
    char name[256];
    if (dir) zip_find(dir, size, count, any_console_file, &system, name, sizeof name);
    free(dir);
    /* Disc images (PS1, Dreamcast) are too big to unpack at each start: the "Mes jeux" page
     * unzips them on upload. */
    return system == SYS_PS1 || system == SYS_DREAMCAST || system == SYS_WII || system == SYS_GAMECUBE || system == SYS_PSP ? -1 : system;
}

bool tremu_system_unzip(const char *zip, TremuSystem system, const char *dir, char *out, size_t out_size) {
    FILE *f = fopen(zip, "rb");
    if (!f) return false;
    uint32_t dir_size;
    uint16_t count;
    uint8_t *cd = zip_directory(f, &dir_size, &count);
    char name[256];
    int want = system;
    const uint8_t *e = cd ? zip_find(cd, dir_size, count, file_of_system, &want, name, sizeof name) : NULL;
    bool ok = false;
    if (e) {
        uint16_t method = u16(e + 10);
        uint32_t packed = u32(e + 20), unpacked = u32(e + 24), local = u32(e + 42);
        const char *base = strrchr(name, '/');
        mkdir(dir, 0700);
        snprintf(out, out_size, "%s/%s", dir, base ? base + 1 : name);
        struct stat st;
        if (!stat(out, &st) && st.st_size == (off_t)unpacked) {
            ok = true;  /* already unpacked */
        } else if ((method == 0 || method == 8) && unpacked <= 64u << 20) {
            uint8_t header[30];
            uint8_t *in = malloc(packed ? packed : 1), *data = malloc(unpacked ? unpacked : 1);
            if (in && data && !fseek(f, local, SEEK_SET) && fread(header, 1, 30, f) == 30 && u32(header) == 0x04034b50 &&
                !fseek(f, (long)local + 30 + u16(header + 26) + u16(header + 28), SEEK_SET) &&
                fread(in, 1, packed, f) == packed) {
                if (method == 0) {
                    ok = packed == unpacked;
                    memcpy(data, in, packed < unpacked ? packed : unpacked);
                } else {
                    z_stream z = {.next_in = in, .avail_in = packed, .next_out = data, .avail_out = unpacked};
                    ok = inflateInit2(&z, -MAX_WBITS) == Z_OK;
                    if (ok) {
                        ok = inflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out == unpacked;
                        inflateEnd(&z);
                    }
                }
                FILE *o = ok ? fopen(out, "wb") : NULL;
                ok = o && fwrite(data, 1, unpacked, o) == unpacked;
                if (o) ok = fclose(o) == 0 && ok;
                if (!ok) remove(out);
            }
            free(in);
            free(data);
        }
        LOG("unzip %s from %s: %s", name, zip, ok ? out : "failed");
    }
    free(cd);
    fclose(f);
    return ok;
}

bool tremu_system_xbox_gun_game(const char *path) {
    /* The original Xbox games that support its light guns; matched on the file name. */
    static const char *const names[] = {"silent scope", "house of the dead", "starsky"};
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcasestr(name, names[i])) return true;
    return false;
}

bool tremu_system_gun_game(const char *path) {
    /* Console games made for a gun (GunCon, Menacer, Justifier, Dreamcast gun); everything else gets
     * the Touch controllers as a gamepad. Matched on the file name. */
    static const char *const names[] = {
        "time crisis", "crisis zone", "point blank", "vampire night", "ninja assault", "dead aim", "survivor",
        "endgame", "dino stalker", "gunfighter", "virtua cop", "die hard", "elemental gearbolt", "ghoul panic",
        "judge dredd", "rescue shot", "crypt killer", "area 51", "maximum force", "horned owl", "lethal enforcers",
        "guncon", "gun bullet", "gunbullet", "mighty hits", "moorhuhn", "ghostbusters", "silent scope", "starsky",
        "gun shooting", "gunvari", "galaxian", "house of the dead", "resident evil gun", "guncom",
        "confidential mission", "death crimson", "menacer", "body count", "arcade game", "the maze of the kings",
        "chaos control", "mechanical violator",
    };
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (strcasestr(name, names[i])) return true;
    return false;
}
