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
void tremu_ext_set_bindings(const int bindings[EXT_ROLES]);
/* Returns 1 when the event belongs to a gun (and must not reach Android, e.g. B = Back). */
int32_t tremu_ext_input(const AInputEvent *event);
bool tremu_ext_pressed(ExtRole role);
/* The gun's joystick or hat, -1..1 (y up positive), for the carousel and the menu. */
void tremu_ext_stick(float *x, float *y);

/* Learning: the next press or axis push is assigned to the current role, then the next one is asked. */
void tremu_ext_learn_start(void);
void tremu_ext_learn_skip(void);
bool tremu_ext_learning(void);
/* Role being asked, or EXT_ROLES when learning finished (then tremu_ext_learned fills the codes). */
ExtRole tremu_ext_learn_role(void);
void tremu_ext_learned(int bindings[EXT_ROLES]);
const char *tremu_ext_role_name(ExtRole role);
/* Last control seen from any gun, in words ("bouton 96", "axe 15 +"); counter changes with it. */
unsigned tremu_ext_last_signal(char *out, size_t size);
