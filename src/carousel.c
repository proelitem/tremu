#include "lang.h"
#include "carousel.h"
#include "netplay.h"
#include "systems.h"
#include <android/log.h>
#include <dirent.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

enum { MAX_ENTRIES = 512 };
static WfgEntry entries[MAX_ENTRIES];
static int entry_count, local_count, selected;  /* entries [local_count, entry_count) are join tiles */
static float position;
static bool dirty = true;
static char last_footer[400];
static int stick_dir, stick_dir_y;
static uint64_t stick_next;
/* The carousel shows entries[visible[0..visible_count)]; position counts in that list. */
enum { TAB_ALL, TAB_ARCADE, TAB_SEGA, TAB_NINTENDO, TAB_PS, TAB_XBOX, TAB_COUNT };
static int visible[MAX_ENTRIES], visible_count, selected_view;
static int tab, bar_item;  /* bar_item TAB_COUNT is the magnifier */
static bool bar_focus;
static char search[33];
static JavaVM *jvm;
static jobject activity_ref;
static uint32_t carousel_pixels[CAROUSEL_WIDTH * CAROUSEL_HEIGHT];
static uint32_t logo_pixels[SPLASH_WIDTH * SPLASH_HEIGHT];
static char scanned_banner_dir[512];
static atomic_bool poster_ready, library_changed;

JNIEXPORT void JNICALL Java_com_tremu_app_LibraryServer_nativeLibraryChanged(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    atomic_store(&library_changed, true);
}

bool wfg_library_changed(void) { return atomic_exchange(&library_changed, false); }

JNIEXPORT void JNICALL Java_com_tremu_app_Posters_nativePosterReady(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    atomic_store(&poster_ready, true);
}

/* Archives the cores load next to a game (BIOS, protection chips); never shown as games. */
static const char *support_archives[] = {"cchip.zip", "neogeo.zip", "pgm.zip", "decocass.zip", "isgsm.zip",
                                         "naomi.zip", "naomi2.zip", "awbios.zip", "hod2bios.zip", NULL};

static const struct { const char *name, *title; } known_games[] = {
    {"opwolf", "Operation Wolf"}, {"term2", "Terminator 2"}, {"lethalen", "Lethal Enforcers"},
    {"ptblank", "Point Blank"}, {"area51", "Area 51"}, {"mechatt", "Mechanized Attack"},
    {"carnevil", "CarnEvil"}, {"maxforce", "Maximum Force"}, {"area51mx", "Area 51 / Maximum Force Duo"},
};

/* Other apps shown as tiles when installed; the banner is looked up under name. */
static const struct { const char *package, *title, *name; } known_apps[] = {
    {"org.timecrisis.quest", "Time Crisis", "timecrisis"},
    {"com.xmjssvr.BowArrowEn", "WonderFitter", "wonderfitter"},  /* WonderFitter's own app, with its gun */
};

void wfg_carousel_init(JavaVM *vm, jobject activity) {
    jvm = vm;
    activity_ref = activity;
}

static JNIEnv *jni(void) {
    JNIEnv *env;
    if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) return env;
    return (*jvm)->AttachCurrentThread(jvm, &env, NULL) == JNI_OK ? env : NULL;
}

static bool is_support_archive(const char *name) {
    for (int i = 0; support_archives[i]; i++)
        if (!strcasecmp(name, support_archives[i])) return true;
    return false;
}

static void find_banner(const char *banner_dir, const char *name, char *out, size_t size) {
    static const char *ext[] = {"png", "jpg", "jpeg", "webp", "PNG", "JPG", "JPEG", "WEBP"};
    out[0] = 0;
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++) {
        snprintf(out, size, "%s/%s.%s", banner_dir, name, ext[i]);
        if (!access(out, R_OK)) return;
    }
    out[0] = 0;
}

static int compare_entries(const void *a, const void *b) {
    return strcasecmp(((const WfgEntry *)a)->title, ((const WfgEntry *)b)->title);
}

