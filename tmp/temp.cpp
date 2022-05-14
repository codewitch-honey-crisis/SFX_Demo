#include <Arduino.h>
#include <SPI.h>

#define LCD_CS 5
#define LCD_MOSI 7
#define LCD_MISO 10
#define LCD_SCLK 6

#define LCD_DC 4
#define LCD_RST 8
#define LCD_BL -1
SPISettings spiSettings;
void SendCommand(uint8_t value) {
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    //SPI.beginTransaction(spiSettings);
    SPI.transfer(value);
    //SPI.endTransaction();
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_DC, HIGH);
}
void SendData(uint8_t value) {
    digitalWrite(LCD_CS, LOW);
    // already high
    // digitalWrite(LCD_DC,HIGH);
    //SPI.beginTransaction(spiSettings);
    SPI.transfer(value);
    //SPI.endTransaction();
    digitalWrite(LCD_CS, HIGH);
}
void InitDisplay() {
    pinMode(LCD_DC, OUTPUT);
    digitalWrite(LCD_DC, HIGH);
    if (LCD_BL != -1) {
        pinMode(LCD_BL, OUTPUT);
        digitalWrite(LCD_BL, HIGH);
    }
    digitalWrite(LCD_RST, HIGH);
    delay(5);
    digitalWrite(LCD_RST, LOW);
    delay(20);
    digitalWrite(LCD_RST, HIGH);
    delay(150);
    SPI.beginTransaction(spiSettings);
    SendCommand(0xEF);
    SendData(0x03);
    SendData(0x80);
    SendData(0x02);

    SendCommand(0xCF);
    SendData(0x00);
    SendData(0XC1);
    SendData(0X30);

    SendCommand(0xED);
    SendData(0x64);
    SendData(0x03);
    SendData(0X12);
    SendData(0X81);

    SendCommand(0xE8);
    SendData(0x85);
    SendData(0x00);
    SendData(0x78);

    SendCommand(0xCB);
    SendData(0x39);
    SendData(0x2C);
    SendData(0x00);
    SendData(0x34);
    SendData(0x02);

    SendCommand(0xF7);
    SendData(0x20);

    SendCommand(0xEA);
    SendData(0x00);
    SendData(0x00);

    SendCommand(0xC0);  // Power control
    SendData(0x23);     // VRH[5:0]

    SendCommand(0xC1);  // Power control
    SendData(0x10);     // SAP[2:0];BT[3:0]

    SendCommand(0xC5);  // VCM control
    SendData(0x3e);
    SendData(0x28);

    SendCommand(0xC7);  // VCM control2
    SendData(0x86);     //--

    SendCommand(0x36);  // Memory Access Control

    SendData(0x40 | 0x08);  // Rotation 0 (portrait mode)

    SendCommand(0x3A);
    SendData(0x55);

    SendCommand(0xB1);
    SendData(0x00);
    SendData(0x13);  // 0x18 79Hz, 0x1B default 70Hz, 0x13 100Hz

    SendCommand(0xB6);  // Display Function Control
    SendData(0x08);
    SendData(0x82);
    SendData(0x27);

    SendCommand(0xF2);  // 3Gamma Function Disable
    SendData(0x00);

    SendCommand(0x26);  // Gamma curve selected
    SendData(0x01);

    SendCommand(0xE0);  // Set Gamma
    SendData(0x0F);
    SendData(0x31);
    SendData(0x2B);
    SendData(0x0C);
    SendData(0x0E);
    SendData(0x08);
    SendData(0x4E);
    SendData(0xF1);
    SendData(0x37);
    SendData(0x07);
    SendData(0x10);
    SendData(0x03);
    SendData(0x0E);
    SendData(0x09);
    SendData(0x00);

    SendCommand(0xE1);  // Set Gamma
    SendData(0x00);
    SendData(0x0E);
    SendData(0x14);
    SendData(0x03);
    SendData(0x11);
    SendData(0x07);
    SendData(0x31);
    SendData(0xC1);
    SendData(0x48);
    SendData(0x08);
    SendData(0x0F);
    SendData(0x0C);
    SendData(0x31);
    SendData(0x36);
    SendData(0x0F);

    SendCommand(0x11);  // Exit Sleep
    SPI.endTransaction();
    delay(120);
    SPI.beginTransaction(spiSettings);
    SendCommand(0x29);
    SPI.endTransaction();
}
void BeginWindow(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2, bool read) {
    SPI.beginTransaction(spiSettings);
    SendCommand(0x2A);
    digitalWrite(LCD_CS,LOW);
    SPI.transfer16(x1);
    SPI.transfer16(x2);
    SendCommand(0x2B);
    digitalWrite(LCD_CS,LOW);
    SPI.transfer16(y1);
    SPI.transfer16(y2);
    if(read) {
        SendCommand(0x2E);
    } else {
        SendCommand(0x2C);
    }
}
void EndWindow() {
    SPI.endTransaction();
}
void FillScreen(uint16_t color) {
    BeginWindow(0,0,319,239,false);
    digitalWrite(LCD_CS,LOW);
    for(int i = 0;i<320*240;++i) {
        SPI.transfer16(color);
    }
    EndWindow();
}
void setup() {
    Serial.begin(115200);
    pinMode(LCD_CS, OUTPUT);
    digitalWrite(LCD_CS, HIGH);
    SPI.begin(LCD_SCLK, LCD_MISO, LCD_MOSI);
    spiSettings._bitOrder = MSBFIRST;
    spiSettings._clock = 20 * 1000 * 1000;
    spiSettings._dataMode = SPI_MODE0;
    InitDisplay();
    FillScreen(0x0000);
}
void loop() {
}