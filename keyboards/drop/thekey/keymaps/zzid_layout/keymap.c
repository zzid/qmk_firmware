/**
 *  COMPILE: qmk compile -kb drop/thekey/v1 -km zzid_layout
 *  FLASH: qmk flash -kb drop/thekey/v1 -km zzid_layout
 *  FLASH: make drop/thekey/v1:zzid_layout:flash
 */ \
#include QMK_KEYBOARD_H

enum layers { _GAMING, _DEV };

static bool key1_pressed   = false;
static bool key2_pressed   = false;
static bool key3_pressed   = false;
static bool switching_mode = false; // Prevent unintended keypresses

// Set RGB backlight based on current layer
void set_backlight_by_layer(void) {
    rgblight_enable();
    rgblight_mode(RGBLIGHT_MODE_STATIC_LIGHT);

    if (layer_state_is(_GAMING)) {
        rgblight_sethsv(43, 255, 255); // Yellow for Gaming
    } else if (layer_state_is(_DEV)) {
        rgblight_sethsv(85, 255, 255); // Green for Development
    }
}

// Default keymaps with layers
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_GAMING] = LAYOUT(KC_LCTL, KC_C, KC_V),               // Gaming Mode
    [_DEV]    = LAYOUT(LGUI(KC_A), LGUI(KC_C), LGUI(KC_V)) // Development Mode [Cmd+A, Cmd+C, Cmd+V]
};

// Mode switching logic
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        if (keycode == KC_LCTL || keycode == LGUI(KC_A)) key1_pressed = true;
        if (keycode == KC_C || keycode == LGUI(KC_C)) key2_pressed = true;
        if (keycode == KC_V || keycode == LGUI(KC_V)) key3_pressed = true;

        if (key1_pressed && key2_pressed && key3_pressed) {
            switching_mode = true; // Prevents key execution
            return false;          // Ignore key presses during mode switch
        }
    } else {
        if (keycode == KC_LCTL || keycode == LGUI(KC_A)) key1_pressed = false;
        if (keycode == KC_C || keycode == LGUI(KC_C)) key2_pressed = false;
        if (keycode == KC_V || keycode == LGUI(KC_V)) key3_pressed = false;

        if (switching_mode) {
            switching_mode = false; // Reset flag after keys are released
            return false;           // Block key execution
        }
    }

    return !switching_mode; // If switching, prevent key execution
}

// Matrix scan for mode toggling
void matrix_scan_user(void) {
    static bool last_state = false;

    if (key1_pressed && key2_pressed && key3_pressed) {
        if (!last_state) {
            clear_keyboard(); // Prevent unintended keypresses
            if (layer_state_is(_GAMING)) {
                layer_move(_DEV);
            } else {
                layer_move(_GAMING);
            }
            set_backlight_by_layer();
            last_state = true;
        }
    } else {
        last_state = false;
    }
}

// Bootloader LED detection
void keyboard_post_init_user(void) {
    rgblight_enable();
    set_backlight_by_layer();
}

// Reset event: Turn LED red
void suspend_power_down_user(void) {
    rgblight_disable_noeeprom(); // Turn off existing lights
    rgblight_enable_noeeprom();
    rgblight_sethsv(0, 255, 255); // Red LED when resetting
}

// Bootloader event: Turn LED red
void bootmagic_lite(void) {
    rgblight_disable_noeeprom();
    rgblight_enable_noeeprom();
    rgblight_sethsv(0, 255, 255); // Red LED when entering bootloader
}
