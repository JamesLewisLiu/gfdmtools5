#include "gfdmhook1/debug.h"

uint32_t gfdm_debug_update(struct gfdm_debug_state *state, uint32_t input)
{
    uint32_t pressed = input & GFDM_DEBUG_MASK;
    uint32_t changed = pressed & ~state->pressed;

    state->pressed = pressed;
    state->enabled ^= changed;
    return changed;
}

char gfdm_debug_param(
    const struct gfdm_debug_state *state, int param, char native_value)
{
    /* Verified gd_dip tables in V4-V8: 1=neverend, 2=autoplay. These are
       software debug parameters, unrelated to the cabinet's hardware DIPs. */
    if (param == 1) {
        return (state->enabled & GFDM_DEBUG_TIMER) != 0;
    }
    if (param == 2) {
        return (state->enabled & GFDM_DEBUG_AUTO) != 0;
    }
    return native_value;
}
