#pragma once
#include <android/input.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Any other gun the Quest sees as a Bluetooth/USB gamepad, mouse or keyboard (Arkade, GunCon,
 * Gun4IR, Samco, OpenFIRE...). In VR the Touch controller mounted on the gun aims, so only its
 * buttons, joystick and slides matter. Defaults cover the usual layouts; the menu can learn a
 * gun's own controls. Everything here runs on the android_main thread (input events, XR loop). */

typedef enum { EXT_TRIGGER, EXT_GRENADE, EXT_COIN, EXT_START, EXT_ACTION, EXT_ROLES } ExtRole;

/* Control codes kept in the options: a key code, 1000 + mouse button index, 2000 + axis
 * (pushed positive) or 3000 + axis (pushed negative). 0 means "defaults" for that role. */
void wfg_ext_set_bindings(const int bindings[EXT_ROLES]);
/* Returns 1 when the event belongs to a gun (and must not reach Android, e.g. B = Back). */
int32_t wfg_ext_input(const AInputEvent *event);
bool wfg_ext_pressed(ExtRole role);
/* Forgets every key, button and axis held (on resume: a release may have gone elsewhere). */
void wfg_ext_release_all(void);
/* The gun's joystick or hat, -1..1 (y up positive), for the carousel and the menu. */
void wfg_ext_stick(float *x, float *y);

/* The external pointer: a Wii Remote through a DolphinBar in its mouse modes (the IR pointer,
 * the bar set under the virtual screen), a Gun4IR/Samco/OpenFIRE in mouse mode, any mouse. Its
 * position over the app window, 0..1 from the top-left (outside that range: off the window), and
 * whether it has moved in the last three seconds. The "external pointer aims" option makes it
 * player 1's aim in place of the Touch controller's ray. The window size comes from the glue. */
void wfg_ext_set_window(int width, int height);
bool wfg_ext_pointer(float *u, float *v, uint64_t now_ns);

/* Learning: the next press or axis push is assigned to the current role, then the next one is asked. */
void wfg_ext_learn_start(void);
void wfg_ext_learn_skip(void);
bool wfg_ext_learning(void);
/* Role being asked, or EXT_ROLES when learning finished (then wfg_ext_learned fills the codes). */
ExtRole wfg_ext_learn_role(void);
void wfg_ext_learned(int bindings[EXT_ROLES]);
const char *wfg_ext_role_name(ExtRole role);
/* Last control seen from any gun, in words ("bouton 96", "axe 15 +"); counter changes with it. */
unsigned wfg_ext_last_signal(char *out, size_t size);
