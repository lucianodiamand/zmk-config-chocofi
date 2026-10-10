/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT custom_case_key
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include "case_state.h"

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
