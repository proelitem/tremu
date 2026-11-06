#pragma once
#include <android/input.h>
#include <stdbool.h>
#include <stdint.h>

/* USB and Bluetooth gamepads as RetroPads, up to four, in the order they first speak: pad 1 is
 * player 1 together with the Touch controllers, pads 2 to 4 are players 2 to 4. Android already
 * talks to Xbox pads (360 wired, One, Series), DualShock 4 and DualSense, 8BitDo and the other
 * generic HID pads, arcade sticks and Rock Band instruments (seen as the pad they emulate):
 * this layer only reads what the system delivers, with Android's standard layout (A south,
 * B east, X west, Y north; sticks on X/Y and Z/RZ, or RX/RY; triggers on LTRIGGER/RTRIGGER or
 * BRAKE/GAS; hat as the d-pad). Everything runs on the android_main thread. Guns that show up as
 * a gamepad (Arkade...) are also pad 1: their gun roles stay in extgun.c. */
enum { TREMU_GAMEPADS = 4 };

/* Returns 1 when the event came from a gamepad or joystick (and was taken). */
int32_t tremu_gamepad_input(const AInputEvent *event);
/* Pad index (0..3) of an Android input device, -1 when it is not a gamepad we know. */
int tremu_gamepad_index(int32_t device_id);
int tremu_gamepad_count(void);
/* Pad `index`: RetroPad buttons (1 << RETRO_DEVICE_ID_JOYPAD_*) and sticks, -0x7fff..0x7fff
 * with y down, in whole steps of 256 as the network sends them. False when no such pad. */
bool tremu_gamepad_state(int index, uint16_t *buttons, int16_t *lx, int16_t *ly, int16_t *rx, int16_t *ry);
/* Any pad holds that RetroPad button. */
bool tremu_gamepad_any(unsigned retro_id);
/* Any pad asks for the menu: Select and Start together, or the guide/mode button. */
bool tremu_gamepad_menu(void);
/* Any pad's left stick or d-pad, -1..1 with y up positive, for the carousel and the menu. */
void tremu_gamepad_nav(float *x, float *y);
/* Forgets every button and axis held (on resume: a release may have gone to another app). */
void tremu_gamepad_release_all(void);
