#include "depth.h"
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

enum { SIZE = 256, MAX_FRAME = 1024 * 1024 };

/* TensorFlow Lite C API, looked up at run time in the libraries of the official AARs. */
typedef struct TfLiteModel TfLiteModel;
typedef struct TfLiteInterpreterOptions TfLiteInterpreterOptions;
typedef struct TfLiteInterpreter TfLiteInterpreter;
typedef struct TfLiteTensor TfLiteTensor;
typedef struct TfLiteDelegate TfLiteDelegate;
/* Only the leading fields are set; the padding covers the fields newer versions add. */
typedef struct {
    int32_t is_precision_loss_allowed, inference_preference, priority1, priority2, priority3;
    int64_t experimental_flags;
    int32_t max_delegated_partitions;
    char rest[256];
} GpuOptions;

static TfLiteModel *(*model_from_file)(const char *);
static void (*model_delete)(TfLiteModel *);
static TfLiteInterpreterOptions *(*options_create)(void);
static void (*options_delete)(TfLiteInterpreterOptions *);
static void (*options_threads)(TfLiteInterpreterOptions *, int32_t);
static void (*options_delegate)(TfLiteInterpreterOptions *, TfLiteDelegate *);
static TfLiteInterpreter *(*interpreter_create)(const TfLiteModel *, const TfLiteInterpreterOptions *);
static void (*interpreter_delete)(TfLiteInterpreter *);
static int (*allocate)(TfLiteInterpreter *);
static TfLiteTensor *(*input_tensor)(const TfLiteInterpreter *, int32_t);
static const TfLiteTensor *(*output_tensor)(const TfLiteInterpreter *, int32_t);
static int (*copy_in)(TfLiteTensor *, const void *, size_t);
static int (*copy_out)(const TfLiteTensor *, void *, size_t);
static int (*invoke)(TfLiteInterpreter *);
static GpuOptions (*gpu_defaults)(void);
static TfLiteDelegate *(*gpu_create)(const GpuOptions *);
static void (*gpu_delete)(TfLiteDelegate *);

static pthread_t worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static atomic_bool running, busy;
static char model_file[512];
/* Newest frame waiting for the network (bottom row first). */
static uint32_t *pending;
static unsigned pending_w, pending_h;
static bool has_pending;
/* Latest depth, 256x256 top row first, 0 far .. 255 near; smoothed over time. */
static float depth_now[SIZE * SIZE];
static uint8_t depth_out[SIZE * SIZE];
static bool has_depth;
static atomic_int last_us;

#define SYM(lib, name, var) \
    if (!(*(void **)&var = dlsym(lib, name))) { LOG("depth: missing %s", name); return false; }

static bool load_api(void) {
    void *tf = dlopen("libtensorflowlite_jni.so", RTLD_NOW | RTLD_LOCAL);
    void *gpu = dlopen("libtensorflowlite_gpu_jni.so", RTLD_NOW | RTLD_LOCAL);
    if (!tf) { LOG("depth: %s", dlerror()); return false; }
    SYM(tf, "TfLiteModelCreateFromFile", model_from_file) SYM(tf, "TfLiteModelDelete", model_delete)
    SYM(tf, "TfLiteInterpreterOptionsCreate", options_create) SYM(tf, "TfLiteInterpreterOptionsDelete", options_delete)
    SYM(tf, "TfLiteInterpreterOptionsSetNumThreads", options_threads)
    SYM(tf, "TfLiteInterpreterOptionsAddDelegate", options_delegate)
    SYM(tf, "TfLiteInterpreterCreate", interpreter_create) SYM(tf, "TfLiteInterpreterDelete", interpreter_delete)
    SYM(tf, "TfLiteInterpreterAllocateTensors", allocate) SYM(tf, "TfLiteInterpreterGetInputTensor", input_tensor)
    SYM(tf, "TfLiteInterpreterGetOutputTensor", output_tensor) SYM(tf, "TfLiteTensorCopyFromBuffer", copy_in)
    SYM(tf, "TfLiteTensorCopyToBuffer", copy_out) SYM(tf, "TfLiteInterpreterInvoke", invoke)
    if (gpu) {
        *(void **)&gpu_defaults = dlsym(gpu, "TfLiteGpuDelegateOptionsV2Default");
        *(void **)&gpu_create = dlsym(gpu, "TfLiteGpuDelegateV2Create");
        *(void **)&gpu_delete = dlsym(gpu, "TfLiteGpuDelegateV2Delete");
    } else {
        LOG("depth: no GPU delegate (%s), CPU only", dlerror());
    }
    return true;
}

static uint64_t now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

/* MiDaS gives relative inverse depth (bigger = closer): scaled by a slowly moving range so the
 * relief does not pump from frame to frame, and blended with the previous result. */
