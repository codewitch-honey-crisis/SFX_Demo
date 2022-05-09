#include <Arduino.h>
#include <SPIFFS.h>

#include <queue>
#include <sfx_midi_clock.hpp>
#include <sfx_midi_file.hpp>

#include "midiusb.h"
using namespace sfx;

void dump_midi(stream* stm, const midi_file& file) {
    Serial.printf("Type: %d\nTimebase: %d\n", (int)file.type, (int)file.timebase);
    Serial.printf("Tracks: %d\n", (int)file.tracks_size);
    for (int i = 0; i < (int)file.tracks_size; ++i) {
        Serial.printf("\tOffset: %d, Size: %d, Preview: ", (int)file.tracks[i].offset, (int)file.tracks[i].size);
        stm->seek(file.tracks[i].offset);
        uint8_t buf[16];
        size_t sz = stm->read(buf, min((int)file.tracks[i].size, 16));
        for (int j = 0; j < sz; ++j) {
            Serial.printf("%02x", (int)buf[j]);
        }
        Serial.println();
    }
}
void dump_midi(const midi_message& msg) {
    if((int)msg.type()<=(int)midi_message_type::control_change) {
        Serial.printf("Channel: %d ",(int)msg.channel());
    }
    switch (msg.type()) {
        case midi_message_type::note_off:
            Serial.printf("Note Off: %d, %d\n", (int)msg.lsb(), (int)msg.msb());
            break;
        case midi_message_type::note_on:
            Serial.printf("Note On: %d, %d\n", (int)msg.lsb(), (int)msg.msb());
            break;
        case midi_message_type::polyphonic_pressure:
            Serial.printf("Poly pressure: %d, %d\n", (int)msg.lsb(), (int)msg.msb());
            break;
        case midi_message_type::control_change:
            Serial.printf("Control change: %d, %d\n", (int)msg.lsb(), (int)msg.msb());
            break;
        case midi_message_type::pitch_wheel_change:
            Serial.printf("Pitch wheel change: %d, %d\n", (int)msg.lsb(), (int)msg.msb());
            break;
        case midi_message_type::song_position:
            Serial.printf("Song position: %d, %d\n", (int)msg.lsb(), (int)msg.msb());
            break;
        case midi_message_type::program_change:
            Serial.printf("Program change: %d\n", (int)msg.value8);
            break;
        case midi_message_type::channel_pressure:
            Serial.printf("Channel pressure: %d\n", (int)msg.value8);
            break;
        case midi_message_type::song_select:
            Serial.printf("Song select: %d\n", (int)msg.value8);
            break;
        case midi_message_type::system_exclusive:
            Serial.printf("Systex data: Size of %d\n", (int)msg.sysex.size);
            break;
        case midi_message_type::reset:
            if (msg.meta.data == nullptr) {
                Serial.printf("Reset");

            } else {
                int32_t result;
                const uint8_t* p = midi_utility::decode_varlen(msg.meta.encoded_length, &result);
                if (p != nullptr) {
                    Serial.printf("Meta message: Type of %02x, Size of %d\n", (int)msg.meta.type, (int)result);
                } else {
                    Serial.println("Error reading message");
                }
            }
            break;
        case midi_message_type::end_system_exclusive:
            Serial.println("End sysex");
            break;
        case midi_message_type::active_sensing:
            Serial.println("Active sensing");
            break;
        case midi_message_type::start_playback:
            Serial.println("Start playback");
            break;
        case midi_message_type::stop_playback:
            Serial.println("Stop playback");
            break;
        case midi_message_type::tune_request:
            Serial.println("Tune request");
            break;
        case midi_message_type::timing_clock:
            Serial.println("Timing clock");
            break;
        default:
            Serial.printf("Illegal message: %02x\n", (int)msg.status);
            while (true)
                ;
    }
}


