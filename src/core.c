/* Minimal libretro frontend: loads one core with dlopen, runs it frame by frame,
 * converts its video to RGBA8 and streams its audio through AAudio. */
#include "core.h"
#include "libretro.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

/* One loaded core per system (arcade, NES...). Cores stay loaded and initialised for the whole
 * process: FBNeo crashes in retro_init after a retro_deinit, so switching system only switches
 * which core runs, and each keeps its own state. */
enum { MAX_CORES = 6, MAX_PORTS = 4 };
typedef struct Core {
    char name[64];
    void *lib;
    void (*init)(void), (*deinit)(void), (*run)(void);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void (*get_av_info)(struct retro_system_av_info *);
    void (*get_system_info)(struct retro_system_info *);
    void (*set_port_device)(unsigned, unsigned);
    size_t (*serialize_size)(void);
    bool (*serialize)(void *, size_t);
    bool (*unserialize)(const void *, size_t);
    void *(*memory_data)(unsigned);
    size_t (*memory_size)(unsigned);
    const uint8_t *(*nes_layers)(unsigned *left, unsigned *top);  /* our FCEUmm build only */
    const uint8_t *(*layers)(unsigned *w, unsigned *h);           /* our FBNeo build, some drivers */
    void (*set_stereo)(int percent);                              /* our N64 build */
    /* Lightgun device each port offers (SET_CONTROLLER_INFO), 0 when none: e.g. Super Scope on
     * SNES port 2. Ports without one keep the core's automatic choice (the NES Zapper). */
    unsigned gun_device[MAX_PORTS];
    unsigned justifier_device[MAX_PORTS];  /* Konami's gun, when the port offers it too */
    unsigned zapper_device;                /* NES Zapper offered on the second port (a mouse subclass) */
    bool initialized;
    enum retro_pixel_format init_format;  /* set in retro_init by some cores (MAME), kept for every game */
} Core;
/* PlayStation GunCon in use: its A and B buttons stand for start and the pedal. */
static bool guncon;
static bool gamepad;  /* this game is played with the Touch gamepad, no gun plugged */
/* PS2 GunCon 2: an off-screen gun reports (0, 0), the "no light" Time Crisis' calibration waits
 * for, so an unused gun blocks it. An idle gun (no trigger, no reload) aimed off screen is
 * reported where it last aimed (the centre at first), like a gun resting towards the screen. */
static bool guncon2;
static int16_t rest_x[TREMU_PLAYERS], rest_y[TREMU_PLAYERS];
/* A real GunCon sees nothing while the screen is black: Time Crisis flashes one black frame after
 * each calibration shot and waits for that "no light" report. Set after each frame. */
static bool frame_dark;
void tremu_core_set_gamepad(bool on) { gamepad = on; }
/* Dolphin: the gun is a Wiimote, aimed through the pointer device, trigger on B. */
static bool wiimote;
static Core cores[MAX_CORES];
static Core *cur;

static char system_path[512], save_path[512];
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static struct retro_system_av_info av;
static retro_keyboard_event_t keyboard_cb;
static uint32_t *frame;
static unsigned frame_w, frame_h, frame_cap;
static uint8_t *layer_frame;
static unsigned layer_cap;
static bool layers_valid;
/* Players' guns (1 to 4) for the frame being emulated. */
static const TremuGun *current_guns;
/* Player whose gun each port reads. Cores with a gun on the first port (arcade, PS1, Dreamcast...)
 * map port n to player n+1; otherwise the first port read as a lightgun is player 1 (the NES
 * Zapper and the Super Scope sit in the console's second port). */
static int gun_player[MAX_PORTS], gun_players;
/* Game file kept in memory for cores that load from a buffer (need_fullpath false). */
static void *rom_data;
static AAudioStream *audio;

/* GPU cores (Flycast...) draw with OpenGL ES into our framebuffer, from the emulator thread, in
 * an EGL context of their own; each finished frame is read back into the RGBA frame buffer, so the
 * rest of the app (XR screen, netplay, menu) works the same as with the software cores. */
static struct retro_hw_render_callback hw;
static bool hw_requested, hw_active;
static EGLDisplay hw_display = EGL_NO_DISPLAY;
static EGLContext hw_context = EGL_NO_CONTEXT, hw_ppsspp_context = EGL_NO_CONTEXT;
static EGLSurface hw_surface = EGL_NO_SURFACE;
static GLuint hw_fbo, hw_color, hw_depth;
static unsigned hw_w, hw_h;

/* Core options declared with SET_VARIABLES: "key" -> "Description; default|other|...". */
enum { MAX_OPTIONS = 256 };
static struct { char key[64]; char value[64]; char choices[512]; } options[MAX_OPTIONS];
static int option_count;
/* Options change from the XR thread (menu) while the core reads them on its own thread. */
static pthread_mutex_t options_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool options_updated, crosshair_on = true;
static atomic_int vulkan_driver;

const char *tremu_core_driver_name(int driver) {
    static const char *const names[TREMU_DRIVER_COUNT] = {"Tremu-Turnip", "Adreno"};
    return names[driver >= 0 && driver < TREMU_DRIVER_COUNT ? driver : 0];
}

void tremu_core_set_vulkan_driver(int driver) { atomic_store(&vulkan_driver, driver); }

static atomic_int internal_res;
static atomic_int stereo_percent;
/* The eyes of the frame the core is sending (RETRO_ENVIRONMENT_SET_VIDEO_VIEWS). */
static unsigned view_left_x, view_right_x, view_w;
static GLuint hw_pbo[2];
static GLsync hw_pbo_fence[2];
static unsigned hw_pbo_w[2], hw_pbo_h[2];
static int hw_pbo_next;

