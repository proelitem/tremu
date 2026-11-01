#include "menu.h"
#include "internet.h"
#include "extgun.h"
#include "lang.h"
#include "core.h"
#include "perf.h"
#include <android/log.h>
#include <stdio.h>
#include <string.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

typedef struct Item {
    const char *label;
    int *(*field)(WfgOptions *);  /* NULL for actions and toggles */
    int min, max, step;
    const char *unit;
} Item;

static int *gun_pitch(WfgOptions *o) { return &o->gun_pitch; }
static int *gun_drop(WfgOptions *o) { return &o->gun_drop; }
static int *gun_back(WfgOptions *o) { return &o->gun_back; }
static int *screen_distance(WfgOptions *o) { return &o->screen_distance; }
static int *screen_width(WfgOptions *o) { return &o->screen_width; }
static int *screen_height(WfgOptions *o) { return &o->screen_height; }
static int *stereo(WfgOptions *o) { return &o->stereo; }

enum { ITEM_BACK = 0, ITEM_CROSSHAIR = 7, ITEM_VIBRATION, ITEM_VOXELS, ITEM_AI_DEPTH, ITEM_STEREO, ITEM_EXT, ITEM_PADS, ITEM_PERF, ITEM_LOCAL_P2, ITEM_MULTI, ITEM_SAVE, ITEM_LOAD, ITEM_RECENTER, ITEM_BACKGROUND, ITEM_X360_DRIVER, ITEM_LANGUAGE, ITEM_RESUME, ITEM_QUIT, ITEM_COUNT };
static const Item items[ITEM_COUNT] = {
    {"Retour au carrousel", NULL, 0, 0, 0, NULL},
    {"Angle de l'arme", gun_pitch, -60, 60, 1, "\xC2\xB0"},
    {"Descente de l'arme", gun_drop, 0, 40, 1, " cm"},
    {"Recul de l'arme", gun_back, -30, 40, 1, " cm"},
    {"Distance de l'\xC3\xA9" "cran", screen_distance, 100, 600, 10, " cm"},
    {"Largeur de l'\xC3\xA9" "cran", screen_width, 50, 600, 10, " cm"},
    {"Hauteur de l'\xC3\xA9" "cran", screen_height, -100, 100, 5, " cm"},
    {"Viseur", NULL, 0, 0, 0, NULL},
    {"Vibration au tir", NULL, 0, 0, 0, NULL},
    {"Vue 3D (voxels)", NULL, 0, 0, 0, NULL},
    {"Relief IA (jeux sans couches)", NULL, 0, 0, 0, NULL},
    {"St\xC3\xA9r\xC3\xA9oscopie (PS2, N64, X360)", stereo, 0, 200, 25, " %"},
    {"Arme Bluetooth / USB", NULL, 0, 0, 0, NULL},
    {"Contr\xC3\xB4les des consoles", NULL, 0, 0, 0, NULL},
    {"Optimisation", NULL, 0, 0, 0, NULL},
    {"Joueur 2 (Touch gauche)", NULL, 0, 0, 0, NULL},
    {"Multijoueur", NULL, 0, 0, 0, NULL},
    {"Sauvegarde rapide", NULL, 0, 0, 0, NULL},
    {"Charger la sauvegarde", NULL, 0, 0, 0, NULL},
    {"Recentrer l'\xC3\xA9" "cran devant moi", NULL, 0, 0, 0, NULL},
    {"Fond", NULL, 0, 0, 0, NULL},
    {"Pilote Xbox 360 (au red\xC3\xA9" "marrage)", NULL, 0, 0, 0, NULL},
    {"Langue", NULL, 0, 0, 0, NULL},
    {"Reprendre la partie", NULL, 0, 0, 0, NULL},
    {"Quitter Tremu", NULL, 0, 0, 0, NULL},
};

static JavaVM *jvm;
static jobject activity_ref;
static int selected;
static bool dirty = true, last_gun;
static char last_net_status[256];

enum { MAX_MULTI = 10 };
static char multi[MAX_MULTI][64];
static int multi_count, multi_current, multi_pick;

void wfg_menu_set_multiplayer(const char choices[][64], int count, int current) {
    if (count > MAX_MULTI) count = MAX_MULTI;
    bool same = count == multi_count && current == multi_current;
    for (int i = 0; same && i < count; i++) same = !strcmp(choices[i], multi[i]);
    if (same) return;
    if (current != multi_current) multi_pick = current;
    for (int i = 0; i < count; i++) snprintf(multi[i], sizeof multi[0], "%s", choices[i]);
    multi_count = count;
    multi_current = current;
    if (multi_pick >= multi_count) multi_pick = multi_current;
    dirty = true;
}

