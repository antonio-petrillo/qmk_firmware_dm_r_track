/* Copyright
 *  2020 Qurn
 *  2022 Pascal Jaeger
 *  ______     _____
 * |_   __ \  |_   _|
 *   | |__) |   | |
 *   |  ___/ _  | |
 *  _| |_   | |_| |
 * |_____|  \.___.'
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	If not, see <http://www.gnu.org/licenses/>.
 */

#include "trackball_functions.h"

#define _LOWER 1
#define _RAISE 2

#define RAISE MO(_RAISE)
#define LOWER MO(_LOWER)

/***************************
 * Trackball related defines
 **************************/

#define CURSOR_MODE 0
#define CARRET_MODE 1
#define SCROLL_MODE 2
#define SND_BRIGHT_MODE 3

uint8_t track_mode =
    0; // 0 Mousecursor; 1 arrowkeys/carret; 2 scrollwheel; 3 sound/brightness
uint8_t prev_track_mode = 0;
bool integration_mode = false;
int16_t cum_x = 0;
int16_t cum_y = 0;
int16_t sensor_x = 0;
int16_t sensor_y = 0;

// Thresholds help to move only horizontal or vertical. When accumulated
// distance reaches threshold, only move one discrete value in direction with
// bigger delta.
uint8_t carret_threshold = CARRET_MODE_SENS;        // higher means slower
uint16_t carret_threshold_inte = CARRET_INTE_SENS; // in integration mode higher threshold

#define regular_smoothscroll_factor 8
bool smooth_scroll = true;
uint8_t scroll_threshold =
    SCROLL_MODE_SENS / regular_smoothscroll_factor; // divide if started smooth
uint16_t scroll_threshold_inte = SCROLL_INTE_SENS / regular_smoothscroll_factor;

uint16_t cursor_multiplier = CURSOR_MODE_SENS; // adjust cursor speed
uint16_t cursor_multiplier_inte = CURSOR_INTE_SENS;

int16_t cur_factor;

/***************************
 * Sticky key related defines
 **************************/

// identify keycombinations
bool is_alt_active = false;
bool is_sft_active = false;
bool is_ctl_active = false;
bool is_gui_active = false;
bool is_low_active = false;
bool is_rai_active = false;

bool activate_alt = false;
bool activate_sft = false;
bool activate_ctl = false;
bool activate_gui = false;
bool activate_low = false;
bool activate_rai = false;

bool sticky_key = false;
uint32_t sticky_timer = 0;
#define STICKY_TERM 500

/***************************
 * Tap dance 1 of 2
 **************************/

// define `ACTION_TAP_DANCE_FN_ADVANCED()` for each tapdance keycode, passing in
// `finished` and `reset` functions
qk_tap_dance_action_t tap_dance_actions[] = {
  [ALT_TM] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, alttm_finished, alttm_reset),
  [SFT_TM] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, sfttm_finished, sfttm_reset),
  [CTL_TM] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, ctltm_finished, ctltm_reset),
  [GUI_TM] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, guitm_finished, guitm_reset),
  [RAI_TM] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, raitm_finished, raitm_reset),
  [LOW_TM] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, lowtm_finished, lowtm_reset),
  [ESC_TM] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, esctm_finished, esctm_reset),
};

// define a type containing as many tapdance states as you need
typedef enum { SINGLE_TAP, SINGLE_HOLD } td_state_t;

// create a global instance of the tapdance state type
static td_state_t td_state;

// function to determine the current tapdance state
int cur_dance(qk_tap_dance_state_t *state);

/***************************
 * Mouse pressed
 **************************/

void on_mouse_button(uint8_t mouse_button, bool pressed) {
  report_mouse_t report = pointing_device_get_report();

  if (pressed)
    report.buttons |= mouse_button;
  else
    report.buttons &= ~mouse_button;
  pointing_device_set_report(report);
  pointing_device_send();
}

/***************************
 * Combos
 **************************/

enum combos_events { RS_MOUSE, ST_MOUSE, RT_MOUSE };

