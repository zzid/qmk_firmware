#include QMK_KEYBOARD_H
#include "timer.h"
#include <math.h>

// ---------------- 상태 관리 ----------------
static bool macro_running = false;
static uint32_t macro_timer = 0;
static uint32_t macro_start_time = 0;
static bool holding_rctl = false;
static bool holding_up = false;

// 타이밍
static float target_hold_time = 0.0f;

// 글리치 관련
static bool glitch_active = false;
static bool glitch_gap_active = false;
static float glitch_gap_time = 0.0f;
static float glitch_remaining_time = 0.0f;

// ---------------- 난수 유틸 ----------------
static float rand_normal(float mean, float stddev) {
    float u1 = ((rand32() % 10000) + 1) / 10001.0f;
    float u2 = ((rand32() % 10000) + 1) / 10001.0f;
    float z0 = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
    return mean + z0 * stddev;
}

static void set_next_hold_time(float mean_ms, float stddev_ms) {
    float t = rand_normal(mean_ms, stddev_ms);
    if (t < 50.0f) t = 50.0f;
    target_hold_time = t;

    glitch_active = ((rand32() % 20) == 0); // 5% 확률
    glitch_gap_active = false;
    glitch_remaining_time = 0.0f;
}

// ---------------- 사람스러운 딜레이/입력 ----------------
static void human_delay_us(uint16_t mean, uint16_t stdev) {
    int d = (int)rand_normal(mean, stdev);
    if (d < 0) d = 0;
    wait_us((uint16_t)d);
}

static void human_press(uint16_t keycode) {
    human_delay_us(1500, 400);   // 누르기 전 약간의 딜레이
    register_code(keycode);
}

static void human_release(uint16_t keycode) {
    unregister_code(keycode);
    // 10% 확률 바운스
    if ((rand32() % 10) == 0) {
        human_delay_us(500, 200);
        register_code(keycode);
        human_delay_us(300, 100);
        unregister_code(keycode);
    }
}

static void release_all(void) {
    if (holding_rctl) {
        human_release(KC_RCTL);
        holding_rctl = false;
    }
    if (holding_up) {
        human_release(KC_UP);
        holding_up = false;
    }
}

// ---------------- 매크로 토글 ----------------
static void toggle_macro(void) {
    if (macro_running) {
        release_all();
        macro_running = false;
    } else {
        release_all();
        macro_running = true;
        macro_start_time = timer_read32();

        human_press(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
        set_next_hold_time(900.0f, 250.0f); // UP 평균 0.9s
    }
}

// ---------------- 메인 루프 ----------------
void matrix_scan_user(void) {
    if (!macro_running) return;

    // 150초 제한
    if (timer_elapsed32(macro_start_time) >= 150000) {
        toggle_macro();
        return;
    }

    uint32_t elapsed = timer_elapsed32(macro_timer);

    // 글리치 gap 처리 중
    if (glitch_gap_active) {
        if (elapsed >= (uint32_t)glitch_gap_time) {
            if (!holding_rctl && !holding_up) {
                if (glitch_remaining_time > 0) {
                    if (rand32() & 1) {
                        human_press(KC_RCTL);
                        holding_rctl = true;
                    } else {
                        human_press(KC_UP);
                        holding_up = true;
                    }
                    target_hold_time = glitch_remaining_time;
                    macro_timer = timer_read32();
                }
                glitch_gap_active = false;
                glitch_active = false;
                glitch_remaining_time = 0.0f;
            }
        }
        return;
    }

    // UP → RCTL
    if (holding_up && elapsed >= (uint32_t)target_hold_time) {
        human_release(KC_UP);
        holding_up = false;

        if (glitch_active) {
            float ratio = (30 + (rand32() % 41)) / 100.0f;
            float total = target_hold_time;
            float second = total * (1.0f - ratio);

            glitch_gap_time = rand_normal(80.0f, 30.0f);
            if (glitch_gap_time < 20.0f) glitch_gap_time = 20.0f;
            glitch_remaining_time = second;

            glitch_gap_active = true;
            macro_timer = timer_read32();
            return;
        }

        human_press(KC_RCTL);
        holding_rctl = true;
        macro_timer = timer_read32();
        set_next_hold_time(4500.0f, 700.0f); // RCTL 평균 4.5s
    }

    // RCTL → UP
    if (holding_rctl && elapsed >= (uint32_t)target_hold_time) {
        human_release(KC_RCTL);
        holding_rctl = false;

        if (glitch_active) {
            float ratio = (30 + (rand32() % 41)) / 100.0f;
            float total = target_hold_time;
            float second = total * (1.0f - ratio);

            glitch_gap_time = rand_normal(80.0f, 30.0f);
            if (glitch_gap_time < 20.0f) glitch_gap_time = 20.0f;
            glitch_remaining_time = second;

            glitch_gap_active = true;
            macro_timer = timer_read32();
            return;
        }

        human_press(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
        set_next_hold_time(900.0f, 250.0f); // UP
    }
}

// ---------------- 키맵 ----------------
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        KC_LCTL,
        KC_C,
        QK_MACRO_0
    )
};

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case QK_MACRO_0:
            if (record->event.pressed) {
                toggle_macro();
            }
            return false;
    }
    return true;
}