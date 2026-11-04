/* Tremu: OpenXR host for lightgun arcade games on Quest.
 * The game screen is an OpenXR quad layer floating in front of the player; the
 * right controller's aim ray is projected onto it and sent to the core as a lightgun. */
#include "lang.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <dirent.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "carousel.h"
#include "core.h"
#include "libretro.h"
#include "gun.h"
#include "menu.h"
#include "netplay.h"
#include "extgun.h"
#include "internet.h"
#include "options.h"
#include "systems.h"
#include "voxel.h"
#include "passthrough.h"
#include "perf.h"
#include "depth.h"
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)
#define XR_CHECK(call) xr_check((call), #call)

enum { LEFT, RIGHT, HANDS };

/* Saved settings (menu); the screen sits options.screen_* from screen_origin, which
 * starts at the LOCAL origin and moves with "recenter". */
static TremuOptions options;
static char options_path[512];
static XrPosef screen_origin = {{0, 0, 0, 1}, {0, 0, 0}};
static bool menu_open, menu_button_was;
static XrSwapchain menu_chain;
static XrSwapchainImageOpenGLESKHR *menu_images;
static uint32_t menu_image_count;
static bool menu_uploaded;

static EGLDisplay egl_display;
static EGLConfig egl_config;
static EGLContext egl_context;
static EGLSurface egl_surface;

static XrInstance instance;
static XrSession session;
static XrSpace local_space, view_space, aim_space[HANDS];
static XrSessionState session_state;
static bool session_running;
static atomic_bool quit;  /* also read by the emulator thread */
static atomic_bool emu_done;  /* the emulator thread closed its game and ended */
static XrActionSet action_set;
static XrAction aim_action, trigger_action, grip_action, a_action, b_action, x_action, y_action, menu_action, stick_action,
    click_action, haptic_action;
static XrPath hand_path[HANDS];
static XrSwapchain screen_chain;
static XrSwapchainImageOpenGLESKHR *screen_images;
static uint32_t screen_image_count, screen_w, screen_h;

/* The emulator runs on its own thread at the arcade's rate, so loading the core
 * never blocks Android's lifecycle callbacks and the XR loop keeps its own rate.
 * Shared state below is guarded by emu_lock. */
static pthread_mutex_t emu_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int emu_state;  /* 0 loading, 1 running, -1 failed */
static atomic_bool emu_active;  /* false while the session is not focused */
/* Player 1 and player 2 guns, written by the XR loop and read once per emulated frame. */
static TremuGun shared_guns[TREMU_PLAYERS] = {{.offscreen = true}, {.offscreen = true}, {.offscreen = true}, {.offscreen = true}};
static uint32_t *shared_frame;
static unsigned shared_w, shared_h, shared_cap, shared_max_w, shared_max_h;
/* Pixel layers of shared_frame (NES), for the voxel view; shared_has_layers false otherwise. */
static uint8_t *shared_layers;
static bool shared_has_layers, shared_continuous;  /* continuous: depth from the network */
/* shared_frame holds both eyes side by side when shared_eye_w is set (PS2 stereo). */
static unsigned shared_left_x, shared_right_x, shared_eye_w;
static bool network_depth(const uint32_t *pixels, unsigned w, unsigned h, uint8_t *out);
static XrSystemId xr_system;
static float shared_aspect = 4.f / 3.f;
static uint64_t shared_seq, uploaded_seq;
static char core_system_dir[512], core_save_dir[512], core_rom[768];
/* Requests from the XR loop to the emulator thread. */
enum { CMD_NONE, CMD_LOAD, CMD_CLOSE };
static atomic_int emu_cmd;
/* Multiplayer requests for the running game (the emulator thread owns netplay). */
enum { NET_REQ_NONE, NET_REQ_HOST, NET_REQ_HOST_INTERNET, NET_REQ_STOP };
static atomic_int net_request;
/* Quick save/load requests for the running game, and the result shown in the menu footer. */
enum { STATE_REQ_NONE, STATE_REQ_SAVE, STATE_REQ_LOAD };
static atomic_int state_request;
static char state_message[256];   /* guarded by emu_lock */
static uint64_t state_message_until;
/* Game being launched: archive name, title, and the host to join (empty when playing solo). */
static char game_key[64], game_title[64], join_ip[64];
static char joined_ip[64];  /* XR thread: host of the current two-headset game, for the menu */

/* Logo at start-up, then the game carousel; a game replaces it until "back to carousel". */
enum { MODE_SPLASH, MODE_CAROUSEL, MODE_GAME };
/* Console whose pad the Touch controllers play besides the gun (TremuPadConsole), or PAD_NONE
 * (set by the emulator thread). */
enum { PAD_NONE = -1 };
static atomic_int pad_layout = PAD_NONE;
/* System of the game being loaded or played (-1: none yet), for the "Optimisation" settings. */
static atomic_int perf_system = -1;
static atomic_bool perf_dirty = true;
static uint64_t loading_since;  /* when the current game launch began */
static bool loading_footer;     /* carousel_footer says the game is loading */
/* The game is loaded but its screen is still black (consoles booting, discs spinning up): the
 * loading picture stays up meanwhile. */
static atomic_bool game_booting;
/* PS2 GunCon 2 games: Select is useless there, so the right A (and the left X for player 2)
 * recalibrate instead, which Time Crisis' calibration screens need. */
static atomic_bool guncon2;
static int mode = MODE_SPLASH;
static uint64_t splash_until;
static char carousel_footer[320];
static bool confirm_was;
static XrSwapchain splash_chain, carousel_chain;
static XrSwapchainImageOpenGLESKHR *splash_images, *carousel_images;
static uint32_t splash_image_count, carousel_image_count;
static bool splash_uploaded, carousel_uploaded;
/* Games and posters go in shared storage so they can be copied over USB; internal roms/ still works. */
static const char *SHARED_DIR = "/storage/emulated/0/Tremu";
static char internal_rom_dir[512], unzip_dir[512], drivers_dir[512];

static bool xr_check(XrResult r, const char *what) {
    if (XR_SUCCEEDED(r)) return true;
    char text[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (instance) xrResultToString(instance, r, text);
    LOG("%s failed: %s (%d)", what, text, r);
    return false;
}

static XrPath path(const char *s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(instance, s, &p);
    return p;
}

static bool init_egl(void) {
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl_display, NULL, NULL);
    const EGLint config_attribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                     EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &count) || count < 1) return false;
    const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    const EGLint surface_attribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    egl_surface = eglCreatePbufferSurface(egl_display, egl_config, surface_attribs);
    return egl_context != EGL_NO_CONTEXT && eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

static bool create_action(XrAction *out, const char *name, XrActionType type) {
    XrActionCreateInfo ci = {XR_TYPE_ACTION_CREATE_INFO};
    ci.actionType = type;
    ci.countSubactionPaths = HANDS;
    ci.subactionPaths = hand_path;
    snprintf(ci.actionName, sizeof ci.actionName, "%s", name);
    snprintf(ci.localizedActionName, sizeof ci.localizedActionName, "%s", name);
    return XR_CHECK(xrCreateAction(action_set, &ci, out));
}

