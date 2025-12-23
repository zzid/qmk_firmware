// Copyright 2023 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

/* 
 * QMK does not have a way to actively detect if the host operating system is macOS or Windows.
 * The keyboard is a generic USB HID device and the OS does not broadcast its identity to the keyboard.
 * 
 * Common approaches are:
 *   - Provide a user-togglable mode (e.g. via a keycode or dip switch) and store this selection in EEPROM.
 *   - Make assumptions based on modifier behavior (not reliable).
 *   - Use keymaps or combos to easily switch modes (for Mac/Win differences).
 * 
 * See: https://docs.qmk.fm/#/feature_macros?id=operating-system-specific-behavior
 *      https://docs.qmk.fm/#/os_detection (explains why auto-detection is not possible)
 */

/* Example: Simple Mac/Win mode toggler using a persistent variable and a keycode. */

enum custom_keycodes {
    KC_TG_OS = SAFE_RANGE, // Custom keycode to toggle OS mode
};

typedef enum {
    OS_WINDOWS = 0,
    OS_MACOS = 1
} os_mode_t;

static os_mode_t current_os_mode = OS_WINDOWS;

// Save mode to EEPROM
void set_os_mode(os_mode_t mode) {
    current_os_mode = mode;
    eeconfig_update_user(current_os_mode);
}

// Load mode from EEPROM
void matrix_init_user(void) {
    uint8_t val = eeconfig_read_user();
    if (val <= OS_MACOS) {
        current_os_mode = (os_mode_t)val;
    } else {
        current_os_mode = OS_WINDOWS;
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case KC_TG_OS:
            if (record->event.pressed) {
                set_os_mode(current_os_mode == OS_WINDOWS ? OS_MACOS : OS_WINDOWS);
            }
            return false;
    }
    return true;
}

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_tkl_ansi(
        KC_ESC,           KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,     KC_PSCR, KC_SCRL, KC_PAUS,

        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC,    KC_INS,  KC_HOME, KC_PGUP,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,    KC_DEL,  KC_END,  KC_PGDN,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
        KC_LSFT,          KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH,          KC_RSFT,             KC_UP,
        // Example: Replace KC_LGUI ("Windows"/"Command" key) with either KC_LGUI or KC_LCTL for macOS
        // and KC_LGUI for Windows. You could also remap Alt/GUI positions, etc, based on `current_os_mode`
        // Here, always LGUI/GUI as an example.
        KC_LCTL, current_os_mode == OS_MACOS ? KC_LGUI : KC_LGUI, KC_LALT,       KC_SPC,                           KC_RALT, KC_RGUI, KC_APP,  KC_RCTL,    KC_LEFT, KC_DOWN, KC_RGHT
    )
};

#ifdef RGB_MATRIX_ENABLE
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    led_t led_state = host_keyboard_led_state();
    if (!led_state.scroll_lock) {
        RGB_MATRIX_INDICATOR_SET_COLOR(1, 0, 0, 0);
    }

    if (!led_state.caps_lock) {
        RGB_MATRIX_INDICATOR_SET_COLOR(0, 0, 0, 0);
    }

    // Optionally, indicate Mac/Win mode visually:
    if (current_os_mode == OS_MACOS) {
        RGB_MATRIX_INDICATOR_SET_COLOR(5, 0, 128, 255); // blue marker for macOS on led 5
    } else {
        RGB_MATRIX_INDICATOR_SET_COLOR(5, 0, 255, 0);   // green marker for Windows on led 5
    }

    return true;
}
#endif
