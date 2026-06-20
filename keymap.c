/*
Copyright 2022 @Yowkees
Copyright 2022 MURAOKA Taro (aka KoRoN, @kaoriya)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include QMK_KEYBOARD_H

#include "quantum.h"

#define HOMEROW_MASK ((1U << 1) | (1U << 5))
#define IS_HOMEROW(k, r, m) (                       \
    (HOMEROW_MASK & (1U << ((r).event.key.row)))    \
    && IS_QK_MOD_TAP((k))                           \
    && (((k) >> 8) & (m))                           \
)

#define UNILATERAL_MASK(n, m) (     \
    (n) == 1 ? (m) :                \
    (n) == 5 ? ((m) << 4) : 0x00    \
)

#define IS_UNILATERAL_INPUT(r, i, m) (          \
    (UNILATERAL_MASK((r).event.key.row, (m)))   \
    & (1U << (i).event.key.row)                 \
)

typedef struct {
    uint8_t index;
    uint8_t bitmask;
} tap_bit_t;

#define TAP_BIT_FROM_KEYCODE(k)                                 \
    ((tap_bit_t){                                               \
        .index   = QK_MOD_TAP_GET_TAP_KEYCODE((k)) / 8,         \
        .bitmask = (1U << QK_MOD_TAP_GET_TAP_KEYCODE((k)) % 8)  \
     })

static uint8_t pressed_keys[32];

static uint16_t    inter_keycode;
static keyrecord_t inter_record;

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (IS_QK_LAYER_TAP(keycode)) {
        uint8_t layer_bit = (keycode >> 8) & 0x0F;
        uint8_t n         = (layer_bit & 0x01) + (layer_bit & 0x02) + (layer_bit & 0x04);
        if (record->event.pressed) {
            layer_on(n);
        } else {
            layer_off(n);
        }
    }
    if (record->event.pressed) {
        inter_keycode = keycode;
        inter_record  = *record;
    } else {
        tap_bit_t tap = TAP_BIT_FROM_KEYCODE(keycode);
        if (pressed_keys[tap.index] & tap.bitmask) {
            pressed_keys[tap.index] &= ~tap.bitmask;
            record->tap.count++;
        }
    }
    return true;
}

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    if (IS_QK_LAYER_TAP(keycode)
        || IS_HOMEROW(keycode, *record, MOD_LALT | MOD_LGUI)) {
        return TAPPING_TERM << 1;
    }
    if (IS_QK_ONE_SHOT_MOD(keycode)) {
        return TAPPING_TERM << 2;
    }
    return TAPPING_TERM;
}

uint16_t get_quick_tap_term(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case LT(0, KC_F20):
        case LT(2, KC_SPC):
        case LT(4, KC_ENT):
            return 0;
    }
    return QUICK_TAP_TERM;
}

bool get_permissive_hold(uint16_t keycode, keyrecord_t *record) {
    return IS_QK_LAYER_TAP(keycode)
        || IS_QK_MOD_TAP(keycode)
        || IS_QK_ONE_SHOT_MOD(keycode);
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record) {
    if ((!IS_UNILATERAL_INPUT(*record, inter_record, 0x02)
         && IS_QK_MOD_TAP(keycode)
         && (inter_keycode & 0xFF) <= KC_Z)) {
        tap_bit_t tap = TAP_BIT_FROM_KEYCODE(keycode);
        pressed_keys[tap.index] |= tap.bitmask;
        record->tap.interrupted = false;
        record->tap.count++;
        return true;
    }
    return false;
}

enum arrowkeys_types {
    TAB_MORPH = 1,
    VOL_MORPH,
    WWW_MORPH,
    CTRL_TAB_MORPH,
    CTRL_YanZ_MORPH,
    FOUR_MOVES_MORPH,
};

uint16_t morph_type       = 0;
uint16_t morph_code       = 0;
bool first_iteration      = false;
bool arrowkeys_registered = false;

void four_moves(uint16_t keycode) {
    for (uint8_t i = 0; i < 4; i++) {
        tap_code(keycode);
    }
}

#define BUFFER_SIZE 16

typedef struct {
    uint16_t buffer[BUFFER_SIZE];
    uint8_t  front;
    uint8_t  rear;
    uint8_t  count;
} mt_cycle_t;

bool enqueue(mt_cycle_t *buf, uint16_t data) {
    if (buf->count >= BUFFER_SIZE) {
        return false;
    }
    buf->buffer[buf->rear] = data;
    buf->rear = (buf->rear + 1) % BUFFER_SIZE;
    buf->count++;
    return true;
}

bool dequeue(mt_cycle_t *buf, uint16_t *data) {
    if (buf->count <= 0) {
        return false;
    }
    *data = buf->buffer[buf->front];
    buf->front = (buf->front + 1) % BUFFER_SIZE;
    buf->count--;
    return true;
}

mt_cycle_t lmts = {{0}, 0, 0, 0};
mt_cycle_t rmts = {{0}, 0, 0, 0};

void mts_hold_on(mt_cycle_t *mts) {
    uint16_t poped_key;
    uint8_t  pended_mods = 0;
    while (dequeue(mts, &poped_key)) {
        uint8_t mod  = (poped_key >> 8) & 0x1F;
        pended_mods |= (mod & 0x10) ? (mod << 4) : mod;
    }
    register_mods(pended_mods);
}

void mts_hold_on_all(void) {
     mts_hold_on(&lmts);
     mts_hold_on(&rmts);
}

void send_report_user(uint16_t keycode) {
    static const uint16_t brcts[][2] = {
        { S(KC_QUOT), S(KC_QUOT) },
        { S(KC_LBRC), S(KC_RBRC) },
        { S(KC_COMM), S(KC_DOT)  },
        { S(KC_9),    S(KC_0)    },
        { KC_QUOT,    KC_QUOT    },
        { KC_LBRC,    KC_RBRC    },
        { KC_GRV,     KC_GRV     },
        { 0,          -1         },
    };
    static const uint8_t null_id = ARRAY_SIZE(brcts) - 1;
    static uint8_t reception_id  = null_id;
    keycode = keycode & 0xFF;

    if (!get_highest_layer(layer_state)) {
        reception_id = null_id;
        return;
    }
    if (get_weak_mods() == MOD_LSFT
        || is_caps_word_on()) {
        keycode |= MOD_LSFT << 8;
    }
    clear_weak_mods();
    if (keycode == brcts[reception_id][1]) {
        tap_code(KC_LEFT);
        reception_id = null_id;
        return;
    }
    for (reception_id = 0; reception_id < null_id; reception_id++) {
        if (keycode == brcts[reception_id][0]) {
            return;
        }
    }
}

void procoss_pended_keys(uint16_t keycode, keyrecord_t record) {
    mt_cycle_t *same  = record.event.key.row == 1 ? &lmts : &rmts;
    mt_cycle_t *other = record.event.key.row == 5 ? &lmts : &rmts;
    uint16_t poped_key;

    mts_hold_on(other);
    while (dequeue(same, &poped_key)) {
        if (is_caps_word_on()) {
            add_weak_mods(MOD_LSFT);
        }
        tap_code(QK_MOD_TAP_GET_TAP_KEYCODE(poped_key));
        send_report_user(poped_key);
        if (keycode == poped_key) {
            return;
        }
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (IS_QK_MOD_TAP(keycode)) {
        if (IS_LAYER_ON(2)) {
            uint8_t saved_mods = get_mods();
            caps_word_on();
            set_mods(saved_mods);
        }
        if (record->event.pressed) {
            if (record->tap.count) {
                procoss_pended_keys(keycode, *record);
            } else {
                enqueue(record->event.key.row == 1 ? &lmts : &rmts, keycode);
                return false;
            }
        } else if (!record->tap.count) {
            uint8_t mod = (keycode >> 8) & 0x1F;
            unregister_mods((mod & 0x10) ? (mod << 4) : mod);
            procoss_pended_keys(keycode, *record);
            return false;
        }
        return true;
    }

    switch (keycode) {
        case KC_MS_BTN1 ... KC_MS_BTN3:
            if (record->event.pressed
                && (lmts.count || rmts.count)) {
                mts_hold_on_all();
                report_mouse_t mouse_report = pointing_device_get_report();
                mouse_report.buttons |= MOUSE_BTN1 << (keycode - KC_MS_BTN1);
                pointing_device_set_report(mouse_report);
                return false;
            }
            return true;
    }

    mts_hold_on_all();

    switch (keycode) {
        case LT(3, 0):
            if (record->tap.count) {
                if (record->event.pressed) {
                    set_oneshot_layer(3, ONESHOT_START);
                }
                return false;
            }
            break;
        case LT(0, KC_APP):
            if (!record->tap.count) {
                if (record->event.pressed) {
                    register_code(KC_DEL);
                } else {
                    unregister_code(KC_DEL);
                }
                return false;
            }
            break;
        case KC_INT4:
            if (record->event.pressed) {
                add_weak_mods(MOD_LGUI);
                register_code(KC_SLSH);
            } else {
                unregister_code(KC_SLSH);
            }
            return false;
        case KC_F14:
            if (record->event.pressed) {
                SEND_STRING(SS_LCTL("c") SS_LGUI("r") SS_DELAY(200)
                        "https://web.archive.org/web/" SS_LCTL("v") SS_TAP(X_ENTER));
            }
            return false;
        case LT(0, KC_F15) ... LT(0, KC_F19):
            keycode          = keycode & 0xFF;
            uint8_t index    = keycode - KC_F15;
            uint8_t mod      =
                    keycode == KC_F16 ? MOD_LALT :
                    keycode == KC_F17 ? MOD_LSFT :
                    keycode == KC_F18 ? MOD_LCTL : 0;
            static uint16_t timer[5]    = {0};
            static uint8_t  KC_EDIT     = 0;
            static bool edit_registered = false;

            if (edit_registered) {
                unregister_code(KC_EDIT);
                edit_registered = false;
            }
            if (record->event.pressed) {
                KC_EDIT = 0;
                if (record->tap.count) {
                    if (timer_elapsed(timer[index]) > (QUICK_TAP_TERM << 3)) {
                        if (mod) {
                            if (get_mods() & mod) {
                                unregister_mods(mod);
                            } else {
                                register_mods(mod);
                                if (keycode == KC_F16) {
                                    tap_code(KC_TAB);
                                }
                            }
                        } else {
                            morph_type  =
                                keycode == KC_F15 ? CTRL_YanZ_MORPH : TAB_MORPH;
                        }
                    } else {
                        if (keycode == KC_F16) {
                            tap_code(KC_ESC);
                        }
                        unregister_mods(mod);
                        KC_EDIT     =
                            keycode == KC_F17 ? KC_V : 0;
                        morph_type  =
                            keycode == KC_F15 ? CTRL_YanZ_MORPH :
                            keycode == KC_F16 ? WWW_MORPH :
                            keycode == KC_F17 ? 0 :
                            keycode == KC_F18 ? CTRL_TAB_MORPH : VOL_MORPH;
                    }
                } else {
                    tap_code(keycode == KC_F17 ? KC_F15 :
                             keycode == KC_F18 ? KC_F16 : 0);
                    KC_EDIT     =
                        keycode == KC_F16 ? KC_C :
                        keycode == KC_F19 ? KC_X : 0;
                    morph_type  =
                        keycode == KC_F15 ? FOUR_MOVES_MORPH : 0;
                }
                if (KC_EDIT) {
                    uint8_t saved_mods = get_mods();
                    del_mods(saved_mods);
                    add_weak_mods(MOD_LCTL);
                    register_code(KC_EDIT);
                    set_mods(saved_mods);
                    edit_registered = true;
                }
                timer[index] = timer_read();
            }
            return false;
        case LT(0, KC_F20):
            if (record->event.pressed) {
                layer_move(record->tap.count ? (get_highest_layer(layer_state) % 2 + 1) : 4);
            }
            return false;
        case KC_F21:
            if (record->event.pressed) {
                SEND_STRING(". ");
                add_oneshot_mods(MOD_LSFT);
            }
            return false;
        case LT(0, KC_LNG1):
        case LT(0, KC_LNG2):
            if (record->event.pressed) {
                tap_code((keycode & 0xFF) == KC_LNG2 ? KC_F13 : KC_F14);
                if (record->tap.count) {
                    caps_word_off();
                } else {
                    caps_word_on();
                }
            }
            return false;
        case KC_UP:
        case KC_DOWN:
        case KC_LEFT:
        case KC_RGHT:
            if (arrowkeys_registered) {
                unregister_code(morph_code);
                arrowkeys_registered = false;
                if (!record->event.pressed) {
                    return false;
                }
            }
            if (record->event.pressed) {
                uint8_t saved_mods = get_mods();

                switch (keycode) {
                    case KC_UP:
                        switch (morph_type) {
                            case VOL_MORPH:
                            case WWW_MORPH:
                                return true;
                        }
                    case KC_DOWN:
                        switch (morph_type) {
                            case TAB_MORPH:
                            case CTRL_YanZ_MORPH:
                                return true;
                        }
                }
                switch (morph_type) {
                    case TAB_MORPH:
                    case CTRL_TAB_MORPH:
                        switch (keycode) {
                            case KC_UP:   morph_code = KC_PGUP;
                                break;
                            case KC_DOWN: morph_code = KC_PGDN;
                                break;
                            case KC_LEFT: add_weak_mods(MOD_LSFT);
                            case KC_RGHT: morph_code = KC_TAB;
                                if (morph_type == CTRL_TAB_MORPH) {
                                    del_mods(saved_mods);
                                    add_weak_mods(MOD_LCTL);
                                }
                        }
                        break;
                    case CTRL_YanZ_MORPH:
                        del_mods(saved_mods);
                        add_weak_mods(MOD_LCTL);
                        morph_code =
                            (keycode == KC_LEFT) ? KC_Z    : KC_Y;
                        break;
                    case VOL_MORPH:
                        morph_code =
                            (keycode == KC_DOWN) ? KC_MUTE :
                            (keycode == KC_LEFT) ? KC_VOLD : KC_VOLU;
                        break;
                    case WWW_MORPH:
                        morph_code =
                            (keycode == KC_DOWN) ? KC_WHOM :
                            (keycode == KC_LEFT) ? KC_WBAK : KC_WFWD;
                        break;
                    case FOUR_MOVES_MORPH:
                        four_moves(keycode);
                        morph_code = keycode;
                        first_iteration = true;
                        arrowkeys_registered = true;
                        return false;
                    default:
                        return true;
                }
                register_code(morph_code);
                set_mods(saved_mods);
                arrowkeys_registered = true;
                return false;
            }
            break;
        case KC_COMM:
            static bool dot_registered = false;
            uint8_t     mod_state      = get_mods();
            uint8_t     oneshot_mods   = get_oneshot_mods();

            if (record->event.pressed) {
                if ((mod_state | oneshot_mods) & MOD_MASK_SHIFT) {
                    del_oneshot_mods(MOD_MASK_SHIFT);
                    del_mods(MOD_MASK_SHIFT);
                    register_code(KC_DOT);
                    set_mods(mod_state);
                    dot_registered = true;
                    return false;
                }
            } else if (dot_registered) {
                unregister_code(KC_DOT);
                dot_registered = false;
                return false;
            }
            break;
        case LT(4, KC_ENT):
            static bool is_layer4_enabled = false;
            if (!record->tap.count) {
                is_layer4_enabled = record->event.pressed;
            }
            break;
        case LT(2, KC_SPC):
            if (!record->tap.count) {
                layer_clear();
                if (!record->event.pressed) {
                    unregister_mods(MOD_MEH);
                    if (morph_type) {
                        if (arrowkeys_registered) {
                            unregister_code(morph_code);
                            arrowkeys_registered = false;
                        }
                        morph_type = 0;
                    }
                    if (is_layer4_enabled) {
                        layer_on(4);
                    }
                }
            }
        case KC_QUOT:
            caps_word_off();
            break;
    }
    if (!record->event.pressed) {
        clear_oneshot_layer_state(ONESHOT_PRESSED);
    }
    return true;
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        send_report_user(keycode);
    }
}

#define REPEAT_DELAY 500
#define REPEAT_INTERVAL 40

void matrix_scan_user(void) {
    static uint16_t repeat_timer = 0;

    if (morph_type == FOUR_MOVES_MORPH
        && arrowkeys_registered) {
        if (repeat_timer == 0) {
            repeat_timer = timer_read();
        } else if (timer_elapsed(repeat_timer)
                > (first_iteration ? REPEAT_DELAY : REPEAT_INTERVAL)) {
            four_moves(morph_code);
            first_iteration = false;
            repeat_timer = timer_read();
        }
    } else {
        repeat_timer = 0;
    }
}

enum combos {
    CMB_APP,
    CMB_LNG1,
    CMB_LNG2,
    CMB_OSL1,
    CMB_MOL3,
    CMB_INT4,
    CMB_PSCR,
    CMB_OS_CTL,
    CMB_OS_SFT,
    CMB_OS_ALT,
    CMB_MS_BTN1,
    CMB_MS_BTN2,
    CMB_MS_BTN3,
};

const uint16_t PROGMEM cmb_app[]     = {KC_Z,         KC_M,         COMBO_END};
const uint16_t PROGMEM cmb_lng1[]    = {LCTL_T(KC_S), KC_G,         COMBO_END};
const uint16_t PROGMEM cmb_lng2[]    = {KC_Y,         RCTL_T(KC_E), COMBO_END};
const uint16_t PROGMEM cmb_osl1[]    = {KC_Z,  KC_M,  KC_C,         COMBO_END};
const uint16_t PROGMEM cmb_mol3[]    = {KC_M,         KC_C,         COMBO_END};
const uint16_t PROGMEM cmb_int4[]    = {KC_Z,         KC_C,         COMBO_END};
const uint16_t PROGMEM cmb_pscr[]    = {KC_L,  KC_D,  KC_W,         COMBO_END};
const uint16_t PROGMEM cmb_os_ctl[]  = {KC_D,         KC_W,         COMBO_END};
const uint16_t PROGMEM cmb_os_sft[]  = {KC_L,         KC_W,         COMBO_END};
const uint16_t PROGMEM cmb_os_alt[]  = {KC_L,         KC_D,         COMBO_END};
const uint16_t PROGMEM cmb_ms_btn1[] = {LSFT_T(KC_T), LCTL_T(KC_S), COMBO_END};
const uint16_t PROGMEM cmb_ms_btn2[] = {LALT_T(KC_R), LSFT_T(KC_T), COMBO_END};
const uint16_t PROGMEM cmb_ms_btn3[] = {LALT_T(KC_R), LCTL_T(KC_S), COMBO_END};

combo_t key_combos[] = {
    [CMB_APP]        = COMBO(cmb_app,       LT(0, KC_APP)),
    [CMB_LNG1]       = COMBO(cmb_lng1,      LT(0, KC_LNG1)),
    [CMB_LNG2]       = COMBO(cmb_lng2,      LT(0, KC_LNG2)),
    [CMB_OSL1]       = COMBO(cmb_osl1,      OSL(1)),
    [CMB_MOL3]       = COMBO(cmb_mol3,      LT(3, 0)),
    [CMB_INT4]       = COMBO(cmb_int4,      KC_INT4),
    [CMB_PSCR]       = COMBO(cmb_pscr,      KC_PSCR),
    [CMB_OS_CTL]     = COMBO(cmb_os_ctl,    OSM(MOD_LCTL)),
    [CMB_OS_SFT]     = COMBO(cmb_os_sft,    OSM(MOD_LSFT)),
    [CMB_OS_ALT]     = COMBO(cmb_os_alt,    OSM(MOD_LALT)),
    [CMB_MS_BTN1]    = COMBO(cmb_ms_btn1,   KC_MS_BTN1),
    [CMB_MS_BTN2]    = COMBO(cmb_ms_btn2,   KC_MS_BTN2),
    [CMB_MS_BTN3]    = COMBO(cmb_ms_btn3,   KC_MS_BTN3),
};

bool caps_word_press_user(uint16_t keycode) {
    switch (keycode) {
        case KC_A ... KC_Z:
        case KC_QUOT:
        case KC_MINS:
            add_weak_mods(MOD_LSFT);
            return true;
        case KC_1 ... KC_0:
            if (IS_LAYER_ON(2)) {
                add_weak_mods(MOD_LSFT);
            }
        case KC_EQL:
        case KC_DEL:
        case KC_BSPC:
        case KC_SLSH:
            return true;
    }
    return false;
}

#define COMBO_REF_DEFAULT 0
#define COMBO_REF_MASK ((1U << 2) | (1U << 4))

uint8_t combo_ref_from_layer(uint8_t layer) {
    uint16_t current_layer = get_highest_layer(layer_state);
    if (COMBO_REF_MASK & (1U << current_layer)) {
        return current_layer;
    }
    return COMBO_REF_DEFAULT;
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    clear_weak_mods();
    if (abs(mouse_report.x) + abs(mouse_report.y)) {
        mts_hold_on_all();
    }
    return mouse_report;
}

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  // keymap for default (VIA)
  [0] = LAYOUT_universal(
    KC_Q     , KC_W     , KC_E     , KC_R     , KC_T     ,                            KC_Y     , KC_U     ,
 KC_I     , KC_O     , KC_P     ,
    KC_A     , KC_S     , KC_D     , KC_F     , KC_G     ,                            KC_H     , KC_J     ,
 KC_K     , KC_L     , KC_MINS  ,
    KC_Z     , KC_X     , KC_C     , KC_V     , KC_B     ,                            KC_N     , KC_M     ,
 KC_COMM  , KC_DOT   , KC_SLSH  ,
    KC_LCTL  , KC_LGUI  , KC_LALT  ,LSFT_T(KC_LNG2),LT(1,KC_SPC),LT(3,KC_LNG1),KC_BSPC,LT(2,KC_ENT),LSFT_T(
KC_LNG2),KC_RALT,KC_RGUI, KC_RSFT
  ),

  [1] = LAYOUT_universal(
    KC_F1    , KC_F2    , KC_F3    , KC_F4    , KC_RBRC  ,                            KC_F6    , KC_F7    ,
 KC_F8    , KC_F9    , KC_F10   ,
    KC_F5    , KC_EXLM  , S(KC_6)  ,S(KC_INT3), S(KC_8)  ,                           S(KC_INT1), KC_BTN1  ,
 KC_PGUP  , KC_BTN2  , KC_SCLN  ,
    S(KC_EQL),S(KC_LBRC),S(KC_7)   , S(KC_2)  ,S(KC_RBRC),                            KC_LBRC  , KC_DLR   ,
 KC_PGDN  , KC_BTN3  , KC_F11   ,
    KC_INT1  , KC_EQL   , S(KC_3)  , _______  , _______  , _______  ,      TO(2)    , TO(0)    , _______  ,
 KC_RALT  , KC_RGUI  , KC_F12
  ),

  [2] = LAYOUT_universal(
    KC_TAB   , KC_7     , KC_8     , KC_9     , KC_MINS  ,                            KC_NUHS  , _______  ,
 KC_BTN3  , _______  , KC_BSPC  ,
   S(KC_QUOT), KC_4     , KC_5     , KC_6     ,S(KC_SCLN),                            S(KC_9)  , KC_BTN1  ,
 KC_UP    , KC_BTN2  , KC_QUOT  ,
    KC_SLSH  , KC_1     , KC_2     , KC_3     ,S(KC_MINS),                           S(KC_NUHS), KC_LEFT  ,
 KC_DOWN  , KC_RGHT  , _______  ,
    KC_ESC   , KC_0     , KC_DOT   , KC_DEL   , KC_ENT   , KC_BSPC  ,      _______  , _______  , _______  ,
 _______  , _______  , _______
  ),

  [3] = LAYOUT_universal(
    RGB_TOG  , AML_TO   , AML_I50  , AML_D50  , _______  ,                            _______  , _______  ,
 SSNP_HOR , SSNP_VRT , SSNP_FRE ,
    RGB_MOD  , RGB_HUI  , RGB_SAI  , RGB_VAI  , SCRL_DVI ,                            _______  , _______  ,
 _______  , _______  , _______  ,
    RGB_RMOD , RGB_HUD  , RGB_SAD  , RGB_VAD  , SCRL_DVD ,                            CPI_D1K  , CPI_D100 ,
 CPI_I100 , CPI_I1K  , KBC_SAVE ,
    QK_BOOT  , KBC_RST  , _______  , _______  , _______  , _______  ,      _______  , _______  , _______  ,
 _______  , KBC_RST  , QK_BOOT
  ),
};
// clang-format on

layer_state_t layer_state_set_user(layer_state_t state) {
    // Auto enable scroll mode when the highest layer is 4
    keyball_set_scroll_mode(get_highest_layer(state) == 4);
#ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
    keyball_handle_auto_mouse_layer_change(state);
#endif
    return state;
}

#ifdef OLED_ENABLE

#    include "lib/oledkit/oledkit.h"

void oledkit_render_info_user(void) {
    keyball_oled_render_keyinfo();
    keyball_oled_render_ballinfo();
    keyball_oled_render_layerinfo();
}
#endif