static bool init_actions(void) {
    XrActionSetCreateInfo si = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(si.actionSetName, "tremu");
    strcpy(si.localizedActionSetName, "Tremu");
    if (!XR_CHECK(xrCreateActionSet(instance, &si, &action_set))) return false;
    hand_path[LEFT] = path("/user/hand/left");
    hand_path[RIGHT] = path("/user/hand/right");
    if (!create_action(&aim_action, "aim", XR_ACTION_TYPE_POSE_INPUT) ||
        !create_action(&trigger_action, "trigger", XR_ACTION_TYPE_FLOAT_INPUT) ||
        !create_action(&grip_action, "grip", XR_ACTION_TYPE_FLOAT_INPUT) ||
        !create_action(&a_action, "a", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
        !create_action(&b_action, "b", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
        !create_action(&x_action, "x", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
        !create_action(&y_action, "y", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
        !create_action(&menu_action, "menu", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
        !create_action(&stick_action, "stick", XR_ACTION_TYPE_VECTOR2F_INPUT) ||
        !create_action(&click_action, "stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT) ||
        !create_action(&haptic_action, "haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT))
        return false;
    XrActionSuggestedBinding b[] = {
        {aim_action, path("/user/hand/left/input/aim/pose")},
        {aim_action, path("/user/hand/right/input/aim/pose")},
        {trigger_action, path("/user/hand/left/input/trigger/value")},
        {trigger_action, path("/user/hand/right/input/trigger/value")},
        {grip_action, path("/user/hand/left/input/squeeze/value")},
        {grip_action, path("/user/hand/right/input/squeeze/value")},
        {a_action, path("/user/hand/right/input/a/click")},
        {b_action, path("/user/hand/right/input/b/click")},
        {x_action, path("/user/hand/left/input/x/click")},
        {y_action, path("/user/hand/left/input/y/click")},
        {menu_action, path("/user/hand/left/input/menu/click")},
        {stick_action, path("/user/hand/left/input/thumbstick")},
        {stick_action, path("/user/hand/right/input/thumbstick")},
        {click_action, path("/user/hand/left/input/thumbstick/click")},
        {click_action, path("/user/hand/right/input/thumbstick/click")},
        {haptic_action, path("/user/hand/left/output/haptic")},
        {haptic_action, path("/user/hand/right/output/haptic")},
    };
    XrInteractionProfileSuggestedBinding sb = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    sb.suggestedBindings = b;
    sb.countSuggestedBindings = sizeof b / sizeof b[0];
    if (!XR_CHECK(xrSuggestInteractionProfileBindings(instance, &sb))) return false;
    for (int h = 0; h < HANDS; h++) {
        XrActionSpaceCreateInfo ai = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        ai.action = aim_action;
        ai.subactionPath = hand_path[h];
        ai.poseInActionSpace.orientation.w = 1;
        if (!XR_CHECK(xrCreateActionSpace(session, &ai, &aim_space[h]))) return false;
    }
    XrSessionActionSetsAttachInfo at = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &action_set;
    return XR_CHECK(xrAttachSessionActionSets(session, &at));
}

static bool init_openxr(struct android_app *app) {
    PFN_xrInitializeLoaderKHR init_loader;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init_loader);
    XrLoaderInitInfoAndroidKHR li = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    li.applicationVM = app->activity->vm;
    li.applicationContext = app->activity->clazz;
    init_loader((XrLoaderInitInfoBaseHeaderKHR *)&li);

    const char *ext[7] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    uint32_t ext_count = 2;
    bool passthrough = tremu_passthrough_supported();
    if (passthrough) ext[ext_count++] = XR_FB_PASSTHROUGH_EXTENSION_NAME;
    tremu_perf_add_extensions(ext, &ext_count);
    XrInstanceCreateInfoAndroidKHR ia = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ia.applicationVM = app->activity->vm;
    ia.applicationActivity = app->activity->clazz;
    XrInstanceCreateInfo ii = {XR_TYPE_INSTANCE_CREATE_INFO};
    ii.next = &ia;
    strcpy(ii.applicationInfo.applicationName, "Tremu");
    ii.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    ii.enabledExtensionCount = ext_count;
    ii.enabledExtensionNames = ext;
    if (!XR_CHECK(xrCreateInstance(&ii, &instance))) return false;

    XrSystemGetInfo sg = {XR_TYPE_SYSTEM_GET_INFO};
    sg.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system;
    if (!XR_CHECK(xrGetSystem(instance, &sg, &system))) return false;
    xr_system = system;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR gles_requirements;
    xrGetInstanceProcAddr(instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&gles_requirements);
    XrGraphicsRequirementsOpenGLESKHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    gles_requirements(instance, system, &req);

    XrGraphicsBindingOpenGLESAndroidKHR gb = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gb.display = egl_display;
    gb.config = egl_config;
    gb.context = egl_context;
    XrSessionCreateInfo sc = {XR_TYPE_SESSION_CREATE_INFO};
    sc.next = &gb;
    sc.systemId = system;
    if (!XR_CHECK(xrCreateSession(instance, &sc, &session))) return false;

    XrReferenceSpaceCreateInfo rs = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1;
    if (!XR_CHECK(xrCreateReferenceSpace(session, &rs, &local_space))) return false;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!XR_CHECK(xrCreateReferenceSpace(session, &rs, &view_space))) return false;
    if (passthrough) tremu_passthrough_init(instance, session);
    tremu_perf_init(instance, session);
    return init_actions();
}

static bool create_swapchain(unsigned w, unsigned h, XrSwapchain *chain, XrSwapchainImageOpenGLESKHR **images,
                             uint32_t *count) {
    /* All pixels are sRGB values; GL_RGBA8 would show them brighter and washed out. */
    int64_t format = GL_RGBA8;
    uint32_t n = 0;
    xrEnumerateSwapchainFormats(session, 0, &n, NULL);
    int64_t *formats = calloc(n, sizeof *formats);
    xrEnumerateSwapchainFormats(session, n, &n, formats);
    for (uint32_t i = 0; i < n; i++)
        if (formats[i] == GL_SRGB8_ALPHA8) format = GL_SRGB8_ALPHA8;
    free(formats);
    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = format;
    ci.sampleCount = 1;
    ci.width = w;
    ci.height = h;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!XR_CHECK(xrCreateSwapchain(session, &ci, chain))) return false;
    xrEnumerateSwapchainImages(*chain, 0, count, NULL);
    *images = calloc(*count, sizeof **images);
    for (uint32_t i = 0; i < *count; i++) (*images)[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    xrEnumerateSwapchainImages(*chain, *count, count, (XrSwapchainImageBaseHeader *)*images);
    LOG("swapchain %ux%u format 0x%llx", w, h, (long long)format);
    return true;
}

static bool create_screen_swapchain(unsigned w, unsigned h) {
    screen_w = w;
    screen_h = h;
    return create_swapchain(w, h, &screen_chain, &screen_images, &screen_image_count);
}

/* Copies RGBA pixels (bottom row first) into the next image of a swapchain. */
static bool upload_pixels(XrSwapchain chain, XrSwapchainImageOpenGLESKHR *images, const uint32_t *pixels,
                          unsigned w, unsigned h) {
    uint32_t index;
    XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!XR_CHECK(xrAcquireSwapchainImage(chain, &ai, &index))) return false;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(chain, &wi);
    glBindTexture(GL_TEXTURE_2D, images[index].image);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glFlush();
    XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(chain, &ri);
    return true;
}

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void sleep_ms(long ms) {
    struct timespec pause = {ms / 1000, (ms % 1000) * 1000000};
    nanosleep(&pause, NULL);
}

/* Runs one game until quit or a game switch; returns false if it could not be loaded. */
static void set_state_message(const char *text) {
    pthread_mutex_lock(&emu_lock);
    snprintf(state_message, sizeof state_message, "%s", text);
    state_message_until = now_ns() + 4000000000ull;
    pthread_mutex_unlock(&emu_lock);
}

/* Quick save slot: one file per game in the app's saves folder (emulator thread). */
static void quick_state(const char *key, bool save) {
    NetState net = netplay_state();
    if (net != NET_OFF && net != NET_FAILED) {
        set_state_message(T("Sauvegarde indisponible en multijoueur"));
        return;
    }
    char path[700];
    snprintf(path, sizeof path, "%s/%s.state", core_save_dir, key);
    size_t size = tremu_core_state_size();
    uint8_t *buf = size ? malloc(size) : NULL;
    bool ok = false;
    if (buf && save) {
        FILE *f = tremu_core_save_state(buf, size) ? fopen(path, "wb") : NULL;
        if (f) {
            ok = fwrite(buf, 1, size, f) == size;
            ok = fclose(f) == 0 && ok;
        }
        set_state_message(T(ok ? "Partie sauvegard\xC3\xA9" "e" : "\xC3\x89" "chec de la sauvegarde"));
    } else if (buf) {
        FILE *f = fopen(path, "rb");
        if (!f) {
            set_state_message(T("Aucune sauvegarde pour ce jeu"));
        } else {
            ok = fread(buf, 1, size, f) == size && tremu_core_load_state(buf, size);
            fclose(f);
            set_state_message(T(ok ? "Sauvegarde charg\xC3\xA9" "e" : "Sauvegarde incompatible"));
        }
    }
    LOG("quick %s %s: %s", save ? "save" : "load", path, ok ? "ok" : "failed");
    free(buf);
}

/* Games whose emulator draws no gun sight (PS2 GunCon 2 games, Namco's PS2 arcade boards):
 * Tremu draws one where each player aims, when the crosshair option is on. */
static bool draw_gun_sights;

/* A cross with a dark outline at the gun's place in an RGBA frame (bottom row first). */
static void draw_sight(uint32_t *frame, unsigned stride, unsigned w, unsigned h, int shift, const TremuGun *g, uint32_t colour) {
    if (g->offscreen) return;
    int cx = (int)((g->x + 0x7fff) * (int64_t)(w - 1) / 0xfffe) + shift;
    int cy = (int)(h - 1) - (int)((g->y + 0x7fff) * (int64_t)(h - 1) / 0xfffe);
    int arm = (int)(w / 40) + 4, gap = arm / 3;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? colour : 0xFF000000u;
        int thick = pass ? 1 : 2;
        for (int d = gap; d <= arm; d++)
            for (int t = -thick; t <= thick; t++) {
                const int pts[4][2] = {{cx + d, cy + t}, {cx - d, cy + t}, {cx + t, cy + d}, {cx + t, cy - d}};
                for (int k = 0; k < 4; k++) {
                    int x = pts[k][0], y = pts[k][1];
                    if (x >= 0 && y >= 0 && x < (int)w && y < (int)h) frame[(size_t)y * stride + x] = c;
                }
            }
    }
}

/* A gun hits the picture half way between where the two eyes see it, so a sight moved by +s in the
 * left eye and -s in the right aims at the same spot wherever it appears in depth. This finds the s
 * that puts it at the depth of what is under it, instead of on the screen plane where the eyes see
 * two sights: the block around the aim that matches best between the eyes. Eased, and it holds on
 * flat colour, where every s matches. */
static int sight_shift(const uint32_t *frame, unsigned stride, unsigned left_x, unsigned right_x, unsigned w, unsigned h,
                       const TremuGun *g, float *eased) {
    int last = (int)lroundf(*eased);
    if (g->offscreen) return last;
    int cx = (int)((g->x + 0x7fff) * (int64_t)(w - 1) / 0xfffe);
    int cy = (int)(h - 1) - (int)((g->y + 0x7fff) * (int64_t)(h - 1) / 0xfffe);
    int bw = (int)w / 32 + 2, bh = bw / 2, reach = (int)w / 48 + 1, best = last;
    int y0 = cy - bh < 0 ? 0 : cy - bh, y1 = cy + bh >= (int)h ? (int)h - 1 : cy + bh;
    int lo = bw + reach, hi = (int)w - 1 - bw - reach, mid = cx < lo ? lo : cx > hi ? hi : cx;
    uint64_t best_cost = UINT64_MAX;
    for (int s = -reach / 3; s <= reach; s++) {
        uint64_t cost = (uint64_t)abs(s - last) * 4;
        for (int y = y0; y <= y1; y += 2) {
            const uint32_t *l = frame + (size_t)y * stride + left_x + mid + s, *r = frame + (size_t)y * stride + right_x + mid - s;
            for (int x = -bw; x <= bw; x += 2) cost += (uint64_t)abs((int)(l[x] >> 8 & 0xFF) - (int)(r[x] >> 8 & 0xFF));
        }
        if (cost < best_cost) { best_cost = cost; best = s; }
    }
    *eased += ((float)best - *eased) * .25f;
    return (int)lroundf(*eased);
}

/* Why the last game could not start, when known (shown in the carousel). */
static char load_error[256];
static void set_load_error(const char *why) {
    pthread_mutex_lock(&emu_lock);
    snprintf(load_error, sizeof load_error, "%s", why);
    pthread_mutex_unlock(&emu_lock);
}

/* Xenia and xemu can only stop a game by ending the process: leaving this game restarts the app. */
static atomic_bool one_game_process;
/* The game running links the headsets itself (PSP ad hoc). */
static atomic_bool adhoc_game;

static bool run_game(void) {
    char rom[sizeof core_rom];
    pthread_mutex_lock(&emu_lock);
    snprintf(rom, sizeof rom, "%s", core_rom);
    pthread_mutex_unlock(&emu_lock);
    /* The system picks the core; a zipped console game is unpacked first (arcade sets stay zipped). */
    int system = tremu_system_detect(rom);
    draw_gun_sights = system == SYS_PS2 || system == SYS_NAMCO246;
    /* Consoles also play their pad games: the Touch controllers become a gamepad. Consoles with a
     * gun keep it for the games made for it (by name); NES and SNES have both at once. */
    bool has_gun = system == SYS_PS1 || system == SYS_PS2 || system == SYS_DREAMCAST || system == SYS_MEGADRIVE ||
                   system == SYS_SATURN;
    bool pad_game = (has_gun && !tremu_system_gun_game(rom)) || system == SYS_N64 || system == SYS_GAMECUBE ||
                    system == SYS_X360 || system == SYS_XBOX || system == SYS_PSP;
    draw_gun_sights = draw_gun_sights && !pad_game;
    /* Xbox gun games: xemu presents a light gun (read at boot), buttons stay on the Touch pad. */
    if (system == SYS_XBOX && tremu_system_xbox_gun_game(rom)) {
        draw_gun_sights = true;
        setenv("TREMU_XBOX_GUN", "1", 1);
    }
    int console = system == SYS_NES ? TREMU_PAD_NES : system == SYS_SNES ? TREMU_PAD_SNES : system == SYS_N64 ? TREMU_PAD_N64
                  : system == SYS_GAMECUBE ? TREMU_PAD_GAMECUBE : system == SYS_WII ? TREMU_PAD_WII : PAD_NONE;
    if (pad_game && system == SYS_PS1) console = TREMU_PAD_PS1;
    if (pad_game && system == SYS_PS2) console = TREMU_PAD_PS2;
    if (pad_game && system == SYS_DREAMCAST) console = TREMU_PAD_DREAMCAST;
    if (pad_game && system == SYS_MEGADRIVE) console = TREMU_PAD_MEGADRIVE;
    if (pad_game && system == SYS_SATURN) console = TREMU_PAD_SATURN;
    if (system == SYS_X360) console = TREMU_PAD_X360;
    if (system == SYS_XBOX) console = TREMU_PAD_XBOX;
    if (system == SYS_PSP) console = TREMU_PAD_PSP;
    atomic_store(&pad_layout, console);
    tremu_menu_set_pad_console(atomic_load(&pad_layout));
    tremu_core_set_gamepad(pad_game);
    atomic_store(&guncon2, system == SYS_PS2 && !pad_game);
    atomic_store(&one_game_process, system == SYS_X360 || system == SYS_XBOX);
    if (system < 0) { LOG("%s: unknown system", rom); return false; }
    char game_file[sizeof core_rom];
    snprintf(game_file, sizeof game_file, "%s", rom);
    const char *dot = strrchr(rom, '.');
    struct stat rom_stat;
    if (system == SYS_MAME && !stat(rom, &rom_stat) && S_ISDIR(rom_stat.st_mode)) {
        /* MAME reads an unzipped set from its folder when given "<folder>.zip", and its hard disk
         * .chd from the same folder. */
        snprintf(game_file, sizeof game_file, "%s.zip", rom);
        const char *set = strrchr(rom, '/');
        set = set ? set + 1 : rom;
        static const char *const chd_sets[] = {"area51", "area51t", "area51a", "area51mx", "a51mxr3k", "carnevil", "carnevil1"};
        bool needs_chd = false;
        for (size_t i = 0; i < sizeof chd_sets / sizeof chd_sets[0]; i++) needs_chd = needs_chd || !strcasecmp(set, chd_sets[i]);
        DIR *d = needs_chd ? opendir(rom) : NULL;
        bool has_chd = false;
        for (struct dirent *f; d && (f = readdir(d)) && !has_chd;) {
            const char *ext = strrchr(f->d_name, '.');
            has_chd = ext && !strcasecmp(ext, ".chd");
        }
        if (d) closedir(d);
        if (needs_chd && !has_chd) {
            char why[256];
            snprintf(why, sizeof why, T("Disque dur manquant : mets %s.chd dans le dossier %s"), set, set);
            set_load_error(why);
            return false;
        }
    } else if (system == SYS_ARCADE && !stat(rom, &rom_stat) && S_ISDIR(rom_stat.st_mode)) {
        const char *set = strrchr(rom, '/');
        snprintf(game_file, sizeof game_file, "%s/%s.zip", unzip_dir, set ? set + 1 : rom);
        mkdir(unzip_dir, 0700);
        if (!tremu_zip_folder(rom, game_file)) return false;
    } else if (system != SYS_ARCADE && system != SYS_MAME && dot && !strcasecmp(dot, ".zip") &&
        !tremu_system_unzip(rom, (TremuSystem)system, unzip_dir, game_file, sizeof game_file))
        return false;
    char assets_dir[600];
    if (system == SYS_WII || system == SYS_GAMECUBE) {
        snprintf(assets_dir, sizeof assets_dir, "%s/dolphin-emu/Sys", core_system_dir);
        if (!tremu_unpack_asset("dolphin-sys.zip", assets_dir)) return false;
    } else if (system == SYS_PS2) {
        /* LRPS2 needs a PS2 BIOS dumped from the player's console, in bios/pcsx2/bios. */
        snprintf(assets_dir, sizeof assets_dir, "%s/pcsx2/bios", core_system_dir);
        mkdir(assets_dir, 0775);
        DIR *d = opendir(assets_dir);
        bool found = false;
        for (struct dirent *f; d && (f = readdir(d)) && !found;) found = f->d_name[0] != '.';
        if (d) closedir(d);
        if (!found) {
            set_load_error(T("BIOS PS2 manquant : mets-le dans Tremu/bios/pcsx2/bios"));
            return false;
        }
    } else if (system == SYS_SATURN || (system == SYS_MEGADRIVE && dot && !strcasecmp(dot, ".cue"))) {
        /* Disc systems that need the console's BIOS in Tremu/bios (the player's own dump). */
        static const char *const saturn[] = {"mpr-17933.bin", "sega_101.bin", NULL};
        static const char *const mega_cd[] = {"bios_CD_U.bin", "bios_CD_E.bin", "bios_CD_J.bin", NULL};
        const char *const *names = system == SYS_SATURN ? saturn : mega_cd;
        bool found = false;
        for (int i = 0; names[i] && !found; i++) {
            char bios[600];
            snprintf(bios, sizeof bios, "%s/%s", core_system_dir, names[i]);
            found = !access(bios, R_OK);
        }
        if (!found) {
            set_load_error(T(system == SYS_SATURN ? "BIOS Saturn manquant : mets mpr-17933.bin (ou sega_101.bin) dans Tremu/bios"
                                                  : "BIOS Mega-CD manquant : mets bios_CD_U.bin (ou _E, _J) dans Tremu/bios"));
            return false;
        }
    } else if (system == SYS_NAMCO246) {
        snprintf(assets_dir, sizeof assets_dir, "%s/play/arcadedefs", core_system_dir);
        if (!tremu_unpack_asset("play-arcadedefs.zip", assets_dir)) return false;
    } else if (system == SYS_X360) {
        /* Our Turnip Vulkan driver (Mesa, MIT) for Xenia, unpacked into the app's private files where
         * libadrenotools may load it; the menu can pick Adreno instead (tremu_core_set_vulkan_driver). */
        static const char *const turnips[] = {"Tremu-Turnip"};
        mkdir(drivers_dir, 0700);
        for (size_t i = 0; i < sizeof turnips / sizeof turnips[0]; i++) {
            char asset[96];
            snprintf(asset, sizeof asset, "turnip-%s.zip", turnips[i]);
            snprintf(assets_dir, sizeof assets_dir, "%s/%s", drivers_dir, turnips[i]);
            if (!tremu_unpack_asset(asset, assets_dir)) LOG("driver %s not unpacked", turnips[i]);
        }
    } else if (system == SYS_PSP) {
        snprintf(assets_dir, sizeof assets_dir, "%s/PPSSPP", core_system_dir);
        if (!tremu_unpack_asset("ppsspp-assets.zip", assets_dir)) return false;
        /* Every headset runs its own ad hoc server, so that hosting needs no reboot of the game; a
         * guest boots on the host's ("ip", or "ip:port" from a party code: then through its relay,
         * since across the Internet only the host's ports are open). */
        char server[64];
        pthread_mutex_lock(&emu_lock);
        snprintf(server, sizeof server, "%s", join_ip);
        if (!options.adhoc_mac[0]) {
            /* Locally administered address, random once, unique among the players. */
            FILE *r = fopen("/dev/urandom", "rb");
            uint8_t b[5] = {0};
            if (r) { fread(b, 1, sizeof b, r); fclose(r); }
            snprintf(options.adhoc_mac, sizeof options.adhoc_mac, "02%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4]);
            tremu_options_save(options_path, &options);
        }
        char mac[sizeof options.adhoc_mac];
        snprintf(mac, sizeof mac, "%s", options.adhoc_mac);
        pthread_mutex_unlock(&emu_lock);
        char *port = strchr(server, ':');
        if (port) *port = 0;
        bool own_server = !server[0];
        if (own_server && !netplay_local_ip(server, sizeof server)) snprintf(server, sizeof server, "127.0.0.1");
        tremu_core_set_adhoc(server, own_server, port != NULL, mac);
    }
    {
        TremuPerf perf;
        pthread_mutex_lock(&emu_lock);
        tremu_perf_resolve(tremu_options_perf(&options, game_key), system, &perf);
        pthread_mutex_unlock(&emu_lock);
        tremu_core_set_internal_res(perf.res);
        atomic_store(&perf_system, system);
        atomic_store(&perf_dirty, true);
    }
    if (!tremu_core_load(tremu_system_info((TremuSystem)system)->core, core_system_dir, core_save_dir, game_file)) return false;
    pthread_mutex_lock(&emu_lock);
    shared_max_w = tremu_core_max_width();
    shared_max_h = tremu_core_max_height();
    shared_aspect = tremu_core_aspect();
    pthread_mutex_unlock(&emu_lock);
    /* Xbox consoles: the game starts at once (its sound plays), so no loading picture over it. */
    atomic_store(&game_booting, system != SYS_X360 && system != SYS_XBOX);
    const uint64_t boot_start = now_ns();
    atomic_store(&emu_state, 1);

    /* Netplay identifies the game by archive name and file CRC on both headsets. */
    char key[64], title[64], host_ip[64];
    pthread_mutex_lock(&emu_lock);
    snprintf(key, sizeof key, "%s", game_key);
    snprintf(title, sizeof title, "%s", game_title);
    snprintf(host_ip, sizeof host_ip, "%s", join_ip);
    join_ip[0] = 0;
    pthread_mutex_unlock(&emu_lock);
    /* PSP: no lockstep to protect, and reading a whole UMD image would delay the boot. */
    uint32_t rom_crc = system == SYS_PSP ? 0 : netplay_file_crc(rom);
    if (host_ip[0] && system != SYS_PSP) netplay_join(host_ip, key, rom_crc);
    atomic_store(&adhoc_game, system == SYS_PSP);

    uint64_t period = (uint64_t)(1e9 / tremu_core_fps());
    uint64_t next = now_ns();
    uint64_t hand_sum = 0, hand_max = 0;  /* time spent giving each frame to the XR thread */
    while (!quit && atomic_load(&emu_cmd) == CMD_NONE) {
        int request = atomic_exchange(&net_request, NET_REQ_NONE);
        if ((request == NET_REQ_HOST || request == NET_REQ_HOST_INTERNET) && system == SYS_PSP) {
            char server[64] = "127.0.0.1";
            netplay_local_ip(server, sizeof server);
            pthread_mutex_lock(&emu_lock);
            char mac[sizeof options.adhoc_mac];
            snprintf(mac, sizeof mac, "%s", options.adhoc_mac);
            pthread_mutex_unlock(&emu_lock);
            tremu_core_set_adhoc(server, true, request == NET_REQ_HOST_INTERNET, mac);
            netplay_host_link(key, title, request == NET_REQ_HOST_INTERNET);
        } else if (request == NET_REQ_HOST || request == NET_REQ_HOST_INTERNET)
            netplay_host(key, title, rom_crc, request == NET_REQ_HOST_INTERNET);
        else if (request == NET_REQ_STOP) netplay_stop();
        int state_req = atomic_exchange(&state_request, STATE_REQ_NONE);
        if (state_req) quick_state(key, state_req == STATE_REQ_SAVE);
        if (!atomic_load(&emu_active)) {
            /* Paused (menu open, headset removed): the game waits, the network does not. */
            NetState net = netplay_state();
            bool networked = net != NET_OFF && net != NET_FAILED;
            if (networked) netplay_idle();
            sleep_ms(networked && netplay_transferring() ? 1 : networked ? 20 : 50);
            next = now_ns();
            continue;
        }
        pthread_mutex_lock(&emu_lock);
        TremuGun guns[TREMU_PLAYERS];
        memcpy(guns, shared_guns, sizeof guns);
        TremuGun local = shared_guns[0];
        pthread_mutex_unlock(&emu_lock);
        /* In a two-headset game each frame waits for both players' inputs (lockstep). */
        NetState net = netplay_state();
        bool networked = net != NET_OFF && net != NET_FAILED && net != NET_LINK;
        if (net == NET_LINK) netplay_idle();
        if (networked && !netplay_frame(&local, guns)) {
            sleep_ms(1);
            next = now_ns();
            continue;
        }
        uint64_t run_start = now_ns();
        tremu_core_run(guns);
        uint64_t run_time = now_ns() - run_start;
        tremu_perf_frame(run_time, tremu_core_fps());
        if (networked) netplay_after_frame();
        {
            /* Every 5 s, what player 1's gun sends: first thing to check when aiming looks wrong.
             * And the real speed: a core frame longer than the wanted period means the CPU falls behind. */
            static uint64_t last_gun_log, run_sum, run_max;
            static unsigned run_frames;
            run_sum += run_time;
            if (run_time > run_max) run_max = run_time;
            run_frames++;
            uint64_t t = now_ns();
            if (t - last_gun_log > 5000000000ull) {
                LOG("speed %.1f fps of %.2f wanted, core frame avg %.1f max %.1f ms, hand-over to the XR thread avg %.1f max %.1f ms",
                    run_frames * 1e9 / (double)(t - last_gun_log), tremu_core_fps(), run_sum / 1e6 / run_frames, run_max / 1e6,
                    hand_sum / 1e6 / run_frames, hand_max / 1e6);
                hand_sum = hand_max = 0;
                run_sum = run_max = 0;
                run_frames = 0;
                last_gun_log = t;
                LOG("P1 gun x=%d y=%d offscreen=%d trigger=%d%s", guns[0].x, guns[0].y, guns[0].offscreen, guns[0].trigger,
                    networked ? " (network)" : "");
            }
        }

        unsigned w, h;
        const uint32_t *pixels = tremu_core_frame(&w, &h);
        uint64_t hand_start = now_ns();
        if (pixels && w && h) {
            pthread_mutex_lock(&emu_lock);
            if (w * h > shared_cap) {
                free(shared_frame);
                free(shared_layers);
                shared_cap = w * h;
                shared_frame = malloc(shared_cap * sizeof *shared_frame);
                shared_layers = malloc(shared_cap);
            }
            memcpy(shared_frame, pixels, w * h * sizeof *pixels);
            if (atomic_load(&game_booting)) {
                /* Still booting while every sampled pixel is near black (at most 3 minutes). */
                bool lit = now_ns() - boot_start > 180000000000ull;
                for (size_t i = 0; i < (size_t)w * h && !lit; i += 7) {
                    uint32_t c = pixels[i];
                    lit = (c & 0xFF) > 40 || (c >> 8 & 0xFF) > 40 || (c >> 16 & 0xFF) > 40;
                }
                if (lit) atomic_store(&game_booting, false);
            }
            unsigned left_x = 0, right_x = 0, eye_w = 0;
            bool stereo = tremu_core_views(&left_x, &right_x, &eye_w);
            shared_left_x = left_x;
            shared_right_x = right_x;
            shared_eye_w = stereo ? eye_w : 0;
            if (draw_gun_sights && options.crosshair) {
                static const uint32_t colours[2] = {0xFF3030FFu, 0xFFFF8030u};  /* player 1 red, player 2 blue */
                static float depth_shift[2];
                for (int p = 0; p < 2; p++) {
                    if (!stereo) {
                        draw_sight(shared_frame, w, w, h, 0, &guns[p], colours[p]);
                        continue;
                    }
                    int s = sight_shift(pixels, w, left_x, right_x, eye_w, h, &guns[p], &depth_shift[p]);
                    draw_sight(shared_frame + left_x, w, eye_w, h, s, &guns[p], colours[p]);
                    draw_sight(shared_frame + right_x, w, eye_w, h, -s, &guns[p], colours[p]);
                }
            }
            const uint8_t *layers = tremu_core_layers();
            shared_has_layers = layers && shared_layers;
            shared_continuous = false;
            if (shared_has_layers) memcpy(shared_layers, layers, w * h);
            else if (shared_layers && !stereo && options.voxels && options.ai_depth && w <= TREMU_VOXEL_MAX && h <= TREMU_VOXEL_MAX)
                shared_has_layers = shared_continuous = network_depth(pixels, w, h, shared_layers);
            shared_w = w;
            shared_h = h;
            shared_seq++;
            pthread_mutex_unlock(&emu_lock);
        }
        uint64_t hand_time = now_ns() - hand_start;
        hand_sum += hand_time;
        if (hand_time > hand_max) hand_max = hand_time;
        /* Absolute deadlines keep the arcade at its own rate; resync after a long stall. MAME only
         * knows the game's refresh once it runs (CarnEvil 56 Hz): a stale rate overflows the audio. */
        period = (uint64_t)(1e9 / tremu_core_fps());
        next += period;
        uint64_t t = now_ns();
        if (next > t) {
            struct timespec until = {(time_t)(next / 1000000000ull), (long)(next % 1000000000ull)};
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, NULL);
        } else if (t - next > 4 * period) {
            next = t;
        }
    }
    return true;
}

/* Games without layers: depth from the neural network (downloaded the first time). */
static bool network_depth(const uint32_t *pixels, unsigned w, unsigned h, uint8_t *out) {
    static bool asked;
    char path[256];
    snprintf(path, sizeof path, "%s/models/midas.tflite", SHARED_DIR);
    if (access(path, R_OK)) {
        if (!asked) { LOG("depth model missing, downloading to %s", path); tremu_fetch_depth_model(path); }
        asked = true;
        return false;
    }
    if (!tremu_depth_start(path)) return false;
    tremu_depth_submit(pixels, w, h);
    return tremu_depth_get(out, w, h);
}

/* Waits for CMD_LOAD, runs that game until the next command, and repeats. */
static void *emulator_thread(void *arg) {
    (void)arg;
    tremu_perf_emulator_thread();
    while (!quit) {
        if (atomic_exchange(&emu_cmd, CMD_NONE) != CMD_LOAD) {
            sleep_ms(50);
            continue;
        }
        atomic_store(&emu_state, 0);
        bool loaded = run_game();
        netplay_stop();
        if (loaded) tremu_core_close_game();
        tremu_depth_stop();  /* frees the GPU; restarted by the next game that needs it */
        pthread_mutex_lock(&emu_lock);
        shared_w = shared_h = 0;
        pthread_mutex_unlock(&emu_lock);
        atomic_store(&emu_state, loaded ? 0 : -1);
    }
    /* The game was closed above (saves written). The cores are not deinitialised: some (Dolphin,
     * PCSX2) hang or crash there, and the process ends right after anyway. */
    atomic_store(&emu_done, true);
    return NULL;
}

/* Copies the newest emulated frame into the screen swapchain; keeps the last size otherwise. */
static bool voxel_view;  /* this frame shows the game in 3D */
static unsigned screen_left_x, screen_right_x, screen_eye_w;  /* what the screen swapchain holds */

static void upload_frame(unsigned *w, unsigned *h) {
    pthread_mutex_lock(&emu_lock);
    voxel_view = options.voxels && shared_has_layers && shared_frame && shared_w <= TREMU_VOXEL_MAX &&
                 shared_h <= TREMU_VOXEL_MAX &&
                 tremu_voxel_init(instance, xr_system, session);
    if (voxel_view) {
        *w = shared_w;
        *h = shared_h;
        if (shared_seq != uploaded_seq) tremu_voxel_upload(shared_frame, shared_layers, *w, *h, shared_continuous);
        uploaded_seq = shared_seq;
        pthread_mutex_unlock(&emu_lock);
        return;
    }
    *w = shared_w;
    *h = shared_h;
    bool fresh = shared_seq != uploaded_seq;
    if (!shared_frame || !*w || *w > screen_w || *h > screen_h) {
        pthread_mutex_unlock(&emu_lock);
        *w = *h = 0;
        return;
    }
    if (!fresh) { pthread_mutex_unlock(&emu_lock); return; }
    uploaded_seq = shared_seq;
    screen_left_x = shared_left_x;
    screen_right_x = shared_right_x;
    screen_eye_w = shared_eye_w;
    if (!upload_pixels(screen_chain, screen_images, shared_frame, *w, *h)) *w = *h = 0;
    pthread_mutex_unlock(&emu_lock);
}

static bool button(XrAction a, XrPath hand) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    gi.subactionPath = hand;
    XrActionStateBoolean s = {XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xrGetActionStateBoolean(session, &gi, &s)) && s.isActive && s.currentState;
}
static float axis(XrAction a, XrPath hand) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    gi.subactionPath = hand;
    XrActionStateFloat s = {XR_TYPE_ACTION_STATE_FLOAT};
    return XR_SUCCEEDED(xrGetActionStateFloat(session, &gi, &s)) && s.isActive ? s.currentState : 0;
}

static XrVector3f rotate(XrQuaternionf q, XrVector3f v) {
    XrVector3f u = {q.x, q.y, q.z}, t = {2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z), 2 * (u.x * v.y - u.y * v.x)};
    return (XrVector3f){v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
                        v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
}

static XrQuaternionf multiply(XrQuaternionf a, XrQuaternionf b) {
    return (XrQuaternionf){a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static XrVector3f add(XrVector3f a, XrVector3f b) { return (XrVector3f){a.x + b.x, a.y + b.y, a.z + b.z}; }
static XrVector3f sub(XrVector3f a, XrVector3f b) { return (XrVector3f){a.x - b.x, a.y - b.y, a.z - b.z}; }
static XrQuaternionf inverse(XrQuaternionf q) { return (XrQuaternionf){-q.x, -q.y, -q.z, q.w}; }

/* The arcade screen: options.screen_* in front of screen_origin. */
static XrPosef screen_pose(void) {
    XrVector3f offset = {0, options.screen_height / 100.f, -options.screen_distance / 100.f};
    return (XrPosef){screen_origin.orientation, add(screen_origin.position, rotate(screen_origin.orientation, offset))};
}

/* Projects the right controller's aim ray onto the screen quad. The gun angle and
 * down/back offsets correct a Touch controller mounted on top of a physical gun. */
static void aim(XrTime time, float aspect, int hand, TremuGun *gun) {
    gun->offscreen = true;
    XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(aim_space[hand], local_space, time, &loc))) return;
    XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & need) != need) return;
    float half_pitch = options.gun_pitch * (float)M_PI / 360.f;
    XrQuaternionf q = multiply(loc.pose.orientation, (XrQuaternionf){sinf(half_pitch), 0, 0, cosf(half_pitch)});
    XrVector3f o = add(loc.pose.position, rotate(q, (XrVector3f){0, -options.gun_drop / 100.f, options.gun_back / 100.f}));
    XrVector3f d = rotate(q, (XrVector3f){0, 0, -1});
    XrPosef sp = screen_pose();
    XrQuaternionf to_screen = inverse(sp.orientation);
    XrVector3f ol = rotate(to_screen, sub(o, sp.position)), dl = rotate(to_screen, d);
    if (dl.z > -1e-4f) return;
    float t = -ol.z / dl.z, width = options.screen_width / 100.f, height = width / aspect;
    float u = (ol.x + t * dl.x) / width + .5f, v = .5f - (ol.y + t * dl.y) / height;
    if (u < 0 || u > 1 || v < 0 || v > 1) return;
    gun->offscreen = false;
    gun->x = (int16_t)lroundf((u * 2 - 1) * 0x7fff);
    gun->y = (int16_t)lroundf((v * 2 - 1) * 0x7fff);
}