/* The choice of a resolution option for this multiple (NULL: not such an option, or the default). */
static const char *internal_res_value(const char *key) {
    int m = atomic_load(&internal_res);
    if (m < 1 || m > 3) return NULL;
    static const char *const size_4_3[] = {"640x480", "1280x960", "1920x1440"};
    static const char *const dolphin[] = {"1", "2", "3"};
    static const char *const pcsx2[] = {"1x", "2x", "2x"};  /* LRPS2 offers 1x, 2x, 4x, 8x */
    static const char *const psp[] = {"480x272", "960x544", "1440x816"};
    if (strstr(key, "reicast_internal_resolution") || strstr(key, "43screensize")) return size_4_3[m - 1];
    if (strstr(key, "dolphin_efb_scale") && !strstr(key, "copy")) return dolphin[m - 1];
    if (strstr(key, "pcsx2_upscale_multiplier")) return pcsx2[m - 1];
    if (strstr(key, "ppsspp_internal_resolution")) return psp[m - 1];
    return NULL;
}

/* PPSSPP's ad hoc network, read through its options. Set under options_lock. */
static struct { char server[13], mac[13], name[16]; bool builtin_server, relay; } adhoc;

/* The value of a PPSSPP network option (NULL: not one). Its server address and MAC address are
 * twelve options of one digit each. */
static const char *adhoc_value(const char *key) {
    static char digit[2];
    int n;
    if (strncmp(key, "ppsspp_", 7) || !adhoc.server[0]) return NULL;
    key += 7;
    if (!strcmp(key, "enable_wlan")) return "enabled";
    if (!strcmp(key, "enable_builtin_pro_ad_hoc_server")) return adhoc.builtin_server ? "enabled" : "disabled";
    if (!strcmp(key, "adhoc_relay")) return adhoc.relay ? "enabled" : "disabled";
    if (!strcmp(key, "change_pro_ad_hoc_server_address")) return "IP address";
    if (sscanf(key, "pro_ad_hoc_server_address%d", &n) == 1 && n >= 1 && n <= 12) digit[0] = adhoc.server[n - 1];
    else if (sscanf(key, "change_mac_address%d", &n) == 1 && n >= 1 && n <= 12) digit[0] = adhoc.mac[n - 1];
    else return NULL;
    return digit;
}

/* Our overrides, matched on key fragments; everything else keeps the core's first (default) value. */
static const struct { const char *key, *value; } overrides[] = {
    {"lightgun_mode", "lightgun"},           /* recent MAME */
    {"xy_device", "lightgun"},               /* MAME 2003-Plus */
    {"diagnostic-input", "none"},            /* FBNeo: coin+start must not open the service menu */
    {"hiscores", "enabled"},                 /* FBNeo: keep high scores (needs bios/fbneo/hiscore.dat) */
    {"dolphin_ir_mode", "2"},                /* Dolphin: the Wiimote pointer follows an absolute pointer */
    {"EnableOverscan", "Disabled"},          /* Our N64: the eyes are copied straight to our framebuffer */
};

static const char *pick_value(const char *key, const char *values) {
    static char chosen[64];
    const char *want = NULL;
    for (size_t i = 0; i < sizeof overrides / sizeof overrides[0]; i++)
        if (strstr(key, overrides[i].key)) want = overrides[i].value;
    /* Crosshair follows the menu setting (FBNeo hides it for lightguns by default). */
    if (strstr(key, "lightgun-crosshair")) want = atomic_load(&crosshair_on) ? "always show" : "always hide";
    else if (strstr(key, "crosshair_enabled") || strstr(key, "show_crosshair"))  /* MAME, FCEUmm */
        want = atomic_load(&crosshair_on) ? "enabled" : "disabled";
    else if (strstr(key, "rearmed_crosshair") || (strstr(key, "_lightgun") && strstr(key, "_crosshair") && !strstr(key, "size")))
        want = atomic_load(&crosshair_on) ? "white" : "disabled";
    else if (strstr(key, "snes9x_") && strstr(key, "_crosshair") && !atomic_load(&crosshair_on)) want = "0";
    else if (strstr(key, "xenia_vulkan_driver")) want = tremu_core_driver_name(atomic_load(&vulkan_driver));
    else if (strstr(key, "virtuagun_crosshair")) want = atomic_load(&crosshair_on) ? "Cross" : "Off";  /* Saturn */
    else if (strstr(key, "gun_cursor")) want = atomic_load(&crosshair_on) ? "enabled" : "disabled";  /* Mega Drive, Mega-CD */
    else if (internal_res_value(key)) want = internal_res_value(key);
    else if (adhoc_value(key)) want = adhoc_value(key);
    /* LRPS2 keeps the room for two eyes from the start; it makes them only while
     * GET_VIDEO_VIEWS_STATUS says we show them, so the menu can turn them on and off in a game. */
    else if (strstr(key, "pcsx2_vr_mode")) want = "stereoscopic";
    else if (strstr(key, "pcsx2_vr_depth")) {
        static char percent[16];
        snprintf(percent, sizeof percent, "%d%%", atomic_load(&stereo_percent));
        want = percent;
    }
    const char *list = strchr(values, ';');
    list = list ? list + 1 : values;
    while (*list == ' ') list++;
    char copy[1024];
    snprintf(copy, sizeof copy, "%s", list);
    char *save = NULL, *first = strtok_r(copy, "|", &save), *item = first;
    snprintf(chosen, sizeof chosen, "%s", first ? first : "");
    for (; want && item; item = strtok_r(NULL, "|", &save))
        if (strcasestr(item, want)) { snprintf(chosen, sizeof chosen, "%s", item); break; }
    return chosen;
}

static void set_variables(const struct retro_variable *vars) {
    pthread_mutex_lock(&options_lock);
    for (; vars && vars->key; vars++) {
        if (!vars->value) continue;  /* LRPS2 declares some options without values */
        /* A key declared again (next game) replaces its entry instead of adding one. */
        int i = 0;
        while (i < option_count && strcmp(options[i].key, vars->key)) i++;
        if (i == MAX_OPTIONS) break;
        snprintf(options[i].key, sizeof options[0].key, "%s", vars->key);
        snprintf(options[i].choices, sizeof options[0].choices, "%s", vars->value);
        snprintf(options[i].value, sizeof options[0].value, "%s", pick_value(vars->key, vars->value));
        LOG("option %s = %s   (%s)", vars->key, options[i].value, vars->value);
        if (i == option_count) option_count++;
    }
    pthread_mutex_unlock(&options_lock);
}