static void store_depth(const float *raw) {
    static float lo, hi;
    static bool started;
    float mn = raw[0], mx = raw[0];
    for (int i = 1; i < SIZE * SIZE; i++) {
        if (raw[i] < mn) mn = raw[i];
        if (raw[i] > mx) mx = raw[i];
    }
    if (!started) { lo = mn; hi = mx; started = true; }
    lo += (mn - lo) * .2f;
    hi += (mx - hi) * .2f;
    float scale = hi > lo ? 1.f / (hi - lo) : 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < SIZE * SIZE; i++) {
        float v = (raw[i] - lo) * scale;
        v = v < 0 ? 0 : v > 1 ? 1 : v;
        depth_now[i] = has_depth ? depth_now[i] * .5f + v * .5f : v;
        depth_out[i] = (uint8_t)(depth_now[i] * 255.f + .5f);
    }
    has_depth = true;
    pthread_mutex_unlock(&lock);
}

static void *run(void *arg) {
    (void)arg;
    TfLiteModel *model = model_from_file(model_file);
    if (!model) { LOG("depth: cannot load %s", model_file); atomic_store(&running, false); return NULL; }
    TfLiteInterpreterOptions *opts = options_create();
    options_threads(opts, 2);
    TfLiteDelegate *delegate = NULL;
    if (gpu_defaults && gpu_create) {
        GpuOptions g = gpu_defaults();
        g.is_precision_loss_allowed = 1;  /* fp16 */
        g.inference_preference = 1;       /* sustained speed */
        delegate = gpu_create(&g);
        if (delegate) options_delegate(opts, delegate);
    }
    TfLiteInterpreter *net = interpreter_create(model, opts);
    if (!net || allocate(net) != 0) {
        LOG("depth: interpreter failed (GPU %s)", delegate ? "on" : "off");
        atomic_store(&running, false);
        return NULL;
    }
    LOG("depth: MiDaS ready (%s)", delegate ? "GPU" : "CPU");
    static float in[SIZE * SIZE * 3], out[SIZE * SIZE];
    uint32_t *frame = malloc(MAX_FRAME * sizeof *frame);
    while (atomic_load(&running) && frame) {
        pthread_mutex_lock(&lock);
        while (atomic_load(&running) && !has_pending) pthread_cond_wait(&wake, &lock);
        unsigned w = pending_w, h = pending_h;
        if (has_pending) memcpy(frame, pending, w * h * sizeof *frame);
        has_pending = false;
        atomic_store(&busy, true);
        pthread_mutex_unlock(&lock);
        if (!atomic_load(&running)) break;
        /* Nearest-neighbour resize to 256x256, top row first, RGB 0..1. */
        for (int y = 0; y < SIZE; y++) {
            const uint32_t *row = frame + (h - 1 - (unsigned)y * h / SIZE) * w;
            for (int x = 0; x < SIZE; x++) {
                uint32_t p = row[(unsigned)x * w / SIZE];
                float *o = in + (y * SIZE + x) * 3;
                o[0] = (p & 0xFF) / 255.f;
                o[1] = ((p >> 8) & 0xFF) / 255.f;
                o[2] = ((p >> 16) & 0xFF) / 255.f;
            }
        }
        uint64_t t0 = now_us();
        bool ok = copy_in(input_tensor(net, 0), in, sizeof in) == 0 && invoke(net) == 0 &&
                  copy_out(output_tensor(net, 0), out, sizeof out) == 0;
        atomic_store(&last_us, (int)(now_us() - t0));
        if (ok) store_depth(out);
        atomic_store(&busy, false);
    }
    free(frame);
    interpreter_delete(net);
    options_delete(opts);
    if (delegate && gpu_delete) gpu_delete(delegate);
    model_delete(model);
    return NULL;
}

bool tremu_depth_start(const char *model_path) {
    if (atomic_load(&running)) return true;
    if (!model_from_file && !load_api()) return false;
    snprintf(model_file, sizeof model_file, "%s", model_path);
    if (!pending) pending = malloc(MAX_FRAME * sizeof *pending);
    if (!pending) return false;
    has_pending = has_depth = false;
    atomic_store(&running, true);
    if (pthread_create(&worker, NULL, run, NULL)) { atomic_store(&running, false); return false; }
    return true;
}

void tremu_depth_stop(void) {
    if (!atomic_load(&running)) return;
    pthread_mutex_lock(&lock);
    atomic_store(&running, false);
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    pthread_join(worker, NULL);
}

void tremu_depth_submit(const uint32_t *pixels, unsigned w, unsigned h) {
    if (!atomic_load(&running) || atomic_load(&busy) || !w || !h || w * h > MAX_FRAME) return;
    pthread_mutex_lock(&lock);
    memcpy(pending, pixels, w * h * sizeof *pixels);
    pending_w = w;
    pending_h = h;
    has_pending = true;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
}

bool tremu_depth_get(uint8_t *out, unsigned w, unsigned h) {
    pthread_mutex_lock(&lock);
    bool ok = has_depth;
    for (unsigned y = 0; ok && y < h; y++) {
        const uint8_t *row = depth_out + (SIZE - 1 - y * SIZE / h) * SIZE;  /* bottom row first */
        for (unsigned x = 0; x < w; x++) out[y * w + x] = row[x * SIZE / w];
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

float tremu_depth_ms(void) { return atomic_load(&last_us) / 1000.f; }
