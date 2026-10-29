#ifndef GFDMHOOK1_GUITAR_H
#define GFDMHOOK1_GUITAR_H

#include <stdint.h>

/* Translate mapped GF panel actions. Effector phases are published separately.
 */
uint32_t gfdm_guitar_input(uint32_t state, int player);

#endif