void tremu_core_set_adhoc(const char *server_ip, bool builtin_server, bool relay, const char *mac) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    sscanf(server_ip, "%u.%u.%u.%u", &a, &b, &c, &d);
    pthread_mutex_lock(&options_lock);
    snprintf(adhoc.server, sizeof adhoc.server, "%03u%03u%03u%03u", a & 255, b & 255, c & 255, d & 255);
    snprintf(adhoc.mac, sizeof adhoc.mac, "%s", mac);
    snprintf(adhoc.name, sizeof adhoc.name, "Tremu-%s", strlen(mac) >= 4 ? mac + strlen(mac) - 4 : mac);
    adhoc.builtin_server = builtin_server;
    adhoc.relay = relay;
    /* PPSSPP stays loaded and declares its options once: choose again among those already declared. */
    for (int i = 0; i < option_count; i++)
        if (adhoc_value(options[i].key))
            snprintf(options[i].value, sizeof options[0].value, "%s", pick_value(options[i].key, options[i].choices));
    pthread_mutex_unlock(&options_lock);
    atomic_store(&options_updated, true);
}

void tremu_core_set_internal_res(int multiple) {
    atomic_store(&internal_res, multiple);
    /* Cores stay loaded and declare their options once: choose again among those already declared. */
    pthread_mutex_lock(&options_lock);
    for (int i = 0; i < option_count; i++) {
        if (!strstr(options[i].key, "reicast_internal_resolution") && !strstr(options[i].key, "43screensize") &&
            !strstr(options[i].key, "dolphin_efb_scale") && !strstr(options[i].key, "pcsx2_upscale_multiplier") &&
            !strstr(options[i].key, "ppsspp_internal_resolution"))
            continue;
        snprintf(options[i].value, sizeof options[0].value, "%s", pick_value(options[i].key, options[i].choices));
        LOG("option %s = %s", options[i].key, options[i].value);
        atomic_store(&options_updated, true);
    }
    pthread_mutex_unlock(&options_lock);
}

void tremu_core_set_stereo(int percent) {
    if (atomic_exchange(&stereo_percent, percent) == percent) return;
    pthread_mutex_lock(&options_lock);
    for (int i = 0; i < option_count; i++) {
        if (!strstr(options[i].key, "pcsx2_vr_depth")) continue;
        snprintf(options[i].value, sizeof options[0].value, "%s", pick_value(options[i].key, options[i].choices));
        atomic_store(&options_updated, true);
    }
    pthread_mutex_unlock(&options_lock);
}

static const char *get_variable(const char *key) {
    static char value[64];
    const char *found = NULL;
    pthread_mutex_lock(&options_lock);
    for (int i = 0; i < option_count && !found; i++)
        if (!strcmp(options[i].key, key)) found = strcpy(value, options[i].value);
    pthread_mutex_unlock(&options_lock);
    return found;
}

void tremu_core_set_crosshair(bool on) {
    atomic_store(&crosshair_on, on);
    pthread_mutex_lock(&options_lock);
    for (int i = 0; i < option_count; i++)
        if (strstr(options[i].key, "crosshair") || strstr(options[i].key, "gun_cursor"))
            snprintf(options[i].value, sizeof options[0].value, "%s", pick_value(options[i].key, options[i].choices));
    pthread_mutex_unlock(&options_lock);
    atomic_store(&options_updated, true);
}

static void core_log(enum retro_log_level level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(level >= RETRO_LOG_WARN ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, "Tremu-core", fmt, ap);
    va_end(ap);
}

static uintptr_t hw_framebuffer(void) { return hw_fbo; }
static retro_proc_address_t hw_proc_address(const char *sym) { return (retro_proc_address_t)eglGetProcAddress(sym); }

/* The emulator thread's EGL context (created once, never destroyed: the display is shared with
 * the XR renderer, which must not be terminated). PPSSPP gets its own: it counts on a context in its
 * default state (after LRPS2 its picture was mostly black), and LRPS2 crashes on a new one. */
static bool hw_make_current(void) {
    EGLContext *context = strstr(cur->name, "ppsspp") ? &hw_ppsspp_context : &hw_context;
    if (hw_display != EGL_NO_DISPLAY && *context == EGL_NO_CONTEXT) {
        const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config;
        EGLint n = 0;
        const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        if (eglChooseConfig(hw_display, attrs, &config, 1, &n) && n > 0)
            *context = eglCreateContext(hw_display, config, EGL_NO_CONTEXT, context_attrs);
        if (*context == EGL_NO_CONTEXT) { LOG("hw: EGL context failed"); return false; }
    }
    if (hw_display == EGL_NO_DISPLAY) {
        hw_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!eglInitialize(hw_display, NULL, NULL)) return false;
        const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config;
        EGLint n = 0;
        if (!eglChooseConfig(hw_display, attrs, &config, 1, &n) || n < 1) { LOG("hw: no EGL config"); return false; }
        const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        *context = eglCreateContext(hw_display, config, EGL_NO_CONTEXT, context_attrs);
        const EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        hw_surface = eglCreatePbufferSurface(hw_display, config, size);
        if (*context == EGL_NO_CONTEXT || hw_surface == EGL_NO_SURFACE) { LOG("hw: EGL context failed"); return false; }
    }
    return eglMakeCurrent(hw_display, hw_surface, hw_surface, *context);
}

