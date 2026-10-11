/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT custom_case_key
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include "case_state.h"

/* Save Shift on physical vowel down, before tap-dance delays its output.
 * Do not clear on upstroke: the selected tap may be emitted after release. */
static uint8_t vowel_shift[36];
static uint32_t vowel_at_position(uint32_t position) {
    switch (position) {
    case 2: return 0x70008;  /* E */
    case 6: return 0x70018;  /* U */
    case 7: return 0x7000c;  /* I */
    case 8: return 0x70012;  /* O */
    case 10: return 0x70004; /* A */
    default: return 0;
    }
}
static int capture_vowel_shift(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->state && ev->position < ARRAY_SIZE(vowel_shift) &&
        vowel_at_position(ev->position)) {
        vowel_shift[ev->position] = zmk_hid_get_explicit_mods() & 0x22;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(chocofi_vowel_shift, capture_vowel_shift);
ZMK_SUBSCRIPTION(chocofi_vowel_shift, zmk_position_state_changed);

static struct case_state state;
static atomic_t display_mode;
uint8_t chocofi_case_mode_get(void) { return atomic_get(&display_mode); }
/* Cache transformed presses so changing mode never leaves a held key down.
 * Position + input also supports sequential macro taps from one position. */
static struct { bool used; uint32_t position, input, output; } held[64];
static int press(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) {
    for (unsigned i = 0; i < ARRAY_SIZE(held); i++) {
        if (held[i].used) continue;
        uint32_t output = b->param1;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        /* Preserve Ctrl/Alt/GUI shortcuts; Unicode hex input uses stock kp. */
        if (!(zmk_hid_get_explicit_mods() & 0xdd))
            output = case_transform(&state, output);
        if (output && e.position < ARRAY_SIZE(vowel_shift) &&
            b->param1 == vowel_at_position(e.position)) {
            output |= (uint32_t)vowel_shift[e.position] << 24;
        }
#endif
        held[i].used = true; held[i].position = e.position;
        held[i].input = b->param1; held[i].output = output;
        atomic_set(&display_mode, state.mode);
        return output ? raise_zmk_keycode_state_changed_from_encoded(output, true, e.timestamp)
                      : ZMK_BEHAVIOR_OPAQUE;
    }
    return -ENOMEM;
}
static int release(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) {
    for (unsigned i = 0; i < ARRAY_SIZE(held); i++) {
        if (!held[i].used || held[i].position != e.position || held[i].input != b->param1)
            continue;
        uint32_t output = held[i].output; held[i].used = false;
        return output ? raise_zmk_keycode_state_changed_from_encoded(output, false, e.timestamp)
                      : ZMK_BEHAVIOR_OPAQUE;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api api = {.binding_pressed=press, .binding_released=release};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT custom_case_mode
static int toggle(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) {
    if (b->param1 < 1 || b->param1 > 4) return -EINVAL;
    case_toggle(&state, b->param1);
    atomic_set(&display_mode, state.mode);
    return ZMK_BEHAVIOR_OPAQUE;
}
static int mode_release(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) {
    return ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api mode_api = {
    .binding_pressed=toggle, .binding_released=mode_release,
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mode_api);