const uint16_t PROGMEM rsm_combo[] = {KC_R, KC_S, COMBO_END};
const uint16_t PROGMEM stm_combo[] = {KC_S, KC_T, COMBO_END};
const uint16_t PROGMEM rtm_combo[] = {KC_R, KC_T, COMBO_END};

combo_t key_combos[COMBO_COUNT] = {
  [RS_MOUSE] = COMBO_ACTION(rsm_combo),
  [ST_MOUSE] = COMBO_ACTION(stm_combo),
  [RT_MOUSE] = COMBO_ACTION(rtm_combo),
};

void process_combo_event(uint16_t combo_index, bool pressed) {
  switch (combo_index) {
  case RS_MOUSE:
    on_mouse_button(MOUSE_BTN2, pressed);
    break;
  case ST_MOUSE:
    on_mouse_button(MOUSE_BTN1, pressed);
    break;
  case RT_MOUSE:
    on_mouse_button(MOUSE_BTN3, pressed);
    break;
  }
}

/***************************
 * Custom Keycodes and Layers
 **************************/
enum custom_keycodes {
  KC_INTE = SAFE_RANGE,
  KC_RAISE,
  KC_LOWER,
  KC_SCLN_INV,
  KC_QUOT_MY, // not US Int style
  KC_TILD_MY, // not US Int style
  KC_CPI_DOWN,
  KC_CPI_STD,
  KC_CPI_UP,
  KC_SMO_SC
};

/***************************
 * Tap dance 2 of 2
 **************************/

// determine the tapdance state to return
int cur_dance(qk_tap_dance_state_t *state) {
  if (state->count == 1) {
    // if (state->interrupted || !state->pressed) { return SINGLE_TAP; }
    // //interrupted sends SINGLE_TAP
    if (!state->pressed) {
      return SINGLE_TAP;
    } // interrupted sends SINGLE_HOLD
    else {
      return SINGLE_HOLD;
    }
  } else {
    return 2;
  } // any number higher than the maximum state value you return above
}

void unstick_keys(void) {
  if (MOD_BIT(KC_LALT)) {
    unregister_mods(MOD_BIT(KC_LALT));
    is_alt_active = false;
  }
  if (MOD_BIT(KC_LSFT)) {
    unregister_mods(MOD_BIT(KC_LSFT));
    is_sft_active = false;
  }
  if (MOD_BIT(KC_LCTL)) {
    unregister_mods(MOD_BIT(KC_LCTL));
    is_ctl_active = false;
  }
  if (MOD_BIT(KC_LGUI)) {
    unregister_mods(MOD_BIT(KC_LGUI));
    is_gui_active = false;
  }
  if (is_rai_active) {
    layer_off(_RAISE);
    is_rai_active = false;
  }
  if (is_low_active) {
    layer_off(_LOWER);
    is_low_active = false;
  }
}

void activate_stkeys(void) {
  if (activate_alt) {
    register_mods(MOD_BIT(KC_LALT));
    activate_alt = false;
    is_alt_active = true;
  }
  if (activate_sft) {
    register_mods(MOD_BIT(KC_LSFT));
    activate_sft = false;
    is_sft_active = true;
  }
  if (activate_ctl) {
    register_mods(MOD_BIT(KC_LCTL));
    activate_ctl = false;
    is_ctl_active = true;
  }
  if (activate_gui) {
    register_mods(MOD_BIT(KC_LGUI));
    activate_gui = false;
    is_gui_active = true;
  }
  if (activate_rai) {
    layer_on(_RAISE);
    activate_rai = false;
    is_rai_active = true;
  }
  if (activate_low) {
    layer_on(_LOWER);
    activate_low = false;
    is_low_active = true;
  }
}

