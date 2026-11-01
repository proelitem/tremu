#include "voxel.h"
#include <EGL/egl.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <android/log.h>
#include <jni.h>
#include <math.h>
#include <openxr/openxr_platform.h>
#include <stdlib.h>
#include <string.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

enum { EYES = 2, MAX_W = WFG_VOXEL_MAX, MAX_H = WFG_VOXEL_MAX };

/* Depth of each layer, in screen pixels (front face, then thickness). Sprites sit on the
 * screen plane so the gun hits what it seems to touch; z grows towards the player.
 * Layers: backdrop, background, sprite behind the background, sprite, foreground (arcade HUD
 * and close scenery), then spares drawn like sprites. */
enum { LAYERS = 8 };
static const float layer_front[LAYERS] = {-24, -8, -12, 0, 6, 0, 0, 0};
static const float layer_depth[LAYERS] = {0, 3, 2, 3, 2, 3, 3, 3};

static XrSession xr_session;
static XrSwapchain chains[EYES];
static XrSwapchainImageOpenGLESKHR *images[EYES];
static uint32_t image_counts[EYES];
static int32_t eye_w, eye_h;
static GLuint depth_buffers[EYES], fbo, program, cube_vbo, vao, color_tex, layer_tex;
static GLint u_mvp, u_size, u_pixel, u_front, u_depth, u_colors, u_layers, u_continuous;
static bool continuous_depth;
/* Continuous depth: 0..255 maps from DEPTH_FAR to the screen plane (pixels); the shader
 * makes those blocks 8 pixels deep to close the steps between neighbours. */
static const float DEPTH_FAR = -28;
static unsigned frame_w, frame_h, tex_w, tex_h;
static bool srgb, ready, failed;  /* failed: not retried every frame */
static XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
static XrCompositionLayerProjectionView views[EYES];
static bool see_through;

void wfg_voxel_set_see_through(bool on) { see_through = on; }

static const char *vertex_src =
    "#version 300 es\n"
    "layout(location = 0) in vec4 corner;\n"  /* xyz in the unit cube, w = face shade */
    "uniform mat4 mvp;\n"
    "uniform ivec2 size;\n"
    "uniform vec2 pixel;\n"
    "uniform float front[8], depth[8];\n"
    "uniform vec2 continuous;\n"  /* x: 1 for a depth map, y: far depth in pixels */
    "uniform sampler2D colors, layers;\n"
    "out vec3 color;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_InstanceID % size.x, gl_InstanceID / size.x);\n"
    "    float value = texelFetch(layers, p, 0).r;\n"
    "    int layer = min(int(value * 255.0 + 0.5), 7);\n"
    "    float f = continuous.x > 0.5 ? continuous.y * (1.0 - value) : front[layer];\n"
    "    float d = continuous.x > 0.5 ? 8.0 : depth[layer];\n"
    "    vec3 pos = vec3((float(p.x) + corner.x - float(size.x) * 0.5) * pixel.x,\n"
    "                    (float(p.y) + corner.y - float(size.y) * 0.5) * pixel.y,\n"
    "                    (f - d + corner.z * d) * pixel.x);\n"
    "    color = texelFetch(colors, p, 0).rgb * corner.w;\n"
    "    gl_Position = mvp * vec4(pos, 1.0);\n"
    "}\n";

static const char *fragment_src =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec3 color;\n"
    "out vec4 frag;\n"
    "void main() { frag = vec4(color, 1.0); }\n";

/* Front, top, bottom, left and right faces of a unit cube (the back is never seen). */
static const float cube[] = {
    0, 0, 1, 1,   1, 0, 1, 1,   1, 1, 1, 1,   0, 0, 1, 1,   1, 1, 1, 1,   0, 1, 1, 1,
    0, 1, 0, .85f, 0, 1, 1, .85f, 1, 1, 1, .85f, 0, 1, 0, .85f, 1, 1, 1, .85f, 1, 1, 0, .85f,
    0, 0, 0, .55f, 1, 0, 0, .55f, 1, 0, 1, .55f, 0, 0, 0, .55f, 1, 0, 1, .55f, 0, 0, 1, .55f,
    0, 0, 0, .7f, 0, 0, 1, .7f, 0, 1, 1, .7f, 0, 0, 0, .7f, 0, 1, 1, .7f, 0, 1, 0, .7f,
    1, 0, 0, .7f, 1, 1, 0, .7f, 1, 1, 1, .7f, 1, 0, 0, .7f, 1, 1, 1, .7f, 1, 0, 1, .7f,
};

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOG("voxel shader: %s", log);
    }
    return s;
}