/* Head position with yaw only, for placing the screen and the menu in front of the player. */
static bool head_yaw_pose(XrTime time, XrPosef *out) {
    XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(view_space, local_space, time, &loc))) return false;
    if (!(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) return false;
    XrVector3f f = rotate(loc.pose.orientation, (XrVector3f){0, 0, -1});
    float yaw = atan2f(-f.x, -f.z);
    *out = (XrPosef){{0, sinf(yaw / 2), 0, cosf(yaw / 2)}, loc.pose.position};
    return true;
}

static XrPosef menu_pose;

/* The menu is showing the game's text request: closing it without A cancels the request. */
static bool text_request;
static bool search_request;  /* the text panel is open for the carousel's search */

static void set_menu_open(bool open, XrTime time) {
    menu_open = open;
    if (!open) search_request = false;
    if (!open && text_request) {
        text_request = false;
        tremu_core_text_submit(NULL);
    }
    if (open) {
        XrPosef head = screen_origin;
        head_yaw_pose(time, &head);
        menu_pose = (XrPosef){head.orientation, add(head.position, rotate(head.orientation, (XrVector3f){0, -.1f, -1.2f}))};
        tremu_menu_reset();
    } else {
        tremu_menu_closed();
    }
}

static XrVector2f left_stick(void);