void alttm_finished(qk_tap_dance_state_t *state, void *user_data) {
  td_state = cur_dance(state);
  sticky_timer = timer_read32();
  activate_alt = true;
  sticky_key = true;
  switch (td_state) {
  case SINGLE_TAP:
    track_mode = TRACK_MODE_ALT;
    break;
  case SINGLE_HOLD:
    activate_stkeys();
    sticky_key = false;
  }
}
void alttm_reset(qk_tap_dance_state_t *state, void *user_data) {
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    unstick_keys();
  }
}
void sfttm_finished(qk_tap_dance_state_t *state, void *user_data) {
  td_state = cur_dance(state);
  sticky_timer = timer_read32();
  activate_sft = true;
  sticky_key = true;
  switch (td_state) {
  case SINGLE_TAP:
    track_mode = TRACK_MODE_SHIFT;
    break;
  case SINGLE_HOLD:
    activate_stkeys();
    sticky_key = false;
  }
}
void sfttm_reset(qk_tap_dance_state_t *state, void *user_data) {
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    unstick_keys();
  }
}
void ctltm_finished(qk_tap_dance_state_t *state, void *user_data) {
  td_state = cur_dance(state);
  sticky_timer = timer_read32();
  activate_ctl = true;
  sticky_key = true;
  switch (td_state) {
  case SINGLE_TAP:
    track_mode = TRACK_MODE_CTRL;
    break;
  case SINGLE_HOLD:
    activate_stkeys();
    sticky_key = false;
  }
}
void ctltm_reset(qk_tap_dance_state_t *state, void *user_data) {
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    unstick_keys();
  }
}
void guitm_finished(qk_tap_dance_state_t *state, void *user_data) {
  td_state = cur_dance(state);
  sticky_timer = timer_read32();
  activate_gui = true;
  sticky_key = true;
  switch (td_state) {
  case SINGLE_TAP:
    track_mode = TRACK_MODE_GUI;
    break;
  case SINGLE_HOLD:
    activate_stkeys();
    sticky_key = false;
  }
}
void guitm_reset(qk_tap_dance_state_t *state, void *user_data) {
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    unstick_keys();
  }
}
void raitm_finished(qk_tap_dance_state_t *state, void *user_data) {
  td_state = cur_dance(state);
  sticky_timer = timer_read32();
  sensor_x = 0;
  sensor_y = 0;
  activate_rai = true;
  is_rai_active = true;
  sticky_key = true;
  integration_mode = true;
  layer_on(_RAISE);
  switch (td_state) {
  case SINGLE_TAP:
    sensor_x = 0;
    sensor_y = 0;
    break;
  case SINGLE_HOLD:
    integration_mode = true;
    activate_stkeys();
    sticky_key = false;
  }
}
void raitm_reset(qk_tap_dance_state_t *state, void *user_data) {
  sensor_x = 0;
  sensor_y = 0;
  cum_x = 0;
  cum_y = 0;
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    integration_mode = false;
    unstick_keys();
  }
}
void lowtm_finished(qk_tap_dance_state_t *state, void *user_data) {
  td_state = cur_dance(state);
  sticky_timer = timer_read32();
  activate_low = true;
  is_low_active = true;
  sticky_key = true;
  layer_on(_LOWER);
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    activate_stkeys();
    sticky_key = false;
    // prev_track_mode = track_mode;
    // track_mode = SND_BRIGHT_MODE;
  }
}
void lowtm_reset(qk_tap_dance_state_t *state, void *user_data) {
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    unstick_keys();
    // track_mode = prev_track_mode;
  }
}
void esctm_finished(qk_tap_dance_state_t *state, void *user_data) {
  td_state = cur_dance(state);
  sticky_timer = timer_read32();
  /* activate_ctl = true; */
  /* sticky_key = true; */
  switch (td_state) {
  case SINGLE_TAP:
    tap_code(KC_ESC);
    break;
  case SINGLE_HOLD:
    track_mode = TRACK_MODE_CTRL;
    break;
    /* activate_stkeys(); */
    /* sticky_key = false; */
  }
}
void esctm_reset(qk_tap_dance_state_t *state, void *user_data) {
  switch (td_state) {
  case SINGLE_TAP:
    break;
  case SINGLE_HOLD:
    unstick_keys();
  }
}

/***************************
 * Trackball handling
 **************************/

void pointing_device_init(void) {
  if (!is_keyboard_master())
    return;
  pmw3360_init();
  pmw3360_set_cpi(PMW3360_CPI);
}

int max(int num1, int num2) { return (num1 > num2) ? num1 : num2; }
int min(int num1, int num2) { return (num1 > num2) ? num2 : num1; }