/* Full title of an arcade set from the table bundled in the APK; false when unknown. */
static bool arcade_title(const char *name, char *out, size_t size) {
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "arcadeTitle", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring s = (*env)->NewStringUTF(env, name);
    jstring title = m ? (*env)->CallStaticObjectMethod(env, cls, m, s) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); title = NULL; }
    bool found = false;
    if (title) {
        const char *t = (*env)->GetStringUTFChars(env, title, NULL);
        if (t) { snprintf(out, size, "%s", t); found = true; (*env)->ReleaseStringUTFChars(env, title, t); }
        (*env)->DeleteLocalRef(env, title);
    }
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
    return found;
}

/* Asks Java to download the posters of the games that have none. */
static void fetch_missing_posters(const char *banner_dir) {
    JNIEnv *env = jni();
    if (!env) return;
    int missing = 0;
    for (int i = 0; i < entry_count; i++) missing += entries[i].rom[0] && !entries[i].banner[0];
    if (!missing) return;
    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    jobjectArray names = (*env)->NewObjectArray(env, missing, string_class, NULL);
    int k = 0;
    for (int i = 0; i < entry_count; i++) {
        if (!entries[i].rom[0] || entries[i].banner[0]) continue;
        char name[64];
        const char *key = wfg_entry_key(&entries[i]), *dot = strrchr(key, '.');
        snprintf(name, sizeof name, "%.*s", dot ? (int)(dot - key) : (int)strlen(key), key);
        /* "<libretro-thumbnails system, empty for arcade sets>\t<file name>\t<title>" */
        const char *thumbnails = wfg_system_info((WfgSystem)entries[i].system)->thumbnails;
        char request[256];
        snprintf(request, sizeof request, "%s\t%s\t%s", thumbnails ? thumbnails : "", name, entries[i].title);
        jstring s = (*env)->NewStringUTF(env, request);
        (*env)->SetObjectArrayElement(env, names, k++, s);
        (*env)->DeleteLocalRef(env, s);
    }
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "fetchPosters", "([Ljava/lang/String;Ljava/lang/String;)V");
    jstring dir = (*env)->NewStringUTF(env, banner_dir);
    if (m) (*env)->CallStaticVoidMethod(env, cls, m, names, dir);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
    (*env)->DeleteLocalRef(env, dir);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, names);
    (*env)->DeleteLocalRef(env, string_class);
    LOG("asked for %d missing posters", missing);
}

bool wfg_library_info(char *out, size_t size) {
    out[0] = 0;
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "libraryInfo", "()Ljava/lang/String;");
    jstring info = m ? (*env)->CallStaticObjectMethod(env, cls, m) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); info = NULL; }
    if (info) {
        const char *t = (*env)->GetStringUTFChars(env, info, NULL);
        if (t) { snprintf(out, size, "%s", t); (*env)->ReleaseStringUTFChars(env, info, t); }
        (*env)->DeleteLocalRef(env, info);
    }
    (*env)->DeleteLocalRef(env, cls);
    return out[0] != 0;
}

void wfg_carousel_poll_posters(void) {
    if (!atomic_exchange(&poster_ready, false)) return;
    for (int i = 0; i < local_count; i++) {
        WfgEntry *e = &entries[i];
        if (!e->rom[0] || e->banner[0]) continue;
        char name[64];
        const char *key = wfg_entry_key(e), *dot = strrchr(key, '.');
        snprintf(name, sizeof name, "%.*s", dot ? (int)(dot - key) : (int)strlen(key), key);
        find_banner(scanned_banner_dir, name, e->banner, sizeof e->banner);
        if (e->banner[0]) dirty = true;
    }
}

void wfg_fetch_depth_model(const char *path) {
    JNIEnv *env = jni();
    if (!env) return;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "fetchDepthModel", "(Ljava/lang/String;)V");
    jstring s = (*env)->NewStringUTF(env, path);
    if (m) (*env)->CallStaticVoidMethod(env, cls, m, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
}

bool wfg_zip_folder(const char *folder, const char *zip) {
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "zipFolder", "(Ljava/lang/String;Ljava/lang/String;)Z");
    jstring a = (*env)->NewStringUTF(env, folder), s = (*env)->NewStringUTF(env, zip);
    bool ok = m && (*env)->CallStaticBooleanMethod(env, cls, m, a, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); ok = false; }
    (*env)->DeleteLocalRef(env, a);
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
    return ok;
}

