#include <Arduino.h>
#include <SPIFFS.h>
#include <SD.h>
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include <ESP32Encoder.h>
#include <tft_io.hpp>

#include <sfx_midi_file.hpp>
#include <sfx_midi_driver.hpp>
#include "midi_esptinyusb.hpp"
#include <ili9341.hpp>
#include <gfx_cpp14.hpp>
#include "telegrama.hpp"
using namespace std;
using namespace sfx;
using namespace gfx;
using namespace arduino;

#define P_SW1 48
#define P_SW2 38
#define P_SW3 39
#define P_SW4 40

#define LCD_CS 5
#define LCD_MOSI 7
#define LCD_MISO 10
#define LCD_SCLK 6

#define LCD_DC 4
#define LCD_RST 8
#define LCD_BL -1
using bus_t = tft_spi_ex<0,LCD_CS,LCD_MOSI,LCD_MISO,LCD_SCLK,SPI_MODE0,false>;
// DC, RST, BL
using lcd_t = ili9341<4,8,-1,bus_t,3,false,400,200>;
using color_t = color<typename lcd_t::pixel_type>;
using view_t = viewport<lcd_t>;
lcd_t lcd;
ESP32Encoder tempo_encoder;
int64_t tempo_encoder_old_count;
midi_clock gclock;
int32_t song_microtempo;
int32_t song_microtempo_old;

