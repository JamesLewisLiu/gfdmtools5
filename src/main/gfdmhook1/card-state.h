#ifndef GFDMHOOK1_CARD_STATE_H
#define GFDMHOOK1_CARD_STATE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct gfdm_card_state {
    uint8_t uid[8], type;
    bool present, tap;
    int pending;
};

static inline int gfdm_card_target(
    int source, bool guitar, bool tap, bool numlock)
{
    /* Match EAMIO keypad multiplexing: NumLock off = P1, on = P2.
       Resolve only a new CardIO event; never move an inserted card. */
    return guitar && tap ? (numlock ? 1 : 0) : source;
}

static inline void gfdm_card_clear(struct gfdm_card_state *s)
{
    memset(s->uid, 0, sizeof(s->uid));
    s->type = 0;
    s->present = s->tap = false;
}

static inline bool gfdm_card_insert(
    struct gfdm_card_state *s, const uint8_t uid[8], uint8_t type, bool tap)
{
    /* CardIO reports a tap, but the game expects a motorized reader that keeps
       the card inside until explicit eject. Reading or RF silence is not eject.
       Drain later taps without replacing the card in an active session. */
    if (s->present && s->tap)
        return false;
    memcpy(s->uid, uid, sizeof(s->uid));
    s->type = type;
    s->present = true;
    s->tap = tap;
    return true;
}
#endif
