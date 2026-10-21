#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "gfdmhook1/coin.c"

static void press(struct gfdm_coin_state *state)
{
    assert(!gfdm_coin_update(state, false));
    assert(gfdm_coin_update(state, true));
    assert(!gfdm_coin_update(state, false));
}

int main(void)
{
    struct gfdm_coin_state state = {0};

    assert(gfdm_coin_take(&state, false) == 0);
    assert(gfdm_coin_take(&state, true) == 0);
    assert(gfdm_coin_update(&state, true));
    assert(gfdm_coin_take(&state, false) == 1);
    for (unsigned int i = 0; i < 10000; i++) {
        assert(!gfdm_coin_update(&state, true));
        assert(gfdm_coin_take(&state, false) == 0);
        assert(gfdm_coin_take(&state, true) == 0);
        assert(state.total == 1); /* Hardware reads never acknowledge coins. */
    }
    assert(!gfdm_coin_update(&state, false));
    for (unsigned int i = 0; i < 10000; i++) {
        assert(!gfdm_coin_update(&state, false));
        assert(gfdm_coin_take(&state, false) == 0);
    }
    press(&state);
    assert(gfdm_coin_take(&state, false) == 1);
    assert(gfdm_coin_take(&state, false) == 0);
    assert(state.total == 2);

    for (unsigned int i = 0; i < 7; i++) {
        press(&state);
    }
    assert(gfdm_coin_take(&state, false) == 1);
    assert(gfdm_coin_take(&state, true) == 6);
    assert(gfdm_coin_take(&state, false) == 0);
    assert(gfdm_coin_take(&state, true) == 0);
    assert(state.total == 9);

    /* A 16-bit board wraps independently of an unconsumed event queue. */
    state.total = UINT16_MAX - 1;
    state.consumed = state.total;
    press(&state);
    press(&state);
    press(&state);
    assert(state.total == 1);
    assert(gfdm_coin_take(&state, false) == 1);
    assert(gfdm_coin_take(&state, true) == 2);
    assert(gfdm_coin_take(&state, false) == 0);
    assert(state.consumed == 1 && state.total == 1);
    puts("PASS: one coin per press, hold/release cannot repeat credits, queued coins, shared single/all consumption and cumulative board wraparound");
    return 0;
}
