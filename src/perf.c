#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "perf.h"
#include "lang.h"
#include "systems.h"
#include <android/log.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

static PFN_xrPerfSettingsSetPerformanceLevelEXT set_level;
static PFN_xrEnumerateDisplayRefreshRatesFB enumerate_rates;
static PFN_xrRequestDisplayRefreshRateFB request_rate;
static PFN_xrGetDisplayRefreshRateFB get_rate;
static PFN_xrSetAndroidApplicationThreadKHR set_thread;
static XrSession perf_session = XR_NULL_HANDLE;
static bool has_sharpening;

enum { MAX_RATES = 8 };
static float rates[MAX_RATES];
static uint32_t rate_count;

static atomic_int emulator_tid;
static bool session_running;
static int registered_emulator_tid;

static WfgPerf current;
static bool have_current;
static float requested_rate;
static XrCompositionLayerSettingsFB quad_settings = {XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB};

/* Written by the emulator thread, read by the XR thread. */
static _Atomic float wanted, shown_fps, shown_wanted, shown_ms;

void wfg_perf_add_extensions(const char **ext, uint32_t *count) {
    static const char *const wanted_names[] = {XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
                                               XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME, XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME};
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL)) || !n) return;
    XrExtensionProperties *props = calloc(n, sizeof *props);
    if (!props) return;
    for (uint32_t i = 0; i < n; i++) props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL, n, &n, props)))
        for (size_t w = 0; w < sizeof wanted_names / sizeof wanted_names[0]; w++) {
            bool found = false;
            for (uint32_t i = 0; i < n && !found; i++) found = !strcmp(props[i].extensionName, wanted_names[w]);
            LOG("perf extension %s: %s", wanted_names[w], found ? "yes" : "no");
            if (found) {
                ext[(*count)++] = wanted_names[w];
                if (!strcmp(wanted_names[w], XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME)) has_sharpening = true;
            }
        }
    free(props);
}

void wfg_perf_init(XrInstance instance, XrSession session) {
    perf_session = session;
#define GET(name, fn) xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)&fn)
    GET("xrPerfSettingsSetPerformanceLevelEXT", set_level);
    GET("xrEnumerateDisplayRefreshRatesFB", enumerate_rates);
    GET("xrRequestDisplayRefreshRateFB", request_rate);
    GET("xrGetDisplayRefreshRateFB", get_rate);
    GET("xrSetAndroidApplicationThreadKHR", set_thread);
#undef GET
    if (enumerate_rates && XR_SUCCEEDED(enumerate_rates(session, 0, &rate_count, NULL))) {
        if (rate_count > MAX_RATES) rate_count = MAX_RATES;
        uint32_t asked = rate_count;
        if (XR_FAILED(enumerate_rates(session, asked, &rate_count, rates))) rate_count = 0;
        if (rate_count > MAX_RATES) rate_count = MAX_RATES;
    }
    for (uint32_t i = 0; i < rate_count; i++) LOG("perf refresh rate available: %.0f Hz", rates[i]);
}

static const XrPerfSettingsLevelEXT levels[WFG_PERF_LEVELS] = {
    XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT,
    XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT};

void wfg_perf_resolve(const WfgPerf *perf, int system, WfgPerf *out) {
    *out = *perf;
    int profile = perf->profile;
    if (profile == WFG_PERF_AUTO) {
        /* The systems whose emulation takes all the processor can give. */
        bool heavy = system == SYS_PS2 || system == SYS_NAMCO246 || system == SYS_X360 || system == SYS_XBOX || system == SYS_WII ||
                     system == SYS_GAMECUBE || system == SYS_SATURN || system == SYS_N64 || system == SYS_MAME;
        /* No game (the carousel): nothing to gain from the higher levels but heat. */
        profile = system < 0 ? WFG_PERF_BATTERY : heavy ? WFG_PERF_PERFORMANCE : WFG_PERF_BALANCED;
    }
    switch (profile) {
    case WFG_PERF_BATTERY: *out = (WfgPerf){perf->profile, WFG_LEVEL_LOW, WFG_LEVEL_LOW, 72, 0, 0}; break;
    case WFG_PERF_BALANCED: *out = (WfgPerf){perf->profile, WFG_LEVEL_HIGH, WFG_LEVEL_HIGH, 0, 0, 0}; break;
    case WFG_PERF_PERFORMANCE: *out = (WfgPerf){perf->profile, WFG_LEVEL_MAX, WFG_LEVEL_HIGH, 0, 0, 0}; break;
    case WFG_PERF_QUALITY: *out = (WfgPerf){perf->profile, WFG_LEVEL_MAX, WFG_LEVEL_MAX, 0, 1, 2}; break;
    default: break;
    }
}

/* The rate closest to a whole multiple of the game's frame rate: each game frame then stays on screen
 * for the same time. On a tie, the lowest rate. Nothing known: the headset's lowest. */
