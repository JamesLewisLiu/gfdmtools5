#include "eamio/cardio-state.h"
#include "bemanitools/eamio.h"
#include <string.h>

void cardio_state_release(struct cardio_state *s)
{
    memset(&s->last, 0, sizeof(s->last));
}

bool cardio_state_offer(
    struct cardio_state *s, const uint8_t uid[8], uint8_t type)
{
    static const uint8_t zero[8];
    if (type != EAM_IO_CARD_ISO15696 && type != EAM_IO_CARD_FELICA)
        return false;
    if (!memcmp(uid, zero, 8)) {
        cardio_state_release(s);
        return true;
    }
    if (s->last.type == type && !memcmp(s->last.uid, uid, 8))
        return true;
    if (s->count == CARDIO_QUEUE_SIZE)
        return false;
    struct cardio_card *card =
        &s->queue[(s->head + s->count) % CARDIO_QUEUE_SIZE];
    memcpy(card->uid, uid, 8);
    card->type = type;
    s->last = *card;
    s->count++;
    return true;
}

/* Some firmware sends only a UID on each tap, without a zero removal report.
   Continuous held reports remain deduplicated; a one-second report-free gap
   permits the same card again. This is a tap heuristic, not RF presence. */
bool cardio_state_offer_timed(
    struct cardio_state *s, const uint8_t uid[8], uint8_t type, uint32_t now)
{
    if (type != EAM_IO_CARD_ISO15696 && type != EAM_IO_CARD_FELICA)
        return false;
    if (s->reported && (uint32_t) (now - s->last_report) >= 1000)
        cardio_state_release(s);
    s->reported = true;
    s->last_report = now;
    return cardio_state_offer(s, uid, type);
}

bool cardio_state_sensor(struct cardio_state *s, uint32_t now, bool *owned)
{
    *owned = true;
    if (s->phase == 1) {
        /* Do not replace a UID between the consumer's sensor and read calls. */
        if (!s->read || (uint32_t) (now - s->since) < 250)
            return true;
        s->phase = 2;
        s->since = now;
        return false;
    }
    if (s->phase == 2 && (uint32_t) (now - s->since) < 50)
        return false;
    if (s->phase != 3) {
        s->phase = 0;
        if (!s->count) {
            *owned = false;
            return false;
        }
        /* Force a sampled low even if the file-card sensor was held high. */
        s->phase = 3;
        s->since = now;
        return false;
    }
    if ((uint32_t) (now - s->since) < 50)
        return false;
    s->current = s->queue[s->head];
    s->head = (s->head + 1) % CARDIO_QUEUE_SIZE;
    s->count--;
    s->phase = 1;
    s->read = false;
    s->since = now;
    return true;
}

uint8_t cardio_state_read(struct cardio_state *s, uint8_t *uid, uint8_t size)
{
    if (s->phase != 1 || uid == NULL || size != 8)
        return EAM_IO_CARD_NONE;
    memcpy(uid, s->current.uid, 8);
    s->read = true;
    return s->current.type;
}