/* Framebuffer of the largest frame the core announced, then the core sets up its GL state. */
static bool hw_start(void) {
    if (!hw_make_current()) return false;
    /* Some cores announce their largest upscaled size (Play!: 5120x3584); frames bigger than
     * the cap are cut, so they keep their native or 2x resolution. */
    hw_w = av.geometry.max_width ? av.geometry.max_width : 640;
    hw_h = av.geometry.max_height ? av.geometry.max_height : 480;
    /* LRPS2 and our N64 at 2x with their two eyes side by side are 2560 wide. */
    unsigned hw_w_cap = strstr(cur->name, "pcsx2") || strstr(cur->name, "n64") ? 4096 : 2048;
    if (hw_w > hw_w_cap) hw_w = hw_w_cap;
    if (hw_h > 2048) hw_h = 2048;
    glGenFramebuffers(1, &hw_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, hw_fbo);
    glGenRenderbuffers(1, &hw_color);
    glBindRenderbuffer(GL_RENDERBUFFER, hw_color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei)hw_w, (GLsizei)hw_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, hw_color);
    if (hw.depth || hw.stencil) {
        glGenRenderbuffers(1, &hw_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, hw_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, hw.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24, (GLsizei)hw_w, (GLsizei)hw_h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, hw.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, hw_depth);
    }
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    LOG("hw framebuffer %ux%u: %s (%s)", hw_w, hw_h, status == GL_FRAMEBUFFER_COMPLETE ? "ok" : "incomplete",
        (const char *)glGetString(GL_RENDERER));
    if (status != GL_FRAMEBUFFER_COMPLETE) return false;
    hw_active = true;
    if (hw.context_reset) hw.context_reset();
    return true;
}

static void hw_stop(void) {
    if (!hw_active) return;
    hw_make_current();
    if (hw.context_destroy) hw.context_destroy();
    for (int i = 0; i < 2; i++) {
        if (hw_pbo_fence[i]) glDeleteSync(hw_pbo_fence[i]);
        hw_pbo_fence[i] = 0;
        hw_pbo_w[i] = hw_pbo_h[i] = 0;
    }
    if (hw_pbo[0]) glDeleteBuffers(2, hw_pbo);
    hw_pbo[0] = hw_pbo[1] = 0;
    glDeleteFramebuffers(1, &hw_fbo);
    glDeleteRenderbuffers(1, &hw_color);
    if (hw_depth) glDeleteRenderbuffers(1, &hw_depth);
    hw_fbo = hw_color = hw_depth = 0;
    hw_active = false;
}

static bool environment(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: pixel_format = *(enum retro_pixel_format *)data; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: *(const char **)data = system_path; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = save_path; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = core_log; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 0; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: set_variables(data); return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = get_variable(v->key);
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = atomic_exchange(&options_updated, false); return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        keyboard_cb = ((struct retro_keyboard_callback *)data)->callback;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: av = *(struct retro_system_av_info *)data; return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: av.geometry = *(struct retro_game_geometry *)data; return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: LOG("message: %s", ((struct retro_message *)data)->msg); return true;
    case RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS:
        *(unsigned *)data = atomic_load(&stereo_percent) > 0 ? RETRO_VIDEO_VIEWS_STATUS_PRESENTS | RETRO_VIDEO_VIEWS_STATUS_STEREO : 0;
        return true;
    case RETRO_ENVIRONMENT_SET_VIDEO_VIEWS: {
        const struct retro_video_views *map = data;
        view_w = 0;
        for (unsigned i = 0; i < map->num_views; i++) {
            if (map->views[i].eye == RETRO_VIDEO_VIEW_EYE_LEFT) view_left_x = map->views[i].x;
            if (map->views[i].eye == RETRO_VIDEO_VIEW_EYE_RIGHT) view_right_x = map->views[i].x;
            view_w = map->views[i].width;
        }
        return map->num_views == 0 || map->num_views == 2;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
        *(uint64_t *)data = (1 << RETRO_DEVICE_JOYPAD) | (1 << RETRO_DEVICE_LIGHTGUN);
        return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: *(unsigned *)data = RETRO_HW_CONTEXT_OPENGLES3; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        struct retro_hw_render_callback *cb = data;
        if (cb->context_type != RETRO_HW_CONTEXT_OPENGLES2 && cb->context_type != RETRO_HW_CONTEXT_OPENGLES3 &&
            cb->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION) {
            LOG("hw render type %d refused (OpenGL ES only)", cb->context_type);
            return false;
        }
        cb->get_current_framebuffer = hw_framebuffer;
        cb->get_proc_address = hw_proc_address;
        hw = *cb;
        hw_requested = true;
        LOG("hw render: GLES type %d %u.%u, depth %d stencil %d, bottom-left %d", cb->context_type, cb->version_major,
            cb->version_minor, cb->depth, cb->stencil, cb->bottom_left_origin);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT: return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_GET_USERNAME:
        if (!adhoc.name[0]) return false;
        *(const char **)data = adhoc.name;
        return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
        /* Called from retro_set_environment, while cur is the core being opened. */
        /* Per port: the last device named "...gun" (FBNeo's "Analog Arcade Gun", the GunCon),
         * else the first lightgun (SNES Super Scope); Konami's gun is kept apart. */
        const struct retro_controller_info *ports = data;
        for (unsigned p = 0; ports && ports[p].types && p < MAX_PORTS; p++) {
            bool named = false;
            for (unsigned i = 0; i < ports[p].num_types; i++) {
                const struct retro_controller_description *t = &ports[p].types[i];
                if (p == 1 && t->desc && strcasestr(t->desc, "zapper")) cur->zapper_device = t->id;
                if ((t->id & RETRO_DEVICE_MASK) != RETRO_DEVICE_LIGHTGUN || !t->desc) continue;
                LOG("%s port %u lightgun %u: %s", cur->name, p, t->id, t->desc);
                bool gun = strcasestr(t->desc, "gun") != NULL || strcasestr(t->desc, "menacer") != NULL;
                if (strcasestr(t->desc, "justifier") || strcasestr(t->desc, "konami")) {
                    if (!strcasestr(t->desc, "(2P)")) cur->justifier_device[p] = t->id;
                } else if (gun || (!named && !cur->gun_device[p])) {
                    cur->gun_device[p] = t->id;
                    named = named || gun;
                }
            }
        }
        return true;
    }
    default: return false;
    }
}

