/**
 *  COMPILE: qmk compile -kb gray_studio/think65/solder -km zzid_layout
 *  FLASH: qmk flash -kb gray_studio/think65/solder -km zzid_layout
 *  FLASH: make gray_studio/think65/solder:zzid_layout:flash
 */

#include QMK_KEYBOARD_H

#define CUSTOM_SLEEP_KEY LT(_FN, CUSTOM_SLEEP)

// Bootloader trigger settings
#define BOOT_TRIGGER_KEY KC_RSFT // 원하는 키로 변경 가능
#define BOOT_PRESS_COUNT 10
#define BOOT_TIME_WINDOW 3000 // 2초 (2000ms)
// Bootloader detection variables
static uint16_t last_boot_press    = 0;
static uint8_t  boot_press_counter = 0;

// 현재 RGB 상태 저장 변수
static uint8_t  prev_rgb_mode;
static uint16_t prev_rgb_hue;
static uint8_t  prev_rgb_sat;
static uint8_t  prev_rgb_val;
static bool     bootloader_active = false; // 부트로더 상태 저장

// 초기화 시 RGB 상태 저장
void keyboard_post_init_user(void) {
    prev_rgb_mode = rgblight_get_mode();
    prev_rgb_hue  = rgblight_get_hue();
    prev_rgb_sat  = rgblight_get_sat();
    prev_rgb_val  = rgblight_get_val();
}

enum layer_names {
    _BASE,
    _FN,
    _ADJUST,
};
enum custom_keycodes {
    CUSTOM_SLEEP = SAFE_RANGE,
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE]   = LAYOUT_65_ansi_blocker(QK_GESC, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, RGB_MOD, KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_BSLS, KC_END, MO(1), KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_ENT, KC_PGUP, KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_RSFT, KC_UP, KC_PGDN, KC_LCTL, KC_LALT, KC_LGUI, KC_SPC, KC_CAPS, MO(2), KC_LEFT, KC_DOWN, KC_RGHT),
    [_FN]     = LAYOUT_65_ansi_blocker(KC_GRV, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_DEL, KC_BRIU, KC_TRNS, KC_NO, KC_WAKE, KC_MYCM, KC_NO, KC_CALC, KC_NO, KC_NO, KC_NO, KC_TRNS, KC_TRNS, KC_WBAK, KC_WFWD, KC_TRNS, KC_BRID, KC_TRNS, KC_TRNS, KC_SLEP, KC_TRNS, KC_WSCH, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_WHOM, KC_WH_U, KC_TRNS, KC_UNDO, KC_CUT, KC_COPY, KC_PSTE, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_MUTE, KC_VOLU, KC_WH_D, KC_TRNS, KC_TRNS, KC_TRNS, KC_MPLY, KC_NO, KC_APP, KC_MPRV, KC_VOLD, KC_MNXT),
    [_ADJUST] = LAYOUT_65_ansi_blocker(KC_NO, DM_PLY1, DM_PLY2, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, DM_REC1, DM_REC2, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_CAPS, BL_STEP, BL_DOWN, BL_BRTG, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, BL_TOGG, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, RGB_VAI, DM_RSTP, KC_NO, KC_NO, KC_NO, RGB_TOG, CUSTOM_SLEEP, KC_TRNS, RGB_RMOD, RGB_VAD, RGB_MOD)
};

// 커스텀 키 처리 함수
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case BOOT_TRIGGER_KEY:
            if (record->event.pressed) {
                uint16_t current_time = timer_read();
                if (current_time - last_boot_press < BOOT_TIME_WINDOW) {
                    boot_press_counter++;
                } else {
                    boot_press_counter = 1;
                }
                last_boot_press = current_time;

                if (boot_press_counter >= BOOT_PRESS_COUNT) {
                    // 🚨 Before jumping to bootloader, set LED to red
                    rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
                    rgblight_sethsv_noeeprom(0, 255, 255); // 빨간색 (H:0, S:255, V:255)
                    wait_ms(200);                          // Small delay to ensure color update

                    reset_keyboard();
                }
            }
            return false;
        case CUSTOM_SLEEP:
            if (record->event.pressed) {
                tap_code16(C(G(KC_Q))); // Ctrl + Cmd + Q (macOS 화면 잠금)
            }
            return false;
    }
    return true;
}
// 레이어 변경 시 RGB 상태 업데이트
layer_state_t layer_state_set_user(layer_state_t state) {
    uint8_t current_layer = get_highest_layer(state);

    // 레이어 변경 전에 현재 RGB 상태 저장 (이전 상태 복원 용도)
    if (current_layer == _BASE) {
        // _BASE로 돌아올 때만 저장하지 않고, 이전 RGB 상태를 복원
        rgblight_mode_noeeprom(prev_rgb_mode);
        rgblight_sethsv_noeeprom(prev_rgb_hue, prev_rgb_sat, prev_rgb_val);
    } else {
        // 현재 RGB 설정을 저장 (나중에 _BASE로 돌아올 때 적용하기 위해)
        prev_rgb_mode = rgblight_get_mode();
        prev_rgb_hue  = rgblight_get_hue();
        prev_rgb_sat  = rgblight_get_sat();
        prev_rgb_val  = rgblight_get_val();
    }

    switch (get_highest_layer(state)) {
        case _FN:
            rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
            rgblight_sethsv_noeeprom(170, 255, 128); // 파란색
            break;
        case _ADJUST:
            rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
            rgblight_sethsv_noeeprom(43, 255, 255); // 노란색
            break;
        default:
            // 이전에 사용하던 RGB 모드 복원
            rgblight_mode_noeeprom(prev_rgb_mode);
            rgblight_sethsv_noeeprom(prev_rgb_hue, prev_rgb_sat, prev_rgb_val);
            break;
    }
    return state;
}

// QK_BOOT 모드에서 RGB를 빨간색으로 변경
void suspend_power_down_user(void) {
    // // 현재 RGB 설정 저장 (부트로더 종료 후 복원하기 위함)
    // prev_rgb_mode = rgblight_get_mode();
    // prev_rgb_hue  = rgblight_get_hue();
    // prev_rgb_sat  = rgblight_get_sat();
    // prev_rgb_val  = rgblight_get_val();

    // 부트로더 상태로 전환
    bootloader_active = true;

    // 부트로더 진입 시 RGB 빨간색으로 변경
    rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
    rgblight_sethsv_noeeprom(0, 255, 255); // 빨간색 (H:0, S:255, V:255)
}

// 부트로더 종료 후 이전 RGB 상태 복원
void suspend_wakeup_init_user(void) {
    bootloader_active = false; // 부트로더 종료 상태

    // 이전 RGB 상태 복원
    rgblight_mode_noeeprom(prev_rgb_mode);
    rgblight_sethsv_noeeprom(prev_rgb_hue, prev_rgb_sat, prev_rgb_val);
}
