/**
 *  COMPILE: qmk compile -kb drop/thekey/v1 -km zzid_layout
 *  FLASH: qmk flash -kb drop/thekey/v1 -km zzid_layout (OR) make drop/thekey/v1:zzid_layout:flash
 */ \
 #include QMK_KEYBOARD_H
 #include "timer.h"
 
 // 매크로 상태
 static bool macro_running = false;
 static uint32_t macro_timer = 0;
 static bool holding_s = false;
 static bool holding_up = false;
 static uint32_t macro_start_time = 0;

 // 난수 범위 함수 (매번 시드 섞음)
 uint16_t random_range(uint16_t min, uint16_t max) {
     srand(timer_read32() ^ rand());  // 실행 시점마다 seed 재설정
     return min + (rand() % (max - min + 1));
 }
 
 // 모든 키 해제
 void release_all(void) {
     if (holding_s) {
         unregister_code(KC_RCTL);
         holding_s = false;
     }
     if (holding_up) {
         unregister_code(KC_UP);
         holding_up = false;
     }
 }
 
 // 매크로 토글
 void toggle_macro(void) {
    if (macro_running) {
        release_all();
        macro_running = false;
    } else {
        release_all();
        macro_running = true;

        // 시작 시간 기록
        macro_start_time = timer_read32();

        // 시작은 ↑ 키
        register_code(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
    }
}

 
 // 매트릭스 스캔 루프
 void matrix_scan_user(void) {
    if (!macro_running) return;

    // 150초 제한
    if (timer_elapsed32(macro_start_time) >= 150000) {
        toggle_macro();
        return;
    }

    if (holding_up && timer_elapsed32(macro_timer) >= random_range(200, 500)) {
        unregister_code(KC_UP);
        holding_up = false;

        register_code(KC_RCTL);
        holding_s = true;
        macro_timer = timer_read32();
    }
    else if (holding_s && timer_elapsed32(macro_timer) >= random_range(3800, 4500)) {
        unregister_code(KC_RCTL);
        holding_s = false;

        register_code(KC_UP);
        holding_up = true;
        macro_timer = timer_read32();
    }
}
 
 // 키맵: 레이어 하나
 const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
     [0] = LAYOUT(
         KC_LCTL, 
         KC_C, 
         QK_MACRO_0  // 여기 매크로 키
     )
 };
 
 // 프로세스
 bool process_record_user(uint16_t keycode, keyrecord_t *record) {
     switch (keycode) {
         case QK_MACRO_0: // V 자리에 있는 키
             if (record->event.pressed) {
                 toggle_macro();
             }
             return false;
     }
     return true;
 }