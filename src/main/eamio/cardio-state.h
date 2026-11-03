#ifndef EAMIO_CARDIO_STATE_H
#define EAMIO_CARDIO_STATE_H
#include <stdbool.h>
#include <stdint.h>

#define CARDIO_QUEUE_SIZE 16
struct cardio_card {
    uint8_t uid[8], type;
};
struct cardio_state {
    struct cardio_card queue[CARDIO_QUEUE_SIZE], current, last;
    unsigned int head, count, phase;
    uint32_t since;
    bool read;
};
bool cardio_state_offer(
    struct cardio_state *s, const uint8_t uid[8], uint8_t type);
void cardio_state_release(struct cardio_state *s);
bool cardio_state_sensor(struct cardio_state *s, uint32_t now, bool *owned);
uint8_t cardio_state_read(struct cardio_state *s, uint8_t *uid, uint8_t size);
#endif
