#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tremu_options_defaults(TremuOptions *o) {
    *o = (TremuOptions){.screen_distance = 220, .screen_width = 200, .screen_height = -10, .crosshair = true, .vibration = true, .voxels = true, .ai_depth = true, .stereo = 100, .language = -1};
    for (int c = 0; c < TREMU_PAD_CONSOLES; c++) tremu_padmap_defaults((TremuPadConsole)c, o->pad_map[c]);
}

static TremuGunProfile *find_profile(TremuOptions *o, const char *name) {
    for (int i = 0; i < o->profile_count; i++)
        if (!strcmp(o->profiles[i].name, name)) return &o->profiles[i];
    return NULL;
}

/* The name as saved: fits the config line, and "=" would end its key. */
static void perf_key(const char *game, char key[TREMU_PERF_KEY_SIZE]) {
    snprintf(key, TREMU_PERF_KEY_SIZE, "%s", game);
    for (char *c = key; *c; c++)
        if (*c == '=') *c = '_';
}

static int find_perf(const TremuOptions *o, const char *game) {
    char key[TREMU_PERF_KEY_SIZE];
    perf_key(game, key);
    for (int i = 0; key[0] && i < o->perf_game_count; i++)
        if (!strcmp(o->perf_games[i].game, key)) return i;
    return -1;
}

const TremuPerf *tremu_options_perf(const TremuOptions *o, const char *game) {
    int i = find_perf(o, game);
    return i >= 0 ? &o->perf_games[i].perf : &o->perf;
}

TremuPerf *tremu_options_perf_own(TremuOptions *o, const char *game) {
    int i = find_perf(o, game);
    if (i >= 0) return &o->perf_games[i].perf;
    if (!game[0] || o->perf_game_count == TREMU_MAX_PERF_GAMES) return NULL;
    TremuGamePerf *g = &o->perf_games[o->perf_game_count++];
    perf_key(game, g->game);
    g->perf = o->perf;
    return &g->perf;
}

bool tremu_options_perf_has_own(const TremuOptions *o, const char *game) { return find_perf(o, game) >= 0; }

void tremu_options_perf_forget(TremuOptions *o, const char *game) {
    int i = find_perf(o, game);
    if (i < 0) return;
    o->perf_games[i] = o->perf_games[--o->perf_game_count];
}

static bool parse_perf(const char *text, TremuPerf *p) {
    return sscanf(text, "%d,%d,%d,%d,%d,%d", &p->profile, &p->cpu, &p->gpu, &p->refresh, &p->sharpen, &p->res) == 6;
}

void tremu_options_load(const char *path, TremuOptions *o) {
    tremu_options_defaults(o);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[160], key[64], text[96];
    int pad_defaults = 0;  /* version of the console pad defaults the saved tables were made from */
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, " %63[^=]=%95[^\r\n]", key, text) != 2) continue;
        if (!strcmp(key, "game")) { snprintf(o->game, sizeof o->game, "%s", text); continue; }
        if (!strcmp(key, "gun")) { snprintf(o->gun, sizeof o->gun, "%s", text); continue; }
        if (!strcmp(key, "adhoc_mac")) { snprintf(o->adhoc_mac, sizeof o->adhoc_mac, "%s", text); continue; }
        if (!strcmp(key, "pad_defaults")) { pad_defaults = atoi(text); continue; }
        if (!strcmp(key, "perf")) { parse_perf(text, &o->perf); continue; }
        if (!strncmp(key, "perf.", 5)) {
            TremuGamePerf *g = &o->perf_games[o->perf_game_count];
            if (o->perf_game_count < TREMU_MAX_PERF_GAMES && parse_perf(text, &g->perf)) {
                snprintf(g->game, sizeof g->game, "%s", key + 5);
                o->perf_game_count++;
            }
            continue;
        }
        if (!strncmp(key, "pad.", 4)) {
            for (int c = 0; c < TREMU_PAD_CONSOLES; c++) {
                if (strcmp(key + 4, tremu_pad_config_key((TremuPadConsole)c))) continue;
                const char *p = text;
                for (int i = 0; i < TI_COUNT && p; i++) {
                    o->pad_map[c][i] = atoi(p);
                    p = strchr(p, ',');
                    if (p) p++;
                }
            }
            continue;
        }
        if (!strncmp(key, "profile.", 8) && o->profile_count < TREMU_MAX_GUN_PROFILES) {
            TremuGunProfile *p = &o->profiles[o->profile_count];
            snprintf(p->name, sizeof p->name, "%s", key + 8);
            if (sscanf(text, "%d,%d,%d", &p->pitch, &p->drop, &p->back) == 3) o->profile_count++;
            continue;
        }
        int value = atoi(text);
        if (!strcmp(key, "gun_pitch")) o->gun_pitch = value;
        else if (!strcmp(key, "gun_drop")) o->gun_drop = value;
        else if (!strcmp(key, "gun_back")) o->gun_back = value;
        else if (!strcmp(key, "screen_distance")) o->screen_distance = value;
        else if (!strcmp(key, "screen_width")) o->screen_width = value;
        else if (!strcmp(key, "screen_height")) o->screen_height = value;
        else if (!strcmp(key, "crosshair")) o->crosshair = value != 0;
        else if (!strcmp(key, "vibration")) o->vibration = value != 0;
        else if (!strcmp(key, "voxels")) o->voxels = value != 0;
        else if (!strcmp(key, "ai_depth")) o->ai_depth = value != 0;
        else if (!strcmp(key, "stereo")) o->stereo = value;
        else if (!strcmp(key, "local_p2")) o->local_p2 = value != 0;
        else if (!strcmp(key, "language")) o->language = value;
        else if (!strcmp(key, "background")) o->background = value;
        else if (!strcmp(key, "x360_driver")) o->x360_driver = value;
        else if (!strcmp(key, "ext_buttons")) sscanf(text, "%d,%d,%d,%d,%d", &o->ext_buttons[0], &o->ext_buttons[1], &o->ext_buttons[2], &o->ext_buttons[3], &o->ext_buttons[4]);
    }
    fclose(f);
    /* Version 2: on SNES, the Touch A gives Y and the Touch Y gives A (the player's choice). */
    if (pad_defaults < 2) tremu_padmap_defaults(TREMU_PAD_SNES, o->pad_map[TREMU_PAD_SNES]);
    /* Version 4: on PS Vita, the triggers are L and R, the grips touch the screens, the left click the rear pad. */
    if (pad_defaults < 4) tremu_padmap_defaults(TREMU_PAD_VITA, o->pad_map[TREMU_PAD_VITA]);
}

