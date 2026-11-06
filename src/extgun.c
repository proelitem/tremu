#include "lang.h"
#include "extgun.h"
#include <android/keycodes.h>
#include <android/log.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Tremu", __VA_ARGS__)

enum { MOUSE_BASE = 1000, AXIS_POS = 2000, AXIS_NEG = 3000, MAX_KEYS = 400, MAX_AXES = 48, MAX_DEFAULTS = 8 };

static bool keys[MAX_KEYS];
static int32_t mouse_buttons;
static float axes[MAX_AXES];
/* An axis counts as pushed only once it has been seen at rest: a gamepad-type gun that reports
 * a trigger or gas axis at 1.0 from its first event (some do, and the Touch mount can hold one)
 * would otherwise be "firing" from the moment it connects, and with it nothing else could
 * confirm in the carousel or stop shooting in a game. */
static bool axis_rested[MAX_AXES];
static int bindings[EXT_ROLES];
static bool learning;
static int learn_role;
static int learned[EXT_ROLES];
static int last_code;
static unsigned last_count;
static int window_w, window_h;
static float pointer_x, pointer_y;
static uint64_t pointer_ns;

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

void tremu_ext_set_window(int width, int height) {
    window_w = width;
    window_h = height;
}

bool tremu_ext_pointer(float *u, float *v, uint64_t now) {
    if (!pointer_ns || now - pointer_ns > 3000000000ull || window_w <= 0 || window_h <= 0) return false;
    *u = pointer_x / (float)window_w;
    *v = pointer_y / (float)window_h;
    return true;
}

/* Where a mouse, touch or pen event puts the pointer; Android keeps the position for a relative
 * mouse too, so this works as a cursor for any of them. */
static void track_pointer(const AInputEvent *event) {
    pointer_x = AMotionEvent_getX(event, 0);
    pointer_y = AMotionEvent_getY(event, 0);
    pointer_ns = now_ns();
}

/* Usual layouts: mouse-mode IR guns, gamepads (Arkade, GunCon adapters) and the MAME keys
 * (1 start, 5 coin) that Gun4IR/Samco/OpenFIRE send in keyboard mode. */
static const int defaults[EXT_ROLES][MAX_DEFAULTS] = {
    [EXT_TRIGGER] = {MOUSE_BASE + 0, AKEYCODE_BUTTON_R2, AKEYCODE_BUTTON_R1, AKEYCODE_BUTTON_A,
                     AXIS_POS + AMOTION_EVENT_AXIS_RTRIGGER, AXIS_POS + AMOTION_EVENT_AXIS_GAS},
    [EXT_GRENADE] = {MOUSE_BASE + 1, AKEYCODE_BUTTON_B, AKEYCODE_BUTTON_L2, AKEYCODE_BUTTON_L1,
                     AXIS_POS + AMOTION_EVENT_AXIS_LTRIGGER, AXIS_POS + AMOTION_EVENT_AXIS_BRAKE},
    [EXT_COIN] = {AKEYCODE_BUTTON_SELECT, AKEYCODE_5, AKEYCODE_BUTTON_X},
    [EXT_START] = {AKEYCODE_BUTTON_START, AKEYCODE_1, AKEYCODE_BUTTON_Y},
    [EXT_ACTION] = {MOUSE_BASE + 2, AKEYCODE_BUTTON_THUMBR, AKEYCODE_BUTTON_THUMBL, AKEYCODE_SPACE},
};

/* Axes read from gamepads: sticks, hat (d-pad), analog triggers, plus Z/RX/RY/RZ for slides. */
static const int32_t watched[] = {
    AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y, AMOTION_EVENT_AXIS_Z, AMOTION_EVENT_AXIS_RX,
    AMOTION_EVENT_AXIS_RY, AMOTION_EVENT_AXIS_RZ, AMOTION_EVENT_AXIS_HAT_X, AMOTION_EVENT_AXIS_HAT_Y,
    AMOTION_EVENT_AXIS_LTRIGGER, AMOTION_EVENT_AXIS_RTRIGGER, AMOTION_EVENT_AXIS_GAS, AMOTION_EVENT_AXIS_BRAKE,
};

static bool code_down(int code) {
    if (code > 0 && code < MAX_KEYS) return keys[code];
    if (code >= MOUSE_BASE && code < MOUSE_BASE + 31) return mouse_buttons & (1 << (code - MOUSE_BASE));
    if (code >= AXIS_POS && code < AXIS_POS + MAX_AXES) return axis_rested[code - AXIS_POS] && axes[code - AXIS_POS] > .5f;
    if (code >= AXIS_NEG && code < AXIS_NEG + MAX_AXES) return axis_rested[code - AXIS_NEG] && axes[code - AXIS_NEG] < -.5f;
    return false;
}

void tremu_ext_release_all(void) {
    memset(keys, 0, sizeof keys);
    memset(axes, 0, sizeof axes);
    mouse_buttons = 0;
}

void tremu_ext_set_bindings(const int b[EXT_ROLES]) { memcpy(bindings, b, sizeof bindings); }

static bool bound_elsewhere(int code) {
    for (int r = 0; r < EXT_ROLES; r++)
        if (bindings[r] == code) return true;
    return false;
}

bool tremu_ext_pressed(ExtRole role) {
    if (learning) return false;
    if (bindings[role]) return code_down(bindings[role]);
    for (int i = 0; i < MAX_DEFAULTS && defaults[role][i]; i++)
        if (!bound_elsewhere(defaults[role][i]) && code_down(defaults[role][i])) return true;
    return false;
}