int wfg_menu_multiplayer_pick(void) { return multi_pick; }

/* The learning screen stays up until the last button is assigned. */
static bool learn_shown;
static int learn_drawn = -1;
static unsigned signal_drawn;

/* Party code being typed: indexes into WFG_CODE_ALPHABET, kept between attempts. */
static bool code_mode;
static int code_digits[WFG_CODE_LENGTH], code_cursor;
static char code_text[WFG_CODE_LENGTH + 1];

/* Text asked by the game: a line of characters edited like the code; the cursor may also stand on
 * the empty place after the last character, where changing the character adds one. */
enum { TEXT_MAX = 32 };
static const char TEXT_ALPHABET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.'";
static bool text_mode;
static char text_title[80], text_value[TEXT_MAX + 1];
static int text_cursor, text_limit;

void wfg_menu_begin_text(const char *title, const char *text, unsigned max_length) {
    text_mode = true;
    snprintf(text_title, sizeof text_title, "%s", title);
    snprintf(text_value, sizeof text_value, "%s", text);
    text_limit = max_length && max_length < TEXT_MAX ? (int)max_length : TEXT_MAX;
    text_value[text_limit] = 0;
    text_cursor = (int)strlen(text_value);
    if (text_cursor == text_limit) text_cursor--;
    dirty = true;
}

void wfg_menu_text_erase(void) {
    int n = (int)strlen(text_value);
    if (!text_mode || !n) return;
    if (text_cursor == n) text_cursor = n - 1;
    memmove(text_value + text_cursor, text_value + text_cursor + 1, (size_t)(n - text_cursor));
    dirty = true;
}

const char *wfg_menu_text(void) {
    int n = (int)strlen(text_value);
    while (n > 0 && text_value[n - 1] == ' ') text_value[--n] = 0;  /* extra spaces erase the end */
    return text_value;
}

void wfg_menu_begin_code(void) {
    code_mode = true;
    code_cursor = 0;
    dirty = true;
}

bool wfg_menu_entering_code(void) { return code_mode; }

const char *wfg_menu_code(void) {
    for (int i = 0; i < WFG_CODE_LENGTH; i++) code_text[i] = WFG_CODE_ALPHABET[code_digits[i]];
    code_text[WFG_CODE_LENGTH] = 0;
    return code_text;
}
/* "Console controls" page: one console's Touch-to-pad table. Lines: the console, the Touch
 * inputs, reset, back. */
enum { PAD_LINE_CONSOLE = 0, PAD_LINE_FIRST_INPUT = 1, PAD_LINE_RESET = PAD_LINE_FIRST_INPUT + TI_COUNT, PAD_LINE_BACK,
       PAD_LINES };
static bool pad_mode;
static int pad_console, pad_line;

void wfg_menu_set_pad_console(int console) {
    if (console >= 0 && console < WFG_PAD_CONSOLES) pad_console = console;
}

/* "Optimisation" page: the settings of this game, or of all games. */
enum { PERF_LINE_SCOPE, PERF_LINE_PROFILE, PERF_LINE_CPU, PERF_LINE_GPU, PERF_LINE_REFRESH, PERF_LINE_SHARPEN, PERF_LINE_RES,
       PERF_LINE_RESET, PERF_LINE_BACK, PERF_LINES };
static bool perf_mode, perf_for_game;
static int perf_line, perf_system = -1;
static char perf_game[64], perf_title[64], perf_speed_drawn[128], perf_system_drawn[128];

void wfg_menu_set_perf_game(const char *key, const char *title, int system) {
    snprintf(perf_game, sizeof perf_game, "%s", key);
    snprintf(perf_title, sizeof perf_title, "%s", title);
    perf_system = system;
    perf_for_game = false;
}

static const char *perf_profile_name(int profile) {
    static const char *const names[WFG_PERF_PROFILES] = {"Automatique", "Batterie", "\xC3\x89quilibr\xC3\xA9", "Performance",
                                                         "Qualit\xC3\xA9", "Personnalis\xC3\xA9"};
    return T(names[profile >= 0 && profile < WFG_PERF_PROFILES ? profile : 0]);
}

