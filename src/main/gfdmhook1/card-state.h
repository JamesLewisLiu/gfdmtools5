#ifndef GFDMHOOK1_CARD_STATE_H
#define GFDMHOOK1_CARD_STATE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct gfdm_card_state {
    uint8_t uid[8], type;
    bool present, tap, read;
    int pending;
};

static inline void gfdm_card_clear(struct gfdm_card_state *s)
{
    memset(s->uid, 0, sizeof(s->uid));
    s->type = 0;
    s->present = s->tap = s->read = false;
}

static inline void gfdm_card_sample(struct gfdm_card_state *s, bool sensor)
{
    /* A contactless tap is released after the game consumes its cached UID.
       File-card insertion retains the existing explicit-eject lifecycle. */
    if (s->tap && s->read && !sensor)
        gfdm_card_clear(s);
}
#endif