static bool setup(XrInstance instance, XrSystemId system, XrSession session);

bool wfg_voxel_init(XrInstance instance, XrSystemId system, XrSession session) {
    if (ready || failed) return ready;
    failed = !setup(instance, system, session);
    if (failed) LOG("voxel view unavailable");
    return ready;
}

static bool setup(XrInstance instance, XrSystemId system, XrSession session) {
    xr_session = session;
    XrViewConfigurationView config[EYES] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, EYES, &n,
                                                    config)) || n != EYES)
        return false;
    eye_w = (int32_t)config[0].recommendedImageRectWidth;
    eye_h = (int32_t)config[0].recommendedImageRectHeight;

    uint32_t count = 0;
    xrEnumerateSwapchainFormats(session, 0, &count, NULL);
    int64_t *formats = calloc(count, sizeof *formats);
    xrEnumerateSwapchainFormats(session, count, &count, formats);
    int64_t format = GL_RGBA8;
    for (uint32_t i = 0; i < count; i++)
        if (formats[i] == GL_SRGB8_ALPHA8) format = GL_SRGB8_ALPHA8;
    free(formats);
    srgb = format == GL_SRGB8_ALPHA8;
    for (int e = 0; e < EYES; e++) {
        XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format = format;
        ci.sampleCount = 1;
        ci.width = (uint32_t)eye_w;
        ci.height = (uint32_t)eye_h;
        ci.faceCount = ci.arraySize = ci.mipCount = 1;
        if (XR_FAILED(xrCreateSwapchain(session, &ci, &chains[e]))) return false;
        xrEnumerateSwapchainImages(chains[e], 0, &image_counts[e], NULL);
        images[e] = calloc(image_counts[e], sizeof *images[e]);
        for (uint32_t i = 0; i < image_counts[e]; i++) images[e][i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        xrEnumerateSwapchainImages(chains[e], image_counts[e], &image_counts[e], (XrSwapchainImageBaseHeader *)images[e]);
        glGenRenderbuffers(1, &depth_buffers[e]);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_buffers[e]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, eye_w, eye_h);
    }
    glGenFramebuffers(1, &fbo);

    program = glCreateProgram();
    glAttachShader(program, compile(GL_VERTEX_SHADER, vertex_src));
    glAttachShader(program, compile(GL_FRAGMENT_SHADER, fragment_src));
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { LOG("voxel program link failed"); return false; }
    u_mvp = glGetUniformLocation(program, "mvp");
    u_size = glGetUniformLocation(program, "size");
    u_pixel = glGetUniformLocation(program, "pixel");
    u_front = glGetUniformLocation(program, "front");
    u_depth = glGetUniformLocation(program, "depth");
    u_colors = glGetUniformLocation(program, "colors");
    u_layers = glGetUniformLocation(program, "layers");
    u_continuous = glGetUniformLocation(program, "continuous");

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &cube_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof cube, cube, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
    glBindVertexArray(0);

    GLuint tex[2];
    glGenTextures(2, tex);
    color_tex = tex[0];
    layer_tex = tex[1];
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    ready = true;
    LOG("voxel view ready: eyes %dx%d, %s", eye_w, eye_h, srgb ? "sRGB" : "linear");
    return true;
}

void wfg_voxel_upload(const uint32_t *pixels, const uint8_t *layers, unsigned w, unsigned h, bool continuous) {
    if (!ready || !w || !h || w > MAX_W || h > MAX_H) return;
    continuous_depth = continuous;
    if (w != tex_w || h != tex_h) {
        glBindTexture(GL_TEXTURE_2D, color_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, layer_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)w, (GLsizei)h, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
        tex_w = w;
        tex_h = h;
    }
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, layer_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h, GL_RED, GL_UNSIGNED_BYTE, layers);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    frame_w = w;
    frame_h = h;
}

/* Column-major 4x4 matrices, as GL expects. */
static void multiply(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = a[row] * b[c * 4] + a[4 + row] * b[c * 4 + 1] + a[8 + row] * b[c * 4 + 2] + a[12 + row] * b[c * 4 + 3];
    memcpy(out, r, sizeof r);
}

static void pose_matrix(XrQuaternionf q, XrVector3f t, float m[16]) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float r[16] = {1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y), 0,
                   2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x), 0,
                   2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y), 0,
                   t.x, t.y, t.z, 1};
    memcpy(m, r, sizeof r);
}

