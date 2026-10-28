#ifndef GENINPUT_MIDI_STATE_H
#define GENINPUT_MIDI_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define MIDI_CHANNELS 16
#define MIDI_KEYS 128
#define MIDI_NOTES (MIDI_CHANNELS * MIDI_KEYS)
#define MIDI_CC_BUTTON MIDI_NOTES
#define MIDI_CC_VALUE (MIDI_NOTES * 2)
#define MIDI_PITCH (MIDI_NOTES * 3)
#define MIDI_CONTROLS (MIDI_PITCH + MIDI_CHANNELS)
#define MIDI_USAGE_NOTE 0xFF700000u
#define MIDI_USAGE_CC_BUTTON 0xFF710000u
#define MIDI_USAGE_CC_VALUE 0xFF720000u
#define MIDI_USAGE_PITCH 0xFF730000u
#define MIDI_PENDING_LIMIT 1024

struct midi_note {
    unsigned int pending;
    bool held;
    bool pressed;
    bool gap;
    uint64_t deadline;
};

struct midi_state {
    struct midi_note notes[MIDI_NOTES];
    uint8_t cc[MIDI_NOTES];
    uint16_t pitch[MIDI_CHANNELS];
    bool overflow;
};

void midi_state_reset(struct midi_state *state);
void midi_state_message(struct midi_state *state, uint32_t message);
void midi_state_poll(
    struct midi_state *state, uint64_t now, bool drum, bool capture);
int32_t midi_state_value(const struct midi_state *state, unsigned int control);

#endif
