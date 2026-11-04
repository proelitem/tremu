#pragma once
#include <stdbool.h>
#include <openxr/openxr.h>

/* Mixed-reality background: the room seen by the Quest's cameras behind our layers
 * (XR_FB_passthrough; the system composes it, the app never reads the cameras). */
enum { TREMU_BACKGROUND_BLACK, TREMU_BACKGROUND_ROOM, TREMU_BACKGROUND_DIMMED, TREMU_BACKGROUNDS };

/* Whether the runtime offers passthrough (call before creating the instance with that extension). */
bool tremu_passthrough_supported(void);
/* After the session exists. */
void tremu_passthrough_init(XrInstance instance, XrSession session);
/* The layer to put first in the frame for this background, or NULL for black (passthrough paused). */
const XrCompositionLayerBaseHeader *tremu_passthrough_layer(int background);