static const char *perf_level_name(int level) {
    static const char *const names[WFG_PERF_LEVELS] = {"\303\211conomie", "Mod\xC3\xA9r\xC3\xA9", "\xC3\x89lev\xC3\xA9", "Maximum"};
    return T(names[level >= 0 && level < WFG_PERF_LEVELS ? level : 0]);
}

/* Left/right or A on a line of the page: h is the direction. Returns true when a value changed. */
static bool perf_change(WfgOptions *o, int h) {
    if (perf_line == PERF_LINE_SCOPE) {
        if (perf_game[0]) perf_for_game = !perf_for_game;
        dirty = true;
        return false;
    }
    if (perf_line < PERF_LINE_PROFILE || perf_line > PERF_LINE_RES) return false;
    WfgPerf *p = perf_for_game ? wfg_options_perf_own(o, perf_game) : &o->perf;
    if (!p) return false;
    if (perf_line == PERF_LINE_PROFILE) {
        int profile = (p->profile + h + WFG_PERF_PROFILES) % WFG_PERF_PROFILES;
        /* The custom profile starts from what was in use. */
        if (profile == WFG_PERF_CUSTOM) wfg_perf_resolve(p, perf_system, p);
        p->profile = profile;
        return true;
    }
    if (p->profile != WFG_PERF_CUSTOM) {
        wfg_perf_resolve(p, perf_system, p);
        p->profile = WFG_PERF_CUSTOM;
    }
    if (perf_line == PERF_LINE_CPU) p->cpu = (p->cpu + h + WFG_PERF_LEVELS) % WFG_PERF_LEVELS;
    if (perf_line == PERF_LINE_GPU) p->gpu = (p->gpu + h + WFG_PERF_LEVELS) % WFG_PERF_LEVELS;
    if (perf_line == PERF_LINE_REFRESH) p->refresh = wfg_perf_next_refresh(p->refresh, h);
    if (perf_line == PERF_LINE_SHARPEN) p->sharpen = (p->sharpen + h + 3) % 3;
    if (perf_line == PERF_LINE_RES) p->res = (p->res + h + 4) % 4;
    return true;
}

static int vertical_dir, horizontal_dir;
static uint64_t vertical_next, horizontal_next;
static bool confirm_was;
static uint32_t pixels[MENU_WIDTH * MENU_HEIGHT];

void wfg_menu_init(JavaVM *vm, jobject activity) {
    jvm = vm;
    activity_ref = activity;
}

void wfg_menu_closed(void) {
    /* Closed during learning: give up and keep the previous buttons. */
    if (learn_shown) while (wfg_ext_learning()) wfg_ext_learn_skip();
    learn_shown = false;
}

void wfg_menu_reset(void) {
    selected = 0;
    code_mode = false;
    text_mode = false;
    pad_mode = false;
    perf_mode = false;
    if (learn_shown && wfg_ext_learning()) {
        /* The menu closed during learning: give up and keep the previous buttons. */
        while (wfg_ext_learning()) wfg_ext_learn_skip();
    }
    learn_shown = false;
    multi_pick = multi_current;
    dirty = true;
    vertical_dir = horizontal_dir = 0;
    confirm_was = true;  /* ignore the press that opened the menu */
}

/* Returns -1/0/+1 for a stick axis with a 400 ms initial delay then 120 ms repeat. */
static int repeat(float value, int *dir, uint64_t *next, uint64_t now) {
    int d = value > .6f ? 1 : value < -.6f ? -1 : 0;
    if (!d) { *dir = 0; return 0; }
    if (d != *dir) { *dir = d; *next = now + 400000000ull; return d; }
    if (now >= *next) { *next = now + 120000000ull; return d; }
    return 0;
}

