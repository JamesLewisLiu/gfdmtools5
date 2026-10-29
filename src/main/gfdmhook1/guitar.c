#include "gfdmhook1/guitar.h"

uint32_t gfdm_guitar_input(uint32_t state, int player)
{
    unsigned int p = player == 1;
    uint32_t result = 0;

    if (state & (UINT32_C(1) << (8 + p)))
        result |= 0x0004; /* START */
    if (state & (UINT32_C(1) << (12 + p)))
        result |= 0x0040; /* WAIL */
    if (state & (UINT32_C(1) << (18 + p)))
        result |= 0x0200; /* RED */
    if (state & (UINT32_C(1) << (20 + p)))
        result |= 0x0400; /* GREEN */
    if (state & (UINT32_C(1) << (22 + p)))
        result |= 0x0800; /* BLUE */

    /* A/B are alternate controller bindings for one native Pick (UP) input.
       Native DOWN is Wail, never the controller's second pick switch. Keep
       the existing action indices so saved bindings remain compatible. */
    if (state & (UINT32_C(3) << (24 + 2 * p)))
        result |= 0x0020;

    return result;
}
