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
#include "midi_sampler.hpp"
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
int32_t song_microtempo;
int32_t song_microtempo_old;
float tempo_multiplier;
midi_sampler sampler;
bool gblip;
using lcd_color = color<rgb_pixel<16>>;
uint8_t* prang_font_buffer;
size_t prang_font_buffer_size;
uint8_t* song_buffer;
size_t song_buffer_size;
midi_esptinyusb out;
int switches[4];
RingbufHandle_t signal_queue;
void dump_midi(stream* stm, const midi_file& file) {
    printf("Type: %d\nTimebase: %d\n", (int)file.type, (int)file.timebase);
    printf("Tracks: %d\n", (int)file.tracks_size);
    for (int i = 0; i < (int)file.tracks_size; ++i) {
        printf("\tOffset: %d, Size: %d, Preview: ", (int)file.tracks[i].offset, (int)file.tracks[i].size);
        stm->seek(file.tracks[i].offset);
        uint8_t buf[16];
        size_t tsz = file.tracks[i].size;
        size_t sz = stm->read(buf, tsz < 16 ? tsz : 16);
        for (int j = 0; j < sz; ++j) {
            printf("%02x", (int)buf[j]);
        }
        printf("\n");
    }
}
void setup() {
    pinMode(P_SW1,INPUT_PULLDOWN);
    pinMode(P_SW2,INPUT_PULLDOWN);
    pinMode(P_SW3,INPUT_PULLDOWN);
    pinMode(P_SW4,INPUT_PULLDOWN);
    memset(switches,0,sizeof(switches));
    tempo_encoder_old_count = 0;
    tempo_multiplier = 1.0;
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
    tempo_encoder_old_count = tempo_encoder.getCount()/4;
    midi_file_source msrc;
    --curfn;
    *curfn='/';
    file = SD.open(curfn, "rb");
    free(fns-1);
    draw::filled_rectangle(lcd,lcd.bounds(),color_t::white);
    const char* playing_text = "pLay1nG";
    float playing_scale = prangfnt.scale(125);
    ssize16 playing_size = prangfnt.measure_text(ssize16::max(),spoint16::zero(),playing_text,playing_scale);
    draw::text(lcd,playing_size.bounds().center((srect16)lcd.bounds()),spoint16::zero(),playing_text,prangfnt,playing_scale,color_t::red,color_t::white,false);
    free(prang_font_buffer);
    file_stream fs(file);
    sfx_result r=midi_sampler::read(&fs,&sampler);
    if(r!=sfx_result::success) {
        Serial.printf("Error loading MIDI file: %d\n",(int)r);
        while(true);
    }
    fs.seek(0);
    midi_file mf;
    midi_file::read(&fs,&mf);
    dump_midi(&fs,mf);
    file.close();
    sampler.output(&out);
    while(true) {

        int64_t ec = tempo_encoder.getCount()/4;
        if(ec!=tempo_encoder_old_count) {
            bool inc = ec<tempo_encoder_old_count;
            tempo_encoder_old_count=ec;
            if(inc) {
     
                tempo_multiplier+=.1;
            } else {
                tempo_multiplier-=.1;
    
            }
            Serial.printf("Tempo multiplier: %0.1f\n",tempo_multiplier);
            sampler.tempo_multiplier(tempo_multiplier);
        }
        int b=digitalRead(P_SW1);
        if(b!=switches[0]) {
            if(b) {
                sampler.start(0);
            } else {
                sampler.stop(0);
            }
            switches[0]=b;
        }
        b=digitalRead(P_SW2);
        if(b!=switches[1]) {
            if(b) {
                sampler.start(1);
            } else {
                sampler.stop(1);
            }
            switches[1]=b;
        }
        b=digitalRead(P_SW3);
        if(b!=switches[2]) {
            if(b) {
                sampler.start(2);
            } else {
                sampler.stop(2);
            }
            switches[2]=b;
        }
        b=digitalRead(P_SW4);
        if(b!=switches[3]) {
            if(b) {
                sampler.start(3);
            } else {
                sampler.stop(3);
            }
            switches[3]=b;
        }
        sampler.update();    
    }
    
}
void loop() {
    ESP.restart();
}