MenuAction wfg_menu_input(float stick_x, float stick_y, bool confirm, uint64_t now, WfgOptions *o, bool *changed) {
    *changed = false;
    if (wfg_ext_learning() || learn_shown) {
        /* Learning a gun's buttons: presses arrive through wfg_ext_input; stick right skips one. */
        int h = repeat(stick_x, &horizontal_dir, &horizontal_next, now);
        if (h > 0) wfg_ext_learn_skip();
        char signal[48];
        if ((int)wfg_ext_learn_role() != learn_drawn || wfg_ext_last_signal(signal, sizeof signal) != signal_drawn) dirty = true;
        confirm_was = confirm;
        if (wfg_ext_learning()) return MENU_NONE;
        learn_shown = false;
        dirty = true;
        return MENU_EXT_LEARNED;
    }
    if (pad_mode) {
        int v = repeat(stick_y, &vertical_dir, &vertical_next, now);
        int h = repeat(stick_x, &horizontal_dir, &horizontal_next, now);
        bool pressed = confirm && !confirm_was;
        confirm_was = confirm;
        if (v) pad_line = (pad_line - v + PAD_LINES) % PAD_LINES;
        if (h && pad_line == PAD_LINE_CONSOLE) pad_console = (pad_console + h + WFG_PAD_CONSOLES) % WFG_PAD_CONSOLES;
        if ((h || pressed) && pad_line >= PAD_LINE_FIRST_INPUT && pad_line < PAD_LINE_RESET) {
            int input = pad_line - PAD_LINE_FIRST_INPUT, *target = &o->pad_map[pad_console][input];
            *target = wfg_pad_target_next((WfgPadConsole)pad_console, (WfgTouchInput)input, *target, h ? h : 1);
            *changed = true;
        }
        if (pressed && pad_line == PAD_LINE_RESET) {
            wfg_padmap_defaults((WfgPadConsole)pad_console, o->pad_map[pad_console]);
            *changed = true;
        }
        if (pressed && pad_line == PAD_LINE_BACK) pad_mode = false;
        if (v || h || pressed) dirty = true;
        return MENU_NONE;
    }
    if (perf_mode) {
        int v = repeat(stick_y, &vertical_dir, &vertical_next, now);
        int h = repeat(stick_x, &horizontal_dir, &horizontal_next, now);
        bool pressed = confirm && !confirm_was;
        confirm_was = confirm;
        if (v) perf_line = (perf_line - v + PERF_LINES) % PERF_LINES;
        if ((h || pressed) && perf_change(o, h ? h : 1)) *changed = true;
        if (pressed && perf_line == PERF_LINE_RESET) {
            if (perf_for_game) wfg_options_perf_forget(o, perf_game);
            else o->perf = (WfgPerf){0};
            *changed = true;
        }
        if (pressed && perf_line == PERF_LINE_BACK) perf_mode = false;
        if (v || h || pressed) dirty = true;
        return MENU_NONE;
    }
    if (text_mode) {
        int v = repeat(stick_y, &vertical_dir, &vertical_next, now);
        int h = repeat(stick_x, &horizontal_dir, &horizontal_next, now);
        if (v) {
            int size = (int)sizeof TEXT_ALPHABET - 1;
            bool adding = !text_value[text_cursor];
            const char *at = adding ? NULL : strchr(TEXT_ALPHABET, text_value[text_cursor]);
            int i = at ? (int)(at - TEXT_ALPHABET) : 0;
            text_value[text_cursor] = TEXT_ALPHABET[(i + v + size) % size];
            if (adding) text_value[text_cursor + 1] = 0;
        }
        if (h > 0 && text_value[text_cursor] && text_cursor + 1 < text_limit) {
            text_cursor++;
        } else if (h < 0 && text_cursor > 0) {
            text_cursor--;
        }
        if (v || h) dirty = true;
        bool pressed = confirm && !confirm_was;
        confirm_was = confirm;
        if (!pressed) return MENU_NONE;
        text_mode = false;
        dirty = true;
        return MENU_TEXT_ENTERED;
    }
    if (code_mode) {
        int v = repeat(stick_y, &vertical_dir, &vertical_next, now);
        int h = repeat(stick_x, &horizontal_dir, &horizontal_next, now);
        if (v) code_digits[code_cursor] = (code_digits[code_cursor] + v + 32) % 32;
        if (h) code_cursor = (code_cursor + h + WFG_CODE_LENGTH) % WFG_CODE_LENGTH;
        if (v || h) dirty = true;
        bool pressed = confirm && !confirm_was;
        confirm_was = confirm;
        if (!pressed) return MENU_NONE;
        code_mode = false;
        dirty = true;
        return MENU_CODE_ENTERED;
    }
    int v = repeat(stick_y, &vertical_dir, &vertical_next, now);
    if (v) {
        selected = (selected - v + ITEM_COUNT) % ITEM_COUNT;  /* stick up moves up the list */
        dirty = true;
    }
    int h = repeat(stick_x, &horizontal_dir, &horizontal_next, now);
    const Item *it = &items[selected];
    if (h && it->field) {
        int *value = it->field(o), next = *value + h * it->step;
        if (next < it->min) next = it->min;
        if (next > it->max) next = it->max;
        if (next != *value) { *value = next; *changed = dirty = true; }
    } else if (h && selected == ITEM_CROSSHAIR) {
        o->crosshair = !o->crosshair;
        *changed = dirty = true;
    } else if (h && selected == ITEM_MULTI && multi_count) {
        multi_pick = (multi_pick + h + multi_count) % multi_count;  /* preview only, A applies */
        dirty = true;
    } else if (h && selected == ITEM_VIBRATION) {
        o->vibration = !o->vibration;
        *changed = dirty = true;
    } else if (h && selected == ITEM_VOXELS) {
        o->voxels = !o->voxels;
        *changed = dirty = true;
    } else if (h && selected == ITEM_AI_DEPTH) {
        o->ai_depth = !o->ai_depth;
        *changed = dirty = true;
    } else if (h && selected == ITEM_LOCAL_P2) {
        o->local_p2 = !o->local_p2;
        *changed = dirty = true;
    } else if (h && selected == ITEM_BACKGROUND) {
        o->background = (o->background + h + 3) % 3;
        *changed = dirty = true;
    } else if (h && selected == ITEM_X360_DRIVER) {
        o->x360_driver = (o->x360_driver + h + WG_DRIVER_COUNT) % WG_DRIVER_COUNT;
        *changed = dirty = true;
    } else if (h && selected == ITEM_LANGUAGE) {
        o->language = ((int)wfg_lang() + h + LANG_COUNT) % LANG_COUNT;
        *changed = dirty = true;
        return MENU_LANGUAGE;
    }
    bool pressed = confirm && !confirm_was;
    confirm_was = confirm;
    if (!pressed) return MENU_NONE;
    if (selected == ITEM_BACK) return MENU_BACK;
    if (selected == ITEM_CROSSHAIR) { o->crosshair = !o->crosshair; *changed = dirty = true; }
    if (selected == ITEM_VIBRATION) { o->vibration = !o->vibration; *changed = dirty = true; }
    if (selected == ITEM_VOXELS) { o->voxels = !o->voxels; *changed = dirty = true; }
    if (selected == ITEM_AI_DEPTH) { o->ai_depth = !o->ai_depth; *changed = dirty = true; }
    if (selected == ITEM_LOCAL_P2) { o->local_p2 = !o->local_p2; *changed = dirty = true; }
    if (selected == ITEM_BACKGROUND) { o->background = (o->background + 1) % 3; *changed = dirty = true; }
    if (selected == ITEM_X360_DRIVER) { o->x360_driver = (o->x360_driver + 1) % WG_DRIVER_COUNT; *changed = dirty = true; }
    if (selected == ITEM_LANGUAGE) {
        o->language = ((int)wfg_lang() + 1) % LANG_COUNT;
        *changed = dirty = true;
        return MENU_LANGUAGE;
    }
    if (selected == ITEM_PADS) {
        pad_mode = true;
        pad_line = PAD_LINE_CONSOLE;
        dirty = true;
        return MENU_NONE;
    }
    if (selected == ITEM_PERF) {
        perf_mode = true;
        perf_line = PERF_LINE_PROFILE;
        perf_for_game = perf_game[0] && wfg_options_perf_has_own(o, perf_game);
        dirty = true;
        return MENU_NONE;
    }
    if (selected == ITEM_EXT) {
        wfg_ext_learn_start();
        learn_shown = true;
        learn_drawn = -1;
        dirty = true;
        return MENU_NONE;
    }
    if (selected == ITEM_SAVE) return MENU_SAVE_STATE;
    if (selected == ITEM_LOAD) return MENU_LOAD_STATE;
    if (selected == ITEM_MULTI) return multi_count && multi_pick != multi_current ? MENU_APPLY_MULTI : MENU_NONE;
    if (selected == ITEM_RECENTER) return MENU_RECENTER;
    if (selected == ITEM_RESUME) return MENU_CLOSE;
    if (selected == ITEM_QUIT) return MENU_QUIT;
    return MENU_NONE;
}

