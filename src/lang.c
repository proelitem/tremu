#include "lang.h"
#include <android/log.h>
#include <stdatomic.h>
#include <string.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

#include "lang_table.inc"

static atomic_int current = LANG_FR;

/* The headset's language, from Java's Locale (index into tools/lang.py's codes, English if none). */
static int system_language(JNIEnv *env, jclass cls) {
    jmethodID m = (*env)->GetStaticMethodID(env, cls, "systemLanguage", "()I");
    int lang = m ? (*env)->CallStaticIntMethod(env, cls, m) : LANG_EN;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); lang = LANG_EN; }
    return lang;
}

void tremu_lang_set(JavaVM *vm, jobject activity, int lang) {
    static JavaVM *saved_vm;
    static jobject saved_activity;
    if (vm) { saved_vm = vm; saved_activity = activity; }
    vm = saved_vm;
    activity = saved_activity;
    JNIEnv *env = NULL;
    if (vm && (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK && (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
        env = NULL;
    jclass cls = env ? (*env)->GetObjectClass(env, activity) : NULL;
    if (lang < 0 || lang >= LANG_COUNT) lang = cls ? system_language(env, cls) : LANG_EN;
    if (lang < 0 || lang >= LANG_COUNT) lang = LANG_EN;
    atomic_store(&current, lang);
    if (cls) {
        jmethodID m = (*env)->GetStaticMethodID(env, cls, "setLanguage", "(I)V");
        if (m) (*env)->CallStaticVoidMethod(env, cls, m, lang);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
    }
    LOG("language %s", LANGUAGE_NAMES[lang]);
}

TremuLang tremu_lang(void) { return (TremuLang)atomic_load(&current); }

const char *tremu_lang_name(TremuLang lang) { return lang < LANG_COUNT ? LANGUAGE_NAMES[lang] : ""; }

const char *T(const char *french) {
    int lang = atomic_load(&current);
    if (lang == LANG_FR) return french;
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
        if (!strcmp(TABLE[i][0], french)) return TABLE[i][lang];
    return french;
}

void tremu_utf8_trim(char *s) {
    size_t n = strlen(s), start = n;
    while (start > 0 && n - start < 4 && ((unsigned char)s[start - 1] & 0xC0) == 0x80) start--;
    if (!start) return;
    unsigned char lead = (unsigned char)s[start - 1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (n - (start - 1) < need) s[start - 1] = 0;
}