/* Navigation: the left Touch stick, or the joystick of a Bluetooth/USB gun (Arkade...). */
static XrVector2f nav_stick(void) {
    XrVector2f s = left_stick();
    float gx, gy;
    tremu_ext_stick(&gx, &gy);
    if (fabsf(gx) > fabsf(s.x)) s.x = gx;
    if (fabsf(gy) > fabsf(s.y)) s.y = gy;
    return s;
}

static XrVector2f stick(int hand) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = stick_action;
    gi.subactionPath = hand_path[hand];
    XrActionStateVector2f stick = {XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xrGetActionStateVector2f(session, &gi, &stick)) || !stick.isActive) return (XrVector2f){0, 0};
    return stick.currentState;
}

static XrVector2f left_stick(void) { return stick(LEFT); }

/* Short vibration of one Touch controller (the one mounted on the gun for the right hand). */
static void pulse(int hand, float amplitude, int ms) {
    if (!options.vibration) return;
    XrHapticVibration v = {XR_TYPE_HAPTIC_VIBRATION};
    v.amplitude = amplitude;
    v.duration = (XrDuration)ms * 1000000;
    v.frequency = XR_FREQUENCY_UNSPECIFIED;
    XrHapticActionInfo hi = {XR_TYPE_HAPTIC_ACTION_INFO};
    hi.action = haptic_action;
    hi.subactionPath = hand_path[hand];
    xrApplyHapticFeedback(session, &hi, (const XrHapticBaseHeader *)&v);
}