/* World -> eye: the inverse of the eye pose. */
static void view_matrix(XrPosef eye, float m[16]) {
    XrQuaternionf inv = {-eye.orientation.x, -eye.orientation.y, -eye.orientation.z, eye.orientation.w};
    float r[16];
    pose_matrix(inv, (XrVector3f){0, 0, 0}, r);
    XrVector3f p = eye.position;
    XrVector3f t = {-(r[0] * p.x + r[4] * p.y + r[8] * p.z), -(r[1] * p.x + r[5] * p.y + r[9] * p.z),
                    -(r[2] * p.x + r[6] * p.y + r[10] * p.z)};
    r[12] = t.x;
    r[13] = t.y;
    r[14] = t.z;
    memcpy(m, r, sizeof r);
}

static void projection_matrix(XrFovf fov, float m[16]) {
    const float n = .05f, f = 100.f;
    float l = tanf(fov.angleLeft), r = tanf(fov.angleRight), u = tanf(fov.angleUp), d = tanf(fov.angleDown);
    float p[16] = {2 / (r - l), 0, 0, 0,
                   0, 2 / (u - d), 0, 0,
                   (r + l) / (r - l), (u + d) / (u - d), -(f + n) / (f - n), -1,
                   0, 0, -2 * f * n / (f - n), 0};
    memcpy(m, p, sizeof p);
}

const XrCompositionLayerBaseHeader *wfg_voxel_render(XrSpace space, XrTime time, XrPosef screen, float width,
                                                     float height) {
    if (!ready || !frame_w) return NULL;
    XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = time;
    li.space = space;
    XrViewState vs = {XR_TYPE_VIEW_STATE};
    XrView eyes[EYES] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    uint32_t n = 0;
    if (XR_FAILED(xrLocateViews(xr_session, &li, &vs, EYES, &n, eyes)) || n != EYES) return NULL;
    XrViewStateFlags need = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if ((vs.viewStateFlags & need) != need) return NULL;

    float model[16];
    pose_matrix(screen.orientation, screen.position, model);
    glUseProgram(program);
    glUniform2i(u_size, (GLint)frame_w, (GLint)frame_h);
    glUniform2f(u_pixel, width / frame_w, height / frame_h);
    glUniform1fv(u_front, LAYERS, layer_front);
    glUniform1fv(u_depth, LAYERS, layer_depth);
    glUniform2f(u_continuous, continuous_depth ? 1.f : 0.f, DEPTH_FAR);
    glUniform1i(u_colors, 0);
    glUniform1i(u_layers, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, layer_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    bool ok = true;
    for (int e = 0; e < EYES && ok; e++) {
        uint32_t index;
        XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (XR_FAILED(xrAcquireSwapchainImage(chains[e], &ai, &index))) { ok = false; break; }
        XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage(chains[e], &wi);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[e][index].image, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffers[e]);
        glViewport(0, 0, eye_w, eye_h);
        glClearColor(0, 0, 0, see_through ? 0 : 1);
        glClearDepthf(1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        float proj[16], view[16], mvp[16];
        projection_matrix(eyes[e].fov, proj);
        view_matrix(eyes[e].pose, view);
        multiply(proj, view, mvp);
        multiply(mvp, model, mvp);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 30, (GLsizei)(frame_w * frame_h));
        /* The depth buffer is not needed after the frame. */
        const GLenum discard[] = {GL_DEPTH_ATTACHMENT};
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, discard);
        XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(chains[e], &ri);
        views[e] = (XrCompositionLayerProjectionView){XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        views[e].pose = eyes[e].pose;
        views[e].fov = eyes[e].fov;
        views[e].subImage.swapchain = chains[e];
        views[e].subImage.imageRect.extent = (XrExtent2Di){eye_w, eye_h};
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
    if (!ok) return NULL;
    layer.space = space;
    layer.layerFlags = see_through ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
    layer.viewCount = EYES;
    layer.views = views;
    return (const XrCompositionLayerBaseHeader *)&layer;
}

void wfg_voxel_destroy(void) {
    if (!ready) return;
    for (int e = 0; e < EYES; e++) {
        xrDestroySwapchain(chains[e]);
        free(images[e]);
        glDeleteRenderbuffers(1, &depth_buffers[e]);
    }
    glDeleteFramebuffers(1, &fbo);
    glDeleteProgram(program);
    glDeleteBuffers(1, &cube_vbo);
    glDeleteVertexArrays(1, &vao);
    GLuint tex[2] = {color_tex, layer_tex};
    glDeleteTextures(2, tex);
    ready = false;
    frame_w = tex_w = tex_h = 0;
}