static bool frame_size(unsigned w, unsigned h) {
    if (w * h > frame_cap) {
        free(frame);
        frame_cap = w * h;
        frame = malloc(frame_cap * sizeof *frame);
        if (!frame) { frame_cap = 0; return false; }
    }
    frame_w = w;
    frame_h = h;
    return true;
}

/* GL frames are read as RGBA bytes, bottom row first: the layout frame already uses. */
static void hw_readback(unsigned w, unsigned h) {
    if (w > hw_w) w = hw_w;
    if (h > hw_h) h = hw_h;
    if (!w || !h || !frame_size(w, h)) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, hw_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, frame);
    for (unsigned i = 0; i < w * h; i++) frame[i] |= 0xFF000000u;  /* cores may leave alpha at 0 */
    if (!hw.bottom_left_origin)
        for (unsigned y = 0; y < h / 2; y++)
            for (unsigned x = 0; x < w; x++) {
                uint32_t t = frame[y * w + x];
                frame[y * w + x] = frame[(h - 1 - y) * w + x];
                frame[(h - 1 - y) * w + x] = t;
            }
}

/* The eyes keep the GPU busy longer, and glReadPixels would hold up the emulator until it is done:
 * while the core sends two eyes, read into a buffer and take the frame before, a frame later. */
static void hw_readback_async(unsigned w, unsigned h) {
    if (w > hw_w) w = hw_w;
    if (h > hw_h) h = hw_h;
    if (!w || !h) return;
    if (!hw_pbo[0]) glGenBuffers(2, hw_pbo);
    int done = hw_pbo_next ^ 1;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, hw_pbo[hw_pbo_next]);
    if (w * h != hw_pbo_w[hw_pbo_next] * hw_pbo_h[hw_pbo_next])
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 4, NULL, GL_STREAM_READ);
    hw_pbo_w[hw_pbo_next] = w;
    hw_pbo_h[hw_pbo_next] = h;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, hw_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    hw_pbo_fence[hw_pbo_next] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    if (hw_pbo_fence[done]) {
        unsigned dw = hw_pbo_w[done], dh = hw_pbo_h[done];
        glClientWaitSync(hw_pbo_fence[done], GL_SYNC_FLUSH_COMMANDS_BIT, 100000000);
        glDeleteSync(hw_pbo_fence[done]);
        hw_pbo_fence[done] = 0;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, hw_pbo[done]);
        const uint32_t *read = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)dw * dh * 4, GL_MAP_READ_BIT);
        if (read && frame_size(dw, dh))
            for (unsigned i = 0; i < dw * dh; i++) frame[i] = read[i] | 0xFF000000u;
        if (read) glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    hw_pbo_next = done;
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;  /* duplicate frame */
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        if (hw_active && view_w && hw.bottom_left_origin) hw_readback_async(w, h);
        else if (hw_active) hw_readback(w, h);
        return;
    }
    if (!frame_size(w, h)) return;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        /* GL textures start at the bottom row; libretro frames start at the top. */
        uint32_t *out = frame + (h - 1 - y) * w;
        if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *in = (const uint32_t *)row;
            for (unsigned x = 0; x < w; x++) {
                uint32_t p = in[x];
                out[x] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
            }
        } else {
            const uint16_t *in = (const uint16_t *)row;
            bool rgb565 = pixel_format == RETRO_PIXEL_FORMAT_RGB565;
            for (unsigned x = 0; x < w; x++) {
                uint16_t p = in[x];
                unsigned r = rgb565 ? (p >> 11) & 31 : (p >> 10) & 31;
                unsigned g = rgb565 ? ((p >> 5) & 63) << 2 : ((p >> 5) & 31) << 3;
                unsigned b = p & 31;
                out[x] = 0xFF000000u | ((b << 3) << 16) | (g << 8) | (r << 3);
            }
        }
    }
}

static size_t audio_batch(const int16_t *data, size_t frames) {
    if (audio) AAudioStream_write(audio, data, (int32_t)frames, 0);
    return frames;
}
static void audio_sample(int16_t left, int16_t right) {
    int16_t s[2] = {left, right};
    audio_batch(s, 1);
}

static void input_poll(void) {}