bool wfg_unpack_asset(const char *asset, const char *dir) {
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "unpackAsset", "(Ljava/lang/String;Ljava/lang/String;)Z");
    jstring a = (*env)->NewStringUTF(env, asset), s = (*env)->NewStringUTF(env, dir);
    bool ok = m && (*env)->CallStaticBooleanMethod(env, cls, m, a, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); ok = false; }
    (*env)->DeleteLocalRef(env, a);
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
    return ok;
}

void wfg_restart_after_exit(void) {
    JNIEnv *env = jni();
    if (!env) return;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "restartAfterExit", "()V");
    if (m) (*env)->CallStaticVoidMethod(env, cls, m);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
    (*env)->DeleteLocalRef(env, cls);
}

bool wfg_intent_game(char *path, size_t size) {
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "takeIntentGame", "()Ljava/lang/String;");
    jstring s = m ? (jstring)(*env)->CallStaticObjectMethod(env, cls, m) : NULL;
    bool found = false;
    if (s) {
        const char *chars = (*env)->GetStringUTFChars(env, s, NULL);
        snprintf(path, size, "%s", chars);
        (*env)->ReleaseStringUTFChars(env, s, chars);
        (*env)->DeleteLocalRef(env, s);
        found = path[0] != 0;
    }
    (*env)->DeleteLocalRef(env, cls);
    return found;
}

static bool package_installed(const char *package) {
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "isPackageInstalled", "(Ljava/lang/String;)Z");
    jstring s = (*env)->NewStringUTF(env, package);
    bool installed = m && (*env)->CallStaticBooleanMethod(env, cls, m, s);
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
    return installed;
}

/* "Duck Hunt (World) [!]" -> "Duck Hunt (NES)": region and dump tags dropped, system added. */
static void console_title(const char *name, const char *system, char *out, size_t size) {
    char base[64];
    snprintf(base, sizeof base, "%s", name);
    char *tag = base + strcspn(base, "([");
    while (tag > base && tag[-1] == ' ') tag--;
    if (tag > base) *tag = 0;
    snprintf(out, size, "%s (%s)", base, system);
}

/* Adds a game: file is its path, file_name the last part, name the same without extension. */
static void add_game(const char *file, const char *file_name, const char *name_in, int system, const char *banner_dir) {
    for (int i = 0; i < entry_count; i++)
        if (!strcasecmp(wfg_entry_key(&entries[i]), file_name)) return;  /* the first folder wins */
    WfgEntry *e = &entries[entry_count++];
    memset(e, 0, sizeof *e);
    e->system = system;
    char name[64];
    snprintf(name, sizeof name, "%s", name_in);
    snprintf(e->rom, sizeof e->rom, "%s", file);
        snprintf(e->title, sizeof e->title, "%s", name);
        if (system == SYS_NAOMI && wfg_naomi_title(name))
            snprintf(e->title, sizeof e->title, "%s", wfg_naomi_title(name));
        else if (system == SYS_NAMCO246)
            snprintf(e->title, sizeof e->title, "%s", wfg_namco_title(name));
        else if (system != SYS_ARCADE && system != SYS_MAME)
            console_title(name, wfg_system_info((WfgSystem)system)->label, e->title, sizeof e->title);
        else if (!arcade_title(name, e->title, sizeof e->title))
            for (size_t k = 0; k < sizeof known_games / sizeof known_games[0]; k++)
                if (!strcasecmp(name, known_games[k].name)) snprintf(e->title, sizeof e->title, "%s", known_games[k].title);
        find_banner(banner_dir, name, e->banner, sizeof e->banner);
}

/* Games of a system run by another app, one tile per game, whatever its files. rows_method is the
 * WfgActivity method listing them as (path, title, detail) triples: a Wii U game's update and DLC
 * only show in its details, a PS3 game's id. */