void tremu_ext_stick(float *x, float *y) {
    *x = *y = 0;
    if (learning) return;
    /* A direction learned for a role is a control, not navigation. */
    float sx = axes[AMOTION_EVENT_AXIS_X], sy = axes[AMOTION_EVENT_AXIS_Y];
    float hx = axes[AMOTION_EVENT_AXIS_HAT_X], hy = axes[AMOTION_EVENT_AXIS_HAT_Y];
    if (bound_elsewhere(AXIS_POS + AMOTION_EVENT_AXIS_X) || bound_elsewhere(AXIS_NEG + AMOTION_EVENT_AXIS_X)) sx = 0;
    if (bound_elsewhere(AXIS_POS + AMOTION_EVENT_AXIS_Y) || bound_elsewhere(AXIS_NEG + AMOTION_EVENT_AXIS_Y)) sy = 0;
    if (bound_elsewhere(AXIS_POS + AMOTION_EVENT_AXIS_HAT_X) || bound_elsewhere(AXIS_NEG + AMOTION_EVENT_AXIS_HAT_X)) hx = 0;
    if (bound_elsewhere(AXIS_POS + AMOTION_EVENT_AXIS_HAT_Y) || bound_elsewhere(AXIS_NEG + AMOTION_EVENT_AXIS_HAT_Y)) hy = 0;
    *x = fabsf(hx) > fabsf(sx) ? hx : sx;
    *y = -(fabsf(hy) > fabsf(sy) ? hy : sy);  /* Android: down is positive */
}

/* A new press: remembered for the diagnostic line, and assigned while learning. */
static void press(int code) {
    last_code = code;
    last_count++;
    if (!learning) return;
    for (int r = 0; r < learn_role; r++)
        if (learned[r] == code) return;  /* the same control again (e.g. trigger still held) */
    learned[learn_role] = code;
    LOG("gun control %d learned as %s", code, tremu_ext_role_name((ExtRole)learn_role));
    if (++learn_role == EXT_ROLES) learning = false;
}

int32_t tremu_ext_input(const AInputEvent *event) {
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(event), action = AKeyEvent_getAction(event);
        /* Leave the headset's own keys to the system. */
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_POWER) return 0;
        if (code <= 0 || code >= MAX_KEYS) return 1;
        if (action == AKEY_EVENT_ACTION_DOWN) {
            if (!keys[code] && AKeyEvent_getRepeatCount(event) == 0) press(code);
            keys[code] = true;
        } else if (action == AKEY_EVENT_ACTION_UP) {
            keys[code] = false;
        }
        return 1;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;
    int32_t source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE) {
        int32_t buttons = AMotionEvent_getButtonState(event);
        int32_t pressed = buttons & ~mouse_buttons;
        for (int b = 0; b < 31; b++)
            if (pressed & (1 << b)) press(MOUSE_BASE + b);
        mouse_buttons = buttons;
        track_pointer(event);  /* the Touch aims unless the "external pointer aims" option is on */
        return 1;
    }
    if ((source & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN || (source & AINPUT_SOURCE_STYLUS) == AINPUT_SOURCE_STYLUS) {
        /* An absolute pointing gun (Gun4IR in "absolute mouse" mode shows up as a touchscreen): a
         * touch is the first mouse button, the position the pointer. The headset has no touchscreen
         * of its own, so these can only come from such a device. */
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            if (!(mouse_buttons & 1)) press(MOUSE_BASE + 0);
            mouse_buttons |= 1;
        } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            mouse_buttons &= ~1;
        }
        track_pointer(event);
        return 1;
    }
    if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK || (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) {
        for (size_t i = 0; i < sizeof watched / sizeof watched[0]; i++) {
            int32_t a = watched[i];
            float v = AMotionEvent_getAxisValue(event, a, 0), before = axes[a];
            if (fabsf(v) < .5f) axis_rested[a] = true;
            if (axis_rested[a] && v > .5f && before <= .5f) press(AXIS_POS + a);
            if (axis_rested[a] && v < -.5f && before >= -.5f) press(AXIS_NEG + a);
            axes[a] = v;
        }
        return 1;
    }
    return 0;
}

void tremu_ext_learn_start(void) {
    learning = true;
    learn_role = 0;
    memset(learned, 0, sizeof learned);
}

void tremu_ext_learn_skip(void) {
    if (!learning) return;
    learned[learn_role] = 0;  /* keep the defaults for this control */
    if (++learn_role == EXT_ROLES) learning = false;
}

bool tremu_ext_learning(void) { return learning; }
ExtRole tremu_ext_learn_role(void) { return (ExtRole)learn_role; }
void tremu_ext_learned(int out[EXT_ROLES]) { memcpy(out, learned, sizeof learned); }

const char *tremu_ext_role_name(ExtRole role) {
    switch (role) {
    case EXT_TRIGGER: return "la G\xC3\x82" "CHETTE";
    case EXT_GRENADE: return "le bouton GRENADE";
    case EXT_COIN: return "le bouton PI\xC3\x88" "CE";
    case EXT_START: return "le bouton START";
    case EXT_ACTION: return "l'ACTION (glissi\xC3\xA8re avant/arri\xC3\xA8re ou bouton)";
    default: return "";
    }
}

unsigned tremu_ext_last_signal(char *out, size_t size) {
    if (!last_count) snprintf(out, size, "%s", T("aucun signal re\xC3\xA7u"));
    else if (last_code < MAX_KEYS) snprintf(out, size, T("bouton %d"), last_code);
    else if (last_code < AXIS_POS) snprintf(out, size, T("clic %d"), last_code - MOUSE_BASE);
    else if (last_code < AXIS_NEG) snprintf(out, size, T("axe %d +"), last_code - AXIS_POS);
    else snprintf(out, size, T("axe %d -"), last_code - AXIS_NEG);
    return last_count;
}