/* Recoil feel: a pulse per gun shot, a longer one on reload; with the Touch alone, a pulse
 * when the trigger goes down and then at a machine-gun rate while it stays down. */
static void game_haptics(uint64_t now, bool touch_trigger, bool p2_trigger) {
    static unsigned last_shots;
    static int last_magazine = -1;
    static uint64_t next_touch[HANDS];
    static bool was[HANDS];
    unsigned shots = tremu_gun_shots();
    if (shots != last_shots) {
        if (last_shots) pulse(RIGHT, .8f, 35);
        last_shots = shots;
    }
    int magazine = tremu_gun_magazine();
    if (magazine == 1 && last_magazine == 0) pulse(RIGHT, 1.f, 120);
    last_magazine = magazine;
    static bool bolt_was;
    bool bolt = tremu_gun_bolt_pressed(now);
    if (bolt && !bolt_was) pulse(RIGHT, 1.f, 80);
    bolt_was = bolt;
    const bool down[HANDS] = {[LEFT] = p2_trigger, [RIGHT] = touch_trigger};
    for (int h = 0; h < HANDS; h++) {
        if (down[h] && (!was[h] || now >= next_touch[h])) {
            pulse(h, .7f, 30);
            next_touch[h] = now + 110000000ull;
        }
        was[h] = down[h];
    }
}

/* Starts a game in the emulator thread; ip, when set, joins that host's two-headset game. */
static void launch_game(const char *rom, const char *key, const char *title, const char *ip) {
    if (strstr(rom, "/wiiu/")) {  /* runs in our Cemu app */
        if (!tremu_launch_wiiu(rom))
            snprintf(carousel_footer, sizeof carousel_footer, "%s", T("Installe notre application Cemu pour jouer aux jeux Wii U"));
        return;
    }
    if (strstr(rom, "/ps3/")) {  /* runs in the ARMSX3 app */
        if (!tremu_launch_ps3(rom))
            snprintf(carousel_footer, sizeof carousel_footer, "%s", T("Installe l'application ARMSX3 pour jouer aux jeux PS3"));
        return;
    }
    if (strstr(rom, "/vita/")) {  /* runs in our Vita3K app */
        if (!tremu_launch_vita(rom))
            snprintf(carousel_footer, sizeof carousel_footer, "%s", T("Installe notre application Vita3K pour jouer aux jeux PS Vita"));
        return;
    }
    pthread_mutex_lock(&emu_lock);
    snprintf(core_rom, sizeof core_rom, "%s", rom);
    snprintf(game_key, sizeof game_key, "%s", key);
    snprintf(game_title, sizeof game_title, "%s", title);
    snprintf(join_ip, sizeof join_ip, "%s", ip);
    pthread_mutex_unlock(&emu_lock);
    snprintf(joined_ip, sizeof joined_ip, "%s", ip);
    atomic_store(&perf_system, -1);
    atomic_store(&emu_state, 0);
    atomic_store(&emu_cmd, CMD_LOAD);
    mode = MODE_GAME;
    loading_since = now_ns();
    snprintf(carousel_footer, sizeof carousel_footer, T("Chargement de %s"), title);
    loading_footer = true;
    LOG("launch %s%s%s", rom, ip[0] ? " joining " : "", ip);
}

/* Starts a carousel entry: a game in the emulator thread, or another app. */
static void launch_entry(int i) {
    const TremuEntry *e = tremu_carousel_entry(i);
    if (!e) return;
    if (e->join_ip[0] && !e->rom[0]) {
        snprintf(carousel_footer, sizeof carousel_footer, T("Pour rejoindre, copie %s dans Download/Roms"), e->join_key);
        return;
    }
    snprintf(options.game, sizeof options.game, "%s", tremu_entry_key(e));
    tremu_options_save(options_path, &options);
    if (e->package[0]) {
        if (!tremu_launch_package(e->package))
            snprintf(carousel_footer, sizeof carousel_footer, T("Impossible de lancer %s"), e->title);
        return;
    }
    launch_game(e->rom, tremu_entry_key(e), e->title, e->join_ip);
}

/* "Multijoueur" menu line: Solo, Host (Wi-Fi), Host (Internet), Join by code, then one "Join"
 * per host heard on the local network. */
enum { MULTI_SOLO, MULTI_HOST, MULTI_HOST_INTERNET, MULTI_CODE, MULTI_LAN_FIRST };
static NetHost menu_hosts[6];
static int menu_host_count;
/* Party code lookup in progress (XR thread): the host is asked which game it plays. */
static bool code_query;
static char code_host[64];

static void update_multiplayer_choices(void) {
    char choices[10][64];
    menu_host_count = netplay_discovered(menu_hosts, 6);
    snprintf(choices[MULTI_SOLO], sizeof choices[0], "%s", T("Solo"));
    snprintf(choices[MULTI_HOST], sizeof choices[0], "%s", T(mode == MODE_GAME ? "H\xC3\xA9" "berger (Wi-Fi)" : "H\xC3\xA9" "berger (lance un jeu)"));
    snprintf(choices[MULTI_HOST_INTERNET], sizeof choices[0], "%s", T(mode == MODE_GAME ? "H\xC3\xA9" "berger sur Internet" : "Internet (lance un jeu)"));
    snprintf(choices[MULTI_CODE], sizeof choices[0], "%s", T("Rejoindre par code"));
    int count = MULTI_LAN_FIRST, current = MULTI_SOLO;
    for (int i = 0; i < menu_host_count; i++)
        snprintf(choices[count++], sizeof choices[0], T("Rejoindre : %s"), menu_hosts[i].title);
    NetState net = netplay_state();
    if (net != NET_OFF && net != NET_FAILED) {
        if (netplay_is_host()) current = netplay_is_internet() ? MULTI_HOST_INTERNET : MULTI_HOST;
        else if (strchr(joined_ip, ':')) current = MULTI_CODE;
        else
            for (int i = 0; i < menu_host_count; i++)
                if (!strcmp(menu_hosts[i].ip, joined_ip)) current = MULTI_LAN_FIRST + i;
    } else if (atomic_load(&adhoc_game) && joined_ip[0]) {
        current = strchr(joined_ip, ':') ? MULTI_CODE : MULTI_SOLO;
        for (int i = 0; i < menu_host_count; i++)
            if (!strcmp(menu_hosts[i].ip, joined_ip)) current = MULTI_LAN_FIRST + i;
    }
    tremu_menu_set_multiplayer((const char (*)[64])choices, count, current);
}

static void back_to_carousel(void) {
    if (mode != MODE_GAME) return;
    atomic_store(&emu_state, 0);
    atomic_store(&emu_cmd, CMD_CLOSE);
    mode = MODE_CAROUSEL;
}

/* Joins the game rom_key hosted at host ("ip" or "ip:port"), if we have that ROM. */
static void join_game(const char *rom_key, const char *host) {
    int i = tremu_carousel_find_game(rom_key);
    if (i < 0) {
        snprintf(carousel_footer, sizeof carousel_footer, T("Pour rejoindre, copie %s dans Download/Roms"), rom_key);
        back_to_carousel();
        return;
    }
    const TremuEntry *e = tremu_carousel_entry(i);
    launch_game(e->rom, rom_key, e->title, host);
}

/* Returns false when the menu must stay open (code entry). */
static bool apply_multiplayer(int pick) {
    NetState net = netplay_state();
    bool active = net != NET_OFF && net != NET_FAILED;
    if (pick == MULTI_SOLO) {
        if (active) atomic_store(&net_request, NET_REQ_STOP);
        joined_ip[0] = 0;
    } else if (pick == MULTI_HOST || pick == MULTI_HOST_INTERNET) {
        if (mode == MODE_GAME) atomic_store(&net_request, pick == MULTI_HOST ? NET_REQ_HOST : NET_REQ_HOST_INTERNET);
        joined_ip[0] = 0;
    } else if (pick == MULTI_CODE) {
        tremu_menu_begin_code();
        return false;
    } else if (pick - MULTI_LAN_FIRST < menu_host_count) {
        const NetHost *h = &menu_hosts[pick - MULTI_LAN_FIRST];
        join_game(h->rom_key, h->ip);
    }
    return true;
}