static void add_external_games(const char *rows_method, WfgSystem system, const char *banner_dir) {
    JNIEnv *env = jni();
    if (!env) return;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, rows_method, "()[Ljava/lang/String;");
    jobjectArray rows = m ? (*env)->CallStaticObjectMethod(env, cls, m) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); rows = NULL; }
    jsize count = rows ? (*env)->GetArrayLength(env, rows) : 0;
    for (jsize i = 0; i + 2 < count && entry_count < MAX_ENTRIES; i += 3) {
        jstring s[3];
        const char *c[3];
        for (int k = 0; k < 3; k++) {
            s[k] = (*env)->GetObjectArrayElement(env, rows, i + k);
            c[k] = (*env)->GetStringUTFChars(env, s[k], NULL);
        }
        const char *slash = strrchr(c[0], '/');
        int before = entry_count;
        add_game(c[0], slash ? slash + 1 : c[0], c[1], system, banner_dir);
        if (entry_count > before) {
            snprintf(entries[before].detail, sizeof entries[before].detail, "%s", c[2]);
            wfg_utf8_trim(entries[before].detail);
        }
        for (int k = 0; k < 3; k++) {
            (*env)->ReleaseStringUTFChars(env, s[k], c[k]);
            (*env)->DeleteLocalRef(env, s[k]);
        }
    }
    if (rows) (*env)->DeleteLocalRef(env, rows);
    (*env)->DeleteLocalRef(env, cls);
}

/* Games in a folder and its subfolders, up to 3 levels (Roms/ps1/<game>/<game>.cue). A folder
 * named after an arcade set is that set unzipped (fbneo/opwolf3/), which FBNeo reads as is. */
static void scan_dir(const char *path, int depth, const char *banner_dir) {
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) && entry_count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        char file[512];
        snprintf(file, sizeof file, "%s/%s", path, ent->d_name);
        if (ent->d_type == DT_DIR) {
            if (depth == 0 && (!strcasecmp(ent->d_name, "wiiu") || !strcasecmp(ent->d_name, "ps3") || !strcasecmp(ent->d_name, "vita"))) continue;  /* add_external_games */
            /* The disc folder of a Namco set belongs to its .zip (Roms/timecrs3 with timecrs3.zip). */
            if (wfg_namco_title(ent->d_name)) continue;
            char title[64];
            if (depth > 0 && wfg_in_mame_folder(file)) add_game(file, ent->d_name, ent->d_name, SYS_MAME, banner_dir);
            else if (depth > 0 && arcade_title(ent->d_name, title, sizeof title)) add_game(file, ent->d_name, ent->d_name, SYS_ARCADE, banner_dir);
            else if (depth < 3) scan_dir(file, depth + 1, banner_dir);
            continue;
        }
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || is_support_archive(ent->d_name)) continue;
        int system = wfg_system_detect(file);
        if (system < 0) continue;
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)(dot - ent->d_name), ent->d_name);
        /* A PSP program is always EBOOT.PBP: its folder names it. */
        if (!strcasecmp(ent->d_name, "EBOOT.PBP")) {
            const char *folder = strrchr(path, '/');
            snprintf(name, sizeof name, "%s", folder ? folder + 1 : path);
        }
        add_game(file, ent->d_name, name, system, banner_dir);
    }
    closedir(dir);
}

static int system_tab(int system) {
    switch (system) {
    case SYS_MEGADRIVE: case SYS_SATURN: case SYS_DREAMCAST: return TAB_SEGA;
    case SYS_NES: case SYS_SNES: case SYS_N64: case SYS_GAMECUBE: case SYS_WII: case SYS_WIIU: return TAB_NINTENDO;
    case SYS_PS1: case SYS_PS2: case SYS_PS3: case SYS_PSP: case SYS_VITA: return TAB_PS;
    case SYS_XBOX: case SYS_X360: return TAB_XBOX;
    default: return TAB_ARCADE;
    }
}

static bool entry_shown(const WfgEntry *e) {
    /* Installed apps and join tiles only appear under "all". */
    if (tab != TAB_ALL && (e->package[0] || e->join_key[0] || system_tab(e->system) != tab)) return false;
    size_t n = strlen(search);
    if (!n) return true;
    for (const char *t = e->title; *t; t++)
        if (!strncasecmp(t, search, n)) return true;
    return false;
}

/* After a change of tab, search or entries: keeps the selection when it is still shown. */
static void rebuild_view(void) {
    int view = -1;
    visible_count = 0;
    for (int i = 0; i < entry_count; i++) {
        if (!entry_shown(&entries[i])) continue;
        if (i == selected) view = visible_count;
        visible[visible_count++] = i;
    }
    if (view < 0) view = 0;
    if (visible_count) selected = visible[view];
    selected_view = view;
    position = (float)view;
    dirty = true;
}

