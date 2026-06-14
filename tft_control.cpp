// tft_control.cpp
#include "tft_control.h"
// ピン定義
#undef TFT_CS
#define TFT_CS PD7
#undef TFT_DC
#define TFT_DC PD11
#undef TFT_RST
#define TFT_RST PE1
#undef TFT_WR
#define TFT_WR PD5
#undef TFT_RD
#define TFT_RD PD4
#undef TFT_BL
#define TFT_BL PD13

// 液晶への1バイト書き込み (8bitパラレル) - 通信の核
void TFTControl::writeBusFast(uint8_t val) {
    uint32_t d_bsrr = (val & 0x01) ? (1 << 14) : (1 << (14 + 16));
    d_bsrr |= (val & 0x02) ? (1 << 15) : (1 << (15 + 16));
    d_bsrr |= (val & 0x04) ? (1 << 0)  : (1 << (0 + 16));
    d_bsrr |= (val & 0x08) ? (1 << 1)  : (1 << (1 + 16));
    GPIOD->BSRR = d_bsrr;

    uint32_t e_bsrr = (val & 0x10) ? (1 << 7)  : (1 << (7 + 16));
    e_bsrr |= (val & 0x20) ? (1 << 8)  : (1 << (8 + 16));
    e_bsrr |= (val & 0x40) ? (1 << 9)  : (1 << (9 + 16));
    e_bsrr |= (val & 0x80) ? (1 << 10) : (1 << (10 + 16));
    GPIOE->BSRR = e_bsrr;

    GPIOD->BRR = (1 << 5); 
    __asm__("nop"); 
    GPIOD->BSRR = (1 << 5);
}

void TFTControl::tftbegin() {
    pinMode(TFT_CS, OUTPUT); pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT); pinMode(TFT_WR, OUTPUT);
    pinMode(TFT_RD, OUTPUT);// pinMode(TFT_BL, OUTPUT);
    
    pinMode(PD14, OUTPUT); pinMode(PD15, OUTPUT);
    pinMode(PD0,  OUTPUT); pinMode(PD1,  OUTPUT);
    pinMode(PE7,  OUTPUT); pinMode(PE8,  OUTPUT);
    pinMode(PE9,  OUTPUT); pinMode(PE10, OUTPUT);

    digitalWrite(TFT_CS, HIGH); digitalWrite(TFT_WR, HIGH);
    digitalWrite(TFT_RD, HIGH); //digitalWrite(TFT_BL, HIGH);

    digitalWrite(TFT_RST, LOW); delay(50);
    digitalWrite(TFT_RST, HIGH); delay(50);

    sendCmd(0x01); delay(150); // SW Reset
    sendCmd(0x11); delay(200); // Sleep Out
    sendCmd(0x3A); sendDat(0x05); // 16bit color
    sendCmd(0x36); sendDat(0x00); // 以前「赤くなった」時の値 (0x00 か 0x08)
    sendCmd(0x36); 
    sendDat(0xA8);//0xA8 0xC8 0x08
    setRotation(2);
    sendCmd(0x29); delay(50); // Display ON
      
}

void TFTControl::sendCmd(uint8_t cmd) {
    digitalWrite(TFT_DC, LOW); digitalWrite(TFT_CS, LOW);
    writeBusFast(cmd); digitalWrite(TFT_CS, HIGH);
}

void TFTControl::sendDat(uint8_t dat) {
    digitalWrite(TFT_DC, HIGH); digitalWrite(TFT_CS, LOW);
    writeBusFast(dat); digitalWrite(TFT_CS, HIGH);
}

// --- Adafruit_GFX用実装 ---

void TFTControl::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if ((x < 0) || (x >= width()) || (y < 0) || (y >= height())) return;

    // 1. 座標を指定 (AddrWindow)
    sendCmd(0x2A); // CASET
    sendDat(x >> 8); sendDat(x & 0xFF); // Start X
    sendDat(x >> 8); sendDat(x & 0xFF); // End X

    sendCmd(0x2B); // RASET
    sendDat(y >> 8); sendDat(y & 0xFF); // Start Y
    sendDat(y >> 8); sendDat(y & 0xFF); // End Y

    // 2. 書き込み開始
    sendCmd(0x2C); // RAMWR
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    
    writeBusFast(color >> 8);
    writeBusFast(color & 0xFF);
    
    digitalWrite(TFT_CS, HIGH);
}

void TFTControl::fillScreen(uint16_t color) {
    // 1. 画面全体の範囲を指定 (0,0 から 159,127 まで)
    sendCmd(0x2A); // Column Address Set
    sendDat(0x00); sendDat(0x00);              // Start X: 0
    sendDat(0x00); sendDat(this->width() - 1); // End X: 159

    sendCmd(0x2B); // Row Address Set
    sendDat(0x00); sendDat(0x00);              // Start Y: 0
    sendDat(0x00); sendDat(this->height() - 1); // End Y: 127

    // 2. 書き込み開始コマンド
    sendCmd(0x2C); // Memory Write

    // 3. チップセレクトを下げて高速転送モードへ
    digitalWrite(TFT_DC, HIGH); 
    digitalWrite(TFT_CS, LOW);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    uint32_t numPixels = (uint32_t)TFT_WIDTH * TFT_HEIGHT;

    // バッファ配列を介さず、直接バスに流し込む
    for (uint32_t i = 0; i < numPixels; i++) {
        writeBusFast(hi);
        writeBusFast(lo);
    }

    digitalWrite(TFT_CS, HIGH);
}

void TFTControl::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    // 範囲外チェックなどは省略
    
    // 1. 塗りつぶす範囲を指定
    sendCmd(0x2A); 
    sendDat(x >> 8); sendDat(x & 0xFF);
    sendDat((x + w - 1) >> 8); sendDat((x + w - 1) & 0xFF);

    sendCmd(0x2B);
    sendDat(y >> 8); sendDat(y & 0xFF);
    sendDat((y + h - 1) >> 8); sendDat((y + h - 1) & 0xFF);

    // 2. 連続書き込み
    sendCmd(0x2C);
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    uint32_t numPixels = (uint32_t)w * h;

    for (uint32_t i = 0; i < numPixels; i++) {
        writeBusFast(hi);
        writeBusFast(lo);
    }
    digitalWrite(TFT_CS, HIGH);
}