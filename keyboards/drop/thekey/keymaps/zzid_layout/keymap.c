#include QMK_KEYBOARD_H
#include "timer.h"
#include <math.h>
#include <stdint.h>

/* ============================
   xorshift128+ PRNG (64-bit)
   - state: two 64-bit values
   - output: 64-bit value
   ============================ */
static uint64_t rng_s0 = 0x243F6A8885A308D3ULL;
static uint64_t rng_s1 = 0x13198A2E03707344ULL;

// Rotate right
static inline uint64_t rotr64(uint64_t x, int r) {
    return (x >> r) | (x << ((-r) & 63));
}

static uint64_t xorshift128plus(void) {
    uint64_t s1 = rng_s0;
    const uint64_t s0 = rng_s1;
    rng_s0 = s0;
    s1 ^= s1 << 23; // a
    rng_s1 = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26); // b, c
    return rng_s1 + s0;
}

// return float in [0,1)
static float my_rand_float(void) {
    // use high bits for better distribution
    uint64_t v = xorshift128plus();
    uint32_t top24 = (uint32_t)(v >> 40); // take top 24 bits
    return (float)top24 / (float)0x01000000; // / 2^24
}

// u32 range [min, max]
static uint32_t my_rand_range_u32(uint32_t min, uint32_t max) {
    if (max <= min) return min;
    uint64_t v = xorshift128plus();
    return min + (uint32_t)(v % (uint64_t)(max - min + 1));
}

/* ============================
   Seeding / entropy mixing
   - Basic: timer_read32() + address mixing
   - Optional: ADC read or USB SOF timestamp if enabled by macros
   ============================ */
   static void rng_seed_mix(void) {
    uint32_t s = timer_read32();

    // AVR(32U4) = 32bit pointer only
    uintptr_t addr = (uintptr_t)&rng_s0;
    s ^= (uint32_t)addr;

    // ADC 노이즈를 추가하려면: 사용 안 하는 핀을 열어 읽기
    // (ATmega32U4 에서는 F4 같은 핀 사용 가능)
    #ifdef ENABLE_ADC_ENTROPY
    // extern uint16_t analogReadPin(pin_t pin);  // QMK analog.h
    // s ^= analogReadPin(F4);
    #endif

    // USB SOF (Start of Frame) timestamp 추가는 AVR에서는 접근 힘듦.
    // -> 사실상 timer_read32()와 주소값 섞기로 충분.

    uint64_t mix = ((uint64_t)s << 32) | (uint64_t)(s ^ 0xA5A5A5A5UL);
    rng_s0 ^= mix;
    xorshift128plus();
    rng_s1 ^= (mix >> 17);

    if (rng_s0 == 0 && rng_s1 == 0) {
        rng_s0 = 0x0123456789ABCDEFULL;
        rng_s1 = 0xFEDCBA9876543210ULL;
    }
}
/* ============================
   Box-Muller for gaussian
   ============================ */
static float rand_normal(float mean, float stddev) {
    float u1 = my_rand_float();
    while (u1 <= 1e-7f) u1 = my_rand_float();
    float u2 = my_rand_float();
    float z0 = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
    return mean + z0 * stddev;
}

/* ============================
   Macro state + human-like behavior
   ============================ */
static bool macro_running = false;
static uint32_t macro_timer = 0;
static uint32_t macro_start_time = 0;
static bool holding_rctl = false;
static bool holding_up = false;

static float target_hold_time = 0.0f;

/* glitch split state */
static bool glitch_active = false;
static bool glitch_gap_active = false;
static float glitch_gap_time = 0.0f;
static float glitch_remaining_time = 0.0f;

/* ============================
   Human-like press/release utilities
   - µs-level gaussian jitter using rand_normal
   - uses wait_us (busy-wait microsecond delay)
   ============================ */

// microsecond-level gaussian delay (mean_us, stddev_us)
static void human_delay_us(int mean_us, int stddev_us) {
    float d = rand_normal((float)mean_us, (float)stddev_us);
    int di = (int)roundf(d);
    if (di < 0) di = 0;
    // wait_us takes uint16_t on many platforms; clamp
    if (di > 60000) di = 60000;
    wait_us((uint16_t)di);
}

