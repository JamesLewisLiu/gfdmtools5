#include "gfdmhook1/coin.h"

bool gfdm_coin_update(struct gfdm_coin_state *state, bool pressed)
{
    bool inserted = pressed && !state->pressed;

    state->pressed = pressed;
    if (inserted) {
        /* The board counter is 16-bit, including its natural wraparound. */
        state->total++;
    }
    return inserted;
}

uint16_t gfdm_coin_take(struct gfdm_coin_state *state, bool all)
{
    uint16_t pending = (uint16_t) (state->total - state->consumed);

    if (all) {
        state->consumed = state->total;
        return pending;
    }
    if (pending != 0) {
        state->consumed++;
        return 1;
    }
    return 0;
}