int wfg_carousel_scan(const char *const *rom_dirs, int rom_dir_count, const char *banner_dir) {
    entry_count = 0;
    for (int d = 0; d < rom_dir_count; d++) scan_dir(rom_dirs[d], 0, banner_dir);
    add_external_games("wiiuRows", SYS_WIIU, banner_dir);
    add_external_games("ps3Rows", SYS_PS3, banner_dir);
    add_external_games("vitaRows", SYS_VITA, banner_dir);
    for (size_t k = 0; k < sizeof known_apps / sizeof known_apps[0] && entry_count < MAX_ENTRIES; k++) {
        if (!package_installed(known_apps[k].package)) continue;
        WfgEntry *e = &entries[entry_count++];
        memset(e, 0, sizeof *e);
        snprintf(e->title, sizeof e->title, "%s", known_apps[k].title);
        snprintf(e->package, sizeof e->package, "%s", known_apps[k].package);
        find_banner(banner_dir, known_apps[k].name, e->banner, sizeof e->banner);
    }
    qsort(entries, entry_count, sizeof entries[0], compare_entries);
    local_count = entry_count;
    snprintf(scanned_banner_dir, sizeof scanned_banner_dir, "%s", banner_dir);
    fetch_missing_posters(banner_dir);
    for (int i = 0; i < entry_count; i++)
        LOG("carousel %d: %s  rom=%s app=%s banner=%s", i, entries[i].title, entries[i].rom, entries[i].package,
            entries[i].banner[0] ? entries[i].banner : "(none)");
    selected = 0;
    rebuild_view();
    return entry_count;
}

int wfg_carousel_count(void) { return entry_count; }
const WfgEntry *wfg_carousel_entry(int i) { return i >= 0 && i < entry_count ? &entries[i] : NULL; }
int wfg_carousel_selected(void) { return visible_count ? selected : -1; }

void wfg_carousel_select(int i) {
    if (i < 0 || i >= entry_count) return;
    selected = i;
    if (!entry_shown(&entries[i])) {
        tab = TAB_ALL;
        search[0] = 0;
    }
    rebuild_view();
}

void wfg_carousel_set_joinable(const NetHost *hosts, int count) {
    WfgEntry joins[16];
    int n = 0;
    for (int h = 0; h < count && n < 16 && local_count + n < MAX_ENTRIES; h++) {
        WfgEntry *e = &joins[n++];
        memset(e, 0, sizeof *e);
        snprintf(e->join_ip, sizeof e->join_ip, "%s", hosts[h].ip);
        snprintf(e->join_key, sizeof e->join_key, "%s", hosts[h].rom_key);
        snprintf(e->title, sizeof e->title, T("Rejoindre : %s"), hosts[h].title);
        wfg_utf8_trim(e->title);
        for (int i = 0; i < local_count; i++)
            if (!entries[i].package[0] && !strcasecmp(wfg_entry_key(&entries[i]), hosts[h].rom_key)) {
                snprintf(e->rom, sizeof e->rom, "%s", entries[i].rom);
                snprintf(e->banner, sizeof e->banner, "%s", entries[i].banner);
            }
    }
    bool same = n == entry_count - local_count;
    for (int i = 0; same && i < n; i++)
        same = !strcmp(joins[i].join_ip, entries[local_count + i].join_ip) &&
               !strcmp(joins[i].join_key, entries[local_count + i].join_key);
    if (same) return;
    memcpy(&entries[local_count], joins, n * sizeof joins[0]);
    entry_count = local_count + n;
    if (selected >= entry_count) selected = entry_count ? entry_count - 1 : 0;
    rebuild_view();
}

int wfg_carousel_find_game(const char *key) {
    for (int i = 0; i < local_count; i++)
        if (!entries[i].package[0] && !strcasecmp(wfg_entry_key(&entries[i]), key)) return i;
    return -1;
}

const char *wfg_entry_key(const WfgEntry *e) {
    if (e->join_key[0]) return e->join_key;
    if (e->package[0]) return e->package;
    const char *slash = strrchr(e->rom, '/');
    return slash ? slash + 1 : e->rom;
}

void wfg_carousel_set_search(const char *text) {
    bar_focus = false;
    dirty = true;
    if (!strcmp(search, text)) return;
    snprintf(search, sizeof search, "%s", text);
    rebuild_view();
}

