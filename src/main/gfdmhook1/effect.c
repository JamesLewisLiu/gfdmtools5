#include "gfdmhook1/effect.h"

static void gfdm_effect_queue(struct gfdm_effect_state *s, int steps)
{
    s->pending += steps;
    if (s->pending > 256) {
        s->pending = 256;
    }
    if (s->pending < -256) {
        s->pending = -256;
    }
}

void gfdm_effect_buttons(struct gfdm_effect_state *s, bool cw, bool ccw)
{
    gfdm_effect_queue(s, (cw && !s->clockwise) - (ccw && !s->counterclockwise));
    s->clockwise = cw;
    s->counterclockwise = ccw;
}

void gfdm_effect_axis(
    struct gfdm_effect_state *s, bool bound, bool absolute, uint8_t value)
{
    int delta;

    if (!bound) {
        s->axis_bound = false;
        return;
    }
    if (!s->axis_bound || s->axis_absolute != absolute) {
        s->axis_bound = true;
        s->axis_absolute = absolute;
        s->axis_last = value;
        s->axis_fraction = 0;
        if (absolute) {
            /* geninput absolute positions have a half-turn offset. */
            unsigned int position = (value + 128u) & 255u;
            int target = (int) (position / 64);
            int steps = (target - s->phase + 4) & 3;
            gfdm_effect_queue(s, steps > 2 ? steps - 4 : steps);
            s->axis_fraction = (int) (position % 64);
        }
        return;
    }

    /* Both mapper axis kinds return a persistent circular byte position,
       not a direction or a freshly consumed relative delta. One of the
       game's four phases spans a quarter of that normalized revolution. */
    delta = ((int) value - s->axis_last + 384) % 256 - 128;
    s->axis_last = value;
    s->axis_fraction += delta;
    while (s->axis_fraction >= 64) {
        gfdm_effect_queue(s, 1);
        s->axis_fraction -= 64;
    }
    while (s->axis_fraction < 0) {
        gfdm_effect_queue(s, -1);
        s->axis_fraction += 64;
    }
}

uint32_t gfdm_effect_publish(struct gfdm_effect_state *s, uint32_t frame)
{
    /* Only the device input consumer publishes a phase, at most once per
       rendered frame. Debug getters may sample repeatedly without skipping
       phases that the native game must observe in order. */
    if (!s->published || s->frame != frame) {
        s->published = true;
        s->frame = frame;
        if (s->pending > 0) {
            s->phase = (s->phase + 1) & 3;
            s->pending--;
        } else if (s->pending < 0) {
            s->phase = (s->phase + 3) & 3;
            s->pending++;
        }
    }
    /* Gameplay effect_num is 2*button7 + button6. The TEST display
       reverses those weights; do not use its displayed number as phase. */
    return ((s->phase & 2) ? UINT32_C(0x10000) : 0)
        | ((s->phase & 1) ? UINT32_C(0x8000) : 0);
}
