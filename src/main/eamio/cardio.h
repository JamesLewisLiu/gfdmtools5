#ifndef EAMIO_CARDIO_H
#define EAMIO_CARDIO_H
#include <stdbool.h>
#include <stdint.h>

/* Optional stock-EAMIO extension. No work occurs before explicit start. */
bool eam_io_cardio_start(void);
void cardio_stop(void);
uint8_t cardio_sensor(uint8_t unit, bool *owned);
uint8_t cardio_read(uint8_t unit, uint8_t *uid, uint8_t size);
#endif