int8_t sign(int x) { return (x > 0) - (x < 0); }
int8_t CLAMP_HID(int value) {
  return value < -127 ? -127 : value > 127 ? 127 : value;
}

void tap_code_fast(uint8_t code) {
  register_code(code);
  // Dont do this:
  // if (code == KC_CAPS) {
  //	 wait_ms(TAP_HOLD_CAPS_DELAY);
  // } else {
  //	 wait_ms(TAP_CODE_DELAY);
  // }
  unregister_code(code);
}

void tap_tb(uint8_t keycode0, uint8_t keycode1, uint8_t keycode2,
            uint8_t keycode3) {
  if (abs(cum_x) + abs(cum_y) >= cur_factor) {
    if (abs(cum_x) > abs(cum_y)) {
      if (cum_x > 0) {
        for (int8_t i = 0; i <= (abs(cum_x) + abs(cum_y)) / cur_factor; i++) {
          tap_code_fast(keycode0);
          cum_x = max(cum_x - cur_factor, 0);
        }
        cum_y = 0;
      } else {
        for (int8_t i = 0; i <= (abs(cum_x) + abs(cum_y)) / cur_factor; i++) {
          tap_code_fast(keycode1);
          cum_x = min(cum_x + cur_factor, 0);
        }
        cum_y = 0;
      }
    } else {
      if (cum_y > 0) {
        for (int8_t i = 0; i <= (abs(cum_x) + abs(cum_y)) / cur_factor; i++) {
          tap_code_fast(keycode2);
          cum_y = max(cum_y - cur_factor, 0);
        }
        cum_x = 0;
      } else {
        for (int8_t i = 0; i <= (abs(cum_x) + abs(cum_y)) / cur_factor; i++) {
          tap_code_fast(keycode3);
          cum_y = min(cum_y + cur_factor, 0);
        }
        cum_x = 0;
      }
    }
  }
}

void handle_pointing_device_modes(void) {
  report_mouse_t mouse_report = pointing_device_get_report();

  if (track_mode == CURSOR_MODE) {
    if (integration_mode)
      cur_factor = cursor_multiplier_inte;
    else
      cur_factor = cursor_multiplier;
    mouse_report.x = CLAMP_HID(sensor_x * cur_factor / 100);
    mouse_report.y = CLAMP_HID(-sensor_y * cur_factor / 100);
  } else {
    // accumulate movement until threshold reached
    cum_x += sensor_x;
    cum_y += sensor_y;
    if (track_mode == CARRET_MODE) {
      if (integration_mode)
        cur_factor = carret_threshold_inte;
      else
        cur_factor = carret_threshold;
      tap_tb(KC_RIGHT, KC_LEFT, KC_UP, KC_DOWN);

    } else if (track_mode == SCROLL_MODE) {
      if (integration_mode)
        cur_factor = scroll_threshold_inte;
      else
        cur_factor = scroll_threshold;
      if (abs(cum_x) + abs(cum_y) >= cur_factor) {
        if (abs(cum_x) > abs(cum_y)) {
          mouse_report.h = sign(cum_x) * (abs(cum_x) + abs(cum_y)) / cur_factor;
        } else {
          mouse_report.v = sign(cum_y) * (abs(cum_x) + abs(cum_y)) / cur_factor;
        }
        cum_x = 0;
        cum_y = 0;
      }
    } else { // sound vol/brightness (3)
      cur_factor = carret_threshold;
      tap_tb(KC_BRIGHTNESS_UP, KC_BRIGHTNESS_DOWN, KC_AUDIO_VOL_UP,
             KC_AUDIO_VOL_DOWN);
    }
  }
  pointing_device_set_report(mouse_report);
  pointing_device_send();
}

void get_sensor_data(void) {
  if (!is_keyboard_master())
    return;
  report_pmw3360_t pmw_report = pmw3360_read_burst();

  if (integration_mode) {
    sensor_x += pmw_report.dx;
    sensor_y += pmw_report.dy;
  } else {
    sensor_x = pmw_report.dx;
    sensor_y = pmw_report.dy;
  }
}