const char *wfg_carousel_search(void) { return search; }

bool wfg_carousel_in_bar(void) { return bar_focus; }

CarouselAction wfg_carousel_input(float stick_x, float stick_y, bool confirmed, uint64_t now) {
    int dy = fabsf(stick_y) <= fabsf(stick_x) ? 0 : stick_y > .6f ? 1 : stick_y < -.6f ? -1 : 0;
    if (dy && dy != stick_dir_y && (dy > 0) != bar_focus) {
        bar_focus = dy > 0;
        bar_item = tab;
        dirty = true;
    }
    stick_dir_y = dy;
    int d = fabsf(stick_x) < fabsf(stick_y) ? 0 : stick_x > .6f ? 1 : stick_x < -.6f ? -1 : 0;
    if (!d) {
        stick_dir = 0;
    } else {
        bool step = d != stick_dir || now >= stick_next;
        stick_next = now + (d != stick_dir ? 450000000ull : 160000000ull);
        stick_dir = d;
        if (step && bar_focus) {
            bar_item = (bar_item + d + TAB_COUNT + 1) % (TAB_COUNT + 1);
            if (bar_item < TAB_COUNT) {
                tab = bar_item;
                rebuild_view();
            }
            dirty = true;
        } else if (step && visible_count) {
            selected_view = (selected_view + d + visible_count) % visible_count;
            selected = visible[selected_view];
            dirty = true;
        }
    }
    if (!confirmed) return CAROUSEL_NONE;
    if (!bar_focus) return visible_count ? CAROUSEL_LAUNCH : CAROUSEL_NONE;
    if (bar_item == TAB_COUNT) return CAROUSEL_SEARCH;
    bar_focus = false;  /* a tab is chosen: back to its games */
    dirty = true;
    return CAROUSEL_NONE;
}

/* ARGB top-first from Android → RGBA bottom-first for GL. */
static void to_gl(const jint *argb, uint32_t *out, int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t p = (uint32_t)argb[y * w + x];
            out[(h - 1 - y) * w + x] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16);
        }
}

const uint32_t *wfg_carousel_render(float dt, const char *footer) {
    /* Wrap-around jumps (last → first) snap instead of scrolling past every poster. */
    if (fabsf(selected_view - position) > 1.5f) position = selected_view;
    float before = position;
    position += (selected_view - position) * fminf(1.f, dt * 12.f);
    if (fabsf(selected_view - position) < .002f) position = selected_view;
    if (strcmp(footer, last_footer)) {
        snprintf(last_footer, sizeof last_footer, "%s", footer);
        wfg_utf8_trim(last_footer);
        dirty = true;
    }
    if (!dirty && position == before) return NULL;
    JNIEnv *env = jni();
    if (!env) return NULL;

    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    jobjectArray titles = (*env)->NewObjectArray(env, visible_count, string_class, NULL);
    jobjectArray banners = (*env)->NewObjectArray(env, visible_count, string_class, NULL);
    for (int i = 0; i < visible_count; i++) {
        const WfgEntry *e = &entries[visible[i]];
        jstring t = (*env)->NewStringUTF(env, e->title), b = (*env)->NewStringUTF(env, e->banner);
        (*env)->SetObjectArrayElement(env, titles, i, t);
        (*env)->SetObjectArrayElement(env, banners, i, b);
        (*env)->DeleteLocalRef(env, t);
        (*env)->DeleteLocalRef(env, b);
    }
    jstring jfooter = (*env)->NewStringUTF(env, last_footer);
    /* Console of the chosen game, nothing for apps and join tiles. */
    const WfgEntry *shown = visible_count ? &entries[selected] : NULL;
    jstring jconsole = (*env)->NewStringUTF(env, shown && !shown->package[0] && !shown->join_key[0]
                                                     ? wfg_system_info((WfgSystem)shown->system)->label : "");
    jstring jsearch = (*env)->NewStringUTF(env, search);
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "renderCarousel",
                                            "([Ljava/lang/String;[Ljava/lang/String;FILjava/lang/String;Ljava/lang/String;IILjava/lang/String;II)[I");
    jintArray result = m ? (*env)->CallStaticObjectMethod(env, cls, m, titles, banners, position, selected_view, jconsole, jfooter,
                                                          tab, bar_focus ? bar_item : -1, jsearch, CAROUSEL_WIDTH, CAROUSEL_HEIGHT)
                         : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); result = NULL; }
    if (result) {
        jint *argb = (*env)->GetIntArrayElements(env, result, NULL);
        to_gl(argb, carousel_pixels, CAROUSEL_WIDTH, CAROUSEL_HEIGHT);
        (*env)->ReleaseIntArrayElements(env, result, argb, JNI_ABORT);
        (*env)->DeleteLocalRef(env, result);
    }
    (*env)->DeleteLocalRef(env, jfooter);
    (*env)->DeleteLocalRef(env, jconsole);
    (*env)->DeleteLocalRef(env, jsearch);
    (*env)->DeleteLocalRef(env, titles);
    (*env)->DeleteLocalRef(env, banners);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, string_class);
    if (!result) return NULL;
    dirty = false;
    return carousel_pixels;
}