const uint32_t *wfg_menu_render(const WfgOptions *o, bool gun_connected, const char *net_status) {
    if (gun_connected != last_gun) { last_gun = gun_connected; dirty = true; }
    if (strcmp(net_status, last_net_status)) {
        snprintf(last_net_status, sizeof last_net_status, "%s", net_status);
        dirty = true;
    }
    if (!dirty || !jvm) return NULL;
    JNIEnv *env;
    if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_6) != JNI_OK &&
        (*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK)
        return NULL;

    char text[ITEM_COUNT + 2][256];
    int highlighted = selected + 1;
    if (learn_shown) {
        for (int i = 0; i < ITEM_COUNT + 2; i++) text[i][0] = 0;
        int role = wfg_ext_learn_role();
        learn_drawn = role;
        snprintf(text[0], sizeof text[0], "%s", T("Arme Bluetooth / USB"));
        snprintf(text[2], sizeof text[0], "%s", T("Connecte d'abord ton arme dans les param\xC3\xA8tres Bluetooth du Quest."));
        if (role < EXT_ROLES) {
            snprintf(text[4], sizeof text[0], T("Appuie sur %s de ton arme"), T(wfg_ext_role_name((ExtRole)role)));
            snprintf(text[5], sizeof text[0], T("Bouton %d sur %d"), role + 1, EXT_ROLES);
        }
        char signal[48];
        signal_drawn = wfg_ext_last_signal(signal, sizeof signal);
        snprintf(text[7], sizeof text[0], T("Dernier signal de l'arme : %s"), signal);
        snprintf(text[9], sizeof text[0], "%s", T("Stick Touch vers la droite : passer (r\xC3\xA9glage par d\xC3\xA9" "faut)"));
        snprintf(text[10], sizeof text[0], "%s", T("Menu : annuler"));
        highlighted = 4;
    } else if (pad_mode) {
        for (int i = 0; i < ITEM_COUNT + 2; i++) text[i][0] = 0;
        WfgPadConsole console = (WfgPadConsole)pad_console;
        snprintf(text[0], sizeof text[0], "%s", T("Contr\xC3\xB4les des consoles"));
        snprintf(text[1 + PAD_LINE_CONSOLE], sizeof text[0], "%s\t< %s >", T("Console"), wfg_pad_console_name(console));
        for (int i = 0; i < TI_COUNT; i++)
            snprintf(text[1 + PAD_LINE_FIRST_INPUT + i], sizeof text[0], "%s\t%s", T(wfg_touch_input_name((WfgTouchInput)i)),
                     T(wfg_pad_target_name(console, o->pad_map[console][i])));
        snprintf(text[1 + PAD_LINE_RESET], sizeof text[0], "%s", T("R\xC3\xA9initialiser cette console"));
        snprintf(text[1 + PAD_LINE_BACK], sizeof text[0], "%s", T("Retour"));
        snprintf(text[ITEM_COUNT + 1], sizeof text[0], "%s",
                 T("Stick gauche : choisir / changer     A : changer / valider"));
        highlighted = 1 + pad_line;
    } else if (perf_mode) {
        for (int i = 0; i < ITEM_COUNT + 2; i++) text[i][0] = 0;
        const WfgPerf *stored = perf_for_game ? wfg_options_perf(o, perf_game) : &o->perf;
        WfgPerf p;
        wfg_perf_resolve(stored, perf_system, &p);
        snprintf(text[0], sizeof text[0], "%s", T("Optimisation"));
        snprintf(text[1 + PERF_LINE_SCOPE], sizeof text[0], "%s\t< %s >", T("R\xC3\xA9glages pour"), perf_for_game ? perf_title : T("Tous les jeux"));
        snprintf(text[1 + PERF_LINE_PROFILE], sizeof text[0], "%s\t< %s >", T("Profil"), perf_profile_name(stored->profile));
        snprintf(text[1 + PERF_LINE_CPU], sizeof text[0], "%s\t%s", T("Processeur (CPU)"), perf_level_name(p.cpu));
        snprintf(text[1 + PERF_LINE_GPU], sizeof text[0], "%s\t%s", T("Graphique (GPU)"), perf_level_name(p.gpu));
        if (p.refresh) snprintf(text[1 + PERF_LINE_REFRESH], sizeof text[0], "%s\t%d Hz", T("Rafra\xC3\xAE" "chissement"), p.refresh);
        else snprintf(text[1 + PERF_LINE_REFRESH], sizeof text[0], "%s\t%s", T("Rafra\xC3\xAE" "chissement"), T("selon le jeu"));
        snprintf(text[1 + PERF_LINE_SHARPEN], sizeof text[0], "%s\t%s", T("Nettet\xC3\xA9"),
                 p.sharpen == 2 ? T("haute") : p.sharpen == 1 ? T("normale") : T("d\xC3\xA9sactiv\xC3\xA9" "e"));
        if (p.res) snprintf(text[1 + PERF_LINE_RES], sizeof text[0], "%s\t%dx", T("R\xC3\xA9solution interne (prochain lancement)"), p.res);
        else snprintf(text[1 + PERF_LINE_RES], sizeof text[0], "%s\t%s", T("R\xC3\xA9solution interne (prochain lancement)"), T("par d\xC3\xA9" "faut"));
        snprintf(text[1 + PERF_LINE_RESET], sizeof text[0], "%s", T(perf_for_game ? "Revenir aux r\xC3\xA9glages g\xC3\xA9n\xC3\xA9raux" : "R\xC3\xA9initialiser"));
        snprintf(text[1 + PERF_LINE_BACK], sizeof text[0], "%s", T("Retour"));
        wfg_perf_status(perf_speed_drawn, sizeof perf_speed_drawn, perf_system_drawn, sizeof perf_system_drawn);
        snprintf(text[1 + PERF_LINES + 1], sizeof text[0], "%s", perf_speed_drawn);
        snprintf(text[1 + PERF_LINES + 2], sizeof text[0], "%s", perf_system_drawn);
        snprintf(text[1 + PERF_LINES + 4], sizeof text[0], "%s", T("Maximum : plus de chaleur, batterie plus courte."));
        snprintf(text[ITEM_COUNT + 1], sizeof text[0], "%s", T("Stick gauche : choisir / changer     A : changer / valider"));
        highlighted = 1 + perf_line;
    } else if (text_mode) {
        for (int i = 0; i < ITEM_COUNT + 2; i++) text[i][0] = 0;
        snprintf(text[0], sizeof text[0], "%s", T("Saisie de texte"));
        snprintf(text[2], sizeof text[0], "%s", text_title);
        char *c = text[4];
        for (int i = 0; text_value[i]; i++) {
            if (i == text_cursor) *c++ = '[';
            if (text_value[i] == ' ') { *c++ = '\xC2'; *c++ = '\xB7'; }  /* middle dot: "_" is a real character */
            else *c++ = text_value[i];
            if (i == text_cursor) *c++ = ']';
        }
        if (!text_value[text_cursor]) { *c++ = '['; *c++ = ' '; *c++ = ']'; }
        *c = 0;
        snprintf(text[6], sizeof text[0], "%s", T("Stick haut/bas : changer le caract\xC3\xA8re"));
        snprintf(text[7], sizeof text[0], "%s", T("Stick gauche/droite : caract\xC3\xA8re pr\xC3\xA9" "c\xC3\xA9" "dent/suivant"));
        snprintf(text[8], sizeof text[0], "%s", T("A : valider     B : effacer     Menu : annuler"));
        highlighted = 4;
    } else if (code_mode) {
        /* Same panel, other lines: the code with the edited character in brackets. */
        for (int i = 0; i < ITEM_COUNT + 2; i++) text[i][0] = 0;
        snprintf(text[0], sizeof text[0], "%s", T("Rejoindre par code"));
        snprintf(text[2], sizeof text[0], "%s", T("Code de la partie (donn\xC3\xA9 par l'h\xC3\xB4te) :"));
        char *c = text[4];
        for (int i = 0; i < WFG_CODE_LENGTH; i++) {
            if (i == 5) { *c++ = '-'; *c++ = ' '; }
            if (i == code_cursor) *c++ = '[';
            *c++ = WFG_CODE_ALPHABET[code_digits[i]];
            if (i == code_cursor) *c++ = ']';
            *c++ = ' ';
        }
        *c = 0;
        snprintf(text[6], sizeof text[0], "%s", T("Stick haut/bas : changer le caract\xC3\xA8re"));
        snprintf(text[7], sizeof text[0], "%s", T("Stick gauche/droite : caract\xC3\xA8re pr\xC3\xA9" "c\xC3\xA9" "dent/suivant"));
        snprintf(text[8], sizeof text[0], "%s", T("A : rejoindre     Menu : annuler"));
        if (net_status[0]) snprintf(text[ITEM_COUNT + 1], sizeof text[0], "%s", net_status);
        highlighted = 4;
    } else {
    if (o->gun[0]) snprintf(text[0], sizeof text[0], T("Pause - options (arme : %s)"), o->gun);
    else snprintf(text[0], sizeof text[0], "%s", T("Pause - options"));
    for (int i = 0; i < ITEM_COUNT; i++) {
        const Item *it = &items[i];
        if (i == ITEM_STEREO && !o->stereo)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), T("d\xC3\xA9sactiv\xC3\xA9" "e"));
        else if (it->field)
            snprintf(text[i + 1], sizeof text[0], it->min < 0 ? "%s\t%+d%s" : "%s\t%d%s", T(it->label),
                     *it->field((WfgOptions *)o), it->unit);
        else if (i == ITEM_MULTI)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s%s", T(it->label), multi_count ? multi[multi_pick] : "",
                     multi_pick != multi_current ? T("  (A : valider)") : "");
        else if (i == ITEM_PADS)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), "NES, SNES, N64, PS, MD, SAT, DC, GC, Wii, 360");
        else if (i == ITEM_PERF)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), perf_profile_name(wfg_options_perf(o, perf_game)->profile));
        else if (i == ITEM_BACKGROUND)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label),
                     T(o->background == 1 ? "pi\xC3\xA8" "ce r\xC3\xA9" "elle" : o->background == 2 ? "pi\xC3\xA8" "ce assombrie" : "noir"));
        else if (i == ITEM_X360_DRIVER)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label),
                     o->x360_driver == WG_DRIVER_SYSTEM ? T("Adreno (syst\xC3\xA8me)") : "Turnip");
        else if (i == ITEM_LANGUAGE)
            /* Also named in English, for whoever opens the menu in a language they cannot read. */
            snprintf(text[i + 1], sizeof text[0], "%s%s\t%s", T(it->label), wfg_lang() == LANG_EN ? "" : " / Language",
                     wfg_lang_name(wfg_lang()));
        else if (i == ITEM_LOCAL_P2)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), o->local_p2 ? T("activ\xC3\xA9") : T("d\xC3\xA9sactiv\xC3\xA9"));
        else if (i == ITEM_EXT)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label),
                     o->ext_buttons[0] || o->ext_buttons[1] || o->ext_buttons[2] || o->ext_buttons[3] || o->ext_buttons[4]
                         ? T("boutons appris  (A : refaire)") : T("auto  (A : apprendre)"));
        else if (i == ITEM_VIBRATION)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), o->vibration ? T("activ\xC3\xA9" "e") : T("d\xC3\xA9sactiv\xC3\xA9" "e"));
        else if (i == ITEM_VOXELS)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), o->voxels ? T("activ\xC3\xA9") : T("d\xC3\xA9sactiv\xC3\xA9"));
        else if (i == ITEM_AI_DEPTH)
            snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), o->ai_depth ? T("activ\xC3\xA9") : T("d\xC3\xA9sactiv\xC3\xA9"));
        else if (i == ITEM_CROSSHAIR) snprintf(text[i + 1], sizeof text[0], "%s\t%s", T(it->label), o->crosshair ? T("affich\xC3\xA9") : T("masqu\xC3\xA9"));
        else snprintf(text[i + 1], sizeof text[0], "%s", T(it->label));
    }
    if (net_status[0])
        snprintf(text[ITEM_COUNT + 1], sizeof text[0], "%s", net_status);
    else
        snprintf(text[ITEM_COUNT + 1], sizeof text[0], T("%s   |   stick gauche : choisir / r\xC3\xA9gler   A : valider   Menu : reprendre"),
                 gun_connected ? T("WonderFitter connect\xC3\xA9") : T("WonderFitter non connect\xC3\xA9"));
    }

    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    jobjectArray lines = (*env)->NewObjectArray(env, ITEM_COUNT + 2, string_class, NULL);
    for (int i = 0; i < ITEM_COUNT + 2; i++) {
        wfg_utf8_trim(text[i]);
        jstring s = (*env)->NewStringUTF(env, text[i]);
        (*env)->SetObjectArrayElement(env, lines, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID render = (*env)->GetStaticMethodID(env, cls, "renderMenu", "([Ljava/lang/String;III)[I");
    jintArray result = render ? (*env)->CallStaticObjectMethod(env, cls, render, lines, highlighted, MENU_WIDTH, MENU_HEIGHT) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); result = NULL; }
    if (result) {
        jint *argb = (*env)->GetIntArrayElements(env, result, NULL);
        /* ARGB top-first from Android → RGBA bottom-first for GL. */
        for (int y = 0; y < MENU_HEIGHT; y++)
            for (int x = 0; x < MENU_WIDTH; x++) {
                uint32_t p = (uint32_t)argb[y * MENU_WIDTH + x];
                pixels[(MENU_HEIGHT - 1 - y) * MENU_WIDTH + x] =
                    (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16);
            }
        (*env)->ReleaseIntArrayElements(env, result, argb, JNI_ABORT);
        (*env)->DeleteLocalRef(env, result);
    }
    (*env)->DeleteLocalRef(env, lines);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, string_class);
    if (!result) { LOG("menu render failed"); return NULL; }
    dirty = false;
    return pixels;
}