/* Keys for the arcade's service menu (MAME reads its test and volume switches from the keyboard). */
void tremu_core_set_key(unsigned key, bool down) {
    if (keyboard_cb) keyboard_cb(down, key, 0, 0);
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    /* Log each distinct query once: shows which inputs the core really reads. */
    static uint8_t seen[8][32];
    unsigned base = device & RETRO_DEVICE_MASK;
    if (port < 2 && base < 8 && id < 32 && !seen[base][id]) {
        seen[base][id] = 1;
        LOG("core reads port %u device %u id %u", port, device, id);
    }
    if (port >= MAX_PORTS || !current_guns) return 0;
    if (wiimote && !gamepad) {  /* Wii (a GameCube game is a pad game) */
        const TremuGun *g = &current_guns[port];
        if (base == RETRO_DEVICE_POINTER && index == 0) {
            switch (id) {
            case RETRO_DEVICE_ID_POINTER_X: return g->offscreen ? -0x8000 : g->x;
            case RETRO_DEVICE_ID_POINTER_Y: return g->offscreen ? -0x8000 : g->y;
            case RETRO_DEVICE_ID_POINTER_PRESSED: return g->trigger;
            }
        } else if (base == RETRO_DEVICE_JOYPAD) {
            /* Wiimote buttons as set in the Wii console controls (B trigger, A, 1, 2, +, -, shake on R2). */
            if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)g->pad;
            return id < 16 && (g->pad >> id & 1);
        }
        return 0;
    }
    if (base == RETRO_DEVICE_LIGHTGUN) {
        if (gun_player[port] < 0) {
            if (gun_players == TREMU_PLAYERS) return 0;
            gun_player[port] = gun_players++;
            LOG("port %u lightgun = player %d", port, gun_player[port] + 1);
        }
        int player = gun_player[port];
        const TremuGun *g = &current_guns[player];
        bool resting = guncon2 && g->offscreen && !g->trigger && !g->reload && !frame_dark;
        if (guncon2 && !g->offscreen) { rest_x[player] = g->x; rest_y[player] = g->y; }
        switch (id) {
        case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X: return resting ? rest_x[player] : g->x;
        case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y: return resting ? rest_y[player] : g->y;
        case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN: return (g->offscreen && !resting) || (guncon2 && frame_dark);
        case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER: return g->trigger;
        case RETRO_DEVICE_ID_LIGHTGUN_RELOAD: return g->reload;
        /* GunCon: A starts the game, B is Time Crisis' pedal (the grenade button). */
        case RETRO_DEVICE_ID_LIGHTGUN_AUX_A: return guncon ? g->start : g->aux_a;
        case RETRO_DEVICE_ID_LIGHTGUN_AUX_B: return guncon ? g->aux_a || g->aux_b : g->aux_b;
        case RETRO_DEVICE_ID_LIGHTGUN_START: return g->start;
        case RETRO_DEVICE_ID_LIGHTGUN_SELECT: return g->select;
        }
        return 0;
    }
    const TremuGun *g = &current_guns[port];
    if (base == RETRO_DEVICE_JOYPAD) {
        /* The Touch gamepad; coin and start are often read from the RetroPad even in lightgun
         * mode (and the NES Zapper games read them from the pad in the first port). */
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)g->pad;
        switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_START: return g->start || (g->pad >> id & 1);
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return g->select || (g->pad >> id & 1);
        }
        return id < 16 && (g->pad >> id & 1);
    }
    if (base == RETRO_DEVICE_ANALOG) {
        bool x = id == RETRO_DEVICE_ID_ANALOG_X;
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) return x ? g->lx : g->ly;
        if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) return x ? g->rx : g->ry;
        /* Pressure of a button (Dreamcast and GameCube triggers): fully pressed or not. */
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) return id < 16 && (g->pad >> id & 1) ? 0x7fff : 0;
    }
    return 0;
}

static void open_audio(double rate) {
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, (int32_t)(rate + .5));
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setBufferCapacityInFrames(b, (int32_t)(rate / 4));
    if (AAudioStreamBuilder_openStream(b, &audio) == AAUDIO_OK) {
        /* About 100 ms of buffer absorbs the jitter between XR frames and emulated frames. */
        AAudioStream_setBufferSizeInFrames(audio, (int32_t)(rate / 10));
        AAudioStream_requestStart(audio);
        LOG("audio %d Hz, buffer %d of %d frames", AAudioStream_getSampleRate(audio),
            AAudioStream_getBufferSizeInFrames(audio), AAudioStream_getBufferCapacityInFrames(audio));
    } else {
        audio = NULL;
    }
    AAudioStreamBuilder_delete(b);
}

#define SYM(name, var) if (!(*(void **)&var = dlsym(c->lib, name))) { LOG("missing %s", name); return false; }

static void *android_vm, *android_activity;
static bool (*core_text_pending)(char *, size_t, char *, size_t, unsigned *);
static void (*core_text_submit)(const char *);

bool tremu_core_text_pending(char *title, size_t title_size, char *text, size_t text_size, unsigned *max_length) {
    return core_text_pending && core_text_pending(title, title_size, text, text_size, max_length);
}

void tremu_core_text_submit(const char *text) {
    if (core_text_submit) core_text_submit(text);
}

void tremu_core_set_android(void *vm, void *activity) {
    android_vm = vm;
    android_activity = activity;
}

static bool open_core(Core *c, const char *path) {
    void (*set_env)(retro_environment_t), (*set_video)(retro_video_refresh_t), (*set_sample)(retro_audio_sample_t),
        (*set_batch)(retro_audio_sample_batch_t), (*set_poll)(retro_input_poll_t), (*set_state)(retro_input_state_t);
    c->lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!c->lib) { LOG("dlopen %s: %s", path, dlerror()); return false; }
    SYM("retro_set_environment", set_env) SYM("retro_set_video_refresh", set_video)
    SYM("retro_set_audio_sample", set_sample) SYM("retro_set_audio_sample_batch", set_batch)
    SYM("retro_set_input_poll", set_poll) SYM("retro_set_input_state", set_state)
    SYM("retro_init", c->init) SYM("retro_deinit", c->deinit) SYM("retro_run", c->run)
    SYM("retro_load_game", c->load_game) SYM("retro_unload_game", c->unload_game)
    SYM("retro_get_system_av_info", c->get_av_info) SYM("retro_get_system_info", c->get_system_info)
    *(void **)&c->set_port_device = dlsym(c->lib, "retro_set_controller_port_device");
    /* Optional: save states and memory access, used by netplay. */
    *(void **)&c->serialize_size = dlsym(c->lib, "retro_serialize_size");
    *(void **)&c->serialize = dlsym(c->lib, "retro_serialize");
    *(void **)&c->unserialize = dlsym(c->lib, "retro_unserialize");
    *(void **)&c->memory_data = dlsym(c->lib, "retro_get_memory_data");
    *(void **)&c->memory_size = dlsym(c->lib, "retro_get_memory_size");
    *(void **)&c->nes_layers = dlsym(c->lib, "tremu_nes_layers");
    *(void **)&c->layers = dlsym(c->lib, "tremu_layers");
    /* Our XenDroid build is dlopen'ed from native code: no JNI_OnLoad, so it gets the VM here. */
    /* Our N64 build takes the stereo strength directly. */
    *(void **)&c->set_stereo = dlsym(c->lib, "retro_tremu_set_stereo");
    void (*set_android)(void *, void *);
    *(void **)&set_android = dlsym(c->lib, "tremu_set_android");
    if (set_android && android_vm) set_android(android_vm, android_activity);
    if (dlsym(c->lib, "tremu_text_pending")) {
        *(void **)&core_text_pending = dlsym(c->lib, "tremu_text_pending");
        *(void **)&core_text_submit = dlsym(c->lib, "tremu_text_submit");
    }
    snprintf(c->name, sizeof c->name, "%s", path);
    cur = c;  /* the environment callback fills this core's controller info */
    set_env(environment);
    set_video(video_refresh);
    set_sample(audio_sample);
    set_batch(audio_batch);
    set_poll(input_poll);
    set_state(input_state);
    pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
    c->init();
    c->init_format = pixel_format;
    c->initialized = true;
    return true;
}

