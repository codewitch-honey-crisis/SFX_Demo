#ifndef HTCW_MIDI_CONTEXT_HPP
#define HTCW_MIDI_CONTEXT_HPP
#include "sfx_midi_core.hpp"

struct midi_channel_context {
    uint8_t note[128];
    uint8_t control[128];
    uint8_t key_pressure[128];
    uint8_t pressure;
    int16_t pitch;
    uint8_t program;
};
struct midi_context {
    midi_channel_context channels[16];
};

#endif // HTCW_MIDI_CONTEXT_HPP