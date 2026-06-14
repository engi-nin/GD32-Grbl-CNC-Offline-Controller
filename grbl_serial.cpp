#include "grbl_serial.h"

// 静的メンバの初期化
volatile uint8_t GrblSerial::rx_fifo[RX_BUF_SIZE];
volatile uint16_t GrblSerial::rx_head = 0;
volatile uint16_t GrblSerial::rx_tail = 0;



static String lastLineStr = "";
static bool hasNewData = false;
static char lineBuffer[128];
static int lineIdx = 0;

void GrblSerial::handleInterrupt() {
    uint32_t sr = USART2->SR;
    // RXNE（受信）または ORE（オーバーランエラー）を確認
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t data = (uint8_t)(USART2->DR & 0xFF); // DRを読んでフラグクリア

        // エラーがない場合のみバッファへ
        if (!(sr & (USART_SR_FE | USART_SR_NE))) {
            uint16_t next_head = (rx_head + 1) % RX_BUF_SIZE;
            if (next_head != rx_tail) {
                rx_fifo[rx_head] = data;
                rx_head = next_head;
            }
        }
    }
}

void GrblSerial::init(uint32_t baud) {
    // 1. Arduinoの標準シリアルを一度開始（ピン設定などを任せる）
    Serial2.begin(baud);

}

void GrblSerial::sendByte(char c) {
    Serial2.write(c);
}

void GrblSerial::sendCommand(const char* cmd) {
    Serial2.println(cmd);
}

int GrblSerial::bfread() {
    if (rx_head == rx_tail) return -1;
    uint8_t c = rx_fifo[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return (int)c;
}

void GrblSerial::update() {
    while (Serial2.available()) {
        char c = Serial2.read();

        // 改行を検知
        if (c == '\n' || c == '\r') {
            if (tempIdx > 0) {
                tempBuffer[tempIdx] = '\0';

                // リングバッファの現在のheadに行をコピー
                strncpy(multiLineBuffer[head], tempBuffer, GRBL_BUF_SIZE - 1);
                multiLineBuffer[head][GRBL_BUF_SIZE - 1] = '\0'; // 確実に終端
                
                // headを進める
                uint8_t nextHead = (head + 1) % GRBL_BUF_LINES;
                
                // 読み出し位置(tail)を追い越す場合は、古いものを捨てる(tailを進める)
                if (nextHead == tail) {
                    tail = (tail + 1) % GRBL_BUF_LINES;
                }
                head = nextHead;
            } 
                tempIdx = 0;
            
        } 
        // 100文字以内でバッファリング
        else if (tempIdx < GRBL_BUF_SIZE - 1) {
            tempBuffer[tempIdx++] = c;
        }
    }
}

const char* GrblSerial::getLastResponse() {
    hasNewData = false; // 読み取られたらフラグを落とす
    return lastLineStr.c_str();
}

bool GrblSerial::grblavailable() {
    // 値を一度ローカル変数にコピーしてから比較する（あるいは一時的に割り込み禁止にする）
    uint8_t h = head;
    uint8_t t = tail;
    return (h != t);
    //return (head != tail);
}

const char* GrblSerial::getNextLine() {
    if (head == tail) return NULL;
    
    const char* line = multiLineBuffer[tail];
    noInterrupts(); // 割り込み一時禁止
    tail = (tail + 1) % GRBL_BUF_LINES;
    interrupts();   // 割り込み許可
    // --------------
    return line;
}

bool GrblSerial::parseGrblResponse(const char* line) {
    // ステータス報告は必ず '<' で始まります
    if (line[0] == '<') {
        // "Idle" という文字列が含まれているかチェック
        if (strstr(line, "<Idle") != NULL) {
            // 💡 メイン側で参照できるように外部フラグを下ろすか、後述のメイン処理で回収します
            // ここでは座標パースも同時に行うため、処理は下に続行させます
        }
    }
    // 1. キーワードの検索 (MPos: または WPos: を想定)
    const char* ptr = strstr(line, "Pos:"); 
    if (!ptr) return false; 

    // 2. 数値の開始位置（":" の直後）へ移動
    ptr = strchr(ptr, ':');
    if (!ptr || *(++ptr) == '\0') return false; 

    // 3. 各座標の抽出 (strtofはパース終了位置をnextPtrに返す)
    char* nextPtr;
    
    // X軸
    float tx = strtof(ptr, &nextPtr);
    if (ptr == nextPtr || *nextPtr != ',') return false;
    
    // Y軸
    ptr = nextPtr + 1;
    float ty = strtof(ptr, &nextPtr);
    if (ptr == nextPtr || *nextPtr != ',') return false;
    
    // Z軸
    ptr = nextPtr + 1;
    float tz = strtof(ptr, &nextPtr);
    if (ptr == nextPtr) return false;

    // 4. 解析成功時のみメンバ変数を一括更新
    currentX = tx;
    currentY = ty;
    currentZ = tz;

    return true;
}

void GrblSerial::clearBuffer() {
    head = 0;
    tail = 0;
    tempIdx = 0;
    // メモリを物理的に消去しておくとより安全です
    memset(multiLineBuffer, 0, sizeof(multiLineBuffer));
}
// 現在バッファに入っている有効な行数を返す
int GrblSerial::getStoredLineCount() {
    if (head >= tail) return head - tail;
    return (GRBL_BUF_LINES - tail) + head;
}

// 読み出し開始位置(tail)からn番目の行のポインタを返す
const char* GrblSerial::getLineAt(int index) {
    int targetIdx = (tail + index) % GRBL_BUF_LINES;
    return multiLineBuffer[targetIdx];
}

GrblSerial grbl;