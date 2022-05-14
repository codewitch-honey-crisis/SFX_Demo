#include "note_tracker.hpp"
#include <string.h>
note_tracker::note_tracker() {
    memset(m_notes,0,sizeof(m_notes));
}
void note_tracker::process(const sfx::midi_message& message) {
    sfx::midi_message_type t = message.type();
    if(t==sfx::midi_message_type::note_off || (t==sfx::midi_message_type::note_on && message.msb()==0)) {
        uint8_t c = message.channel();
        uint8_t n = message.lsb();
        if(n<64) {
            const uint64_t mask = uint64_t(~(1<<n));    
            m_notes[c].low&=mask;
        } else {
            const uint64_t mask = uint64_t(~(1<<(n-64)));
            m_notes[c].high&=mask;
        }
    } else if(t==sfx::midi_message_type::note_on) {
        uint8_t c = message.channel();
        uint8_t n = message.lsb();
        if(n<64) {
            const uint64_t set = uint64_t(1<<n);    
            m_notes[c].low|=set;
        } else {
            const uint64_t set = uint64_t(1<<(n-64));    
            m_notes[c].high|=set;
        }
    }
}
void note_tracker::send_off(sfx::midi_output& output) {
    for(int i = 0;i<16;++i) {
        for(int j=0;j<64;++j) {
            const uint64_t mask = uint64_t(1<<j);
            if(m_notes[i].low&mask) {
                sfx::midi_message msg;
                msg.status = uint8_t(uint8_t(sfx::midi_message_type::note_off)|uint8_t(i));
                msg.lsb(j);
                msg.msb(0);
                output.send(msg);
            }
        }
        for(int j=0;j<64;++j) {
            const uint64_t mask = uint64_t(1<<j);
            if(m_notes[i].high&mask) {
                sfx::midi_message msg;
                msg.status = uint8_t(uint8_t(sfx::midi_message_type::note_off)|uint8_t(i));
                msg.lsb(j+64);
                msg.msb(0);
                output.send(msg);
            }
        }
    }
    memset(m_notes,0,sizeof(m_notes));
}