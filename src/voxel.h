#pragma once
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <stdbool.h>
#include <stdint.h>

/* 3D view of NES games (and arcade drivers that report layers): each pixel of the frame becomes a small block, placed in depth by its
 * layer (see tremu_core_layers): sprites on the screen plane (where the gun aims), the background
 * tiles behind them, sprites hidden behind the background further back, the backdrop colour at
 * the back. Drawn into a stereo projection layer, on the XR thread. */

/* Creates the eye swapchains and the GL objects; false when the runtime refuses them. */
/* Largest frame drawn in 3D (one block per pixel); bigger games keep the flat screen. */
enum { TREMU_VOXEL_MAX = 512 };

bool tremu_voxel_init(XrInstance instance, XrSystemId system, XrSession session);
/* New frame to show: RGBA pixels and layers, both w*h with the bottom row first. With
 * continuous set, "layers" holds a depth per pixel instead (0 far .. 255 near, see depth.h). */
void tremu_voxel_upload(const uint32_t *pixels, const uint8_t *layers, unsigned w, unsigned h, bool continuous);
/* Renders both eyes around the screen (pose, size in meters) and returns the projection
 * layer to submit, or NULL when nothing could be drawn. */
/* With a room background the space around the voxels stays see-through. */
void tremu_voxel_set_see_through(bool on);
const XrCompositionLayerBaseHeader *tremu_voxel_render(XrSpace space, XrTime time, XrPosef screen, float width,
                                                     float height);
void tremu_voxel_destroy(void);
