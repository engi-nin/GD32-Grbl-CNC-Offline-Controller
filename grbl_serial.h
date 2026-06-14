// grbl_serial.h
#ifndef GRBL_SERIAL_H
#define GRBL_SERIAL_H

#include <Arduino.h>

#define RX_BUF_SIZE 256
#define GRBL_BUF_LINES 40   // 保持する行数（$コマンド等の大量返信用）
#define GRBL_BUF_SIZE 100   // 1行あたりの最大文字数

class GrblSerial {
public:
    GrblSerial() : currentX(0.0f), currentY(0.0f), currentZ(0.0f), lineIdx(0), hasNewData(false) {}
    float currentX, currentY, currentZ; // 最新の座標データを保持する変数

    void init(uint32_t baud);
    void sendByte(char c);
    void sendCommand(const char* cmd);
    int  bfread();      // バッファから1バイト読み出し
    void update();      // 受信バッファを解析して座標を更新
    bool grblavailable();   // 新しい座標データがあるか
    const char* getLastResponse();
    
    // 割り込みハンドラからアクセスするために公開
    static void handleInterrupt(); 
    const char* getNextLine();
    void clearBuffer(); // バッファを空にする関数を追加
    int getStoredLineCount();
    const char* getLineAt(int index);
    bool parseGrblResponse(const char* line);


private:
    
    bool hasNewData;
    String lastLineStr;
    char lineBuffer[256];
    uint8_t lineIdx = 0;
    bool dataValid = false;
    // 割り込みで使用するリングバッファ
    static volatile uint8_t rx_fifo[RX_BUF_SIZE];
    static volatile uint16_t rx_head;
    static volatile uint16_t rx_tail;
    // リングバッファ構造
    char multiLineBuffer[GRBL_BUF_LINES][GRBL_BUF_SIZE];
    volatile uint8_t head; // 次に書き込む位置
    volatile uint8_t tail; // 次に読み出す位置
    
    char tempBuffer[GRBL_BUF_SIZE]; // 受信中の作業用
    uint8_t tempIdx;
};

extern GrblSerial grbl;

#endif