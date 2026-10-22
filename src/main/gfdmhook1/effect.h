#ifndef GFDMHOOK1_EFFECT_H
#define GFDMHOOK1_EFFECT_H

#include <stdbool.h>
#include <stdint.h>

#define GFDM_EFFECT_REVERSE_P1 (UINT32_C(1) << 10)
#define GFDM_EFFECT_REVERSE_P2 (UINT32_C(1) << 11)
#define GFDM_EFFECT_REVERSE_MASK \
    (GFDM_EFFECT_REVERSE_P1 | GFDM_EFFECT_REVERSE_P2)

struct gfdm_effect_state {
    uint8_t phase;
    bool clockwise;
    bool counterclockwise;
    bool axis_bound;
    bool axis_absolute;
    uint8_t axis_last;
    int axis_fraction;
    int pending;
    uint32_t frame;
    bool published;
};

void gfdm_effect_buttons(struct gfdm_effect_state *s, bool cw, bool ccw);
void gfdm_effect_axis(
    struct gfdm_effect_state *s, bool bound, bool absolute, uint8_t value);
uint32_t gfdm_effect_publish(struct gfdm_effect_state *s, uint32_t frame);

#endif
