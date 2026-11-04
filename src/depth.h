#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Depth estimated by a neural network (MiDaS v2, 256x256, TensorFlow Lite on the GPU) for games
 * whose emulator cannot tell layers apart (Terminator 2...). It runs on its own thread, a few
 * frames behind the game; the voxel view uses it as a continuous depth. */

/* Loads TensorFlow Lite and the model file (on the worker thread); false if a library is missing. */
bool tremu_depth_start(const char *model_path);
void tremu_depth_stop(void);
/* Hands over the newest frame (RGBA, bottom row first); skipped while the network is busy. */
void tremu_depth_submit(const uint32_t *pixels, unsigned w, unsigned h);
/* Latest depth resized to w x h (bottom row first, 0 far .. 255 near); false before the first. */
bool tremu_depth_get(uint8_t *out, unsigned w, unsigned h);
/* Milliseconds of the last inference, 0 if none (for the log and the menu). */
float tremu_depth_ms(void);