/* The core library named path, opened and initialised on first use; NULL on failure. */
static Core *find_core(const char *path) {
    for (int i = 0; i < MAX_CORES; i++)
        if (cores[i].lib && !strcmp(cores[i].name, path)) return &cores[i];
    for (int i = 0; i < MAX_CORES; i++) {
        if (cores[i].lib) continue;
        if (open_core(&cores[i], path)) return &cores[i];
        if (cores[i].lib) dlclose(cores[i].lib);
        memset(&cores[i], 0, sizeof cores[i]);
        return NULL;
    }
    return NULL;
}

static bool game_loaded;

/* Reads a whole file, for cores that load games from memory. */
static void *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    void *data = NULL;
    if (!fseek(f, 0, SEEK_END)) {
        long n = ftell(f);
        data = n > 0 && n <= 256L << 20 ? malloc((size_t)n) : NULL;
        if (data && (fseek(f, 0, SEEK_SET) || fread(data, 1, (size_t)n, f) != (size_t)n)) { free(data); data = NULL; }
        if (data) *size = (size_t)n;
    }
    fclose(f);
    return data;
}

bool tremu_core_load(const char *core, const char *system_dir, const char *save_dir, const char *rom) {
    snprintf(system_path, sizeof system_path, "%s", system_dir);
    snprintf(save_path, sizeof save_path, "%s", save_dir);
    tremu_core_close_game();
    Core *c = find_core(core);
    if (!c) return false;
    cur = c;
    pixel_format = c->init_format;
    frame_w = frame_h = 0;
    memset(&av, 0, sizeof av);
    for (int p = 0; p < MAX_PORTS; p++) gun_player[p] = -1;
    gun_players = 0;

    hw_requested = false;
    memset(&hw, 0, sizeof hw);
    struct retro_system_info info = {0};
    c->get_system_info(&info);
    struct retro_game_info game = {.path = rom};
    if (!info.need_fullpath) {
        rom_data = read_file(rom, &game.size);
        if (!rom_data) { LOG("cannot read %s", rom); return false; }
        game.data = rom_data;
    }
    if (!c->load_game(&game)) {
        LOG("retro_load_game failed for %s", rom);
        free(rom_data);
        rom_data = NULL;
        return false;
    }
    game_loaded = true;
    /* Konami's gun for its own games (Lethal Enforcers), else the usual gun of each port. */
    bool konami = strcasestr(rom, "lethal enforcers") != NULL;
    wiimote = strstr(core, "dolphin") != NULL;
    guncon = false;
    /* PS2: a GunCon 2 on each of the two USB ports (players 1 and 2). */
    unsigned gun_ports = gamepad ? 0 : strstr(core, "pcsx2") ? 2 : TREMU_PLAYERS;
    /* A pad game: gamepads everywhere, also unplugging the gun a previous game left there. */
    for (unsigned p = 0; gamepad && c->set_port_device && p < TREMU_PLAYERS; p++) c->set_port_device(p, RETRO_DEVICE_JOYPAD);
    /* Mega Drive: the Menacer (or Konami's Justifier) in the second port, the pad stays in the first. */
    bool mega_drive = strstr(core, "genesis") != NULL;
    for (unsigned p = 0; c->set_port_device && p < gun_ports; p++) {
        unsigned device = konami && c->justifier_device[p] ? c->justifier_device[p] : c->gun_device[p];
        if (!device || (mega_drive && p != 1)) continue;
        LOG("port %u set to device %u", p, device);
        /* FBNeo maps the gun only when a port's device changes, and it keeps the device from game to
         * game: without this, a second gun game in the same process never reads the gun. */
        if (strstr(core, "tremu_core")) c->set_port_device(p, RETRO_DEVICE_JOYPAD);
        c->set_port_device(p, device);
        guncon = guncon || (device != c->justifier_device[p] && strstr(core, "pcsx.so"));  /* PS1, not PS2 */
    }
    /* Dolphin binds its emulated Wiimotes only when told each port holds one ("WiiMote (upright)"
     * is RETRO_DEVICE_JOYPAD): without this the gun reaches nothing. */
    if (wiimote && c->set_port_device)
        for (unsigned p = 0; p < TREMU_PLAYERS; p++) c->set_port_device(p, RETRO_DEVICE_JOYPAD);
    /* NES: the Zapper goes in the second port (where Duck Hunt reads it), the pad stays in the
     * first for start and select. FCEUmm's own detection misses some dumps. */
    if (!c->gun_device[0] && c->zapper_device && c->set_port_device) {
        LOG("port 1 set to the Zapper (%u)", c->zapper_device);
        c->set_port_device(1, c->zapper_device);
    }
    /* Mega Drive: port 0 only offers the Master System's gun; its Menacer and Justifier are read on
     * port 1 (and 2) and stay "first port read = player 1", or player 1's gun would do nothing. */
    if (!gamepad && !mega_drive && (c->gun_device[0] || c->justifier_device[0])) {
        for (int p = 0; p < MAX_PORTS; p++) gun_player[p] = p;
        gun_players = TREMU_PLAYERS;
    }
    guncon2 = !gamepad && strstr(core, "pcsx2") != NULL;
    frame_dark = false;
    view_w = 0;
    if (guncon2) {
        /* Time Crisis II calibrates player 1 with the GunCon 2 in the second USB port. */
        gun_player[0] = 1;
        gun_player[1] = 0;
    }
    memset(rest_x, 0, sizeof rest_x);
    memset(rest_y, 0, sizeof rest_y);
    c->get_av_info(&av);
    if (hw_requested && !hw_start()) {
        LOG("no OpenGL ES for %s", rom);
        tremu_core_close_game();
        return false;
    }
    LOG("loaded %s with %s: %ux%u (max %ux%u) aspect %.3f, %.3f fps, %.0f Hz", rom, core, av.geometry.base_width,
        av.geometry.base_height, av.geometry.max_width, av.geometry.max_height, av.geometry.aspect_ratio,
        av.timing.fps, av.timing.sample_rate);
    open_audio(av.timing.sample_rate);
    return true;
}

