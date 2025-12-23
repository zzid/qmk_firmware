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
   ============================ */
static void rng_seed_mix(void) {
    uint32_t s = timer_read32();

    // Mix with address of rng_s0 for entropy
    uintptr_t addr = (uintptr_t)&rng_s0;
    s ^= (uint32_t)addr;

    // Optional ADC noise can be added here for even more entropy (commented out)
    #ifdef ENABLE_ADC_ENTROPY
    // extern uint16_t analogReadPin(pin_t pin);  // QMK analog.h
    // s ^= analogReadPin(F4);
    #endif

    uint64_t mix = ((uint64_t)s << 32) | (uint64_t)(s ^ 0xA5A5A5A5UL);
    rng_s0 ^= mix;
    xorshift128plus();
    rng_s1 ^= (mix >> 17);

    // Ensure state is never both zero
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
// Macro mode selector: original (OG) or extra (UP <-> A)
typedef enum {
    MACRO_NONE = 0,
    MACRO_OG,     // UP <-> RCTL macro
    MACRO_EXTRA   // UP <-> A macro
} macro_mode_t;

static macro_mode_t macro_mode = MACRO_NONE;    // tracks which macro is running

static bool macro_running = false;              // macro is currently in progress
static uint32_t macro_timer = 0;                // for interval timing between key actions
static uint32_t macro_start_time = 0;           // for safety timeout

// Track what buttons are logically held (for release_all etc)
static bool holding_rctl = false;
static bool holding_up = false;
static bool holding_a = false;                  // only used in extra macro

static float target_hold_time = 0.0f;           // ms: how long to hold the current key

// glitch “split” state
static bool glitch_active = false;
static bool glitch_gap_active = false;
static float glitch_gap_time = 0.0f;            // ms
static float glitch_remaining_time = 0.0f;      // ms

/* ============================
   Human-like press/release utilities
   ============================ */
// microsecond-level gaussian delay (mean_us, stddev_us)
static void human_delay_us(int mean_us, int stddev_us) {
    float d = rand_normal((float)mean_us, (float)stddev_us);
    int di = (int)roundf(d);
    if (di < 0) di = 0;
    // wait_us takes uint16_t on many platforms; clamp max value
    if (di > 60000) di = 60000;
    wait_us((uint16_t)di);
}

// Simulate a "press" with a touch of travel time (human-likeness)
static void human_press(uint16_t keycode) {
    // travel delay ~1.5ms ±0.4ms (in µs)
    human_delay_us(1500, 400);
    register_code(keycode);
}

// Simulate a "release" with occasional short bounce
static void human_release(uint16_t keycode) {
    unregister_code(keycode);
    // 10% random chance of simulating a micro-bounce on release
    if ((xorshift128plus() % 10) == 0) {
        human_delay_us(500, 200);      // short off
        register_code(keycode);        // quick re-press
        human_delay_us(300, 100);
        unregister_code(keycode);
    }
}

/* ============================
   Timing selection: mean_ms ± stddev_ms + ~5% glitch
   ============================ */
// Sets target_hold_time (for the next key in macro), with a small chance to set up a “glitch” period
static void set_next_hold_time(float mean_ms, float stddev_ms) {
    float t = rand_normal(mean_ms, stddev_ms);
    if (t < 50.0f) t = 50.0f;     // minimum 50ms
    target_hold_time = t;

    // ~5% chance of "glitch" (split: key up, short wait, re-press)
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
// Release all "held" keys for macro, regardless of mode
static void release_all(void) {
    if (holding_rctl) {
        human_release(KC_RCTL);
        holding_rctl = false;
    }
    if (holding_a) {
        human_release(KC_A);
        holding_a = false;
    }
    if (holding_up) {
        human_release(KC_UP);
        holding_up = false;
    }
}

/* ============================
   Toggle macro OG (UP <-> RCTL, original timings)
   ============================ */
static void toggle_macro_og(void) {
    if (macro_running && macro_mode == MACRO_OG) {
        // Stop OG macro
        release_all();
        macro_running = false;
        macro_mode = MACRO_NONE;
    } else {
        // Start OG macro (UP <-> RCTL)
        rng_seed_mix();      // Mix the PRNG for entropy before running

        release_all();
        macro_running = true;
        macro_mode = MACRO_OG;
        macro_start_time = timer_read32();

        // Start by pressing UP. Next: UP 0.9s ±0.25s -> RCTL 4.5s ±0.7s, loop
        human_press(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
        set_next_hold_time(900.0f, 250.0f); // UP: 0.9s ±0.25s
    }
}

/* ============================
   Toggle macro EXTRA (UP <-> A, alternate timings)
   ============================ */
static void toggle_macro_extra(void) {
    if (macro_running && macro_mode == MACRO_EXTRA) {
        // Stop EXTRA macro
        release_all();
        macro_running = false;
        macro_mode = MACRO_NONE;
    } else {
        // Start EXTRA macro (UP <-> A)
        rng_seed_mix();

        release_all();
        macro_running = true;
        macro_mode = MACRO_EXTRA;
        macro_start_time = timer_read32();

        // Start by pressing UP. Next: UP 0.7s ±0.2s -> A 2.4s ±0.3s, loop
        human_press(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
        set_next_hold_time(700.0f, 200.0f); // UP: 0.7s ±0.2s
    }
}

/* ============================
   Main scan loop (handles both macros and glitches)
   ============================ */
void matrix_scan_user(void) {
    if (!macro_running) return;

    // Safety timeout: 150 seconds, auto-disables current macro
    if (timer_elapsed32(macro_start_time) >= 150000) {
        if (macro_mode == MACRO_OG) {
            toggle_macro_og();
        } else if (macro_mode == MACRO_EXTRA) {
            toggle_macro_extra();
        }
        return;
    }

    uint32_t elapsed = timer_elapsed32(macro_timer);

    // If currently in a glitch gap, wait for it to expire, then "re-press"
    if (glitch_gap_active) {
        if (elapsed >= (uint32_t)glitch_gap_time) {
            // After gap, pick which side to re-press (logic depends on macro mode)
            if (!holding_rctl && !holding_a && !holding_up) {
                if (glitch_remaining_time > 0.0f) {
                    if (macro_mode == MACRO_OG) {
                        // OG macro glitch: randomize between RCTL or UP
                        if ((xorshift128plus() & 1) != 0) {
                            human_press(KC_RCTL);
                            holding_rctl = true;
                        } else {
                            human_press(KC_UP);
                            holding_up = true;
                        }
                    } else if (macro_mode == MACRO_EXTRA) {
                        // EXTRA macro glitch: randomize between A or UP
                        if ((xorshift128plus() & 1) != 0) {
                            human_press(KC_A);
                            holding_a = true;
                        } else {
                            human_press(KC_UP);
                            holding_up = true;
                        }
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

    // Main OG macro logic: UP <-> RCTL, original timings
    if (macro_mode == MACRO_OG) {
        // UP is being held
        if (holding_up && elapsed >= (uint32_t)target_hold_time) {
            // Release UP after time
            human_release(KC_UP);
            holding_up = false;

            // %5 chance: insert brief glitch up-time before next phase
            if (glitch_active) {
                // Take a random split (30-70%) before/after
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

            // Press RCTL, set next hold time (4.5s ±0.7s)
            human_press(KC_RCTL);
            holding_rctl = true;
            macro_timer = timer_read32();
            set_next_hold_time(4500.0f, 700.0f); // RCTL: 4.5s ±0.7s
        }

        // RCTL is being held
        if (holding_rctl && elapsed >= (uint32_t)target_hold_time) {
            // Release RCTL
            human_release(KC_RCTL);
            holding_rctl = false;

            // %5 chance: insert brief glitch up-time before next phase
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

            // Press UP, set next hold time (0.9s ±0.25s)
            human_press(KC_UP);
            holding_up = true;
            macro_timer = timer_read32();
            set_next_hold_time(900.0f, 250.0f); // UP: 0.9s ±0.25s
        }
    }
    // Main EXTRA macro logic: UP <-> A, alternate timings
    else if (macro_mode == MACRO_EXTRA) {
        // UP is being held
        if (holding_up && elapsed >= (uint32_t)target_hold_time) {
            // Release UP after time
            human_release(KC_UP);
            holding_up = false;

            // %5 chance: insert brief glitch up-time before next phase
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

            // Press A, set next hold time (2.4s ±0.3s)
            human_press(KC_A);
            holding_a = true;
            macro_timer = timer_read32();
            set_next_hold_time(2400.0f, 300.0f); // A: 2.4s ±0.3s
        }

        // A is being held
        if (holding_a && elapsed >= (uint32_t)target_hold_time) {
            // Release A
            human_release(KC_A);
            holding_a = false;

            // %5 chance: insert brief glitch up-time before next phase
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

            // Press UP, set next hold time (0.7s ±0.2s)
            human_press(KC_UP);
            holding_up = true;
            macro_timer = timer_read32();
            set_next_hold_time(700.0f, 200.0f); // UP: 0.7s ±0.2s
        }
    }
}

/* ============================
   Keymap & process
   ============================ */
// Key assignment (top to bottom):
//  QK_MACRO_1  -- triggers extra macro (UP <-> A)
//  KC_C        -- standard C
//  QK_MACRO_0  -- triggers OG macro (UP <-> RCTL)
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        QK_MACRO_1, // Key top: alternate macro (UP <-> A)
        KC_C,
        QK_MACRO_0  // Key bottom: OG macro (UP <-> RCTL)
    )
};

/*
Each macro is toggled by its button. Only one runs at a time.
  OG macro (QK_MACRO_0): UP (0.9s ±0.25s) <-> RCTL (4.5s ±0.7s)
  Extra macro (QK_MACRO_1): UP (0.7s ±0.2s) <-> A (2.4s ±0.3s)
*/
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case QK_MACRO_0:
            if (record->event.pressed) {
                toggle_macro_og();
            }
            return false;
        case QK_MACRO_1:
            if (record->event.pressed) {
                toggle_macro_extra();
            }
            return false;
    }
    return true;
}