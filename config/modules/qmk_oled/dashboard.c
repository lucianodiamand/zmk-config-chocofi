/* SPDX-License-Identifier: MIT
 * ZMK main / Zephyr 4.1 / LVGL 9.3. The central publishes packed display state through
 * a GLOBAL behavior; the peripheral renders it and reads its own battery.
 */
#define DT_DRV_COMPAT custom_dashboard_sync
#include <stdio.h>
#include <hal/nrf_power.h>
#include <string.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <lvgl.h>
#include <zmk/battery.h>
#include <zmk/display/status_screen.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
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
static atomic_t last_key_at;
static bool panel_is_idle(void);
static lv_obj_t *rows[10];
static const int y_pos[] = {0,20,40,60,72,84,96,116,136,148};
static const char *layers[] = {"NORM","NAVI","NUMB","SYMB","MEDI","FUNC","MOUS"};
static const char *modes[] = {"TEXT","UPSK","SNAK","PASC","CAML"};

uint8_t chocofi_case_mode_get(void);

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
    {
        bool idle = (uint32_t)((uint32_t)k_uptime_get() - (uint32_t)atomic_get(&last_key_at)) >= CONFIG_ZMK_IDLE_TIMEOUT;
        uint32_t state = (MIN(MAX(zmk_wpm_get_state(),0),999) & 1023)
          | ((zmk_keymap_highest_layer_active() & 15) << 10)
          | ((uint32_t)zmk_hid_get_explicit_mods() << 14)
          | ((uint32_t)(chocofi_case_mode_get() & 7) << 22);
        uint32_t connection = (zmk_ble_active_profile_index() + 1)
          | ((uint32_t)zmk_ble_active_profile_is_connected() << 8)
          | ((uint32_t)zmk_ble_active_profile_is_open() << 9)
          | ((uint32_t)idle << 10);
        int64_t now = k_uptime_get();
        if (state != old_state || connection != old_connection || (!idle && now - sent_at >= 2000)) {
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
    if (panel_is_idle()) return;
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
    /* VBUS detection also works on the peripheral without a USB HID stack.
     * It reports USB power availability, not charger-current telemetry. */
    bool usb_power = nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
    snprintf(text, sizeof(text), "%3u%%%c", (unsigned)zmk_battery_state_of_charge(),
             usb_power ? '+' : ' ');
    lv_label_set_text(rows[9], text);
}


/* LVGL I1 uses horizontal MSB-first bits after an 8-byte palette.
 * Convert changed logical regions (68x160) into the nice!view native 160x68
 * framebuffer, rotated 180 degrees from the initial portrait orientation.
 * Respect driver pixel polarity. */
static const struct device *oled = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static struct display_capabilities oled_caps;
static uint8_t native_frame[160 * 68 / 8];
static atomic_t panel_idle;
static lv_obj_t *dashboard_screen;
struct k_work_q *zmk_display_work_q(void);
/* nice!view has no display-enable GPIO: clear its memory LCD explicitly.
 * Run all display writes and LVGL calls on the display's own queue. */
static void panel_power_work(struct k_work *work) {
    if (!dashboard_screen) return;
    if (atomic_get(&panel_idle)) {
        static uint8_t blank_frame[160 * 68 / 8];
        memset(blank_frame, oled_caps.current_pixel_format == PIXEL_FORMAT_MONO01 ? 0 : 255,
               sizeof(blank_frame));
        struct display_buffer_descriptor desc = {
            .buf_size = sizeof(blank_frame), .width = 160, .height = 68, .pitch = 160,
        };
        int err = display_write(oled, 0, 0, &desc, blank_frame);
        if (err) LOG_ERR("Display idle clear failed: %d", err);
    } else {
        lv_obj_invalidate(dashboard_screen);
    }
}
/* Keep the lightweight LVGL timer running so timeout and wakeup are reliable.
 * No panel writes or label updates are performed while idle. */
static bool panel_is_idle(void) {
    bool idle = (uint32_t)((uint32_t)k_uptime_get() - (uint32_t)atomic_get(&last_key_at))
                >= CONFIG_ZMK_IDLE_TIMEOUT;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (zmk_split_bt_peripheral_is_connected() && atomic_get(&last_received))
        idle = (atomic_get(&packed_connection) & BIT(10)) != 0;
#endif
    if (idle != (bool)atomic_get(&panel_idle)) {
        atomic_set(&panel_idle, idle);
        panel_power_work(NULL);
    }
    return idle;
}
static int panel_key_activity(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev) atomic_set(&last_key_at, (uint32_t)k_uptime_get());
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(chocofi_panel_keys, panel_key_activity);
ZMK_SUBSCRIPTION(chocofi_panel_keys, zmk_position_state_changed);


static void portrait_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    if (atomic_get(&panel_idle)) { lv_display_flush_ready(display); return; }
    const uint8_t *src = px_map + 8;
    uint32_t width = area->x2 - area->x1 + 1;
    uint32_t stride_bits = ROUND_UP(width, CONFIG_LV_DRAW_BUF_STRIDE_ALIGN);
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= 68 || y < 0 || y >= 160) { continue; }
            uint32_t bit_index = (y - area->y1) * stride_bits + x - area->x1;
            bool white = (src[bit_index / 8] & BIT(7 - bit_index % 8)) != 0;
            int px = 159 - y, py = x;
            size_t idx;
            uint8_t bit;
            if (oled_caps.screen_info & SCREEN_INFO_MONO_VTILED) {
                idx = px + (py / 8) * 160;
                bit = py % 8;
            } else {
                idx = px / 8 + py * 20;
                bit = px % 8;
            }
            if (oled_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) { bit = 7 - bit; }
            bool on = oled_caps.current_pixel_format == PIXEL_FORMAT_MONO10 ? white : !white;
            if (on) { native_frame[idx] |= BIT(bit); }
            else { native_frame[idx] &= ~BIT(bit); }
        }
    }
    if (lv_display_flush_is_last(display)) {
        struct display_buffer_descriptor desc = {
            .buf_size = sizeof(native_frame), .width = 160, .height = 68, .pitch = 160,
        };
        int err = display_write(oled, 0, 0, &desc, native_frame);
        if (err) { LOG_ERR("Display write failed: %d", err); }
    }
    lv_display_flush_ready(display);
}

