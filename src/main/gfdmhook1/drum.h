#ifndef GFDMHOOK1_DRUM_H
#define GFDMHOOK1_DRUM_H

#include <stdint.h>

/* DM uses bank 0 for pads/START and bank 1 for menu navigation. */
static inline uint32_t gfdm_drum_input(uint32_t state, int bank)
{
    uint32_t result = 0;
    if (bank == 0) {
        if (state & (1u << 8)) result |= 0x0004;
        if (state & (1u << 10)) result |= 0x0020;
        if (state & (1u << 12)) result |= 0x0040;
        if (state & (1u << 14)) result |= 0x0080;
        if (state & (1u << 16)) result |= 0x0100;
        if (state & (1u << 18)) result |= 0x0200;
        if (state & (1u << 22)) result |= 0x0800;
    } else if (bank == 1) {
        if (state & (1u << 15)) result |= 0x0080;
        if (state & (1u << 17)) result |= 0x0100;
    }
    return result;
}

#endif