bool gblip;
using lcd_color = color<rgb_pixel<16>>;
uint8_t* prang_font_buffer;
size_t prang_font_buffer_size;
uint8_t* song_buffer;
size_t song_buffer_size;
midi_esptinyusb out;
midi_driver driver(out);
RingbufHandle_t signal_queue;
void setup() {
    pinMode(P_SW1,INPUT_PULLDOWN);
    pinMode(P_SW2,INPUT_PULLDOWN);
    pinMode(P_SW3,INPUT_PULLDOWN);
    pinMode(P_SW4,INPUT_PULLDOWN);
    tempo_encoder_old_count = 0;
    ESP32Encoder::useInternalWeakPullResistors=UP;
    gblip = false;
    Serial.begin(115200);
    SPIFFS.begin();
    signal_queue = xRingbufferCreate(sizeof(int32_t) * 8 + (sizeof(int32_t) - 1), RINGBUF_TYPE_NOSPLIT);
    if(signal_queue==nullptr) {
        Serial.printf("Unable to create signal queue");
        while(true);
        
    }
    tempo_encoder.attachFullQuad(37,36);
    // ensure the SPI bus is initialized
    lcd.initialize();
    SD.begin(11,spi_container<0>::instance());
    Serial.printf("Card size: %fGB\n",SD.cardSize()/1024.0/1024.0/1024.0);
    lcd.fill(lcd.bounds(),color_t::white);
    File file = SPIFFS.open("/MIDI.jpg","rb");
    draw::image(lcd,rect16(0,0,319,144).center_horizontal(lcd.bounds()),&file);
    file.close();
    file = SPIFFS.open("/PaulMaul.ttf","rb");
    file.seek(0,SeekMode::SeekEnd);
    size_t sz = file.position()+1;
    file.seek(0);
    prang_font_buffer=(uint8_t*)malloc(sz);
    if(prang_font_buffer==nullptr) {
        Serial.println("Out of memory loading font");
        while(true);
    }
    file.readBytes((char*)prang_font_buffer,sz);
    prang_font_buffer_size = sz;
    file.close();
    const_buffer_stream fntstm(prang_font_buffer,prang_font_buffer_size);

    open_font prangfnt;
    gfx_result gr = open_font::open(&fntstm,&prangfnt);
    if(gr!=gfx_result::success) {
        Serial.println("Error loading font.");
        while(true);
    }
    const char* title = "pr4nG";
    float title_scale = prangfnt.scale(200);
    ssize16 title_size = prangfnt.measure_text(ssize16::max(),spoint16::zero(),title,title_scale);
    draw::text(lcd,title_size.bounds().center_horizontal((srect16)lcd.bounds()).offset(0,45),spoint16::zero(),title,prangfnt,title_scale,color_t::red,color_t::white,true,true);
    gclock.tick_callback([](uint32_t pending,unsigned long long elapsed, void* state){
        size_t sz = sizeof(int32_t);
        int32_t *pmt = (int32_t*)xRingbufferReceive(
                  signal_queue,
                  &sz,0);
        if(pmt!=nullptr&&sz==sizeof(int32_t)) {
            if(*pmt!=0) {
                gclock.microtempo(*pmt);
                vRingbufferReturnItem(signal_queue,pmt);
                char sz[64];
                float tscale = Telegrama_otf.scale(30);
                sprintf(sz,"tempo %0.1f",gclock.tempo());
                ssize16 tsz = Telegrama_otf.measure_text(ssize16::max(),spoint16::zero(),sz,tscale);
                srect16 trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0,5);
                draw::filled_rectangle(lcd,srect16(0,trc.y1,lcd.dimensions().width-1,trc.y2),color_t::white);
                draw::text(lcd,trc,spoint16::zero(),sz,Telegrama_otf,tscale,color_t::black,color_t::white,false);
            } else {
                vRingbufferReturnItem(signal_queue,pmt);
                gclock.stop();
                gclock.start();
                return;
            }
        }
        for(int i = elapsed-pending;i<=elapsed;++i) {
            if(0==((i%(gclock.timebase()/2)))) {
                gblip=!gblip;
                draw::filled_ellipse(lcd,rect16(0,0,15,15).center_horizontal(lcd.bounds()).offset(0,38),gblip?color_t::red:color_t::white);
            }
        }
        
    });
    file = SD.open("/","r");
    size_t fn_count = 0;
    size_t fn_total = 0;
    while(true) {
        File f = file.openNextFile();
        if(!f) {
            break;
        }
        if(!f.isDirectory()) {
            const char* fn = f.name();
            size_t fnl = strlen(fn);
            if(fnl>4 && (0==strcmp(".mid",fn+fnl-4) || 0==strcmp(".MID",fn+fnl-4))) {
                ++fn_count;
                fn_total+=fnl+1;
            }    
        }
        f.close();
    }
    file.close();
    char* fns = (char*)malloc(fn_total+1)+1;
    if(fns==nullptr) {
        Serial.println("Out of memory");
        while(1);
    }
    file = SD.open("/","r");
    char* str = fns;
    while(true) {
        File f = file.openNextFile();
        if(!f) {
            break;
        }
        if(!f.isDirectory()) {
            const char* fn = f.name();
            size_t fnl = strlen(fn);
            if(fnl>4 && (0==strcmp(".mid",fn+fnl-4) || 0==strcmp(".MID",fn+fnl-4))) {
                memcpy(str,fn,fnl+1);
                str+=fnl+1;
            }    
        }
        f.close();
    }
    file.close();
    str = fns;
    for(int i = 0;i<fn_count;++i) {
        Serial.println(str);
        str+=strlen(str)+1;
    }
    delay(3000);
    draw::filled_rectangle(lcd,lcd.bounds(),color_t::white);
    char* curfn = fns;
    if(fn_count>1) {
        const char* seltext = "select filE";
        float fscale = prangfnt.scale(80);
        ssize16 tsz = prangfnt.measure_text(ssize16::max(),spoint16::zero(),seltext,fscale);
        srect16 trc = tsz.bounds().center_horizontal((srect16)lcd.bounds());
        draw::text(lcd,trc.offset(0,20),spoint16::zero(),seltext,prangfnt,fscale,color_t::red,color_t::white,false);
        fscale = Telegrama_otf.scale(20);
        bool done = false;
        size_t fni=0;
        int64_t ocount = tempo_encoder.getCount()/4;
        int osw = digitalRead(P_SW1) || digitalRead(P_SW2) || digitalRead(P_SW3) || digitalRead(P_SW4);
        while(!done) {
            
            tsz= Telegrama_otf.measure_text(ssize16::max(),spoint16::zero(),curfn,fscale);
            trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0,110);
            draw::filled_rectangle(lcd,srect16(0,trc.y1,lcd.dimensions().width-1,trc.y2),color_t::white);
            draw::text(lcd,trc,spoint16::zero(),curfn,Telegrama_otf,fscale,color_t::black,color_t::white,false);
            bool inc;
            while(ocount==(tempo_encoder.getCount()/4)) {
                int sw = digitalRead(P_SW1) || digitalRead(P_SW2) || digitalRead(P_SW3) || digitalRead(P_SW4);
                if(osw!=sw && !sw) {
                    // button was released
                    done = true;
                    break;
                }
                osw=sw;
                delay(1);
            }
            if(!done) {
                int64_t count = (tempo_encoder.getCount()/4);
                inc = ocount>count;
                ocount = count;
                if(inc) {
                    if(fni<fn_count-1) {
                        ++fni;
                        curfn+=strlen(curfn)+1;
                    }
                } else {
                    if(fni>0) {
                        --fni;
                        curfn=fns;
                        for(int j = 0;j<fni;++j) {
                            curfn+=strlen(curfn)+1;
                        }
                    }
                }
            }
        }
    }
    // avoids the 1 second init delay later
    out.initialize();
    midi_file_source msrc;
    --curfn;
    *curfn='/';
    file = SD.open(curfn, "rb");
    free(fns-1);
    file.seek(0,SeekMode::SeekEnd);
    sz = file.position();
    file.seek(0);
    Serial.printf("Midi size is %d bytes\n",(int)sz);
    // load the stream into memory because SPIFFS
    // is too slow for faster MIDIs.
    song_buffer = (uint8_t*)malloc(sz);
    if(song_buffer==nullptr) {
        Serial.println("Out of memory loading MIDI");
        while(true);
    }
    file.readBytes((char*)song_buffer,sz);
    song_buffer_size = sz;
    file.close();
    draw::filled_rectangle(lcd,lcd.bounds(),color_t::white);
    const char* playing_text = "pLay1nG";
    float playing_scale = prangfnt.scale(125);
    ssize16 playing_size = prangfnt.measure_text(ssize16::max(),spoint16::zero(),playing_text,playing_scale);
    draw::text(lcd,playing_size.bounds().center((srect16)lcd.bounds()),spoint16::zero(),playing_text,prangfnt,playing_scale,color_t::red,color_t::white,false);
    free(prang_font_buffer);
    delay(5000);
    
    float tscale = Telegrama_otf.scale(30);
    char tempsz[64];
    sprintf(tempsz,"tempo %0.1f",gclock.tempo());
    ssize16 tsz = Telegrama_otf.measure_text(ssize16::max(),spoint16::zero(),tempsz,tscale);
    srect16 trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0,5);
    draw::text(lcd,trc,spoint16::zero(),tempsz,Telegrama_otf,tscale,color_t::black,color_t::white,false);
    
    
    // we use streams as a cross platform way to wrap platform dependent filesystem stuff
    //file_stream stm(file);
    const_buffer_stream stm(song_buffer,song_buffer_size);
    // open the midi file source
    sfx_result r = midi_file_source::open(&stm, &msrc);
    if (sfx_result::success != r) {
        Serial.printf("Error opening file: %d\n", (int)r);
        while (true)
            ;
    }
    TaskHandle_t gfxhandle;
    xTaskCreatePinnedToCore([](void* state){
        while(true) {
            gclock.update();
        }

    },"GFX_Core",8000,nullptr,0,&gfxhandle, 1-xPortGetCoreID());
    //driver.tempo_scale(1.5);
    driver.timebase(msrc.file().timebase);
    gclock.timebase(driver.timebase());
    
    driver.start();
    gclock.start();
    while(true) {

        int64_t ec = tempo_encoder.getCount()/2;
        if(ec!=tempo_encoder_old_count) {
            tempo_encoder_old_count = ec;
            if(ec<-100) {
                ec=-100;
            } else if(ec>100) {
                ec=100;
            }
            ec=ec+100;
            Serial.printf("multiplier %f\n",ec/100.0);
            driver.velocity_scale(ec/100.0);
    
            
    
        }
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
                song_microtempo = 0;
                xRingbufferSend(signal_queue, &song_microtempo, sizeof(int32_t), portMAX_DELAY);
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
        if(driver.microtempo()!=song_microtempo) {
            song_microtempo=driver.microtempo();
            xRingbufferSend(signal_queue, &song_microtempo, sizeof(int32_t), portMAX_DELAY);
        }
        
    }
    if(gfxhandle!=nullptr) {
        vTaskDelete(gfxhandle);
    }
    if(signal_queue!=nullptr) {
        vRingbufferDelete(signal_queue);
    }
    if(song_buffer!=nullptr) {
        free(song_buffer);
    }
    
}
void loop() {
    ESP.restart();
}