void tremu_core_close_game(void) {
    if (audio) { AAudioStream_requestStop(audio); AAudioStream_close(audio); audio = NULL; }
    hw_stop();  /* before unloading, as RetroArch does */
    if (game_loaded) { cur->unload_game(); game_loaded = false; }
    free(rom_data);
    rom_data = NULL;
}

void tremu_core_unload(void) {
    tremu_core_close_game();
    for (int i = 0; i < MAX_CORES; i++) {
        if (cores[i].initialized) cores[i].deinit();
        if (cores[i].lib) dlclose(cores[i].lib);
        memset(&cores[i], 0, sizeof cores[i]);
    }
    cur = NULL;
}

size_t tremu_core_state_size(void) { return cur && cur->serialize_size ? cur->serialize_size() : 0; }
bool tremu_core_save_state(void *data, size_t size) { return cur && cur->serialize && cur->serialize(data, size); }
bool tremu_core_load_state(const void *data, size_t size) { return cur && cur->unserialize && cur->unserialize(data, size); }

/* Every byte of a small RAM; of a big one (PS2 32 MB, Dreamcast 16 MB) one byte in N so the
 * check reads a megabyte at most. This runs on the emulator thread once a second: bit by bit
 * over the whole RAM it took 100 to 300 ms on the headset, a hitch every second that also
 * held back the input packets. A divergence is never confined to one byte, so a sample
 * catches it a check or two later at worst. */
uint32_t tremu_core_ram_crc(void) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
            table[i] = c;
        }
    const uint8_t *ram = cur && cur->memory_data ? cur->memory_data(RETRO_MEMORY_SYSTEM_RAM) : NULL;
    size_t size = cur && cur->memory_size ? cur->memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
    size_t step = size > (size_t)1 << 20 ? size >> 20 : 1;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; ram && i < size; i += step) crc = table[(crc ^ ram[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void tremu_core_run(const TremuGun guns[TREMU_PLAYERS]) {
    current_guns = guns;
    if (hw_active) hw_make_current();
    if (cur && cur->set_stereo) cur->set_stereo(atomic_load(&stereo_percent));
    if (game_loaded) cur->run();
    if (guncon2 && frame && frame_w && frame_h) {
        /* Dark when nearly every sampled pixel is black. */
        size_t n = (size_t)frame_w * frame_h, lit = 0, samples = 0;
        for (size_t i = 0; i < n; i += 13, samples++) {
            uint32_t c = frame[i];
            lit += (c & 0xFF) > 24 || (c >> 8 & 0xFF) > 24 || (c >> 16 & 0xFF) > 24;
        }
        bool dark = lit * 50 < samples;
        if (dark != frame_dark) LOG("GunCon 2: screen %s", dark ? "black" : "lit");
        frame_dark = dark;
    }
    /* Layers come top row first: the NES ones as the full 256x240 picture (cropped like the
     * frame), the arcade ones at the frame size. Flipped like the frame. */
    layers_valid = false;
    if (!game_loaded || !frame_w || !frame_h) return;
    unsigned left = 0, top = 0, stride = frame_w, lw = 0, lh = 0;
    const uint8_t *src = NULL;
    if (cur->nes_layers) {
        src = cur->nes_layers(&left, &top);
        stride = 256;
        if (left + frame_w > 256 || top + frame_h > 240) src = NULL;
    } else if (cur->layers) {
        src = cur->layers(&lw, &lh);
        if (lw != frame_w || lh != frame_h) src = NULL;
    }
    if (!src) return;
    if (frame_w * frame_h > layer_cap) {
        free(layer_frame);
        layer_cap = frame_w * frame_h;
        layer_frame = malloc(layer_cap);
        if (!layer_frame) { layer_cap = 0; return; }
    }
    for (unsigned y = 0; y < frame_h; y++)
        memcpy(layer_frame + (frame_h - 1 - y) * frame_w, src + (top + y) * stride + left, frame_w);
    layers_valid = true;
}

bool tremu_core_views(unsigned *left_x, unsigned *right_x, unsigned *eye_w) {
    if (!view_w || view_left_x + view_w > frame_w || view_right_x + view_w > frame_w) return false;
    *left_x = view_left_x;
    *right_x = view_right_x;
    *eye_w = view_w;
    return true;
}

const uint32_t *tremu_core_frame(unsigned *w, unsigned *h) {
    *w = frame_w;
    *h = frame_h;
    return frame;
}

const uint8_t *tremu_core_layers(void) { return layers_valid ? layer_frame : NULL; }

double tremu_core_fps(void) { return av.timing.fps > 1 ? av.timing.fps : 60; }
float tremu_core_aspect(void) {
    if (av.geometry.aspect_ratio > 0) return av.geometry.aspect_ratio;
    return av.geometry.base_height ? (float)av.geometry.base_width / av.geometry.base_height : 4.f / 3.f;
}
unsigned tremu_core_max_width(void) { return av.geometry.max_width; }
unsigned tremu_core_max_height(void) { return av.geometry.max_height; }
