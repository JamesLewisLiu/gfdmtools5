#ifndef GFDMHOOK1_COIN_H
#define GFDMHOOK1_COIN_H

#include <stdbool.h>
#include <stdint.h>

struct gfdm_coin_state {
    uint16_t total;
    uint16_t consumed;
    bool pressed;
};

/* Callers serialize access: P3IO reports the cumulative hardware counter,
   while the two libdevice getters share a separate consumption cursor. */
bool gfdm_coin_update(struct gfdm_coin_state *state, bool pressed);
uint16_t gfdm_coin_take(struct gfdm_coin_state *state, bool all);

#endif
