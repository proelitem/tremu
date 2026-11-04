#include "gamepad.h"
#include "libretro.h"
#include <android/keycodes.h>
#include <android/log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu-pad", __VA_ARGS__)

/* The axes read, by position in Pad.axis. */
enum { AX_X, AX_Y, AX_Z, AX_RZ, AX_RX, AX_RY, AX_HAT_X, AX_HAT_Y, AX_LTRIGGER, AX_RTRIGGER, AX_GAS, AX_BRAKE, AX_COUNT };
static const int32_t axis_ids[AX_COUNT] = {
    AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y, AMOTION_EVENT_AXIS_Z, AMOTION_EVENT_AXIS_RZ,
    AMOTION_EVENT_AXIS_RX, AMOTION_EVENT_AXIS_RY, AMOTION_EVENT_AXIS_HAT_X, AMOTION_EVENT_AXIS_HAT_Y,
    AMOTION_EVENT_AXIS_LTRIGGER, AMOTION_EVENT_AXIS_RTRIGGER, AMOTION_EVENT_AXIS_GAS, AMOTION_EVENT_AXIS_BRAKE,
};

typedef struct Pad {
    int32_t device;        /* Android input device id; 0 = free slot */
    uint16_t keys;         /* RetroPad buttons held through key events */
    bool mode;             /* guide / PS / home button */
    float axis[AX_COUNT];
    /* An axis counts only once seen at rest: a pad reporting a trigger at 1.0 from its first
     * event would otherwise hold that button for good (see extgun.c for the same rule). */
    bool rested[AX_COUNT];
    /* Right stick: Android's standard is Z/RZ; a few layouts use RX/RY. Whichever moved first. */
    bool right_on_rx;
    bool right_known;
} Pad;

static Pad pads[WFG_GAMEPADS];

static bool is_gamepad_source(int32_t source) {
    return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
           (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK ||
           (source & AINPUT_SOURCE_DPAD) == AINPUT_SOURCE_DPAD;
}

int wfg_gamepad_index(int32_t device_id) {
    if (!device_id) return -1;
    for (int i = 0; i < WFG_GAMEPADS; i++)
        if (pads[i].device == device_id) return i;
    return -1;
}

int wfg_gamepad_count(void) {
    int n = 0;
    for (int i = 0; i < WFG_GAMEPADS; i++) n += pads[i].device != 0;
    return n;
}

/* The slot of that device, opening one on its first event; NULL when four pads already speak. */
static Pad *pad_of(int32_t device_id) {
    int i = wfg_gamepad_index(device_id);
    if (i >= 0) return &pads[i];
    for (i = 0; i < WFG_GAMEPADS; i++)
        if (!pads[i].device) {
            pads[i] = (Pad){.device = device_id};
            LOG("device %d is pad %d (player %d)", device_id, i + 1, i + 1);
            return &pads[i];
        }
    return NULL;
}

/* Android's standard layout to the RetroPad: A is south (RetroPad B), B east (A), X west (Y),
 * Y north (X). -1 for a key that is not a pad button. */
static int retro_of_key(int32_t code) {
    switch (code) {
    case AKEYCODE_BUTTON_A: return RETRO_DEVICE_ID_JOYPAD_B;
    case AKEYCODE_BUTTON_B: return RETRO_DEVICE_ID_JOYPAD_A;
    case AKEYCODE_BUTTON_X: return RETRO_DEVICE_ID_JOYPAD_Y;
    case AKEYCODE_BUTTON_Y: return RETRO_DEVICE_ID_JOYPAD_X;
    case AKEYCODE_BUTTON_L1: return RETRO_DEVICE_ID_JOYPAD_L;
    case AKEYCODE_BUTTON_R1: return RETRO_DEVICE_ID_JOYPAD_R;
    case AKEYCODE_BUTTON_L2: return RETRO_DEVICE_ID_JOYPAD_L2;
    case AKEYCODE_BUTTON_R2: return RETRO_DEVICE_ID_JOYPAD_R2;
    case AKEYCODE_BUTTON_THUMBL: return RETRO_DEVICE_ID_JOYPAD_L3;
    case AKEYCODE_BUTTON_THUMBR: return RETRO_DEVICE_ID_JOYPAD_R3;
    case AKEYCODE_BUTTON_START: return RETRO_DEVICE_ID_JOYPAD_START;
    case AKEYCODE_BUTTON_SELECT: case AKEYCODE_BACK: return RETRO_DEVICE_ID_JOYPAD_SELECT;
    case AKEYCODE_DPAD_UP: return RETRO_DEVICE_ID_JOYPAD_UP;
    case AKEYCODE_DPAD_DOWN: return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case AKEYCODE_DPAD_LEFT: return RETRO_DEVICE_ID_JOYPAD_LEFT;
    case AKEYCODE_DPAD_RIGHT: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    /* Six-button pads through some adapters: C and Z as the shoulders. */
    case AKEYCODE_BUTTON_C: return RETRO_DEVICE_ID_JOYPAD_R;
    case AKEYCODE_BUTTON_Z: return RETRO_DEVICE_ID_JOYPAD_L;
    default: return -1;
    }
}

int32_t wfg_gamepad_input(const AInputEvent *event) {
    int32_t source = AInputEvent_getSource(event);
    if (!is_gamepad_source(source)) return 0;
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(event), action = AKeyEvent_getAction(event);
        /* The headset's own keys stay with the system. */
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_POWER) return 0;
        Pad *pad = pad_of(AInputEvent_getDeviceId(event));
        if (!pad) return 1;
        bool down = action == AKEY_EVENT_ACTION_DOWN;
        if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) return 1;
        if (code == AKEYCODE_BUTTON_MODE) {
            pad->mode = down;
            return 1;
        }
        int id = retro_of_key(code);
        if (id < 0) return 1;  /* taken anyway: a pad's key is never the system's Back */
        if (down) pad->keys |= (uint16_t)(1u << id);
        else pad->keys &= (uint16_t)~(1u << id);
        return 1;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;
    if ((source & AINPUT_SOURCE_JOYSTICK) != AINPUT_SOURCE_JOYSTICK && (source & AINPUT_SOURCE_GAMEPAD) != AINPUT_SOURCE_GAMEPAD) return 0;
    Pad *pad = pad_of(AInputEvent_getDeviceId(event));
    if (!pad) return 1;
    for (int a = 0; a < AX_COUNT; a++) {
        float v = AMotionEvent_getAxisValue(event, axis_ids[a], 0);
        if (fabsf(v) < .5f) pad->rested[a] = true;
        pad->axis[a] = v;
    }
    if (!pad->right_known) {
        if (fabsf(pad->axis[AX_Z]) > .5f || fabsf(pad->axis[AX_RZ]) > .5f) pad->right_known = true;
        else if (fabsf(pad->axis[AX_RX]) > .5f || fabsf(pad->axis[AX_RY]) > .5f) pad->right_known = pad->right_on_rx = true;
    }
    return 1;
}

