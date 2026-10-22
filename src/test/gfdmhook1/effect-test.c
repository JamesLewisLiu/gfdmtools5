#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "gfdmhook1/effect.c"

static void check(struct gfdm_effect_state *s, uint32_t frame, unsigned int phase)
{
    static const uint32_t masks[] = {0, 0x10000, 0x8000, 0x18000};
    uint32_t result = gfdm_effect_publish(s, frame);
    if (result != masks[phase]) {
        fprintf(stderr, "frame %u: expected phase %u, got %u, pending %d\n",
            frame, phase, s->phase, s->pending);
    }
    assert(result == masks[phase]);
    assert(s->phase == phase);
}

int main(void)
{
    struct gfdm_effect_state a = {0}, b = {0};
    uint32_t frame = 0;
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
