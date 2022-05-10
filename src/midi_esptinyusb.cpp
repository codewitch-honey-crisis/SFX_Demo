#include <midiusb.h>
#include <midi_esptinyusb.hpp>
namespace arduino {
MIDIusb midi_esptinyusb_midi;
bool midi_esptinyusb_initialized = false;
bool midi_esptinyusb::initialized() const {
    return midi_esptinyusb_initialized;
}
sfx::sfx_result midi_esptinyusb::initialize(const char* device_name) {
    if(!midi_esptinyusb_initialized) {
        midi_esptinyusb_initialized=true;
#ifdef CONFIG_IDF_TARGET_ESP32S3
        midi_esptinyusb_midi.setBaseEP(3);
#endif
        char buf[256];
        strncpy(buf,device_name==nullptr?"SFX MIDI Out":device_name,255);
        midi_esptinyusb_midi.begin(buf);
#ifdef CONFIG_IDF_TARGET_ESP32S3
        midi_esptinyusb_midi.setBaseEP(3);
#endif
        delay(1000);
        midi_esptinyusb_initialized = true;
    }
    return sfx::sfx_result::success;
}
sfx::sfx_result midi_esptinyusb::send(const sfx::midi_message& message) {
    sfx::sfx_result rr = initialize();
    if(rr!=sfx::sfx_result::success) {
        return rr;
    }
    uint8_t buf[3];
    if(message.type()==sfx::midi_message_type::meta_event && (message.meta.type!=0 || message.meta.data!=nullptr)) {
        return sfx::sfx_result::success;
    }
    if (message.type()==sfx::midi_message_type::system_exclusive) {
        // send a sysex message
        uint8_t* p = (uint8_t*)malloc(message.sysex.size + 1);
        if (p != nullptr) {
            *p = message.status;
            if(message.sysex.size) {
                memcpy(p + 1, message.sysex.data, message.sysex.size);
            }
            tud_midi_stream_write(0, p, message.sysex.size + 1);
            // write the end sysex
            *p=0xF7;
            tud_midi_stream_write(0, p, 1);
            free(p);
        }
    } else {
        // send a regular message
        // build a buffer and send it using raw midi
        buf[0] = message.status;
        switch (message.wire_size()) {
            case 1:
                tud_midi_stream_write(0, buf, 1);
                break;
            case 2:
                buf[1] = message.value8;
                tud_midi_stream_write(0, buf, 2);
                break;
            case 3:
                buf[1] = message.lsb();
                buf[2] = message.msb();
                tud_midi_stream_write(0, buf, 3);
                break;
            default:
                break;
        }            
    }
    return sfx::sfx_result::success;
}
}

