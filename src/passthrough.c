#include "passthrough.h"
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

static PFN_xrCreatePassthroughFB create_passthrough;
static PFN_xrCreatePassthroughLayerFB create_layer;
static PFN_xrPassthroughStartFB start;
static PFN_xrPassthroughLayerResumeFB resume;
static PFN_xrPassthroughLayerPauseFB pause_layer;
static PFN_xrPassthroughLayerSetStyleFB set_style;
static XrPassthroughFB passthrough = XR_NULL_HANDLE;
static XrPassthroughLayerFB layer = XR_NULL_HANDLE;
static bool started, running;
static int styled = -1;

bool tremu_passthrough_supported(void) {
    uint32_t count = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL)) || !count) return false;
    XrExtensionProperties *props = calloc(count, sizeof *props);
    if (!props) return false;
    for (uint32_t i = 0; i < count; i++) props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    bool found = false;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL, count, &count, props)))
        for (uint32_t i = 0; i < count && !found; i++) found = !strcmp(props[i].extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME);
    free(props);
    return found;
}

void tremu_passthrough_init(XrInstance instance, XrSession session) {
#define GET(name, fn) xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)&fn)
    GET("xrCreatePassthroughFB", create_passthrough);
    GET("xrCreatePassthroughLayerFB", create_layer);
    GET("xrPassthroughStartFB", start);
    GET("xrPassthroughLayerResumeFB", resume);
    GET("xrPassthroughLayerPauseFB", pause_layer);
    GET("xrPassthroughLayerSetStyleFB", set_style);
#undef GET
    if (!create_passthrough || !create_layer || !start || !resume || !pause_layer || !set_style) return;
    /* Created stopped: the cameras run only while a room background is shown. */
    XrPassthroughCreateInfoFB pc = {XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
    if (XR_FAILED(create_passthrough(session, &pc, &passthrough))) { passthrough = XR_NULL_HANDLE; return; }
    XrPassthroughLayerCreateInfoFB lc = {XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
    lc.passthrough = passthrough;
    lc.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    if (XR_FAILED(create_layer(session, &lc, &layer))) layer = XR_NULL_HANDLE;
    LOG("passthrough %s", layer ? "ready" : "unavailable");
}

const XrCompositionLayerBaseHeader *tremu_passthrough_layer(int background) {
    if (!layer) return NULL;
    if (background == TREMU_BACKGROUND_BLACK) {
        if (running && XR_SUCCEEDED(pause_layer(layer))) running = false;
        return NULL;
    }
    if (!started) started = XR_SUCCEEDED(start(passthrough));
    if (!running) running = started && XR_SUCCEEDED(resume(layer));
    if (!running) return NULL;
    if (styled != background) {
        /* The dimmed room keeps the game in front without the room drawing the eye. */
        XrPassthroughStyleFB style = {XR_TYPE_PASSTHROUGH_STYLE_FB};
        style.textureOpacityFactor = background == TREMU_BACKGROUND_DIMMED ? .35f : 1.f;
        style.edgeColor = (XrColor4f){0, 0, 0, 0};
        if (XR_SUCCEEDED(set_style(layer, &style))) styled = background;
    }
    static XrCompositionLayerPassthroughFB composition = {XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
    composition.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    composition.layerHandle = layer;
    return (const XrCompositionLayerBaseHeader *)&composition;
}
