#include <Arduino.h>
#include <SPIFFS.h>

#include <sfx_midi_file.hpp>
#include <sfx_midi_driver.hpp>
#include "midi_esptinyusb.hpp"

using namespace sfx;
using namespace arduino;
midi_esptinyusb out;
midi_driver driver(out);
void setup() {
    Serial.begin(115200);
    SPIFFS.begin();
    // avoids the 1 second init delay later
    out.initialize();
    midi_file_source msrc;
    File file = SPIFFS.open("/awarmplc.mid", "rb");
    // we use streams as a cross platform way to wrap platform dependent filesystem stuff
    file_stream fstm(file);
    // open the midi file source
    sfx_result r = midi_file_source::open(&fstm, &msrc);
    if (sfx_result::success != r) {
        Serial.printf("Error opening file: %d\n", (int)r);
        while (true)
            ;
    }
    driver.tempo_scale(1.5);
    driver.timebase(msrc.file().timebase);
    driver.start();
    while(true) {
        midi_event e;
        midi_event_ex mse;
        r=msrc.receive(&e);
        mse.delta = e.delta;
        mse.absolute = msrc.elapsed();
        mse.message = e.message;
        if(sfx_result::success!=r) {
            if(r!=sfx_result::end_of_stream) {
                Serial.println("Error receiving event. Exiting.");
                break;
            } else {
                msrc.reset();
                while(driver.queued()) {
                    r=driver.update();
                    if(sfx_result::success!=r) {
                        Serial.println("Error updating driver. Exiting.");
                        break;
                    }
                }
                driver.stop();
                driver.start();
                continue;
            }
        }
        while(driver.full()) {
            r=driver.update();
            if(sfx_result::success!=r) {
                Serial.println("Error updating driver. Exiting.");
                break;
            }
        }
        r=driver.send(mse);
        if(sfx_result::success!=r) {
            Serial.println("Error sending to driver. Exiting.");
            break;
        }
        
    }
}
void loop() {

}