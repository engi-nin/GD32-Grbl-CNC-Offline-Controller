// tft_control.h
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h> // 追加

#define TFT_WIDTH 160
#define TFT_HEIGHT 128
#define TFT_LOGICAL_WIDTH  128  // 横向きでの幅
#define TFT_LOGICAL_HEIGHT 160  // 横向きでの高さ

// Adafruit_GFX を継承したクラス定義 1つにまとめます
class TFTControl : public Adafruit_GFX {
public:
    // コンストラクタ
    TFTControl() : Adafruit_GFX(TFT_WIDTH, TFT_HEIGHT) {}

    void tftbegin();
    void update();
    
    // Adafruit_GFX を使うために必須の関数
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    
    // 高速化のためのオーバーライド（任意ですが推奨）
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void fillScreen(uint16_t color) override;
    
private:
        
    // 物理通信用（privateに隠蔽）
    void writeBusFast(uint8_t val);
    void sendCmd(uint8_t cmd);
    void sendDat(uint8_t dat);
};