static float axis(const Pad *p, int a) { return p->rested[a] ? p->axis[a] : 0.f; }

static int16_t stick(float v) {
    int s = (int)lroundf(v * 0x7fff);
    if (s > 0x7fff) s = 0x7fff;
    if (s < -0x7fff) s = -0x7fff;
    return (int16_t)((s >> 8) * 256);
}

static uint16_t buttons_of(const Pad *p) {
    uint16_t b = p->keys;
    float hx = axis(p, AX_HAT_X), hy = axis(p, AX_HAT_Y);
    if (hx < -.5f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (hx > .5f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (hy < -.5f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (hy > .5f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (axis(p, AX_LTRIGGER) > .5f || axis(p, AX_BRAKE) > .5f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_L2;
    if (axis(p, AX_RTRIGGER) > .5f || axis(p, AX_GAS) > .5f) b |= 1u << RETRO_DEVICE_ID_JOYPAD_R2;
    return b;
}

bool wfg_gamepad_state(int index, uint16_t *buttons, int16_t *lx, int16_t *ly, int16_t *rx, int16_t *ry) {
    if (index < 0 || index >= WFG_GAMEPADS || !pads[index].device) return false;
    const Pad *p = &pads[index];
    *buttons = buttons_of(p);
    *lx = stick(axis(p, AX_X));
    *ly = stick(axis(p, AX_Y));
    *rx = stick(axis(p, p->right_on_rx ? AX_RX : AX_Z));
    *ry = stick(axis(p, p->right_on_rx ? AX_RY : AX_RZ));
    return true;
}

bool wfg_gamepad_any(unsigned retro_id) {
    for (int i = 0; i < WFG_GAMEPADS; i++)
        if (pads[i].device && (buttons_of(&pads[i]) >> retro_id & 1)) return true;
    return false;
}

bool wfg_gamepad_menu(void) {
    const uint16_t both = 1u << RETRO_DEVICE_ID_JOYPAD_SELECT | 1u << RETRO_DEVICE_ID_JOYPAD_START;
    for (int i = 0; i < WFG_GAMEPADS; i++) {
        if (!pads[i].device) continue;
        if (pads[i].mode) return true;
        if ((buttons_of(&pads[i]) & both) == both) return true;
    }
    return false;
}

void wfg_gamepad_nav(float *x, float *y) {
    *x = *y = 0;
    for (int i = 0; i < WFG_GAMEPADS; i++) {
        const Pad *p = &pads[i];
        if (!p->device) continue;
        uint16_t b = buttons_of(p);
        float px = axis(p, AX_X), py = -axis(p, AX_Y);  /* Android: down is positive */
        if (b >> RETRO_DEVICE_ID_JOYPAD_LEFT & 1) px = -1;
        if (b >> RETRO_DEVICE_ID_JOYPAD_RIGHT & 1) px = 1;
        if (b >> RETRO_DEVICE_ID_JOYPAD_UP & 1) py = 1;
        if (b >> RETRO_DEVICE_ID_JOYPAD_DOWN & 1) py = -1;
        if (fabsf(px) > fabsf(*x)) *x = px;
        if (fabsf(py) > fabsf(*y)) *y = py;
    }
}

void wfg_gamepad_release_all(void) {
    for (int i = 0; i < WFG_GAMEPADS; i++) {
        pads[i].keys = 0;
        pads[i].mode = false;
        memset(pads[i].axis, 0, sizeof pads[i].axis);
    }
}
