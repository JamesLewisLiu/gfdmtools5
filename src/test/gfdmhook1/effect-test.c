#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "gfdmhook1/effect.c"

static void check(struct gfdm_effect_state *s, uint32_t frame, unsigned int phase)
{
    static const uint32_t masks[] = {0, 0x8000, 0x10000, 0x18000};
    uint32_t result = gfdm_effect_publish(s, frame);
    if (result != masks[phase]) {
        fprintf(stderr, "frame %u: expected phase %u, got %u, pending %d\n",
            frame, phase, s->phase, s->pending);
    }
    assert(result == masks[phase]);
    /* Native gameplay weights right/button7 twice, left/button6 once.
       TEST displays those bits with the opposite weights. */
    assert(2 * ((result & 0x10000) != 0)
        + ((result & 0x8000) != 0) == phase);
    assert(s->phase == phase);
}

/* Native game.dll consumer: V4 RVA 0x3ED60, with the independently
   verified libshare getter's 2*right+left weighting (V4 RVA 0x7D10). */
static void check_native_effect_cycle(void)
{
    struct gfdm_effect_state s = {0};
    uint32_t frame = 0;
    unsigned int previous = 0, effect = 0;

    for (unsigned int direction = 0; direction < 2; direction++) {
        for (unsigned int step = 1; step <= 6; step++) {
            uint32_t input;
            unsigned int phase;
            bool cw = direction == 0;

            gfdm_effect_buttons(&s, cw, !cw);
            input = gfdm_effect_publish(&s, frame++);
            phase = 2 * ((input & 0x10000) != 0)
                + ((input & 0x8000) != 0);
            assert(phase != previous);
            effect = (effect + (phase == ((previous + 1) & 3) ? 1 : 5)) % 6;
            previous = phase;
            assert(effect == (cw ? step % 6 : (6 - step) % 6));
            assert(gfdm_effect_publish(&s, frame++) == input);
            gfdm_effect_buttons(&s, false, false);
            assert(gfdm_effect_publish(&s, frame++) == input);
        }
    }
}

int main(void)
{
    struct gfdm_effect_state a = {0}, b = {0};
    uint32_t frame = 0;
    check_native_effect_cycle();
    check(&a, frame++, 0);
    for (unsigned int n = 1; n <= 12; n++) {
        gfdm_effect_buttons(&a, true, false);
        check(&a, frame++, n & 3);
        for (unsigned int i = 0; i < 1000; i++) {
            gfdm_effect_buttons(&a, true, false);
            check(&a, frame++, n & 3);
        }
        gfdm_effect_buttons(&a, false, false);
        check(&a, frame++, n & 3); /* Release retains position. */
    }
    gfdm_effect_buttons(&a, false, true);
    check(&a, frame++, 3);
    gfdm_effect_buttons(&a, false, false);
    gfdm_effect_buttons(&a, false, true);
    check(&a, frame++, 2);
    check(&b, frame++, 0); /* Other player is independent. */

    a = (struct gfdm_effect_state) {0};
    gfdm_effect_axis(&a, true, false, 0);
    gfdm_effect_axis(&a, true, false, 100);
    gfdm_effect_axis(&a, true, false, 200);
    check(&a, frame, 1);
    for (unsigned int i = 0; i < 1000; i++) {
        check(&a, frame, 1);
    }
    check(&a, ++frame, 2);
    check(&a, ++frame, 3); /* Large moves expose adjacent phases in order. */
    gfdm_effect_axis(&a, true, false, 8); /* Byte wrap, +64. */
    check(&a, ++frame, 0);
    gfdm_effect_axis(&a, true, false, 200); /* -64. */
    check(&a, ++frame, 3);
    gfdm_effect_axis(&a, true, false, 200); /* Stationary cumulative value. */
    check(&a, ++frame, 3);
    gfdm_effect_axis(&a, false, false, 0);
    check(&a, ++frame, 3); /* Unbound axis must not reset a digital knob. */

    b = (struct gfdm_effect_state) {0};
    gfdm_effect_axis(&b, true, true, 128); /* geninput absolute minimum. */
    check(&b, frame++, 0);
    gfdm_effect_axis(&b, true, true, 192);
    check(&b, frame++, 1);
    gfdm_effect_axis(&b, true, true, 0); /* Absolute midpoint. */
    check(&b, frame++, 2);
    gfdm_effect_axis(&b, true, true, 192);
    check(&b, frame++, 1);
    gfdm_effect_axis(&b, true, true, 128);
    check(&b, frame++, 0);
    gfdm_effect_buttons(&b, true, true);
    check(&b, frame++, 0); /* Simultaneous opposite button edges cancel. */
    puts("PASS: native rotary phase sequence, press/hold/release, reverse, independent players, cumulative axes, wrap and repeated reads");
    return 0;
}
