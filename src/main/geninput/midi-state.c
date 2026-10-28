#include <string.h>

#include "geninput/midi-state.h"

void midi_state_reset(struct midi_state *state)
{
    unsigned int channel;

    memset(state, 0, sizeof(*state));
    for (channel = 0; channel < MIDI_CHANNELS; channel++) {
        state->pitch[channel] = 8192;
    }
}

void midi_state_message(struct midi_state *state, uint32_t message)
{
    unsigned int status = message & 255;
    unsigned int channel = status & 15;
    unsigned int key = (message >> 8) & 255;
    unsigned int value = (message >> 16) & 255;
    unsigned int index = channel * MIDI_KEYS + key;
    struct midi_note *note;

    if (status == 255) {
        midi_state_reset(state);
        return;
    }
    if (status < 128 || key >= 128 || value >= 128) {
        return;
    }
    switch (status & 0xF0) {
        case 0x80:
        case 0x90:
            note = &state->notes[index];
            note->held = (status & 0xF0) == 0x90 && value != 0;
            if (note->held) {
                if (note->pending < MIDI_PENDING_LIMIT) {
                    note->pending++;
                } else {
                    state->overflow = true;
                }
            }
            break;
        case 0xB0:
            if (key == 120 || key == 123) {
                memset(
                    &state->notes[channel * MIDI_KEYS],
                    0,
                    MIDI_KEYS * sizeof(state->notes[0]));
            } else if (key == 121) {
                memset(&state->cc[channel * MIDI_KEYS], 0, MIDI_KEYS);
                state->pitch[channel] = 8192;
            } else if (key < 120) {
                state->cc[index] = (uint8_t) value;
            }
            break;
        case 0xE0:
            state->pitch[channel] = (uint16_t) (key | (value << 7));
            break;
        default:
            break;
    }
}

void midi_state_poll(
    struct midi_state *state, uint64_t now, bool drum, bool capture)
{
    unsigned int i;
    struct midi_note *note;

    for (i = 0; i < MIDI_NOTES; i++) {
        note = &state->notes[i];
        if (note->pressed) {
            if (now >= note->deadline &&
                (drum || capture || !note->held || note->pending != 0)) {
                note->pressed = false;
                note->gap = true;
                /* Capture runs on a 17 ms window timer. Runtime reads use
                   the GFDM P3IO polling path, so do not add a frame of delay.
                 */
                note->deadline = now + (capture ? 20 : 2);
            }
        } else if (
            (!note->gap || now >= note->deadline) && note->pending != 0) {
            note->pending--;
            note->pressed = true;
            note->gap = false;
            note->deadline = now + 20;
        }
    }
}

int32_t midi_state_value(const struct midi_state *state, unsigned int control)
{
    if (control < MIDI_CC_BUTTON) {
        return state->notes[control].pressed;
    } else if (control < MIDI_CC_VALUE) {
        return state->cc[control - MIDI_CC_BUTTON] >= 64;
    } else if (control < MIDI_PITCH) {
        return state->cc[control - MIDI_CC_VALUE];
    } else if (control < MIDI_CONTROLS) {
        return state->pitch[control - MIDI_PITCH];
    }
    return 0;
}