const uint32_t *wfg_loading_render(const char *key, const char *title, const char *status, float angle) {
    JNIEnv *env = jni();
    if (!env) return NULL;
    int i = wfg_carousel_find_game(key);
    const char *banner = i >= 0 ? entries[i].banner : "";
    char line[400];
    snprintf(line, sizeof line, "%s", status);
    wfg_utf8_trim(line);
    jstring jt = (*env)->NewStringUTF(env, title), jb = (*env)->NewStringUTF(env, banner), js = (*env)->NewStringUTF(env, line);
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "renderLoading", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;FII)[I");
    jintArray result = m ? (*env)->CallStaticObjectMethod(env, cls, m, jt, jb, js, angle, CAROUSEL_WIDTH, CAROUSEL_HEIGHT) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); result = NULL; }
    if (result) {
        jint *argb = (*env)->GetIntArrayElements(env, result, NULL);
        to_gl(argb, carousel_pixels, CAROUSEL_WIDTH, CAROUSEL_HEIGHT);
        (*env)->ReleaseIntArrayElements(env, result, argb, JNI_ABORT);
        (*env)->DeleteLocalRef(env, result);
    }
    (*env)->DeleteLocalRef(env, jt);
    (*env)->DeleteLocalRef(env, jb);
    (*env)->DeleteLocalRef(env, js);
    (*env)->DeleteLocalRef(env, cls);
    dirty = true;  /* the carousel picture was overwritten */
    return result ? carousel_pixels : NULL;
}

const uint32_t *wfg_load_logo(void) {
    JNIEnv *env = jni();
    if (!env) return NULL;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "loadAsset", "(Ljava/lang/String;II)[I");
    jstring name = (*env)->NewStringUTF(env, "logo.png");
    jintArray result = m ? (*env)->CallStaticObjectMethod(env, cls, m, name, SPLASH_WIDTH, SPLASH_HEIGHT) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); result = NULL; }
    if (result) {
        jint *argb = (*env)->GetIntArrayElements(env, result, NULL);
        to_gl(argb, logo_pixels, SPLASH_WIDTH, SPLASH_HEIGHT);
        (*env)->ReleaseIntArrayElements(env, result, argb, JNI_ABORT);
        (*env)->DeleteLocalRef(env, result);
    }
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, cls);
    return result ? logo_pixels : NULL;
}

static bool launch_in_app(const char *launch_method, const char *path) {
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, launch_method, "(Ljava/lang/String;)Z");
    jstring s = (*env)->NewStringUTF(env, path);
    bool ok = m && (*env)->CallStaticBooleanMethod(env, cls, m, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); ok = false; }
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
    return ok;
}

bool wfg_launch_wiiu(const char *path) { return launch_in_app("launchWiiu", path); }

bool wfg_launch_ps3(const char *path) { return launch_in_app("launchPs3", path); }
bool wfg_launch_vita(const char *path) { return launch_in_app("launchVita", path); }

bool wfg_launch_package(const char *package) {
    JNIEnv *env = jni();
    if (!env) return false;
    jclass cls = (*env)->GetObjectClass(env, activity_ref);
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "launchPackage", "(Ljava/lang/String;)Z");
    jstring s = (*env)->NewStringUTF(env, package);
    bool ok = m && (*env)->CallStaticBooleanMethod(env, cls, m, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); ok = false; }
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cls);
    return ok;
}
