#pragma once
#include <string.h>
#include <sfx_midi_core.hpp>
#include <sfx_midi_clock.hpp>
#include "note_tracker.hpp"
class midi_sampler final {
    struct track {
        sfx::midi_clock clock;
        sfx::midi_event_ex event;
        note_tracker tracker;
        int32_t base_microtempo;
        float tempo_multiplier;
        uint8_t* buffer;
        size_t buffer_size;
        size_t buffer_position;
        sfx::midi_output* output;
    };
    void* (*m_allocator)(size_t);
    void (*m_deallocator)(void*);
    size_t m_tracks_size;
    track* m_tracks;

    static void callback(uint32_t pending,unsigned long long elapsed, void* state);
    void deallocate();
    midi_sampler(const midi_sampler& rhs)=delete;
    midi_sampler& operator=(const midi_sampler& rhs)=delete;
public:
    midi_sampler();
    midi_sampler(midi_sampler&& rhs);
    midi_sampler& operator=(midi_sampler&& rhs);
    ~midi_sampler();
    sfx::sfx_result update();
    void output(sfx::midi_output* value);
    sfx::sfx_result start(size_t index);
    sfx::sfx_result stop(size_t index);
    void tempo_multiplier(float value);
    static sfx::sfx_result read(sfx::stream* stream,midi_sampler* out_sampler,void*(allocator)(size_t)=::malloc,void(deallocator)(void*)=::free);
};