static float auto_rate(float fps) {
    float best = rate_count ? rates[0] : 0, best_error = 1e9f;
    for (uint32_t i = 0; i < rate_count; i++) {
        float multiple = fps > 1 ? roundf(rates[i] / fps) : 1;
        if (multiple < 1) multiple = 1;
        float error = fps > 1 ? fabsf(rates[i] - multiple * fps) / rates[i] : (rates[i] < best ? 0 : 1);
        if (error < best_error - .001f || (fabsf(error - best_error) <= .001f && rates[i] < best)) {
            best = rates[i];
            best_error = error;
        }
    }
    return best;
}

static void request_refresh(float rate) {
    if (!request_rate || rate <= 0 || rate == requested_rate) return;
    XrResult r = request_rate(perf_session, rate);
    LOG("perf refresh %.0f Hz -> %d", rate, r);
    if (XR_SUCCEEDED(r)) requested_rate = rate;
}

static void send_settings(void) {
    if (!have_current || !session_running) return;
    if (set_level) {
        XrResult cpu = set_level(perf_session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, levels[current.cpu]);
        XrResult gpu = set_level(perf_session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, levels[current.gpu]);
        LOG("perf levels cpu %d -> %d, gpu %d -> %d", current.cpu, cpu, current.gpu, gpu);
    }
    wfg_perf_tick();
}

void wfg_perf_apply(const WfgOptions *o, const char *game_key, int system) {
    wfg_perf_resolve(wfg_options_perf(o, game_key), system, &current);
    if (current.cpu < 0 || current.cpu >= WFG_PERF_LEVELS) current.cpu = WFG_LEVEL_HIGH;
    if (current.gpu < 0 || current.gpu >= WFG_PERF_LEVELS) current.gpu = WFG_LEVEL_HIGH;
    have_current = true;
    if (system < 0) atomic_store(&wanted, 0);  /* the carousel has no frame rate to follow */
    quad_settings.layerFlags = current.sharpen == 1 ? XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB
                               : current.sharpen == 2 ? XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB : 0;
    LOG("perf for '%s' system %d: profile %d cpu %d gpu %d refresh %d sharpen %d resolution %d", game_key, system, current.profile,
        current.cpu, current.gpu, current.refresh, current.sharpen, current.res);
    send_settings();
}

void wfg_perf_session_begun(void) {
    session_running = true;
    requested_rate = 0;
    if (set_thread) {
        XrResult r = set_thread(perf_session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, gettid());
        LOG("perf XR thread registered -> %d", r);
    }
    registered_emulator_tid = 0;
    send_settings();
}

void wfg_perf_emulator_thread(void) { atomic_store(&emulator_tid, gettid()); }

void wfg_perf_tick(void) {
    if (!session_running) return;
    int tid = atomic_load(&emulator_tid);
    if (set_thread && tid && tid != registered_emulator_tid) {
        XrResult r = set_thread(perf_session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, tid);
        LOG("perf emulator thread registered -> %d", r);
        registered_emulator_tid = tid;
    }
    if (!have_current) return;
    request_refresh(current.refresh ? (float)current.refresh : auto_rate(atomic_load(&wanted)));
}

const void *wfg_perf_quad_next(void) { return has_sharpening && quad_settings.layerFlags ? &quad_settings : NULL; }

void wfg_perf_frame(uint64_t run_ns, double wanted_fps) {
    static uint64_t window_start, sum;
    static unsigned frames;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    atomic_store(&wanted, (float)wanted_fps);
    /* A long gap is a pause (menu open, headset removed), not a slow game: the last measure stands. */
    if (now - window_start > 3000000000ull) {
        window_start = now;
        sum = 0;
        frames = 0;
        return;
    }
    sum += run_ns;
    frames++;
    if (now - window_start >= 1000000000ull) {
        atomic_store(&shown_fps, (float)(frames * 1e9 / (double)(now - window_start)));
        atomic_store(&shown_wanted, (float)wanted_fps);
        atomic_store(&shown_ms, (float)(sum / 1e6 / frames));
        window_start = now;
        sum = 0;
        frames = 0;
    }
}

int wfg_perf_next_refresh(int refresh, int direction) {
    /* Choices: 0, then each rate. */
    int index = 0;
    for (uint32_t i = 0; i < rate_count; i++)
        if ((int)rates[i] == refresh) index = (int)i + 1;
    int size = (int)rate_count + 1;
    index = (index + direction + size) % size;
    return index ? (int)rates[index - 1] : 0;
}

bool wfg_perf_status(char *speed, size_t speed_size, char *system, size_t system_size) {
    float fps = atomic_load(&shown_fps);
    if (fps > 0)
        snprintf(speed, speed_size, T("Derni\xC3\xA8re mesure : %.1f / %.1f images/s, %.1f ms par image"), fps, atomic_load(&shown_wanted),
                 atomic_load(&shown_ms));
    else
        snprintf(speed, speed_size, "%s", T("Derni\xC3\xA8re mesure : aucune, lance un jeu"));
    float rate = 0;
    if (get_rate && perf_session) get_rate(perf_session, &rate);
    double available_gb = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[96];
        long kb;
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "MemAvailable: %ld", &kb) == 1) { available_gb = kb / 1048576.0; break; }
        fclose(f);
    }
    snprintf(system, system_size, T("\xC3\x89" "cran : %.0f Hz     M\xC3\xA9moire libre : %.1f Go"), rate, available_gb);
    return fps > 0;
}
