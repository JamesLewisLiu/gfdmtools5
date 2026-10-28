#ifndef GENINPUT_MIDI_H
#define GENINPUT_MIDI_H

#include <stdbool.h>

void midi_init(void);
void midi_enable(const char *game_type);
void midi_fini(void);
/* Called with the HID manager locked, once before reading a snapshot. */
void midi_poll(void);
void midi_set_capture(bool capture);

#endif
