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
static uint8_t momentary_layer = 0;

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (IS_QK_LAYER_TAP(keycode)) {
        momentary_layer = (keycode >> 8) & 0x0F;
        if (momentary_layer == 4) {
            keyball_set_scroll_mode(record->event.pressed);
        }
        if (!record->event.pressed) {
            momentary_layer = 0;
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
    if (IS_LAYER_ON(2)
        && IS_QK_LAYER_TAP(keycode)
        && ((keycode >> 8) & 0x0F) == 0) {
        return TAPPING_TERM << 2;
    }
    if (IS_QK_LAYER_TAP(keycode)
        || IS_QK_ONE_SHOT_MOD(keycode)
        || IS_HOMEROW(keycode, *record, MOD_HYPR & ~MOD_LSFT)) {
        return TAPPING_TERM << 1;
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
    return IS_QK_MOD_TAP(keycode)
        || IS_QK_LAYER_TAP(keycode)
        || IS_QK_ONE_SHOT_MOD(keycode);
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record) {
    if (momentary_layer == 0
        && IS_QK_MOD_TAP(keycode)
        && (inter_keycode & 0xFF) <= KC_Z
        && !IS_UNILATERAL_INPUT(*record, inter_record, 0x02)) {
        tap_bit_t tap = TAP_BIT_FROM_KEYCODE(keycode);
        pressed_keys[tap.index] |= tap.bitmask;
        record->tap.interrupted = false;
        record->tap.count++;
        return true;
    }
    return false;
}

enum custom_keycodes {
    KC_MY_BTN1 = 1,
    KC_MY_BTN2,
    KC_MY_BTN3,
};

enum navkey_types {
    TAB_MORPH = 1,
    VOL_MORPH,
    WWW_MORPH,
    CTRL_YZ_MORPH,
    CTRL_TAB_MORPH,
    MOVES_X4_MORPH,
};

uint16_t morph_type    = 0;
uint16_t morph_code    = 0;
bool first_iteration   = false;
bool navkey_registered = false;

void send_four_times(uint16_t keycode) {
    for (uint8_t i = 0; i < 4; i++) {
        tap_code(keycode);
    }
}

void within_word(uint16_t keycode) {
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
    static uint8_t  reception_id = null_id;
    keycode = keycode & 0xFF;

    if (is_caps_word_on()
        || get_weak_mods() == MOD_LSFT) {
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

#define BUFFER_SIZE 16

typedef struct {
    uint16_t buffer[BUFFER_SIZE];
    uint8_t  front;
    uint8_t  rear;
    uint8_t  count;
} mt_queue_t;

bool enqueue(mt_queue_t *buf, uint16_t data) {
    if (buf->count >= BUFFER_SIZE) {
        return false;
    }
    buf->buffer[buf->rear] = data;
    buf->rear = (buf->rear + 1) % BUFFER_SIZE;
    buf->count++;
    return true;
}

bool dequeue(mt_queue_t *buf, uint16_t *data) {
    if (buf->count <= 0) {
        return false;
    }
    *data = buf->buffer[buf->front];
    buf->front = (buf->front + 1) % BUFFER_SIZE;
    buf->count--;
    return true;
}

mt_queue_t lmts = {{0}, 0, 0, 0};
mt_queue_t rmts = {{0}, 0, 0, 0};

void enable_mts_mods(mt_queue_t *mts) {
    uint16_t poped_key;
    uint8_t  pended_mods = 0;
    while (dequeue(mts, &poped_key)) {
        uint8_t mod  = (poped_key >> 8) & 0x1F;
        pended_mods |= (mod & 0x10) ? (mod << 4) : mod;
    }
    register_mods(pended_mods);
}

void enable_all_mts_mods(void) {
     enable_mts_mods(&lmts);
     enable_mts_mods(&rmts);
}

void send_mts_taps(mt_queue_t *mts, uint16_t keycode) {
    uint16_t poped_key;
    while (dequeue(mts, &poped_key)) {
        if (is_caps_word_on()) {
            add_weak_mods(MOD_LSFT);
        }
        tap_code(QK_MOD_TAP_GET_TAP_KEYCODE(poped_key));
        within_word(poped_key);
        if (poped_key == keycode) {
            return;
        }
    }
}

void procoss_pended_keys(uint16_t keycode, keyrecord_t record, const uint8_t mask) {
    uint8_t row            = record.event.key.row;
    mt_queue_t *same_hand  = mask        & (1U << row) ? &lmts : &rmts;
    mt_queue_t *other_hand = (mask << 4) & (1U << row) ? &lmts : &rmts;
    enable_mts_mods(other_hand);
    send_mts_taps(same_hand, keycode);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    static bool layer4_is_held;
    if (layer4_is_held && !IS_LAYER_ON(2)) {
        layer_on(4);
    }

    if (IS_QK_MOD_TAP(keycode)) {
        if (IS_LAYER_ON(2)) {
            uint8_t saved_mods = get_mods();
            caps_word_on();
            set_mods(saved_mods);
        }
        if (record->event.pressed) {
            if (record->tap.count) {
                procoss_pended_keys(keycode, *record, 0x02);
            } else {
                enqueue(record->event.key.row == 1 ? &lmts : &rmts, keycode);
                return false;
            }
        } else if (!record->tap.count) {
            uint8_t mod = (keycode >> 8) & 0x1F;
            unregister_mods(mod & 0x10 ? (mod << 4) : mod);
            procoss_pended_keys(keycode, *record, 0x02);
            return false;
        }
        return true;
    }

    if (0x55 & (1U << record->event.key.row)) {
        procoss_pended_keys(keycode, *record, 0x05);
    }

    enable_all_mts_mods();

    switch (keycode) {
        case LT(3, KC_NO):
            if (record->tap.count) {
                if (record->event.pressed) {
                    set_oneshot_layer(3, ONESHOT_START);
                }
                return false;
            }
            return true;
        default:
            if (record->event.pressed) {
                clear_oneshot_layer_state(ONESHOT_PRESSED);
            }
    }

    switch (keycode) {
        case KC_MY_BTN1 ... KC_MY_BTN3:
            report_mouse_t mouse_report = pointing_device_get_report();
            uint8_t btn = MOUSE_BTN1 << (keycode - KC_MY_BTN1);
            if (record->event.pressed) {
                mouse_report.buttons |= btn;
            } else {
                mouse_report.buttons &= ~btn;
            }
            pointing_device_set_report(mouse_report);
            return false;
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
        case KC_F21:
            if (record->event.pressed) {
                SEND_STRING(SS_LCTL("c") SS_LGUI("r") SS_DELAY(200));
                switch (keycode) {
                    case KC_F14: SEND_STRING("https://web.archive.org/web/");
                         break;
                    default:     SEND_STRING("https://www.google.com/search?q=");
                }
                SEND_STRING(SS_LCTL("v") SS_TAP(X_ENTER));
            }
            return false;
        case KC_F15 ... KC_F16:
        case KC_F18 ... KC_F20:
            if (record->event.pressed) {
                switch (keycode) {
                    case KC_F15: SEND_STRING("===");
                        break;
                    case KC_F16: SEND_STRING("=>");
                        break;
                    case KC_F18: SEND_STRING(". ");
                        add_oneshot_mods(MOD_LSFT);
                        break;
                    case KC_F19: SEND_STRING("->");
                        break;
                    default:     SEND_STRING("../");
                }
            }
            return false;
        case LT(0, KC_F1) ... LT(0, KC_F5):
            uint8_t index = (keycode & 0xFF) - KC_F1;
            uint8_t mod   =
                   index == 1 ? MOD_LALT :
                   index == 2 ? MOD_LSFT :
                   index == 3 ? MOD_LCTL : 0;
            static uint8_t  KC_EDIT        = 0;
            static uint16_t timer[5]       = {0};
            static bool editkey_registered = false;

            if (editkey_registered) {
                unregister_code(KC_EDIT);
                editkey_registered = false;
            }
            if (record->event.pressed) {
                if (record->tap.count) {
                    if (timer_elapsed(timer[index]) > (QUICK_TAP_TERM << 2)) {
                        if (mod) {
                            if (get_mods() & mod) {
                                unregister_mods(mod);
                            } else {
                                register_mods(mod);
                                if (index == 1) {
                                    tap_code(KC_TAB);
                                }
                            }
                        } else if (index == 0) {
                            morph_type = CTRL_YZ_MORPH;
                        }
                        KC_EDIT =
                            index == 4 ? KC_V : 0;
                        timer[index] = timer_read();
                    } else {
                        if (index == 1) {
                            tap_code(KC_ESC);
                        }
                        unregister_mods(mod);
                        morph_type =
                            index == 0 ? VOL_MORPH :
                            index == 1 ? WWW_MORPH :
                            index == 2 ? TAB_MORPH :
                            index == 3 ? CTRL_TAB_MORPH : 0;
                        KC_EDIT =
                            index == 4 ? KC_V : 0;
                    }
                } else {
                    tap_code(index == 2 ? KC_F15 :
                             index == 3 ? KC_F16 : 0);
                    morph_type =
                        index == 0 ? MOVES_X4_MORPH : 0;
                    KC_EDIT =
                        index == 1 ? KC_X :
                        index == 4 ? KC_C : 0;
                }
                if (KC_EDIT) {
                    uint8_t saved_mods = get_mods();
                    clear_mods();
                    add_weak_mods(MOD_LCTL);
                    register_code(KC_EDIT);
                    set_mods(saved_mods);
                    editkey_registered = true;
                }
            }
            return false;
        case LT(0, KC_L):
            if (record->event.pressed) {
                layer_move(record->tap.count ? (get_highest_layer(layer_state) % 2 + 1) : 4);
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
        case KC_RGHT ... KC_LEFT:
        case KC_DOWN ... KC_UP:
            if (navkey_registered) {
                unregister_code(morph_code);
                navkey_registered = false;
                if (!record->event.pressed) {
                    return false;
                }
            }
            if (record->event.pressed) {
                uint8_t saved_mods = get_mods();

                switch (keycode) {
                    case KC_DOWN ... KC_UP:
                        switch (morph_type) {
                            case TAB_MORPH ... CTRL_YZ_MORPH:
                                return true;
                        }
                }
                switch (morph_type) {
                    case TAB_MORPH:
                        switch (keycode) {
                            case KC_LEFT: add_weak_mods(MOD_LSFT);
                            default: morph_code = KC_TAB;
                        }
                        break;
                    case VOL_MORPH: morph_code =
                            keycode == KC_LEFT ? KC_VOLD : KC_VOLU;
                        break;
                    case WWW_MORPH: morph_code =
                            keycode == KC_LEFT ? KC_WBAK : KC_WFWD;
                        break;
                    case CTRL_YZ_MORPH: morph_code =
                            keycode == KC_LEFT ? KC_Z    : KC_Y;
                        clear_mods();
                        add_weak_mods(MOD_LCTL);
                        break;
                    case CTRL_TAB_MORPH:
                        switch (keycode) {
                            case KC_LEFT: add_weak_mods(MOD_LSFT);
                            case KC_RGHT: morph_code = KC_TAB;
                                clear_mods();
                                add_weak_mods(MOD_LCTL);
                                break;
                            default: morph_code =
                                keycode == KC_UP ? KC_PGUP : KC_PGDN;
                        }
                        break;
                    case MOVES_X4_MORPH:
                        send_four_times(keycode);
                        morph_code = keycode;
                        first_iteration = true;
                        navkey_registered = true;
                        return false;
                    default:
                        return true;
                }
                register_code(morph_code);
                set_mods(saved_mods);
                navkey_registered = true;
                return false;
            }
            break;
        case KC_COMM:
            static uint8_t registered_key;
            if (record->event.pressed) {
                registered_key    = KC_COMM;
                uint8_t mod_state = get_mods();
                if ((mod_state | get_oneshot_mods()) & MOD_MASK_SHIFT) {
                    registered_key = KC_DOT;
                    del_mods(MOD_MASK_SHIFT);
                    del_oneshot_mods(MOD_MASK_SHIFT);
                }
                register_code(registered_key);
                set_mods(mod_state);
            } else {
                unregister_code(registered_key);
            }
            return false;
        case LT(2, KC_SPC):
            if (!record->tap.count) {
                if (!record->event.pressed) {
                    if (morph_type) {
                        if (navkey_registered) {
                            unregister_code(morph_code);
                            navkey_registered = false;
                        }
                        morph_type = 0;
                    }
                    unregister_mods(MOD_MEH);
                }
                layer_clear();
            }
            caps_word_off();
            break;
        case LT(4, KC_ENT):
            if (!record->tap.count) {
                layer4_is_held = record->event.pressed;
            }
            break;
    }
    return true;
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        within_word(keycode);
    }
}

#define REPEAT_DELAY 500
#define REPEAT_INTERVAL 40

void matrix_scan_user(void) {
    static uint16_t repeat_timer = 0;
    if (navkey_registered
        && morph_type == MOVES_X4_MORPH) {
        if (repeat_timer == 0) {
            repeat_timer = timer_read();
        } else if (timer_elapsed(repeat_timer)
                > (first_iteration ? REPEAT_DELAY : REPEAT_INTERVAL)) {
            send_four_times(morph_code);
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
    CMB_INT4,
    CMB_PSCR,
    CMB_OS_CTL,
    CMB_OS_SFT,
    CMB_OS_ALT,
    CMB_OS_MOL3,
    CMB_MS_BTN1,
    CMB_MS_BTN2,
    CMB_MS_BTN3,
};

const uint16_t PROGMEM cmb_app[]     = {KC_Z,         KC_M,                       COMBO_END};
const uint16_t PROGMEM cmb_lng1[]    = {LCTL_T(KC_S), KC_G,                       COMBO_END};
const uint16_t PROGMEM cmb_lng2[]    = {KC_Y,         RCTL_T(KC_H),               COMBO_END};
const uint16_t PROGMEM cmb_osl1[]    = {LALT_T(KC_R), LSFT_T(KC_T), LCTL_T(KC_S), COMBO_END};
const uint16_t PROGMEM cmb_int4[]    = {KC_Z,         KC_C,                       COMBO_END};
const uint16_t PROGMEM cmb_pscr[]    = {KC_L,         KC_D,         KC_W,         COMBO_END};
const uint16_t PROGMEM cmb_os_ctl[]  = {KC_D,         KC_W,                       COMBO_END};
const uint16_t PROGMEM cmb_os_sft[]  = {KC_L,         KC_W,                       COMBO_END};
const uint16_t PROGMEM cmb_os_alt[]  = {KC_L,         KC_D,                       COMBO_END};
const uint16_t PROGMEM cmb_os_mol3[] = {KC_M,         KC_C,                       COMBO_END};
const uint16_t PROGMEM cmb_ms_btn1[] = {LSFT_T(KC_T), LCTL_T(KC_S),               COMBO_END};
const uint16_t PROGMEM cmb_ms_btn2[] = {LALT_T(KC_R), LSFT_T(KC_T),               COMBO_END};
const uint16_t PROGMEM cmb_ms_btn3[] = {LALT_T(KC_R), LCTL_T(KC_S),               COMBO_END};

combo_t key_combos[] = {
    [CMB_APP]        = COMBO(cmb_app,       LT(0, KC_APP)),
    [CMB_LNG1]       = COMBO(cmb_lng1,      LT(0, KC_LNG1)),
    [CMB_LNG2]       = COMBO(cmb_lng2,      LT(0, KC_LNG2)),
    [CMB_OSL1]       = COMBO(cmb_osl1,      OSL(1)),
    [CMB_INT4]       = COMBO(cmb_int4,      KC_INT4),
    [CMB_PSCR]       = COMBO(cmb_pscr,      KC_PSCR),
    [CMB_OS_CTL]     = COMBO(cmb_os_ctl,    OSM(MOD_LCTL)),
    [CMB_OS_SFT]     = COMBO(cmb_os_sft,    OSM(MOD_LSFT)),
    [CMB_OS_ALT]     = COMBO(cmb_os_alt,    OSM(MOD_LALT)),
    [CMB_OS_MOL3]    = COMBO(cmb_os_mol3,   LT(3, KC_NO)),
    [CMB_MS_BTN1]    = COMBO(cmb_ms_btn1,   KC_MY_BTN1),
    [CMB_MS_BTN2]    = COMBO(cmb_ms_btn2,   KC_MY_BTN2),
    [CMB_MS_BTN3]    = COMBO(cmb_ms_btn3,   KC_MY_BTN3),
};

bool caps_word_press_user(uint16_t keycode) {
    switch (keycode) {
        case KC_A ... KC_Z:
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
        case KC_QUOT:
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
    if (abs(mouse_report.x) || abs(mouse_report.y)) {
        enable_all_mts_mods();
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
