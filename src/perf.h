#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <openxr/openxr.h>
#include "options.h"

/* "Optimisation" page: what an app may ask the Quest for itself, without ADB. CPU and GPU levels
 * (XR_EXT_performance_settings), display refresh (XR_FB_display_refresh_rate), sharpening of the
 * game's screen (XR_FB_composition_layer_settings) and the threads that matter
 * (XR_KHR_android_thread_settings). Each is skipped when the runtime does not offer it. */

/* Adds the extensions the runtime offers; ext must have room for 4 more. */
void wfg_perf_add_extensions(const char **ext, uint32_t *count);
/* After the session exists. */
void wfg_perf_init(XrInstance instance, XrSession session);
/* After each xrBeginSession (from the XR thread): registers the threads, asks for the settings again. */
void wfg_perf_session_begun(void);
/* The emulator thread, from itself. */
void wfg_perf_emulator_thread(void);
/* The settings of this game (key "" and system -1: the carousel). From the XR thread. */
void wfg_perf_apply(const WfgOptions *o, const char *game_key, int system);
/* Every frame, from the XR thread: follows the game's frame rate when the refresh is automatic. */
void wfg_perf_tick(void);
/* Chained to the game screen's quad layer; NULL without sharpening. */
const void *wfg_perf_quad_next(void);
/* After each core frame, from the emulator thread. */
void wfg_perf_frame(uint64_t run_ns, double wanted_fps);

/* A profile's real values; the custom profile keeps its own. */
void wfg_perf_resolve(const WfgPerf *perf, int system, WfgPerf *out);
/* Refresh choices: 0 = automatic, then the rates of this headset. */
int wfg_perf_next_refresh(int refresh, int direction);
/* Last measure: speed, core frame time, display refresh, free memory. Returns false before any. */
bool wfg_perf_status(char *speed, size_t speed_size, char *system, size_t system_size);