using midi_queue = std::queue<midi_stream_event>;
struct midi_clock_state {
    midi_clock* clock;
    midi_queue* queue;
    midi_file_source* source;
};
MIDIusb midi;
void tick_callback(uint32_t pending, unsigned long long elapsed, void* state) {
    uint8_t buf[3];
    midi_clock_state* st = (midi_clock_state*)state;
    while (true) {
        // TODO: since I haven't made a usb_midi_output_device
        // we have to do this manually
        // events on the queue?
        if (st->queue->size()) {
            // peek the next one
            const midi_stream_event& event = st->queue->front();
            // is it ready to be played
            if (event.absolute <= elapsed) {
                // special handing for midi meta file tempo events
                if (event.message.type() == midi_message_type::meta_event && event.message.meta.type == 0x51) {
                    int32_t mt = (event.message.meta.data[0] << 16) | (event.message.meta.data[1] << 8) | event.message.meta.data[2];
                    // update the clock microtempo
                    st->clock->microtempo(mt);
                }
                if (event.message.status != 0xFF || event.message.meta.data == nullptr) {
                    // send a sysex message
                    if (event.message.type() == midi_message_type::system_exclusive && event.message.sysex.data != nullptr) {
                        uint8_t* p = (uint8_t*)malloc(event.message.sysex.size + 1);
                        if (p != nullptr) {
                            *p = event.message.status;
                            memcpy(p + 1, event.message.sysex.data, event.message.sysex.size);
                            tud_midi_stream_write(0, p, event.message.sysex.size + 1);
                            free(p);
                        }
                    } else {
                        // send a regular message
                        // build a buffer and send it using raw midi
                        buf[0] = event.message.status;
                        if ((int)event.message.type() <= (int)midi_message_type::control_change) {
                            switch (event.message.wire_size()) {
                                case 1:
                                    //tud_midi_stream_write(event.message.channel(), buf, 1);
                                    tud_midi_stream_write(0, buf, 1);
                                    break;
                                case 2:
                                    buf[1] = event.message.value8;
                                    //tud_midi_stream_write(event.message.channel(), buf, 2);
                                    tud_midi_stream_write(0, buf, 2);
                                    break;
                                case 3:
                                    buf[1] = event.message.lsb();
                                    buf[2] = event.message.msb();
                                    //tud_midi_stream_write(event.message.channel(), buf, 3);
                                    tud_midi_stream_write(0, buf, 3);
                                    break;
                                default:
                                    break;
                            }
                        } else {
                            switch (event.message.wire_size()) {
                                case 1:
                                    tud_midi_stream_write(0, buf, 1);
                                    break;
                                case 2:
                                    buf[1] = event.message.value8;
                                    tud_midi_stream_write(0, buf, 2);
                                    break;
                                case 3:
                                    buf[1] = event.message.lsb();
                                    buf[2] = event.message.msb();
                                    tud_midi_stream_write(0, buf, 3);
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                }
                // ensure the message gets destroyed
                // (necessary? i don't think so but i'd rather not leak)
                event.message.~midi_message();
                // remove the message
                st->queue->pop();
            } else {
                break;
            }
        } else {
            break;
        }
    }
}
void setup() {
    // an midi event source over a midi file
    midi_file_source msrc;
    // a midi clock
    midi_clock mclk;
    // a queue used to hold pending events
    midi_queue mqueue;
    // the state for the callback
    midi_clock_state mstate;
    mstate.clock = &mclk;
    mstate.queue = &mqueue;
    mstate.source = &msrc;
    Serial.begin(115200);
    mclk.tick_callback(tick_callback,&mstate);
    SPIFFS.begin();
    midi.setBaseEP(3);
    midi.begin();
    midi.setBaseEP(3);
    Serial.println("10 seconds to set up equipment starting now.");
    // must delay at least 1000!
    delay(10000);

    File file = SPIFFS.open("/indaclub.mid", "rb");
    // we use streams as a cross platform way to wrap platform dependent filesystem stuff
    file_stream fstm(file);
    // open the midi file source
    sfx_result r = midi_file_source::open(&fstm, &msrc);
    if (sfx_result::success != r) {
        Serial.printf("Error opening file: %d\n", (int)r);
        while (true)
            ;
    }
    // set the clock's timebase
    mclk.timebase(msrc.file().timebase);
    // start the clock
    mclk.start();
    while (true) {
        // the midi event
        midi_event e;
        // don't let the queue get overly big, there's no reason to
        if (mqueue.size() >= 16) {
            // just pump the clock
            mclk.update();
            continue;
        }
        // get the next event
        sfx_result r = msrc.receive(&e);
        if (r != sfx_result::success) {
            if (r == sfx_result::end_of_stream) {
                // pump the queue until out of messages
                while(mqueue.size()>0) {
                    mclk.update();
                }
                mclk.stop();
                msrc.reset();
                mclk.start();
                continue;
                
            }
            Serial.printf("Error receiving message: %d\n", (int)r);
            break;
        } else {
            dump_midi(e.message);
            // add the event to the queue
            mqueue.push({(unsigned long long)msrc.elapsed(), e.delta, e.message});
            // pump the clock
            mclk.update();
        }
    }
}


void loop() {

}

// #endif