lv_obj_t *zmk_display_status_screen(void) {
    display_get_capabilities(oled, &oled_caps);
    if (oled_caps.x_resolution != 160 || oled_caps.y_resolution != 68 ||
        (oled_caps.current_pixel_format != PIXEL_FORMAT_MONO01 &&
         oled_caps.current_pixel_format != PIXEL_FORMAT_MONO10)) {
        LOG_ERR("Dashboard requires a 160x68 nice!view display");
        return NULL;
    }
    memset(native_frame, oled_caps.current_pixel_format == PIXEL_FORMAT_MONO10 ? 0 : 255,
           sizeof(native_frame));
    lv_display_t *display = lv_display_get_default();
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    lv_display_set_flush_cb(display, portrait_flush);
    lv_obj_t *screen = lv_obj_create(NULL);
    dashboard_screen = screen;
    atomic_set(&last_key_at, (uint32_t)k_uptime_get());
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_size(screen, 68, 160);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    for (int i=0;i<ARRAY_SIZE(rows);i++) {
        rows[i] = lv_label_create(screen);
        lv_obj_set_style_text_font(rows[i], &lv_font_unscii_8, 0);
        lv_obj_set_style_text_color(rows[i], lv_color_white(), 0);
        lv_obj_set_style_text_letter_space(rows[i], 0, 0);
        lv_obj_set_pos(rows[i], i == 9 ? 14 : 18, y_pos[i]);
    }
    update(NULL);
    lv_timer_create(update, 100, NULL);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    k_work_schedule(&publish_work, K_NO_WAIT);
#endif
    return screen;
}