/* A party code was typed: decode it and ask that host which game it plays. */
static void start_code_query(const char *code) {
    uint32_t ip;
    uint16_t port;
    if (!tremu_code_decode(code, &ip, &port)) {
        snprintf(carousel_footer, sizeof carousel_footer, T("Code invalide : %.5s-%.5s"), code, code + 5);
        back_to_carousel();
        return;
    }
    snprintf(code_host, sizeof code_host, "%u.%u.%u.%u:%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255, port);
    if (!netplay_query_start(ip, port)) {
        snprintf(carousel_footer, sizeof carousel_footer, "%s", T("R\xC3\xA9seau indisponible"));
        return;
    }
    LOG("party code %s -> %s", code, code_host);
    code_query = true;
    back_to_carousel();
    snprintf(carousel_footer, sizeof carousel_footer, T("Recherche de la partie %.5s-%.5s..."), code, code + 5);
}

static void poll_code_query(void) {
    if (!code_query) return;
    char key[64], title[64];
    int r = netplay_query_poll(key, sizeof key, title, sizeof title);
    if (!r) return;
    code_query = false;
    if (r < 0) {
        snprintf(carousel_footer, sizeof carousel_footer, "%s", T("Partie introuvable : v\xC3\xA9rifie le code (l'h\xC3\xB4te doit h\xC3\xA9" "berger sur Internet)"));
        return;
    }
    LOG("party %s plays %s (%s)", code_host, key, title);
    join_game(key, code_host);
}

/* The Touch controllers as the console's pad, as set in the "console controls" menu (padmap.h).
 * The left stick moves: the cross on NES, SNES and PS1, the analog stick on N64 and PS2. */
static void touch_pad(TremuPadConsole console, bool gun_trigger, bool gun_grenade, TremuGun *g) {
    enum { DEAD = 0x4000 };
    const int *map = options.pad_map[console];
    XrVector2f l = stick(LEFT), r = stick(RIGHT);
    int lx = (int)lroundf(l.x * 0x7fff), ly = (int)lroundf(-l.y * 0x7fff);
    int rx = (int)lroundf(r.x * 0x7fff), ry = (int)lroundf(-r.y * 0x7fff);
    bool vertical = abs(ry) > abs(rx);
    const bool in[TI_COUNT] = {
        [TI_A] = button(a_action, hand_path[RIGHT]), [TI_B] = button(b_action, hand_path[RIGHT]),
        [TI_X] = button(x_action, hand_path[LEFT]), [TI_Y] = button(y_action, hand_path[LEFT]),
        [TI_LGRIP] = axis(grip_action, hand_path[LEFT]) > .55f, [TI_RGRIP] = gun_grenade,
        [TI_LTRIGGER] = axis(trigger_action, hand_path[LEFT]) > .55f, [TI_RTRIGGER] = gun_trigger,
        [TI_RS_UP] = ry < -DEAD && vertical, [TI_RS_DOWN] = ry > DEAD && vertical,
        [TI_RS_LEFT] = rx < -DEAD && !vertical, [TI_RS_RIGHT] = rx > DEAD && !vertical,
        [TI_LCLICK] = button(click_action, hand_path[LEFT]), [TI_RCLICK] = button(click_action, hand_path[RIGHT]),
    };
    uint16_t pad = 0;
    for (int i = 0; i < TI_COUNT; i++)
        if (in[i] && map[i] >= 0 && map[i] < 16) pad |= 1u << map[i];
    if (console == TREMU_PAD_NES || console == TREMU_PAD_SNES || console == TREMU_PAD_PS1 || console == TREMU_PAD_MEGADRIVE ||
        console == TREMU_PAD_SATURN) {
        /* Digital pads: the left stick is the cross. */
        if (lx < -DEAD) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (lx > DEAD) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
        if (ly < -DEAD) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
        if (ly > DEAD) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    }
    /* Right stick directions set to the pad's right stick pass through as analog. */
    int ax = 0, ay = 0;
    if (ry < 0 && map[TI_RS_UP] == TREMU_PAD_RIGHT_ANALOG) ay = ry;
    if (ry > 0 && map[TI_RS_DOWN] == TREMU_PAD_RIGHT_ANALOG) ay = ry;
    if (rx < 0 && map[TI_RS_LEFT] == TREMU_PAD_RIGHT_ANALOG) ax = rx;
    if (rx > 0 && map[TI_RS_RIGHT] == TREMU_PAD_RIGHT_ANALOG) ax = rx;
    g->pad = pad;
    /* Whole steps of 256, as the network sends them, so both headsets of a party agree. */
    g->lx = (int16_t)((lx >> 8) * 256);
    g->ly = (int16_t)((ly >> 8) * 256);
    g->rx = (int16_t)((ax >> 8) * 256);
    g->ry = (int16_t)((ay >> 8) * 256);
}

/* Syncs the controllers; drives the menu while it is open, the splash and carousel outside games,
 * otherwise fills the lightgun state. Touch and WonderFitter inputs are combined. */
static void read_input(XrTime time, float aspect, TremuGun guns[2]) {
    TremuGun g = {.offscreen = true}, p2 = {.offscreen = true};
    guns[0] = g;
    guns[1] = p2;
    XrActiveActionSet as = {action_set, XR_NULL_PATH};
    XrActionsSyncInfo si = {XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &as;
    if (session_state != XR_SESSION_STATE_FOCUSED || XR_FAILED(xrSyncActions(session, &si))) return;
    uint64_t now = now_ns();
    bool ext_trigger = tremu_ext_pressed(EXT_TRIGGER);
    bool trigger = axis(trigger_action, hand_path[RIGHT]) > .55f || tremu_gun_trigger_held(now) || ext_trigger;
    bool confirm = button(a_action, hand_path[RIGHT]) || trigger;
    bool confirmed = confirm && !confirm_was;
    confirm_was = confirm;

    bool menu_button = button(menu_action, hand_path[LEFT]);
    if (menu_button && !menu_button_was && mode != MODE_SPLASH) set_menu_open(!menu_open, time);
    if (!menu_open && mode == MODE_GAME) {
        char ask_title[80], ask_text[64];
        unsigned ask_length = 0;
        if (tremu_core_text_pending(ask_title, sizeof ask_title, ask_text, sizeof ask_text, &ask_length)) {
            set_menu_open(true, time);
            tremu_menu_begin_text(ask_title, ask_text, ask_length);
            text_request = true;
        }
    }
    menu_button_was = menu_button;

    if (menu_open) {
        XrVector2f stick = nav_stick();
        bool changed;
        static bool erase_was;
        bool erase = button(b_action, hand_path[RIGHT]);
        if (erase && !erase_was) tremu_menu_text_erase();
        erase_was = erase;
        MenuAction action = tremu_menu_input(stick.x, stick.y, confirm, now, &options, &changed);
        if (changed) {
            tremu_options_store_gun(&options);
            tremu_options_save(options_path, &options);
            tremu_core_set_crosshair(options.crosshair);
            tremu_core_set_vulkan_driver(options.x360_driver);
            tremu_core_set_stereo(options.stereo);
            atomic_store(&perf_dirty, true);
        }
        if (action == MENU_RECENTER) {
            head_yaw_pose(time, &screen_origin);
            set_menu_open(false, time);
        } else if (action == MENU_CLOSE) {
            set_menu_open(false, time);
        } else if (action == MENU_APPLY_MULTI) {
            if (apply_multiplayer(tremu_menu_multiplayer_pick())) set_menu_open(false, time);
        } else if (action == MENU_QUIT) {
            quit = true;
        } else if (action == MENU_LANGUAGE) {
            tremu_lang_set(NULL, NULL, options.language);
        } else if (action == MENU_EXT_LEARNED) {
            int learned[EXT_ROLES];
            tremu_ext_learned(learned);
            memcpy(options.ext_buttons, learned, sizeof learned);
            tremu_ext_set_bindings(options.ext_buttons);
            tremu_options_save(options_path, &options);
            LOG("gun controls learned: %d %d %d %d %d", learned[0], learned[1], learned[2], learned[3], learned[4]);
        } else if (action == MENU_SAVE_STATE || action == MENU_LOAD_STATE) {
            if (mode == MODE_GAME && atomic_load(&emu_state) == 1)
                atomic_store(&state_request, action == MENU_SAVE_STATE ? STATE_REQ_SAVE : STATE_REQ_LOAD);
            else
                set_state_message(T("Aucune partie en cours"));
        } else if (action == MENU_TEXT_ENTERED) {
            if (search_request) {
                tremu_carousel_set_search(tremu_menu_text());
            } else {
                text_request = false;
                tremu_core_text_submit(tremu_menu_text());
            }
            set_menu_open(false, time);
        } else if (action == MENU_CODE_ENTERED) {
            start_code_query(tremu_menu_code());
            set_menu_open(false, time);
        } else if (action == MENU_BACK) {
            if (mode == MODE_GAME && atomic_load(&one_game_process)) {
                LOG("leaving the game: restarting the app");
                tremu_restart_after_exit();
                quit = true;
            } else if (mode == MODE_GAME) {
                atomic_store(&emu_state, 0);
                atomic_store(&emu_cmd, CMD_CLOSE);
                mode = MODE_CAROUSEL;
                carousel_footer[0] = 0;
            }
            set_menu_open(false, time);
        }
        confirm_was = true;  /* the press that closes the menu must not also launch a game */
        return;
    }
    if (mode == MODE_SPLASH) {
        if (confirmed) mode = MODE_CAROUSEL;
        return;
    }
    if (mode == MODE_CAROUSEL) {
        static bool clear_was;
        bool clear = button(b_action, hand_path[RIGHT]);
        if (clear && !clear_was && tremu_carousel_search()[0]) tremu_carousel_set_search("");
        clear_was = clear;
        XrVector2f nav = nav_stick();
        CarouselAction action = tremu_carousel_input(nav.x, nav.y, confirmed, now);
        if (action == CAROUSEL_LAUNCH) {
            launch_entry(tremu_carousel_selected());
        } else if (action == CAROUSEL_SEARCH) {
            set_menu_open(true, time);
            tremu_menu_begin_text(T("Rechercher un titre"), tremu_carousel_search(), 20);
            search_request = true;
        }
        return;
    }
    if (atomic_load(&emu_state) != 1) return;

    aim(time, aspect, RIGHT, &g);
    g.trigger = trigger;
    int layout = atomic_load(&pad_layout);
    if (layout != PAD_NONE) {
        /* The right trigger and grip include the WonderFitter's and a Bluetooth gun's. */
        g.aux_a = axis(grip_action, hand_path[RIGHT]) > .55f || tremu_gun_bolt_pressed(now) || tremu_ext_pressed(EXT_GRENADE);
        touch_pad((TremuPadConsole)layout, trigger, g.aux_a, &g);
        g.start = (g.pad >> RETRO_DEVICE_ID_JOYPAD_START & 1) || tremu_ext_pressed(EXT_START);
        g.select = (g.pad >> RETRO_DEVICE_ID_JOYPAD_SELECT & 1) || tremu_ext_pressed(EXT_COIN);
        g.reload = tremu_ext_pressed(EXT_ACTION);
        game_haptics(now, axis(trigger_action, hand_path[RIGHT]) > .55f || ext_trigger, false);
        guns[0] = g;
        return;
    }
    g.aux_a = axis(grip_action, hand_path[RIGHT]) > .55f || tremu_gun_bolt_pressed(now) ||
              tremu_ext_pressed(EXT_GRENADE);                                                  /* grenade */
    bool recalibrate = atomic_load(&guncon2);
    if (recalibrate) g.reload = button(a_action, hand_path[RIGHT]);
    else g.select = button(a_action, hand_path[RIGHT]);                                     /* coin */
    g.select = g.select || tremu_ext_pressed(EXT_COIN);
    g.reload = g.reload || tremu_ext_pressed(EXT_ACTION);
    g.start = button(b_action, hand_path[RIGHT]) || tremu_ext_pressed(EXT_START);
    if (options.local_p2) {
        /* Player 2 on the left Touch: X coin (recalibrate on PS2), Y start, trigger fires, grip
         * second button (Time Crisis' pedal). */
        aim(time, aspect, LEFT, &p2);
        p2.trigger = axis(trigger_action, hand_path[LEFT]) > .55f;
        p2.aux_a = axis(grip_action, hand_path[LEFT]) > .55f;
        if (recalibrate) p2.reload = button(x_action, hand_path[LEFT]);
        else p2.select = button(x_action, hand_path[LEFT]);
        p2.start = button(y_action, hand_path[LEFT]);
    } else {
        g.reload = g.reload || button(x_action, hand_path[LEFT]);
        g.aux_b = button(y_action, hand_path[LEFT]);
    }
    game_haptics(now, axis(trigger_action, hand_path[RIGHT]) > .55f || ext_trigger, options.local_p2 && p2.trigger);
    guns[0] = g;
    guns[1] = p2;
}

static void handle_xr_events(void) {
    XrEventDataBuffer e = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance, &e) == XR_SUCCESS) {
        if (e.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            session_state = ((XrEventDataSessionStateChanged *)&e)->state;
            LOG("session state %d", session_state);
            if (session_state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                session_running = XR_CHECK(xrBeginSession(session, &bi));
                if (session_running) tremu_perf_session_begun();
            } else if (session_state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(session);
                session_running = false;
            } else if (session_state == XR_SESSION_STATE_EXITING || session_state == XR_SESSION_STATE_LOSS_PENDING) {
                quit = true;
            }
        } else if (e.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            quit = true;
        }
        e = (XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
    }
}

static bool resumed;

/* Gamepads, mice and keyboards: buttons of Bluetooth/USB guns (see extgun.c). */
static int32_t on_input(struct android_app *app, AInputEvent *event) {
    (void)app;
    return tremu_ext_input(event);
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    (void)app;
    if (cmd == APP_CMD_RESUME) resumed = true;
    else if (cmd == APP_CMD_PAUSE) resumed = false;
    else if (cmd == APP_CMD_DESTROY) quit = true;
}

void android_main(struct android_app *app) {
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    /* ROMs live in internal storage (filled with `adb shell run-as`); a roms folder that adb
     * created in external storage belongs to the shell user and is unreadable by the app. */
    /* The core's system folder holds BIOS/device archives (e.g. cchip.zip), kept apart from the games. */
    snprintf(core_system_dir, sizeof core_system_dir, "%s/bios", SHARED_DIR);
    snprintf(core_save_dir, sizeof core_save_dir, "%s/saves", app->activity->internalDataPath);
    snprintf(internal_rom_dir, sizeof internal_rom_dir, "%s/roms", app->activity->internalDataPath);
    snprintf(unzip_dir, sizeof unzip_dir, "%s/unzipped", app->activity->internalDataPath);
    snprintf(drivers_dir, sizeof drivers_dir, "%s/drivers", app->activity->internalDataPath);
    snprintf(options_path, sizeof options_path, "%s/tremu.cfg", app->activity->internalDataPath);
    tremu_options_load(options_path, &options);
    tremu_ext_set_bindings(options.ext_buttons);
    tremu_core_set_crosshair(options.crosshair);
    tremu_core_set_stereo(options.stereo);
    tremu_menu_init(app->activity->vm, app->activity->clazz);
    tremu_core_set_android(app->activity->vm, app->activity->clazz);
    tremu_core_set_vulkan_driver(options.x360_driver);
    tremu_lang_set(app->activity->vm, app->activity->clazz, options.language);
    tremu_carousel_init(app->activity->vm, app->activity->clazz);
    mkdir(app->activity->internalDataPath, 0700);
    mkdir(core_save_dir, 0700);
    char fbneo_saves[600];
    snprintf(fbneo_saves, sizeof fbneo_saves, "%s/fbneo", core_save_dir);
    mkdir(fbneo_saves, 0700);  /* FBNeo writes high scores (.hi) and NVRAM here */
    mkdir(internal_rom_dir, 0700);
    char shared_roms[512], shared_banners[512];
    snprintf(shared_roms, sizeof shared_roms, "%s/Roms", SHARED_DIR);
    /* The player's own collection, sorted by system in subfolders, read where it is. */
    const char *download_roms = "/storage/emulated/0/Download/Roms";
    snprintf(shared_banners, sizeof shared_banners, "%s/Banner", SHARED_DIR);
    mkdir(SHARED_DIR, 0775);
    mkdir(shared_roms, 0775);
    mkdir(shared_banners, 0775);
    mkdir(core_system_dir, 0775);

    pthread_t emu;
    bool emu_started = false, xr_ready = false;
    while (!quit && !app->destroyRequested) {
        int events;
        struct android_poll_source *source;
        while (ALooper_pollOnce(session_running ? 0 : 100, NULL, &events, (void **)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
        }
        /* The Quest runtime waits for a resumed activity while creating the session, and the
         * activity cannot resume until these lifecycle events are answered: initialise only
         * after resume, never before the first poll. */
        if (!xr_ready) {
            if (!resumed) continue;
            if (!init_egl()) { LOG("EGL init failed"); break; }
            if (!init_openxr(app)) { LOG("OpenXR init failed"); break; }
            xr_ready = true;
            char xbox_games[600];
            snprintf(xbox_games, sizeof xbox_games, "%s/Xbox/games", SHARED_DIR);
            const char *rom_dirs[] = {shared_roms, internal_rom_dir, download_roms, xbox_games};
            int count = tremu_carousel_scan(rom_dirs, 4, shared_banners);
            for (int i = 0; i < count; i++)
                if (!strcmp(tremu_entry_key(tremu_carousel_entry(i)), options.game)) tremu_carousel_select(i);
            emu_started = pthread_create(&emu, NULL, emulator_thread, NULL) == 0;
            /* Test hook: "am start ... --es tremu_game <path>" starts that game at once (works with the
             * headset off: the emulator thread loads it even while the XR session is idle). */
            char intent_game[sizeof core_rom];
            if (tremu_intent_game(intent_game, sizeof intent_game)) {
                const char *name = strrchr(intent_game, '/');
                launch_game(intent_game, name ? name + 1 : intent_game, name ? name + 1 : intent_game, "");
            }
        }
        handle_xr_events();
        if (!session_running) continue;

        XrFrameState fs = {XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo fw = {XR_TYPE_FRAME_WAIT_INFO};
        if (!XR_CHECK(xrWaitFrame(session, &fw, &fs))) continue;
        XrFrameBeginInfo fb = {XR_TYPE_FRAME_BEGIN_INFO};
        xrBeginFrame(session, &fb);

        {
            /* "Optimisation": the game's settings while it runs, the general ones in the carousel. */
            static int applied_system = -2;
            int system = mode == MODE_GAME ? atomic_load(&perf_system) : -1;
            if (system != applied_system || atomic_exchange(&perf_dirty, false)) {
                applied_system = system;
                char key[sizeof game_key] = "", title[sizeof game_title] = "";
                if (system >= 0) {
                    pthread_mutex_lock(&emu_lock);
                    snprintf(key, sizeof key, "%s", game_key);
                    snprintf(title, sizeof title, "%s", game_title);
                    pthread_mutex_unlock(&emu_lock);
                }
                tremu_perf_apply(&options, key, system);
                if (!menu_open) tremu_menu_set_perf_game(key, title, system);
            }
            tremu_perf_tick();
        }

        /* A game switch closes the current game: drop its screen, the next one may differ in size. */
        if (screen_chain && atomic_load(&emu_state) != 1) {
            xrDestroySwapchain(screen_chain);
            free(screen_images);
            screen_chain = XR_NULL_HANDLE;
            screen_images = NULL;
        }
        if (!screen_chain && atomic_load(&emu_state) == 1) {
            pthread_mutex_lock(&emu_lock);
            unsigned max_w = shared_max_w, max_h = shared_max_h;
            pthread_mutex_unlock(&emu_lock);
            create_screen_swapchain(max_w ? max_w : 640, max_h ? max_h : 480);
        }
        pthread_mutex_lock(&emu_lock);
        float aspect = shared_aspect;
        pthread_mutex_unlock(&emu_lock);
        TremuGun guns[2];
        if (menu_open) update_multiplayer_choices();
        read_input(fs.predictedDisplayTime, aspect, guns);
        pthread_mutex_lock(&emu_lock);
        shared_guns[0] = guns[0];
        shared_guns[1] = guns[1];
        pthread_mutex_unlock(&emu_lock);
        /* The arcade pauses while the menu is open or the headset menu has focus. */
        atomic_store(&emu_active, session_state == XR_SESSION_STATE_FOCUSED && !menu_open && mode == MODE_GAME);
        if (mode == MODE_GAME && atomic_load(&emu_state) == -1) {
            const TremuEntry *e = tremu_carousel_entry(tremu_carousel_selected());
            pthread_mutex_lock(&emu_lock);
            if (load_error[0])
                snprintf(carousel_footer, sizeof carousel_footer, "%s : %s", e ? e->title : T("ce jeu"), load_error);
            else
                snprintf(carousel_footer, sizeof carousel_footer, T("\xC3\x89" "chec du chargement de %s (ROM incompl\xC3\xA8te ?)"),
                         e ? e->title : T("ce jeu"));
            load_error[0] = 0;
            loading_footer = false;
            pthread_mutex_unlock(&emu_lock);
            atomic_store(&emu_state, 0);
            mode = MODE_CAROUSEL;
        }

        if (mode == MODE_SPLASH) {
            if (!splash_until) splash_until = now_ns() + 3000000000ull;
            if (now_ns() > splash_until) mode = MODE_CAROUSEL;
            if (!splash_chain) create_swapchain(SPLASH_WIDTH, SPLASH_HEIGHT, &splash_chain, &splash_images, &splash_image_count);
            const uint32_t *logo = splash_chain && !splash_uploaded ? tremu_load_logo() : NULL;
            if (logo && upload_pixels(splash_chain, splash_images, logo, SPLASH_WIDTH, SPLASH_HEIGHT)) splash_uploaded = true;
            if (!logo && !splash_uploaded && splash_chain) mode = MODE_CAROUSEL;  /* no logo: skip */
        }
        /* Local-network multiplayer: hosts announce their game, we list them as "join" tiles. */
        netplay_discovery_poll(now_ns());
        /* Each gun keeps its own mounting (angle, drop, back), switched when it connects. */
        static unsigned gun_generation;
        char gun_name[32];
        unsigned generation = tremu_gun_name(gun_name, sizeof gun_name);
        if (generation != gun_generation) {
            gun_generation = generation;
            if (tremu_options_use_gun(&options, gun_name)) LOG("gun %s: its own mounting restored", gun_name);
            tremu_options_save(options_path, &options);
        }
        poll_code_query();
        tremu_carousel_poll_posters();
        if (tremu_library_changed()) {
            /* Files arrived through the "add games" page: list them, keeping the selection. */
            char key[64] = "";
            const TremuEntry *sel = tremu_carousel_entry(tremu_carousel_selected());
            if (sel) snprintf(key, sizeof key, "%s", tremu_entry_key(sel));
            const char *rom_dirs[] = {shared_roms, internal_rom_dir, download_roms};
            int count = tremu_carousel_scan(rom_dirs, 3, shared_banners);
            for (int i = 0; i < count; i++)
                if (!strcmp(tremu_entry_key(tremu_carousel_entry(i)), key)) tremu_carousel_select(i);
        }
        static char library[128];
        static uint64_t next_library_check;
        if (now_ns() > next_library_check) {
            tremu_library_info(library, sizeof library);
            next_library_check = now_ns() + 3000000000ull;
        }
        static uint64_t next_join_refresh;
        if (now_ns() > next_join_refresh) {
            NetHost hosts[8];
            tremu_carousel_set_joinable(hosts, netplay_discovered(hosts, 8));
            next_join_refresh = now_ns() + 500000000ull;
        }
        NetState net = netplay_state();
        char net_status[256];
        netplay_status(net_status, sizeof net_status);
        if (mode == MODE_GAME && net == NET_OFF && atomic_load(&adhoc_game) && joined_ip[0])
            snprintf(net_status, sizeof net_status, T("PSP ad hoc : h\xC3\xB4te %s, ouvre le mode multijoueur du jeu"), joined_ip);
        if (mode == MODE_GAME && net == NET_FAILED) {
            /* Joining failed or the host left: back to the carousel with the reason. */
            snprintf(carousel_footer, sizeof carousel_footer, "%s", net_status);
            loading_footer = false;
            netplay_dismiss_failure();
            atomic_store(&emu_state, 0);
            atomic_store(&emu_cmd, CMD_CLOSE);
            mode = MODE_CAROUSEL;
        }
        bool joining = net == NET_JOINING || net == NET_RECEIVING;
        /* While a game starts: its loading picture where its screen will be, instead of black. */
        bool show_loading = mode == MODE_GAME && !joining && (atomic_load(&emu_state) == 0 ||
                                                              (atomic_load(&emu_state) == 1 && atomic_load(&game_booting)));
        bool show_carousel = mode == MODE_CAROUSEL || (mode == MODE_GAME && joining);
        if (show_loading) {
            if (!carousel_chain)
                create_swapchain(CAROUSEL_WIDTH, CAROUSEL_HEIGHT, &carousel_chain, &carousel_images, &carousel_image_count);
            static uint64_t next_loading_frame;
            uint64_t t = now_ns();
            if (carousel_chain && t >= next_loading_frame) {
                next_loading_frame = t + 40000000ull;  /* 25 pictures a second are plenty for the wheel */
                uint64_t ms = (t - loading_since) / 1000000;
                char key[64], title[64], status[400];
                pthread_mutex_lock(&emu_lock);
                snprintf(key, sizeof key, "%s", game_key);
                snprintf(title, sizeof title, "%s", game_title);
                pthread_mutex_unlock(&emu_lock);
                snprintf(status, sizeof status, T("Chargement de %s"), title);
                snprintf(status + strlen(status), sizeof status - strlen(status), "...   %u s", (unsigned)(ms / 1000));
                const uint32_t *pixels = tremu_loading_render(key, title, status, (float)(ms % 1000) * 0.36f);
                if (pixels && upload_pixels(carousel_chain, carousel_images, pixels, CAROUSEL_WIDTH, CAROUSEL_HEIGHT))
                    carousel_uploaded = true;
            }
        }
        if (show_carousel) {
            if (!carousel_chain)
                create_swapchain(CAROUSEL_WIDTH, CAROUSEL_HEIGHT, &carousel_chain, &carousel_images, &carousel_image_count);
            char footer[400];
            if (mode == MODE_GAME && joining) snprintf(footer, sizeof footer, "%s", net_status);
            else if (carousel_footer[0]) snprintf(footer, sizeof footer, "%s", carousel_footer);
            else if (!tremu_carousel_count() && library[0])
                snprintf(footer, sizeof footer, T("Aucun jeu : sur ton PC ou ton t\xC3\xA9l\xC3\xA9phone, ouvre %s"), library);
            else if (!tremu_carousel_count()) snprintf(footer, sizeof footer, "%s", T("Aucun jeu : copie tes ROM dans Download/Roms et tes affiches dans Tremu/Banner"));
            else if (library[0] && now_ns() / 6000000000ull % 2)  /* alternates every 6 s with the controls */
                snprintf(footer, sizeof footer, T("Ajouter des jeux : %s"), library);
            else {
                const char *controls = tremu_carousel_in_bar()
                    ? T("Stick gauche : onglet ou loupe     Bas : revenir aux jeux     A / g\xC3\xA2" "chette : valider")
                    : T("Stick gauche : choisir     Haut : onglets et recherche     A / g\xC3\xA2" "chette : jouer     Menu : options");
                const TremuEntry *shown = tremu_carousel_entry(tremu_carousel_selected());
                if (shown && shown->detail[0]) snprintf(footer, sizeof footer, "%s     %s", shown->detail, controls);
                else snprintf(footer, sizeof footer, "%s", controls);
            }
            const uint32_t *pixels = carousel_chain ? tremu_carousel_render(fs.predictedDisplayPeriod / 1e9f, footer) : NULL;
            if (pixels && upload_pixels(carousel_chain, carousel_images, pixels, CAROUSEL_WIDTH, CAROUSEL_HEIGHT))
                carousel_uploaded = true;
        }
        if (loading_footer && (mode != MODE_GAME || atomic_load(&emu_state) != 0)) {
            carousel_footer[0] = 0;
            loading_footer = false;
        }

        if (menu_open && !menu_chain)
            create_swapchain(MENU_WIDTH, MENU_HEIGHT, &menu_chain, &menu_images, &menu_image_count);
        if (menu_open && menu_chain) {
            char footer[256];
            pthread_mutex_lock(&emu_lock);
            bool show_state = now_ns() < state_message_until;
            snprintf(footer, sizeof footer, "%s", show_state ? state_message : net_status);
            pthread_mutex_unlock(&emu_lock);
            const uint32_t *menu_pixels = tremu_menu_render(&options, tremu_gun_connected(), footer);
            if (menu_pixels && upload_pixels(menu_chain, menu_images, menu_pixels, MENU_WIDTH, MENU_HEIGHT))
                menu_uploaded = true;
        }

        unsigned w = 0, h = 0;
        if (screen_chain) upload_frame(&w, &h);
        XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        quad.next = tremu_perf_quad_next();
        quad.space = local_space;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = screen_chain;
        quad.subImage.imageRect.extent.width = (int32_t)w;
        quad.subImage.imageRect.extent.height = (int32_t)h;
        quad.pose = screen_pose();
        quad.size = (XrExtent2Df){options.screen_width / 100.f, options.screen_width / 100.f / aspect};
        XrCompositionLayerQuad right_eye = quad;
        if (screen_eye_w) {
            quad.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
            right_eye.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
            quad.subImage.imageRect.offset.x = (int32_t)screen_left_x;
            right_eye.subImage.imageRect.offset.x = (int32_t)screen_right_x;
            quad.subImage.imageRect.extent.width = right_eye.subImage.imageRect.extent.width = (int32_t)screen_eye_w;
        }
        XrCompositionLayerQuad menu = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        menu.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        menu.space = local_space;
        menu.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        menu.subImage.swapchain = menu_chain;
        menu.subImage.imageRect.extent.width = MENU_WIDTH;
        menu.subImage.imageRect.extent.height = MENU_HEIGHT;
        menu.pose = menu_pose;
        menu.size = (XrExtent2Df){1.0f, 1.0f * MENU_HEIGHT / MENU_WIDTH};
        XrCompositionLayerQuad splash = quad, carousel = quad;
        splash.eyeVisibility = carousel.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        splash.subImage.imageRect.offset.x = carousel.subImage.imageRect.offset.x = 0;
        splash.next = carousel.next = NULL;  /* sharpening is for the game's screen */
        splash.layerFlags = carousel.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        splash.subImage.swapchain = splash_chain;
        splash.subImage.imageRect.extent = (XrExtent2Di){SPLASH_WIDTH, SPLASH_HEIGHT};
        splash.size = (XrExtent2Df){1.8f, 1.8f * SPLASH_HEIGHT / SPLASH_WIDTH};
        carousel.subImage.swapchain = carousel_chain;
        carousel.subImage.imageRect.extent = (XrExtent2Di){CAROUSEL_WIDTH, CAROUSEL_HEIGHT};
        carousel.size = (XrExtent2Df){options.screen_width / 100.f * 1.2f,
                                      options.screen_width / 100.f * 1.2f * CAROUSEL_HEIGHT / CAROUSEL_WIDTH};
        const XrCompositionLayerBaseHeader *layers[5];
        uint32_t layer_count = 0;
        /* Mixed-reality background: the room under everything else. */
        const XrCompositionLayerBaseHeader *room = tremu_passthrough_layer(options.background);
        if (room) layers[layer_count++] = room;
        tremu_voxel_set_see_through(room != NULL);
        if (mode == MODE_SPLASH && splash_uploaded) layers[layer_count++] = (XrCompositionLayerBaseHeader *)&splash;
        else if ((show_carousel || show_loading) && carousel_uploaded) layers[layer_count++] = (XrCompositionLayerBaseHeader *)&carousel;
        else if (mode == MODE_GAME && w && h && voxel_view) {
            const XrCompositionLayerBaseHeader *voxels =
                fs.shouldRender ? tremu_voxel_render(local_space, fs.predictedDisplayTime, quad.pose, quad.size.width, quad.size.height)
                                : NULL;
            if (voxels) layers[layer_count++] = voxels;
        } else if (mode == MODE_GAME && w && h) {
            layers[layer_count++] = (XrCompositionLayerBaseHeader *)&quad;
            if (screen_eye_w) layers[layer_count++] = (XrCompositionLayerBaseHeader *)&right_eye;
        }
        if (menu_open && menu_uploaded) layers[layer_count++] = (XrCompositionLayerBaseHeader *)&menu;
        XrFrameEndInfo fe = {XR_TYPE_FRAME_END_INFO};
        fe.displayTime = fs.predictedDisplayTime;
        fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fe.layerCount = fs.shouldRender ? layer_count : 0;
        fe.layers = layers;
        xrEndFrame(session, &fe);
    }
    /* Quitting (menu, headset's quit, app closed): close the game so its saves are written, then end
     * the process. A core stuck in its exit must not keep the app half-alive, which made the next
     * launch fail: wait at most 4 s for the emulator thread. */
    LOG("quitting");
    quit = true;
    for (int i = 0; emu_started && i < 400 && !atomic_load(&emu_done); i++) sleep_ms(10);
    if (emu_started && !atomic_load(&emu_done)) LOG("emulator thread still busy, leaving anyway");
    tremu_options_save(options_path, &options);
    if (session) xrDestroySession(session);
    if (instance) xrDestroyInstance(instance);
    ANativeActivity_finish(app->activity);
    LOG("bye");
    _exit(0);
}
