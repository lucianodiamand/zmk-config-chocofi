/* SPDX-License-Identifier: MIT
 * ZMK v0.3 / LVGL 8. The central publishes packed display state through
 * a GLOBAL behavior; the peripheral renders it and reads its own battery.
 */
#define DT_DRV_COMPAT custom_dashboard_sync
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <lvgl.h>
#include <zmk/battery.h>
#include <zmk/display/status_screen.h>
#include <zmk/activity.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/wpm.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/ble.h>
#else
#include <zmk/split/bluetooth/peripheral.h>
#endif

/* Packed: WPM bits 0..9, layer 10..13, modifiers 14..21, case 22..24.
 * Connection: profile bits 0..7, connected bit 8, unpaired bit 9. */
static atomic_t packed_state, packed_connection;
static atomic_t last_received;
static lv_obj_t *rows[10];
static const int y_pos[] = {0,16,32,48,56,64,72,88,104,112};
static const char *layers[] = {"NORM","NAVI","NUMB","SYMB","MEDI","FUNC","MOUS"};
static const char *modes[] = {"TEXT","UPSK","SNAK","PASC","CAML"};

/* Override this when the casing behavior is migrated; display labels alone
 * must not claim a mode is active. 0 TEXT, 1 UPSK, 2 SNAK, 3 PASC, 4 CAML. */
__attribute__((weak)) uint8_t chocofi_case_mode_get(void) { return 0; }

static int receive(struct zmk_behavior_binding *binding,
                   struct zmk_behavior_binding_event event) {
    atomic_set(&packed_state, binding->param1);
    atomic_set(&packed_connection, binding->param2);
    atomic_set(&last_received, (uint32_t)k_uptime_get());
    return ZMK_BEHAVIOR_OPAQUE;
}
static int release(struct zmk_behavior_binding *binding,
                   struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api sync_api = {
    .binding_pressed = receive,
    .binding_released = release,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                       CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sync_api);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static void publish(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(publish_work, publish);
static void publish(struct k_work *work) {
    static uint32_t old_state = UINT32_MAX, old_connection = UINT32_MAX;
    static int64_t sent_at;
    if (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE) {
        uint32_t state = (MIN(MAX(zmk_wpm_get_state(),0),999) & 1023)
          | ((zmk_keymap_highest_layer_active() & 15) << 10)
          | ((uint32_t)zmk_hid_get_explicit_mods() << 14)
          | ((uint32_t)(chocofi_case_mode_get() & 7) << 22);
        uint32_t connection = (zmk_ble_active_profile_index() + 1)
          | ((uint32_t)zmk_ble_active_profile_is_connected() << 8)
          | ((uint32_t)zmk_ble_active_profile_is_open() << 9);
        int64_t now = k_uptime_get();
        if (state != old_state || connection != old_connection || now - sent_at >= 2000) {
            struct zmk_behavior_binding binding = {
                .behavior_dev = DEVICE_DT_NAME(DT_INST(0, custom_dashboard_sync)),
                .param1 = state, .param2 = connection,
            };
            struct zmk_behavior_binding_event event = {.timestamp = now};
            int err = zmk_behavior_invoke_binding(&binding, event, true);
            if (err >= 0) { old_state=state; old_connection=connection; sent_at=now; }
        }
    }
    k_work_reschedule(&publish_work, K_MSEC(100));
}
#endif

static void update(lv_timer_t *timer) {
    uint32_t s = atomic_get(&packed_state), c = atomic_get(&packed_connection);
    uint8_t mods = (s >> 14) & 255, layer = (s >> 10) & 15, mode = (s >> 22) & 7;
    char text[8], modifier_text[5] = "    ";
    bool fresh = true;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    fresh = zmk_split_bt_peripheral_is_connected()
      && (uint32_t)((uint32_t)k_uptime_get() - (uint32_t)atomic_get(&last_received)) < 5000;
#endif
    snprintf(text, sizeof(text), "%03u", (unsigned)(s & 1023));
    lv_label_set_text(rows[0], fresh ? text : "---");
    lv_label_set_text(rows[1], fresh ? (mode < ARRAY_SIZE(modes) ? modes[mode] : "????") : "----");
    lv_label_set_text(rows[2], fresh ? (layer < ARRAY_SIZE(layers) ? layers[layer] : "UNKN") : "----");
    lv_label_set_text(rows[3], "NONE");
    lv_label_set_text(rows[4], "H000");
    lv_label_set_text(rows[5], "S000");
    lv_label_set_text(rows[6], "V000");
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    const uint8_t bits[] = {3,2,0,1};
    const char *letters = "WACS";
#else
    const uint8_t bits[] = {5,4,6,7};
    const char *letters = "SCAW";
#endif
    for (int i=0;i<4;i++) if (fresh && (mods & BIT(bits[i]))) modifier_text[i]=letters[i];
    lv_label_set_text(rows[7], modifier_text);
    snprintf(text, sizeof(text), "BT%u%c", (unsigned)(c & 255),
             (c & BIT(8)) ? '+' : ((c & BIT(9)) ? '?' : '-'));
    lv_label_set_text(rows[8], fresh ? text : "BT--");
    snprintf(text, sizeof(text), "%3u%%", (unsigned)zmk_battery_state_of_charge());
    lv_label_set_text(rows[9], text);
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_disp_get_default()->driver->sw_rotate = 1;
    lv_disp_set_rotation(lv_disp_get_default(), LV_DISP_ROT_270);
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_size(screen, 32, 128);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    for (int i=0;i<ARRAY_SIZE(rows);i++) {
        rows[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(rows[i], &lv_font_unscii_8, 0);
        lv_obj_set_style_text_color(rows[i], lv_color_white(), 0);
        lv_obj_set_style_text_letter_space(rows[i], 0, 0);
        lv_obj_set_pos(rows[i], 0, y_pos[i]);
    }
    update(NULL);
    lv_timer_create(update, 100, NULL);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    k_work_schedule(&publish_work, K_NO_WAIT);
#endif
    return screen;
}
