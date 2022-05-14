#include "midi_sampler.hpp"
#include <sfx_midi_stream.hpp>
#include <sfx_midi_file.hpp>
using namespace sfx;
void midi_sampler::callback(uint32_t pending,unsigned long long elapsed, void* pstate) {
    track *t = (track*)pstate;
    while(t->event.absolute<=elapsed) {
        if (t->event.message.type() == midi_message_type::meta_event) {
            // if it's a tempo event update the clock tempo
            if(t->event.message.meta.type == 0x51) {
                int32_t mt = (t->event.message.meta.data[0] << 16) | (t->event.message.meta.data[1] << 8) | t->event.message.meta.data[2];
                // update the clock microtempo
                t->base_microtempo = mt;
                t->clock.microtempo(mt/t->tempo_multiplier);
            }
        }
        else if(t->event.message.status!=0) {    
            t->tracker.process(t->event.message);
            if(t->output!=nullptr) {
                t->output->send(t->event.message);
            }
        }
        bool restarted = false;
        if(t->buffer_position>=t->buffer_size) {
            t->buffer_position = 0;
            t->event.absolute = 0;
            t->event.delta = 0;
            t->event.message.~midi_message();
            t->event.message.status=0;
            t->clock.stop();
            t->clock.microtempo(t->base_microtempo/t->tempo_multiplier);
            t->clock.start();
            restarted = true;
            if(t->output!=nullptr) {
                t->tracker.send_off(*t->output);
            }
        }

        const_buffer_stream cbs(t->buffer,t->buffer_size);
        cbs.seek(t->buffer_position);
        size_t sz = midi_stream::decode_event(true,(stream*)&cbs,&t->event);
        t->buffer_position+=sz;
        if(sz==0) {
            t->clock.stop();
        }
        if(restarted) {
            break;
        }
    }
}
void midi_sampler::deallocate() {
    if(m_deallocator!=nullptr) {
        // free everything   
        if(m_tracks!=nullptr) {
            for(size_t i = 0;i<m_tracks_size;++i) {
                track* t = m_tracks+i;
                if(t->buffer!=nullptr) {
                    m_deallocator(t->buffer);
                }
            }
            m_deallocator(m_tracks);
            m_tracks = nullptr;
            m_tracks_size = 0;
        }
    }
}
midi_sampler::midi_sampler() : m_allocator(nullptr),m_deallocator(nullptr),m_tracks_size(0),m_tracks(nullptr){

}
midi_sampler::midi_sampler(midi_sampler&& rhs) {
    m_allocator = rhs.m_allocator;
    m_deallocator = rhs.m_deallocator;
    m_tracks_size = rhs.m_tracks_size;
    m_tracks = rhs.m_tracks;
    rhs.m_deallocator = nullptr;
}
midi_sampler& midi_sampler::operator=(midi_sampler&& rhs) {
    deallocate();
    m_allocator = rhs.m_allocator;
    m_deallocator = rhs.m_deallocator;
    m_tracks_size = rhs.m_tracks_size;
    m_tracks = rhs.m_tracks;
    rhs.m_deallocator = nullptr;
    return *this;
}
midi_sampler::~midi_sampler() {
    deallocate();
}
sfx_result midi_sampler::read(stream* in,midi_sampler* out_sampler,void*(allocator)(size_t),void(deallocator)(void*)) {
    if(in==nullptr || out_sampler==nullptr||allocator==nullptr||deallocator==nullptr) {
        return sfx_result::invalid_argument;
    }
    if(!in->caps().read || !in->caps().seek) {
        return sfx_result::io_error;
    }
    midi_file file;
    sfx_result res = midi_file::read(in,&file);
    if(res!=sfx_result::success) {
        return res;
    }
    track *tracks = (track*)allocator(sizeof(track)*file.tracks_size);
    if(tracks==nullptr) {
        return sfx_result::out_of_memory;
    }
    for(int i = 0;i<file.tracks_size;++i) {
        track *t = tracks+i;
        *t = track();
        t->buffer = nullptr;
    }
    for(int i = 0;i<file.tracks_size;++i) {
        track *t = tracks+i;
        midi_track *mt = file.tracks+i;
        t->buffer = (uint8_t*)allocator(file.tracks[i].size);
        if(t->buffer==nullptr) {
            res= sfx_result::out_of_memory;
            goto free_all;
        }
        
        if(mt->offset!=in->seek(mt->offset) || mt->size!=in->read(t->buffer,mt->size)) {
            res = sfx_result::io_error;
            goto free_all;
        }
        t->tempo_multiplier = 1.0;
        t->base_microtempo = 500000;
        t->clock.timebase(file.timebase);
        t->clock.microtempo(500000);
        t->clock.tick_callback(callback,t);
        t->buffer_size = mt->size;
        t->buffer_position = 0;
        t->output = nullptr;
    }
    out_sampler->m_allocator = allocator;
    out_sampler->m_deallocator = deallocator;
    out_sampler->m_tracks = tracks;
    out_sampler->m_tracks_size = file.tracks_size;
    return sfx_result::success;
free_all:
    if(tracks!=nullptr) {
        for(size_t i=0;i<file.tracks_size;++i) {
            track& t = tracks[i];
            if(t.buffer!=nullptr)  {
                deallocator(t.buffer);
            }
        }
        deallocator(tracks);
    }
    return res;
}
sfx_result midi_sampler::update() {
    for(size_t i = 0;i<m_tracks_size;++i) {
        track *t = m_tracks + i;
        t->clock.update();
    }
    return sfx_result::success;
}
void midi_sampler::output(midi_output* value) {
    for(size_t i = 0;i<m_tracks_size;++i) {
        m_tracks[i].output = value;
    }
}
sfx_result midi_sampler::start(size_t index) {
    if(0>index || index>=m_tracks_size) {
        return sfx_result::invalid_argument;
    }
    track *t = m_tracks+index;
    stop(index);
    t->clock.start();
    return sfx_result::success;
}
sfx_result midi_sampler::stop(size_t index) {
    if(0>index || index>=m_tracks_size) {
        return sfx_result::invalid_argument;
    }
    track *t = m_tracks+index;
    t->clock.stop();
    t->buffer_position = 0;
    t->event.absolute = 0;
    t->event.delta = 0;
    t->event.message.~midi_message();
    t->event.message.status = 0;
    t->base_microtempo = 500000;
    t->clock.microtempo(t->base_microtempo/t->tempo_multiplier);
    if(t->output!=nullptr) {
        t->tracker.send_off(*t->output);
    }
    return sfx_result::success;
}
void midi_sampler::tempo_multiplier(float value) {
    if(value!=value || value==0 || value>5) {
        return;
    }
    for(int i = 0;i<m_tracks_size;++i) {
        track *t = m_tracks + i;
        t->tempo_multiplier = value;
        t->clock.microtempo(t->base_microtempo/value);
    }
}