void pointing_device_task(void) {
#ifndef POLLING
  if (is_keyboard_master() && integration_mode)
    handle_pointing_device_modes();
#else
  get_sensor_data();
  handle_pointing_device_modes();
#endif
  if (sticky_key && timer_elapsed(sticky_timer) > STICKY_TERM) {
    unstick_keys();
    activate_alt = false;
    activate_sft = false;
    activate_ctl = false;
    activate_gui = false;
    activate_rai = false;
    activate_low = false;
    sticky_key = false;
  }
}

#ifndef POLLING
ISR(INT2_vect) {
  get_sensor_data();
  handle_pointing_device_modes();
}
#endif

/***************************
 * process_record_kb
 **************************/

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
  if (!process_record_user(keycode, record)) {
    return false;
  }

  switch (keycode) {
    // no repetitive ::: with holding
  case KC_SCLN_INV:
    if (record->event.pressed) {
      if (is_sft_active) {
        unregister_mods(MOD_BIT(KC_LSFT));
        tap_code(KC_SCLN);
        register_mods(MOD_BIT(KC_LSFT));
      } else {
        register_mods(MOD_BIT(KC_LSFT));
        tap_code(KC_SCLN);
        unregister_mods(MOD_BIT(KC_LSFT));
      }
    }
    return false;

    // ; and : gets confused in some corner cases
    // case KC_SCLN_INV:
    //	if (record->event.pressed) {
    //	if (is_sft_active) {
    //		unregister_mods(MOD_BIT(KC_LSFT));
    //		register_code(KC_SCLN);
    //	} else {
    //		register_mods(MOD_BIT(KC_LSFT));
    //		register_code(KC_SCLN);
    //	}
    //	} else {
    //	if (is_sft_active) {
    //		unregister_code(KC_SCLN);
    //		register_mods(MOD_BIT(KC_LSFT));
    //	} else {
    //		unregister_code(KC_SCLN);
    //		unregister_mods(MOD_BIT(KC_LSFT));
    //	}
    //	}
    //	return false;

  case KC_TILD_MY:
    if (record->event.pressed) {
      tap_code16(KC_TILD);
      tap_code(KC_SPC);
    }
    return false;

  case KC_QUOT_MY:
    if (record->event.pressed) {
      tap_code(KC_QUOT);
      tap_code(KC_SPC);
    }
    return false;

  // handle mouse
  case KC_BTN1:
    on_mouse_button(MOUSE_BTN1, record->event.pressed);
    return false;

  case KC_BTN2:
    on_mouse_button(MOUSE_BTN2, record->event.pressed);
    return false;

  case KC_BTN3:
    on_mouse_button(MOUSE_BTN3, record->event.pressed);
    return false;

  case KC_BTN4:
    on_mouse_button(MOUSE_BTN4, record->event.pressed);
    return false;

  case KC_BTN5:
    on_mouse_button(MOUSE_BTN5, record->event.pressed);
    return false;

  case KC_CPI_DOWN:
    if (cursor_multiplier > CPI_STEP_SENS)
      cursor_multiplier = cursor_multiplier - CPI_STEP_SENS;
    return false;

  case KC_CPI_STD:
    cursor_multiplier = PMW3360_CPI;
    return false;

  case KC_CPI_UP:
    cursor_multiplier = cursor_multiplier + CPI_STEP_SENS;
    return false;

  case KC_SMO_SC:
    if (record->event.pressed) {
      if (smooth_scroll) {
        scroll_threshold = scroll_threshold * regular_smoothscroll_factor;
        scroll_threshold_inte =
            scroll_threshold_inte * regular_smoothscroll_factor;
        smooth_scroll = false;
      } else {
        scroll_threshold = scroll_threshold / regular_smoothscroll_factor;
        scroll_threshold_inte =
            scroll_threshold_inte / regular_smoothscroll_factor;
        smooth_scroll = true;
      }
    }

  default:
    return true;
  }
}

// debug
//		char snum[5];
//		itoa(variable, snum, 10);
//		SEND_STRING(" ");
//		send_string(snum);
