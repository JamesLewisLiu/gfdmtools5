#ifndef GFDMHOOK1_DEBUG_H
#define GFDMHOOK1_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

#define GFDM_DEBUG_AUTO (UINT32_C(1) << 30)
#define GFDM_DEBUG_TIMER (UINT32_C(1) << 31)
#define GFDM_DEBUG_MASK (GFDM_DEBUG_AUTO | GFDM_DEBUG_TIMER)

struct gfdm_debug_state {
    uint32_t pressed;
    uint32_t enabled;
    bool freeze_timer;
    bool unlock_music;
};

/* Callers serialize updates and reads. Buttons toggle persistent DIP levels. */
uint32_t gfdm_debug_update(struct gfdm_debug_state *state, uint32_t input);
char gfdm_debug_param(
    const struct gfdm_debug_state *state, int param, char native_value);

#endif
