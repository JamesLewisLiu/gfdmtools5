#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "gfdmhook1/guitar.c"

/* V4-V8 libsystem sys_input_update: rising = current & ~previous.
   libshare-pj gf_input_pick uses UP (0x20); Wail uses DOWN (0x40). */
struct native_input {
    uint32_t previous;
    uint32_t current;
    uint32_t rising;
};

static void
native_update(struct native_input *native, uint32_t mapped, int player)
{
    native->current = gfdm_guitar_input(mapped, player);
    native->rising = native->current & ~native->previous;
    native->previous = native->current;
}

static void check_player(unsigned int player)
{
    unsigned int offset = player * 2;
    uint32_t a = UINT32_C(1) << (24 + offset);
    uint32_t b = UINT32_C(1) << (25 + offset);
    uint32_t wail = UINT32_C(1) << (12 + player);
    struct native_input native = {0};

    for (unsigned int source = 0; source < 3; source++) {
        uint32_t pick = source == 0 ? a : source == 1 ? b : a | b;
        native_update(&native, pick, player);
        assert(native.current == 0x20 && native.rising == 0x20);
        for (unsigned int poll = 0; poll < 100; poll++) {
            native_update(&native, pick, player);
            assert(native.current == 0x20 && native.rising == 0);
        }
        native_update(&native, 0, player);
        assert(native.current == 0 && native.rising == 0);
    }

    unsigned int picks = 0;
    for (unsigned int i = 0; i < 200; i++) {
        native_update(&native, i & 1 ? b : a, player);
        assert(!(native.current & 0x40));
        picks += (native.rising & 0x20) != 0;
        native_update(&native, 0, player);
    }
    assert(picks == 200);

    /* Two held switches are one held Pick, not two game buttons or a turbo. */
    native_update(&native, a, player);
    assert(native.rising == 0x20);
    native_update(&native, a | b, player);
    assert(native.current == 0x20 && native.rising == 0);
    native_update(&native, b, player);
    assert(native.current == 0x20 && native.rising == 0);
    native_update(&native, 0, player);
    assert(native.current == 0);

    native_update(&native, wail, player);
    assert(native.current == 0x40 && native.rising == 0x40);
    native_update(&native, wail | b, player);
    assert(native.current == 0x60 && native.rising == 0x20);
    native_update(&native, b, player);
    assert(native.current == 0x20 && native.rising == 0);
    native_update(&native, 0, player);

    /* Check every mapper bit: old non-Pick GF actions remain unchanged,
       including player isolation and no leaked DM/debug/reverse actions. */
    for (unsigned int bit = 0; bit < 32; bit++) {
        uint32_t expected = 0;
        if (bit == 8 + player)
            expected = 0x4;
        if (bit == 12 + player)
            expected = 0x40;
        if (bit == 18 + player)
            expected = 0x200;
        if (bit == 20 + player)
            expected = 0x400;
        if (bit == 22 + player)
            expected = 0x800;
        if (bit == 24 + offset || bit == 25 + offset)
            expected = 0x20;
        assert(gfdm_guitar_input(UINT32_C(1) << bit, player) == expected);
    }
}

int main(void)
{
    check_player(0);
    check_player(1);
    assert(gfdm_guitar_input(UINT32_MAX, 0) == 0xE64);
    assert(gfdm_guitar_input(UINT32_MAX, 1) == 0xE64);
    puts(
        "PASS: both GF pick bindings feed one native Pick, never Wail; native "
        "level/edge consumers, alternation, holds, overlap, player isolation "
        "and existing panel actions");
    return 0;
}
