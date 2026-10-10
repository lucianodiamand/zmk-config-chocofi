/* SPDX-License-Identifier: MIT */
#ifndef CHOCOFI_CASE_STATE_H
#define CHOCOFI_CASE_STATE_H
#include <stdbool.h>
#include <stdint.h>
struct case_state { uint8_t mode; bool cap_next; };
/* HID keyboard usages; 0 means consume the key. Shift is encoded at bit 25. */
static inline void case_toggle(struct case_state *s, unsigned mode) {
    s->mode = s->mode == mode ? 0 : mode;
    s->cap_next = s->mode == 3;
}
static inline uint32_t case_transform(struct case_state *s, uint32_t key) {
    if (!s->mode) return key;
    if (key == 0x70029 || key == 0x70028 || key == 0x7002b) {
        s->mode = 0; s->cap_next = false; return 0;
    }
    if (key == 0x7002c) {
        if (s->mode <= 2) return 0x207002d; /* LS(MINUS) */
        s->cap_next = true; return 0;
    }
    if (key >= 0x70004 && key <= 0x7001d) {
        bool upper = s->mode == 1 || (s->mode >= 3 && s->cap_next);
        s->cap_next = false;
        return upper ? key | 0x2000000 : key;
    }
    return key;
}
#endif
