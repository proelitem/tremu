#include "gun.h"
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

/* Longest gap between two full-auto shot frames that still counts as one held trigger. */
static const uint64_t HOLD_NS = 150000000ull;
static const uint64_t BOLT_PRESS_NS = 150000000ull;

static atomic_uint_least64_t last_shot_ns, bolt_until_ns;
static atomic_int magazine = -1;
static atomic_uint shots, name_generation;
static pthread_mutex_t name_lock = PTHREAD_MUTEX_INITIALIZER;
static char gun_name[32];

static uint64_t now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

JNIEXPORT void JNICALL Java_com_tremu_app_WonderFitterGun_nativeShot(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    atomic_store(&last_shot_ns, now());
    atomic_fetch_add(&shots, 1);
}

JNIEXPORT void JNICALL Java_com_tremu_app_WonderFitterGun_nativeName(JNIEnv *env, jclass cls, jstring name) {
    (void)cls;
    const char *s = (*env)->GetStringUTFChars(env, name, NULL);
    if (!s) return;
    pthread_mutex_lock(&name_lock);
    snprintf(gun_name, sizeof gun_name, "%s", s);
    pthread_mutex_unlock(&name_lock);
    (*env)->ReleaseStringUTFChars(env, name, s);
    atomic_fetch_add(&name_generation, 1);
}

JNIEXPORT void JNICALL Java_com_tremu_app_WonderFitterGun_nativeMagazine(JNIEnv *env, jclass cls, jint state) {
    (void)env; (void)cls;
    atomic_store(&magazine, state);
}

JNIEXPORT void JNICALL Java_com_tremu_app_WonderFitterGun_nativeBolt(JNIEnv *env, jclass cls, jint state) {
    (void)env; (void)cls;
    if (state) atomic_store(&bolt_until_ns, now() + BOLT_PRESS_NS);
}

bool tremu_gun_connected(void) { return atomic_load(&magazine) >= 0; }

bool tremu_gun_trigger_held(uint64_t now_ns) {
    uint64_t last = atomic_load(&last_shot_ns);
    return last && now_ns - last < HOLD_NS;
}

bool tremu_gun_bolt_pressed(uint64_t now_ns) { return now_ns < atomic_load(&bolt_until_ns); }

int tremu_gun_magazine(void) { return atomic_load(&magazine); }

unsigned tremu_gun_shots(void) { return atomic_load(&shots); }

unsigned tremu_gun_name(char *out, size_t size) {
    pthread_mutex_lock(&name_lock);
    snprintf(out, size, "%s", gun_name);
    pthread_mutex_unlock(&name_lock);
    return atomic_load(&name_generation);
}