// press with slight travel delay (µs)
static void human_press(uint16_t keycode) {
    // travel delay ~1.5ms ±0.4ms -> in µs
    human_delay_us(1500, 400);
    register_code(keycode);
}

// release with occasional micro-bounce
static void human_release(uint16_t keycode) {
    unregister_code(keycode);
    // 10% chance to simulate bounce (short re-press)
    if ((xorshift128plus() % 10) == 0) {
        human_delay_us(500, 200);
        register_code(keycode);
        human_delay_us(300, 100);
        unregister_code(keycode);
    }
}

/* ============================
   Timing selection: mean_ms ± stddev_ms + 5% glitch
   ============================ */
static void set_next_hold_time(float mean_ms, float stddev_ms) {
    float t = rand_normal(mean_ms, stddev_ms);
    if (t < 50.0f) t = 50.0f;
    target_hold_time = t;

    // ~5% glitch chance
    if ((xorshift128plus() % 20) == 0) {
        glitch_active = true;
    } else {
        glitch_active = false;
    }
    glitch_gap_active = false;
    glitch_remaining_time = 0.0f;
}

/* ============================
   helpers
   ============================ */
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

/* ============================
   Toggle macro
   ============================ */
static void toggle_macro(void) {
    if (macro_running) {
        release_all();
        macro_running = false;
    } else {
        // mix seed on start for extra entropy
        rng_seed_mix();

        release_all();
        macro_running = true;
        macro_start_time = timer_read32();

        human_press(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
        set_next_hold_time(900.0f, 250.0f); // UP: 0.9s ±0.25s
    }
}

/* ============================
   Main scan loop
   ============================ */
void matrix_scan_user(void) {
    if (!macro_running) return;

    // safety timeout: 150 seconds
    if (timer_elapsed32(macro_start_time) >= 150000) {
        toggle_macro();
        return;
    }

    uint32_t elapsed = timer_elapsed32(macro_timer);

    // if currently in a glitch gap (released & waiting to re-press)
    if (glitch_gap_active) {
        if (elapsed >= (uint32_t)glitch_gap_time) {
            if (!holding_rctl && !holding_up) {
                if (glitch_remaining_time > 0.0f) {
                    // press (maintain randomness in choice to avoid perfect parity)
                    if ((xorshift128plus() & 1) != 0) {
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

    // UP -> RCTL
    if (holding_up && elapsed >= (uint32_t)target_hold_time) {
        human_release(KC_UP);
        holding_up = false;

        if (glitch_active) {
            // split: first portion already passed; compute remaining
            float ratio = ((float)my_rand_range_u32(30, 70)) / 100.0f;
            float total = target_hold_time;
            float second = total * (1.0f - ratio);

            float gap = rand_normal(80.0f, 30.0f);
            if (gap < 20.0f) gap = 20.0f;

            glitch_gap_time = gap;
            glitch_remaining_time = second;

            glitch_gap_active = true;
            macro_timer = timer_read32();
            return;
        }

        human_press(KC_RCTL);
        holding_rctl = true;
        macro_timer = timer_read32();
        set_next_hold_time(4500.0f, 700.0f); // RCTL: 4.5s ±0.7s
    }

    // RCTL -> UP
    if (holding_rctl && elapsed >= (uint32_t)target_hold_time) {
        human_release(KC_RCTL);
        holding_rctl = false;

        if (glitch_active) {
            float ratio = ((float)my_rand_range_u32(30, 70)) / 100.0f;
            float total = target_hold_time;
            float second = total * (1.0f - ratio);

            float gap = rand_normal(80.0f, 30.0f);
            if (gap < 20.0f) gap = 20.0f;

            glitch_gap_time = gap;
            glitch_remaining_time = second;

            glitch_gap_active = true;
            macro_timer = timer_read32();
            return;
        }

        human_press(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
        set_next_hold_time(900.0f, 250.0f); // UP: 0.9s ±0.25s
    }
}

/* ============================
   Keymap & process
   ============================ */
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