bool tremu_options_save(const char *path, const TremuOptions *o) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    bool ok = fprintf(f,
                      "gun_pitch=%d\ngun_drop=%d\ngun_back=%d\nscreen_distance=%d\nscreen_width=%d\n"
                      "screen_height=%d\ncrosshair=%d\nvibration=%d\nlocal_p2=%d\ngame=%s\ngun=%s\n",
                      o->gun_pitch, o->gun_drop, o->gun_back, o->screen_distance, o->screen_width,
                      o->screen_height, o->crosshair ? 1 : 0, o->vibration ? 1 : 0, o->local_p2 ? 1 : 0,
                      o->game, o->gun) > 0;
    fprintf(f, "voxels=%d\nai_depth=%d\nstereo=%d\nlanguage=%d\n", o->voxels ? 1 : 0, o->ai_depth ? 1 : 0, o->stereo, o->language);
    fprintf(f, "ext_buttons=%d,%d,%d,%d,%d\n", o->ext_buttons[0], o->ext_buttons[1], o->ext_buttons[2], o->ext_buttons[3], o->ext_buttons[4]);
    fprintf(f, "pad_defaults=4\nbackground=%d\nx360_driver=%d\nadhoc_mac=%s\n", o->background, o->x360_driver, o->adhoc_mac);
    for (int c = 0; c < TREMU_PAD_CONSOLES; c++) {
        fprintf(f, "pad.%s=", tremu_pad_config_key((TremuPadConsole)c));
        for (int i = 0; i < TI_COUNT; i++) fprintf(f, i ? ",%d" : "%d", o->pad_map[c][i]);
        fputc('\n', f);
    }
    for (int i = 0; i < o->profile_count; i++)
        fprintf(f, "profile.%s=%d,%d,%d\n", o->profiles[i].name, o->profiles[i].pitch, o->profiles[i].drop,
                o->profiles[i].back);
    fprintf(f, "perf=%d,%d,%d,%d,%d,%d\n", o->perf.profile, o->perf.cpu, o->perf.gpu, o->perf.refresh, o->perf.sharpen, o->perf.res);
    for (int i = 0; i < o->perf_game_count; i++) {
        const TremuPerf *p = &o->perf_games[i].perf;
        fprintf(f, "perf.%s=%d,%d,%d,%d,%d,%d\n", o->perf_games[i].game, p->profile, p->cpu, p->gpu, p->refresh, p->sharpen, p->res);
    }
    return fclose(f) == 0 && ok;
}

bool tremu_options_use_gun(TremuOptions *o, const char *name) {
    if (!name[0]) return false;
    snprintf(o->gun, sizeof o->gun, "%s", name);
    TremuGunProfile *p = find_profile(o, name);
    if (!p) {
        tremu_options_store_gun(o);
        return false;
    }
    bool changed = p->pitch != o->gun_pitch || p->drop != o->gun_drop || p->back != o->gun_back;
    o->gun_pitch = p->pitch;
    o->gun_drop = p->drop;
    o->gun_back = p->back;
    return changed;
}

void tremu_options_store_gun(TremuOptions *o) {
    if (!o->gun[0]) return;
    TremuGunProfile *p = find_profile(o, o->gun);
    if (!p) {
        if (o->profile_count == TREMU_MAX_GUN_PROFILES) return;
        p = &o->profiles[o->profile_count++];
        snprintf(p->name, sizeof p->name, "%s", o->gun);
    }
    p->pitch = o->gun_pitch;
    p->drop = o->gun_drop;
    p->back = o->gun_back;
}
