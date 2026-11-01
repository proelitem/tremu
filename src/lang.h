#pragma once
#include <jni.h>

/* Languages of the headset texts and of the "add games" page, in tools/lang.py's order. */
typedef enum { LANG_FR, LANG_EN, LANG_DE, LANG_ES, LANG_ZH, LANG_RU, LANG_JA, LANG_COUNT } WfgLang;

/* Picks the language (also for the Java side); -1 follows the headset's system language.
 * vm and activity are kept from the first call; later calls may pass NULL. */
void wfg_lang_set(JavaVM *vm, jobject activity, int lang);
WfgLang wfg_lang(void);
/* The language's own name ("Deutsch"). */
const char *wfg_lang_name(WfgLang lang);
/* The text in the current language: french is the key, as written in the sources (tools/lang.py
 * holds the translations). Unknown texts come back unchanged. */
const char *T(const char *french) __attribute__((format_arg(1)));
/* Cuts an incomplete UTF-8 character left at the end of s by a truncating snprintf (Java
 * rejects such strings). */
void wfg_utf8_trim(char *s);
