#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "gfdmhook1/debug.c"

int main(void)
{
    struct gfdm_debug_state state = {0};

    assert(gfdm_debug_param(&state, 1, 0) == 0);
    assert(gfdm_debug_param(&state, 2, 0) == 0);
    assert(gfdm_debug_update(&state, GFDM_DEBUG_AUTO | 4) == GFDM_DEBUG_AUTO);
    for (unsigned int i = 0; i < 10000; i++) {
        assert(gfdm_debug_update(&state, GFDM_DEBUG_AUTO) == 0);
        assert(gfdm_debug_param(&state, 2, 0) == 1);
        assert(gfdm_debug_param(&state, 1, 0) == 0);
    }
    assert(gfdm_debug_update(&state, 0) == 0);
    assert(gfdm_debug_param(&state, 2, 0) == 1);
    assert(gfdm_debug_update(&state, GFDM_DEBUG_TIMER) == GFDM_DEBUG_TIMER);
    assert(gfdm_debug_param(&state, 1, 0) == 1);
    assert(gfdm_debug_param(&state, 2, 0) == 1);
    assert(gfdm_debug_update(&state, 0) == 0);
    assert(gfdm_debug_update(&state, GFDM_DEBUG_AUTO) == GFDM_DEBUG_AUTO);
    assert(gfdm_debug_param(&state, 2, 0) == 0);
    assert(gfdm_debug_param(&state, 1, 0) == 1);
    assert(gfdm_debug_update(&state, 0) == 0);
    assert(gfdm_debug_update(&state, GFDM_DEBUG_MASK) == GFDM_DEBUG_MASK);
    assert(gfdm_debug_param(&state, 2, 0) == 1);
    assert(gfdm_debug_param(&state, 1, 0) == 0);
    assert(gfdm_debug_update(&state, 7) == 0); /* COIN/TEST/SERVICE cannot toggle. */
    assert(gfdm_debug_param(&state, 2, 0) == 1);
    for (int param = -1; param < 32; param++) {
        if (param != 1 && param != 2) {
            assert(gfdm_debug_param(&state, param, (char) 0x7B) == (char) 0x7B);
        }
    }
    puts("PASS: independent latched Auto Play/Neverend, held/released input and repeated getters cannot toggle, unrelated DIPs preserved");
    return 0;
}
