#include <Arduino.h>
#include "config.h"
#include "tft_control.h"
#include "switch_control.h"
#include "grbl_serial.h"
#include "sd_control.h"
#include <SPI.h>
#include <SdFat.h>
#include "probe_macro.h"


// --- 設定・定数 ---
#define BTN_W 20      // 標準の横幅
#define BTN_H 16      // 標準の高さ
#define BTN_R 5       // 角の丸み
#define BTN_Z_W 20    // Z軸などの細長いボタン用
#define ESC_BTN EXIT
#define OK_BTN OK
#define Y_PL   Y_UP // Ｙ＋
#define Y_DN   Y_DOWN  // Ｙ－
#define X_LF   X_DOWN  // Ｘ－
#define X_RT   X_UP  // Ｘ＋
#define Z_PL   Z_UP  // Ｚ＋
#define Z_DN   Z_DOWN  // Ｚ－
#define TFT_BL PD13
#define SD_CONFIG SdSpiConfig(PC7, SHARED_SPI, SD_SCK_MHZ(10), &softSpi)


// --- インスタンス ---
TFTControl tft;
SwitchControl sw;

SdFat fatFs;
FsFile currentGCodeFile;
SoftSpiDriver<PB14, PB15, PB13> softSpi;

//enum DisplayMode { MODE_MENU,MODE_JOG, MODE_SD, MODE_SETTING ,MODE_SENDING,MODE_PROBE};

struct JogButton {
    Button pin;        // スイッチのピン番号
    String axis;    // 軸名 ("X", "Y", "Z")
    float direction; // 向き (1.0 または -1.0)
    const char* label; // ボタンの表示名
    int16_t x, y, w, h; // 描画位置
    uint16_t color;  // 通常時の色
    int stateIdx;// lastStates配列の何番目を使うか
};

HardwareSerial SerialGrbl(PA3, PA2);

DisplayMode currentMode = MODE_MENU;

bool lastStates[6] = {false, false, false, false, false, false};
bool escLastState = false;
bool backLastState = false;
bool escIsBeingPressed = false;      // 今まさに押されているか
bool isEscLongTargetReached = false; // 長押し確定演出用
bool isFirstOpen = true; // 画面を開いた直後判定用
bool okIsBeingPressed = false;       // OKボタン状態
bool isSpindleOn = false;            // スピンドル状態
bool xUpLast = false, xDnLast = false;
bool yUpLast = false, yDnLast = false;
bool zUpLast = false, zDnLast = false;
bool xUpLastState = false;
bool xDnLastState = false;
bool yUpLastState = false;
bool yDnLastState = false; 
bool zUpLastState = false;
bool zDnLastState = false;
bool isSending = false;        // 送信中フラグ
bool isConfirming = false;
bool isStreaming = false;       // 送信中かどうかのフラグ
bool waitingForOk = false; // GRBLからの返答待ちフラグ
bool isWaitingInitialResponse = false; // 最初のリプライ待ちフラグ
bool isShowingConsole = false;
bool isMovingToZero = false; // 💡 ゼロ位置への移動中かどうかを保持するフラグ

uint32_t streamStartTime = 0;          // タイムアウト監視用
char lastError[32] = "";       // エラーメッセージ格納用
SdFile gcodeFile;              // 現在開いているGコードファイル
unsigned long escPressStartTime_ex = 0; // 押し始めた時刻
unsigned long escPressStartTime_xl = 0;
unsigned long escPressStartTime_xr = 0;
unsigned long escPressStartTime_zu = 0;
unsigned long escPressStartTime_zd = 0;

float moveDistance = 1.0;            // 現在の移動距離 (1mm, 10mmなど)
float curX, curY, curZ;
int moveSpeed = 100;                 // 初期送り速度 100
unsigned long okPressStartTime = 0;  // OKボタン用
uint32_t totalFileBytes = 0;    // ファイルの総サイズ
uint32_t sentFileBytes = 0;     // 送信済みのサイズ
char lastSentGCode[64] = "";    // 最後に送った行
char lastGrblResponse[64] = ""; // GRBLからの最後の返事
static char currentFileName[32]; // 選択されたファイル名を保持する配列
char lineBuffer[64]; // Gコード1行分を溜めるバッファ（CNCの1行は通常50文字程度なので64で十分です）
int lineIdx = 0;     // バッファのどこまで文字を入れたかを記録する添え字
int consoleLineY = 20; // ログの書き出し開始位置
static int consoleLineCount = 0; 

static int currentLogPage = 0;
static int totalCapturedLines = 0;
bool escHandled_ex = false;
bool escHandled_xl = false;
bool escHandled_xr = false;
bool escHandled_zu = false;
bool escHandled_zd = false;

int currentPage = 0;          // 現在のページ番号（0から開始）

// 設定項目の構造体定義
struct SettingItem {
    const char* name;    // 画面に表示する名前
    const char* command; // GRBLに送るコマンド
};
enum LogType { TYPE_ERROR, TYPE_ALARM, TYPE_HOLD, TYPE_DOOR, TYPE_INFO };

struct GrblLog {
    LogType type;
    int code;
    const char* msg; // メッセージの要約（任意）
};

static uint32_t lastUpdateMillis = 0;

// --- ここに項目を追加・削除するだけでメニューが増えます ---
const SettingItem SETTINGS[] = {
    {"Homing Start ($H)",     "$H"},
    {"Alarm Unlock ($X)",     "$X"},
    {"GrblConfig ($$)",     "$$"},
    {"Check Mode ($C)", "$C"},
    {"Sleep mode ($SLP)",    "$SLP"},
    {"Status Read ($G)",     "$G"},
    {"<WPos:> ($10=0)",       "$10=0"},   
    {"<MPos:> ($10=1)",         "$10=1"},     
    {"Soft Reset (Ctrl+X)",   "SoftRESET"}  // 特殊処理用にキーワードを定義
};

// 項目の総数を自動計算
const int TOTAL_SETTINGS = sizeof(SETTINGS) / sizeof(SETTINGS[0]);
String grblResponse = "";    // 受信した返答を格納
bool pendingResponse = false; // 返答待ちフラグ

int selectedIndex = 0;       // 現在選択されている番号
bool enterLastState = false; // 決定ボタンの状態保存用
bool upLastState    = false; // 上ボタンの状態保存用
bool downLastState  = false; // 下ボタンの状態保存用

extern void rawUartPrint(USART_TypeDef *uart, const char* s);

extern void rawUartWriteChar(USART_TypeDef *uart, char c);
extern void rawUartFlush(USART_TypeDef *uart);
extern bool rawUartAvailable(USART_TypeDef *uart);
extern char rawUartReadChar(USART_TypeDef *uart);
extern int rawUartRead(USART_TypeDef *uart);
extern void sendByte(uint8_t b);

void drawJogUI();
void drawSDFileListUI();
void drawMenuUI();
void drawSettingUI();
void updateStatusDisplay();
void updateDebugConsole(String msg);
bool startGCodeStream(const char* filename);
void ConsoleLog(const char* log, uint16_t color, int16_t x, int16_t y);
void drawProbeUI();
static float countX = 0.00f;
static float countY = 0.00f;
static float countZ = 0.00f;
// 送信を許可するコマンドのリスト（ホワイトリスト）
const char* whiteList[] = {
    "G0", "G1", "G2", "G3", "G17", "G20", "G21", "G90", "G91", "G54",
    "M2","M3", "M5", "M8", "M9", "M30", "S", "F", "$"
};
void stopGCodeStream();
bool isAllowed(const char* line);
void GrblCurrentPos();
bool isPaused = false;
static uint32_t lastOkTime = 0;

// ボタンの「役割」を定義
enum ButtonType {
    BTN_PRIMARY,   // 標準ボタン
    BTN_ENABLED,    // 決定ボタン
    BTN_SEND,   // 完了・実行
    BTN_DISABLED   // 無効状態（グレー）
};

// 色の管理用構造体
struct ButtonStyle {
    uint16_t body;
    uint16_t text;
};

// 役割に応じた色の設定（ここで一元管理！）
ButtonStyle getStyle(ButtonType type) {
    switch (type) {
        case BTN_PRIMARY:  return {0x64D9, 0xFFFF};
        case BTN_ENABLED:  return {0xF800, 0xFFFF};
        case BTN_SEND:     return {0xFFE0, 0xFFFF};
        case BTN_DISABLED: return {0x01AD, 0xFFFF}; // ダークグレーとライトグレー
        default:           return {0xFFFF, 0x0000};
    }
}

// GRBLから取得した実座標格納用
float prbX = 0.0f, prbY = 0.0f, prbZ = 0.0f;

// 内径測定用のステップ管理と一時記憶
static float center_X1 = 0.0f;
static float center_X2 = 0.0f;
static float center_Y1 = 0.0f;
static float center_Y2 = 0.0f;
static float target_X_Center = 0.0f;
static float target_Y_Center = 0.0f;

// ─── ファイルスコープ（またはグローバル）で維持する変数群 ───
static float macroStart_X = 0.0f;
static float macroStart_Y = 0.0f;
static float macroStart_Z = 0.0f;
static float calculated_X_Center = 0.0f;
static float calculated_Y_Center = 0.0f;

// 各測定ステップで取得するプローブ座標（PRB応答から抽出）

static float center_Z1 = 0.0f;
static float center_Z2 = 0.0f;

static float corner_X1 = 0.0f;
static float corner_Y1 = 0.0f;


// ─── 選択型メニュー用の管理変数 ───
int menuCursorX = 0; // 0:左列(アクション), 1:右列(モード遷移)
int menuCursorY = 0; // 0〜2行目 (左:X/Y, Z, 空き / 右:JOG, SD, SETTING, Probe)

// 長押しポップアップ管理用
bool isPopupActive = false;
int popupSelection = 0; // ポップアップ内の選択肢 (0: ゼロセット, 1: ゼロへ移動, 2: キャンセル)
int targetAxisMode = 0; // 0: X/Y軸, 1: Z軸

// ボタンの配置定義を扱いやすくするための構造体
struct MenuButtonPos {
    int x; int y; int w; int h;
    const char* label;
};

// 画面のボタン配置と完全に同期した配列
static const MenuButtonPos leftMenuBtns[2] = {
    {5,  38, 70, 18, "X/Y ZERO"},
    {5, 110, 70, 18, "Z ZERO"} 
};

static const MenuButtonPos rightMenuBtns[4] = {
    {85,  38, 65, 18, "JOG MODE"},
    {85,  62, 65, 18, "SD FILES"},
    {85,  85, 65, 18, "SETTINGS"},
    {85, 110, 65, 18, "Probe"}
};

// 内径探査のステート追跡用
enum DummyState { STATE_NONE, STATE_X1, STATE_X2, STATE_Y1, STATE_Y2,STATE_Z1,STATE_Z2,STATE_CR_X,STATE_CR_Y };

static const char* dummyZ_P1  = "D_Z_P1";
static const char* dummyZ_P2  = "D_Z_P2";

static const char* dummyX_P   = "D_X_P";
static const char* dummyX_M   = "D_X_M";
static const char* dummyY_P   = "D_Y_P";
static const char* dummyY_M   = "D_Y_M";

static const char* dummyCR_X1 = "D_CR_X1"; // コーナー用：X探査
static const char* dummyCR_X2 = "D_CR_X2"; // コーナー用：X退避(絶対座標)
static const char* dummyCR_Y1 = "D_CR_Y1"; // コーナー用：Y回り込み位置(絶対座標)
static const char* dummyCR_Y2 = "D_CR_Y2"; // コーナー用：ワーク前方に進む(絶対座標)
static const char* dummyCR_Y3 = "D_CR_Y3"; // コーナー用：Y探査
static const char* dummyCR_G10= "D_CR_G10";// コーナー用：最終座標設定＆安全退避

static const char* dummyIN_X1 = "D_IN_X1";
static const char* dummyIN_X2 = "D_IN_X2";
static const char* dummyIN_CX = "D_IN_CX";
static const char* dummyIN_Y1 = "D_IN_Y1";
static const char* dummyIN_Y2 = "D_IN_Y2";
static const char* dummyIN_CY = "D_IN_CY";

static const char* dummyOUT_X1 = "D_OUT_X1";
static const char* dummyOUT_X2 = "D_OUT_X2";
static const char* dummyOUT_X3 = "D_OUT_X3";
static const char* dummyOUT_X4 = "D_OUT_X4";
static const char* dummyOUT_X5 = "D_OUT_X5";
static const char* dummyOUT_Y1 = "D_OUT_Y1";
static const char* dummyOUT_Y2 = "D_OUT_Y2";
static const char* dummyOUT_Y3 = "D_OUT_Y3";
static const char* dummyOUT_Y4 = "D_OUT_Y4";
static const char* dummyOUT_Y5 = "D_OUT_Y5";
static const char* dummyOUT_FIN= "D_OUT_FIN";

bool isEditingWorkDia = false;  // ★追加：ワーク径のポップアップ表示/編集フラグ
int pendingProbeItem = 0;       // ★追加：ポップアップを開いた元のプローブ項目を一時保持
float workDiameter = 50.0f;     // ★追加：ワーク/穴の直径 (初期値)


// ボタンのラベルを描画するヘルパー関数
void drawButtonLabel(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t textColor) {
    tft.setTextColor(textColor);
    tft.setTextSize(1); 

    // --- 中央配置のための計算 ---
    // 標準フォント（Size 1）は 1文字 = 幅6px, 高さ8px です
    int16_t textWidth = strlen(label) * 6;
    int16_t textHeight = 8;

    // ボタンの幅(w)から文字の幅を引いて半分にすることで、正確に中央へ配置
    int16_t textX = x + (w - textWidth) / 2;
    int16_t textY = y + (h - textHeight) / 2;

    // もし計算結果がマイナス（ボタンより文字が長い）になった場合の安全策
    if (textX < x) textX = x + 2; 

    tft.setCursor(textX, textY);
    tft.print(label);
}
//ボタン描画
void drawMyButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, ButtonType type) {
    // getStyleに引数で受け取った「type」を渡す
    ButtonStyle style = getStyle(type);
    
    tft.fillRoundRect(x, y, w, h, BTN_R, style.body);
    drawButtonLabel(x, y, w, h, label, style.text);
}
//起動時のロゴ画面
void drawOpeningScreen() {
    tft.fillScreen(0x0000); // 黒背景
    
    tft.setTextSize(2);
    
    tft.setTextColor(0x065F);
    tft.setCursor(60, 15);
    tft.print(F("Grbl"));
    tft.setCursor(40, 40);
    tft.setTextColor(0xEFFD);
    tft.print(F("OFFLINE"));
    tft.setCursor(20, 65);
    tft.print(F("CONTROLLER"));

    tft.setTextColor(0xFFFF);
    tft.setTextSize(1);
    tft.setCursor(60, 90);
    tft.print(F(VERSION_STR));
    
    //tft.update();
    delay(3000); // 2秒間ロゴを表示
}
//メニューコンソール用エラーコード表示データ
void drawDetailedLog(const char* rawRes, int16_t x, int16_t y) {
    LogType type = TYPE_INFO;
    int code = -1;
    uint16_t color = 0xFFFF; // デフォルト白
    String displayMsg = "";

    // --- 1. 種類の判定と解析 ---
    if (strstr(rawRes, "error:") != NULL) {
        type = TYPE_ERROR;
        code = atoi(rawRes + 6); // "error:" の後を取得
        color = 0xF800; // 赤
        displayMsg = "ERROR [" + String(code) + "]: ";
    } 
    else if (strstr(rawRes, "ALARM:") != NULL) {
        type = TYPE_ALARM;
        code = atoi(rawRes + 6);
        color = 0xFB20; // オレンジ
        displayMsg = "ALARM [" + String(code) + "]: ";
    }
    else if (strstr(rawRes, "Hold:") != NULL) {
        type = TYPE_HOLD;
        code = atoi(rawRes + 5);
        color = 0xFFE0; // 黄
        displayMsg = "HOLD [" + String(code) + "]: ";
    }
    else if (strstr(rawRes, "Door:") != NULL) {
        type = TYPE_DOOR;
        code = atoi(rawRes + 5);
        color = 0x07FF; // 水色
        displayMsg = "DOOR [" + String(code) + "]: ";
    }

    // --- 3. TFTへ表示 ---
    if (displayMsg != "") {
        ConsoleLog(displayMsg.c_str(), color, x, y);
    }
}
//コンソールエラー表示
void ConsoleLog(const char* log, uint16_t color, int16_t x, int16_t y) {
    tft.fillRect(x,y,150,8,0x0000);
    tft.setTextSize(1);
    tft.setTextColor(color); 
    tft.setCursor(x, y); 
    tft.print(log);
}
//メニュー画面ＸＹＺ描画
void updateMenuCoordinates() {
    // 通信確認
    bool isTimeout = (millis() - lastUpdateMillis > 1000);
    uint16_t txtColor = isTimeout ? 0xF800 : 0xFFFF;

    // X軸
    tft.setCursor(28, 65);
    tft.setTextColor(txtColor, 0x0000);
    if (!isTimeout) tft.print(String(grbl.currentX, 2)); 
    else tft.print("  ?.??");

    // Y軸
    tft.setCursor(28, 80);
    tft.setTextColor(txtColor, 0x0000);
    if (!isTimeout) tft.print(String(grbl.currentY, 2)); 
    else tft.print("  ?.??");

    // Z軸
    tft.setCursor(28, 95);
    tft.setTextColor(txtColor, 0x0000);
    if (!isTimeout) tft.print(String(grbl.currentZ, 2)); 
    else tft.print("  ?.??");
}
//ＸＹＺ位置取得
void GrblCurrentPos(int16_t x,int16_t y){
    grbl.sendByte('?');
    grbl.update(); 
        while (grbl.grblavailable()) {
        const char* res = grbl.getNextLine();
        
        if (res[0] == '<') {
            // ★重要：ここで解析関数を呼ぶ
            if (grbl.parseGrblResponse(res)) {
                lastUpdateMillis = millis(); // 座標解析に成功したらタイマーリセット
            }
        } 
        else if (strcmp(res, "ok") != 0) {
            // ok 以外の重要なメッセージ（error等）があればログへ
          drawDetailedLog(res,x,y);  
        }
    }
}
// 2. メインメニュー画面
void drawMenuUI() {
    grbl.clearBuffer();

    tft.fillScreen(0x0000); // 背景ブラック
    
    tft.setCursor(40, 5);
    tft.setTextColor(0xFFFF); // ホワイト
    tft.println(F("[ Main MENU ]"));
     
    // --- 座標表示エリアの背景と枠線 ---
    tft.fillRect(5, 20, 150, 15, 0x0000); // レスポンスコンソール背景
    tft.drawRect(5, 20, 150, 15, 0xFFFF); // レスポンスコンソール白い枠線
    tft.drawRect(5, 60, 75, 47, 0xFFFF); // ＸＹＺ Ａｘｉｓ白枠

    tft.setTextSize(1);
    tft.setTextColor(0xFFFF);
    
    GrblCurrentPos(10, 25);
    
    // X軸
    tft.setCursor(10, 65);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.print("X:");
    tft.setCursor(28, 65);
    tft.print(String(grbl.currentX, 2));

    // Y軸
    tft.setCursor(10, 80);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.print("Y:");
    tft.setCursor(28, 80);
    tft.print(String(grbl.currentY, 2));

    // Z軸
    tft.setCursor(10, 95);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.print("Z:");
    tft.setCursor(28, 95);
    tft.print(String(grbl.currentZ, 2));
   
    // ==================================================
    // --- 十字キー選択対応：左列ボタンの配置 (menuCursorX == 0) ---
    // ==================================================
    for (int i = 0; i < 2; i++) {
        // ポップアップが閉じており、且つカーソルが左列(0)のi行目にある場合は ENABLED、それ以外は PRIMARY
        ButtonType btnType = (!isPopupActive && menuCursorX == 0 && menuCursorY == i) ? BTN_PRIMARY : BTN_DISABLED;
        
        drawMyButton(
            leftMenuBtns[i].x, 
            leftMenuBtns[i].y, 
            leftMenuBtns[i].w, 
            leftMenuBtns[i].h, 
            leftMenuBtns[i].label, 
            btnType
        );
    }

    // ==================================================
    // --- 十字キー選択対応：右列ボタンの配置 (menuCursorX == 1) ---
    // ==================================================
    for (int i = 0; i < 4; i++) {
        // ポップアップが閉じており、且つカーソルが右列(1)のi行目にある場合は ENABLED、それ以外は PRIMARY
        ButtonType btnType = (!isPopupActive && menuCursorX == 1 && menuCursorY == i) ? BTN_PRIMARY : BTN_DISABLED;
        
        drawMyButton(
            rightMenuBtns[i].x, 
            rightMenuBtns[i].y, 
            rightMenuBtns[i].w, 
            rightMenuBtns[i].h, 
            rightMenuBtns[i].label, 
            btnType
        );
    }
   
    grbl.clearBuffer();
}
//メニュー画面のＺボタンのポップアップ描画
void drawZeroPopup(const char* title) {
    // ポップアップの外枠と背景
    tft.fillRect(15, 35, 130, 80, 0x0000); // 黒塗り潰し
    tft.drawRect(15, 35, 130, 80, 0xF800); // 赤い警告枠
    
    tft.setCursor(25, 42);
    tft.setTextColor(0xFFFF);
    tft.print(title);
    
    // 選択肢の描画
    const char* options[3] = {
        " 1. Zero Set (G10)",
        " 2. Move to Zero",
        " 3. Cancel"
    };
    
    for (int i = 0; i < 3; i++) {
        tft.setCursor(20, 60 + (i * 15));
        if (popupSelection == i) {
            tft.setTextColor(0x0000, 0xFFFF); // 反転表示（選択中）
        } else {
            tft.setTextColor(0xFFFF, 0x0000);
        }
        tft.println(options[i]);
    }
}
// メニュー画面のボタン処理
void handleMenuButtons() {
    // 💡 移動状態の管理用静的変数
    static bool isMovingToZero = false;
    static uint32_t moveStartTime = 0;
    const uint32_t CANCEL_ACCEPT_TIME = 4000; // 4秒間ESC受け入れ

    // 軸移動中のタイマー監視
    if (isMovingToZero && (millis() - moveStartTime > CANCEL_ACCEPT_TIME)) {
        isMovingToZero = false;
        drawMenuUI();
    }

    // ==========================================
    // 🅰️ パターン1：ポップアップ（小画面）表示中の処理
    // ==========================================
    if (isPopupActive) {
        if (sw.isPressed(Y_PL)) { // 上キーでポップアップ内移動
            // 💡 選択肢が4つ（0〜3）になったため、% 4 でループさせます
            popupSelection = (popupSelection - 1 + 3) % 3;
            drawZeroPopup(targetAxisMode == 0 ? "[ X/Y Axis ]" : "[ Z Axis ]");
            delay(150);
        }
        else if (sw.isPressed(Y_DN)) { // 下キーでポップアップ内移動
            popupSelection = (popupSelection + 1) % 3;
            drawZeroPopup(targetAxisMode == 0 ? "[ X/Y Axis ]" : "[ Z Axis ]");
            delay(150);
        }
        else if (sw.isPressed(OK_BTN)) { // ポップアップ内での決定（短押し）
            isPopupActive = false;
            
            switch (popupSelection) {
                case 0: // 1. ワーク原点設定 (G10 L20 X0 Y0 / Z0)
                    if (targetAxisMode == 0) {
                        grbl.sendCommand("G10 L20 P1 X0.00 Y0.00");
                        countX = 0; countY = 0;
                    } else {
                        grbl.sendCommand("G10 L20 P1 Z0.00");
                        countZ = 0;
                    }
                    tft.fillRect(5, 20, 150, 15, 0x0000);
                    tft.setCursor(10, 23); tft.setTextColor(0x07E0); tft.print("Zero Set Success!");
                    delay(1000);
                    GrblCurrentPos(10, 25);
                    break;

                case 1: // 2. 原点へ移動 (G90 G0 X0 Y0 / Z0)
                    tft.fillRect(5, 20, 150, 15, 0x0000);
                    tft.setCursor(10, 23); tft.setTextColor(0xF800);
                    
                    isMovingToZero = true;
                    moveStartTime = millis();
                    
                    if (targetAxisMode == 0) {
                        tft.print("Moving to X/Y0... [ESC]");
                        grbl.sendCommand("G90 G0 X0.00 Y0.00");
                    } else {
                        tft.print("Moving to Z0... [ESC]");
                        grbl.sendCommand("G90 G0 Z0.00");
                    }
                    delay(600);
                    break;

                case 3: // 3. キャンセル (何もしないで閉じる)
                    
                    break;

                }
            
            // 💡 チャタリング・連続入力防止のためのウェイト
            while(sw.isPressed(OK_BTN)) { delay(10); }
            
            // 通常メニューに戻す
            drawMenuUI();
        }
        else if (sw.isPressed(ESC_BTN)) { // ESCで閉じる
            isPopupActive = false;
            drawMenuUI();
            delay(150);
        }
        return; // ポップアップ表示中は以下の通常処理を行わない
    }

    // ==========================================
    // 🅱️ パターン2：通常メニュー時の処理（すべて短押しで判定）
    // ==========================================
    
    // 1. 移動中のESC緊急キャンセル処理
    if (isMovingToZero && sw.isPressed(ESC_BTN)) {
        grbl.sendCommand("!"); // Feed Hold
        delay(10);
        char resetCmd[] = {0x18, '\0'};
        grbl.sendCommand(resetCmd); // GRBLソフトリセット
        
        tft.fillRect(5, 20, 150, 15, 0x0000);
        tft.setCursor(10, 23); tft.setTextColor(0xF800); tft.print("Move Cancelled");
        isMovingToZero = false;
        delay(800);
        drawMenuUI();
        return;
    }

    // 2. 十字キーによるカーソル移動 (X軸 / 左右)
    if (sw.isPressed(X_RT)) { // 右キー
        if (menuCursorX == 0) {
            menuCursorX = 1;
            menuCursorY = (menuCursorY == 0) ? 0 : 3; // 1段目 ↔ 1段目、2段目 ↔ 4段目リンク
            drawMenuUI();
            delay(150);
        }
    }
    else if (sw.isPressed(X_LF)) { // 左キー
        if (menuCursorX == 1) {
            menuCursorX = 0;
            menuCursorY = (menuCursorY == 0) ? 0 : 1; // 1段目 ↔ 1段目、4段目 ↔ 2段目リンク
            drawMenuUI();
            delay(150);
        }
    }

    // 3. 十字キーによるカーソル移動 (Y軸 / 上下)
    if (sw.isPressed(Y_PL)) { // 上キー
        menuCursorY--;
        if (menuCursorY < 0) {
            menuCursorY = (menuCursorX == 0) ? 1 : 3;
        }
        drawMenuUI();
        delay(150);
    }
    else if (sw.isPressed(Y_DN)) { // 下キー
        menuCursorY++;
        int maxRows = (menuCursorX == 0) ? 2 : 4; 
        if (menuCursorY >= maxRows) menuCursorY = 0;
        drawMenuUI();
        delay(150);
    }

    // 4. OKボタンが押された瞬間の「短押し決定」ロジック
    if (sw.isPressed(OK_BTN)) {
        
        if (menuCursorX == 1) { // ─── 右列：各モードへの画面遷移 ───
            switch(menuCursorY) {
                case 0: currentMode = MODE_JOG; isFirstOpen = true; drawJogUI(); break;
                case 1: currentMode = MODE_SD; drawSDFileListUI(); break;
                case 2: currentMode = MODE_SETTING; drawSettingUI(); break;
                case 3: currentMode = MODE_PROBE; isFirstOpen = true; drawProbeUI(); break;
            }
        }
        else if (menuCursorX == 0) { // ─── 左列：ZEROボタン選択時はポップアップを展開 ───
            if (menuCursorY == 0) { // X/Y ZERO
                isPopupActive = true;
                targetAxisMode = 0; // X/Yモード用フラグ
                popupSelection = 0; // デフォルトは先頭を選択
                drawZeroPopup("[ X/Y Axis ]");
            }
            else if (menuCursorY == 1) { // Z ZERO
                isPopupActive = true;
                targetAxisMode = 1; // Zモード用フラグ
                popupSelection = 0;
                drawZeroPopup("[ Z Axis ]");
            }
        }

        // 💡 ボタンが押しっぱなしにされてループが連打されるのを防ぐガード
        while(sw.isPressed(OK_BTN)) { delay(10); }
    }
}
// ＪＯＧ画面のステータス表示の更新
void updateStatusDisplay() {
    tft.setTextSize(1);
    tft.setTextColor(0xFFFF, 0x0000); // 背景色を指定しているので、古い数字は消えます
    
    // --- 送り速度と移動距離 ---
    tft.setCursor(8, 98); 
    tft.print(F("F:")); tft.print(moveSpeed);
    tft.print(F(" mm/min ")); // 10.00から0.01に戻った時のためにスペースを入れておくと安全

    // --- 移動ステップの表示 ---
    tft.setCursor(100, 98); 
    tft.print(F("S:"));
    tft.print(moveDistance, 2); 
    tft.print(F("mm")); // 10.00から0.01に戻った時のためにスペースを入れておくと安全
    GrblCurrentPos(5,116);
    // --- XYZ座標の表示 ---
    tft.setCursor(110, 20); tft.print(F("<  X  >"));
    tft.setCursor(110, 32); tft.print(String(grbl.currentX, 2));
    tft.setCursor(110, 44); tft.print(F("<  Y  >"));
    tft.setCursor(110, 56); tft.print(String(grbl.currentY, 2));
    tft.setCursor(110, 68); tft.print(F("<  Z  >"));
    tft.setCursor(110, 80); tft.print(String(grbl.currentZ, 2));
    
    // ※ ここでは update() を呼ばず、呼出元のボタン処理側で最後に呼ぶのが安全です
}
// ＪＯＧ画面のUI描画
void drawJogUI() {
    tft.fillScreen(0x0000);

    tft.setCursor(38,2);
    tft.print("[  JOG MODE  ]");
    
    tft.drawRect(102, 15, 57, 77, 0xFFFF); // ＸＹＺ　Ａｘｉｓ白枠
    tft.drawRect(2, 15, 99, 77, 0xFFFF);//ボタン枠
    tft.drawRect(2, 95, 158, 15, 0xFFFF);//ステータス枠
    tft.drawRect(2, 112, 158, 15, 0x07E0);//コンソール枠
    tft.drawLine(102,41,160,41,0xFFFF);
    tft.drawLine(102,65,160,65,0xFFFF);

    drawMyButton(40, 24,  BTN_W, BTN_H, "Y+", BTN_PRIMARY);
    drawMyButton(40, 69, BTN_W, BTN_H, "Y-", BTN_PRIMARY);
    drawMyButton(15,  46, BTN_W, BTN_H, "X-", BTN_PRIMARY);
    drawMyButton(65, 46, BTN_W, BTN_H, "X+", BTN_PRIMARY);
    drawMyButton(40, 46, 20, 16, "OK", BTN_ENABLED);
    drawMyButton(5, 24, BTN_Z_W, 16, "Z+", BTN_PRIMARY);
    drawMyButton(5, 69, BTN_Z_W, 16, "Z-", BTN_PRIMARY);
    drawMyButton(75, 69, 20, 16, "ESC", BTN_PRIMARY);
    drawMyButton(72, 24, 28, 16, "SPDL", BTN_DISABLED);
    
}
// 応答表示用のエリアを更新する関数
void updateDebugConsole(String Dbgmsg) {
    // 1. 描画エリアの定義 (画面最下部)
    int16_t areaX = 0;
    int16_t areaY = 115; // 128ピクセル画面の最下部付近
    int16_t areaW = 155; // 画面全幅
    int16_t areaH = 10;  // 1行分

    // 2. 古い文字を完全に消す (黒で塗りつぶし)
    tft.fillRect(areaX+3, areaY, areaW, areaH, 0x0000); 
    
    // 3. 文字の位置を「消した範囲内」に合わせる
    tft.setCursor(areaX + 2, areaY + 1); 
    tft.setTextSize(1);
    tft.setTextColor(0x07E0); // 緑色
    
    // 4. 表示（はみ出し防止で25文字程度に制限）
    tft.setCursor(5,116);
    tft.print("> " + Dbgmsg.substring(0, 25)); 
    
    //tft.update();
}
// ＪＯＧ操作のボタン処理
void handleJogButtons() {
    // 1. 画面を開いた瞬間に1回だけ初期化
    if (isFirstOpen) {
        // 必要に応じて countX = countY = countZ = 0.0f; を入れる
        updateStatusDisplay(); 
        isFirstOpen = false;
    }

    // 2. ジョグボタンの定義
    uint16_t jogcolor = 0x64D9;
    JogButton buttons[] = {
        {Y_PL, "Y",  1.0, "Y+", 40, 24, BTN_W, BTN_H, jogcolor, 0},
        {Y_DN, "Y", -1.0, "Y-", 40, 69, BTN_W, BTN_H, jogcolor, 1}, 
        {X_LF, "X", -1.0, "X-", 15, 46, BTN_W, BTN_H, jogcolor, 2}, 
        {X_RT, "X",  1.0, "X+", 65, 46, BTN_W, BTN_H, jogcolor, 3}, 
        {Z_PL, "Z",  1.0, "Z+",  5, 24, BTN_Z_W, 16,  jogcolor, 4},
        {Z_DN, "Z", -1.0, "Z-",  5, 69, BTN_Z_W, 16,  jogcolor, 5}
    };

    // 3. ボタンチェックループ
    for (auto& btn : buttons) {
        bool now = sw.isPressed(btn.pin);
        if (now != lastStates[btn.stateIdx]) {
            if (now) {
                // 押下：色反転描画
                drawMyButton(btn.x, btn.y, btn.w, btn.h, btn.label, BTN_SEND);
                
                // 移動量の計算
                float move = moveDistance * btn.direction;

                // --- 軸ごとにcountX, Y, Zに累積加算 ---
                if (btn.axis == "X")      countX += move;
                else if (btn.axis == "Y") countY += move;
                else if (btn.axis == "Z") countZ += move;
                
                // コマンド送信 (G91:相対指令)
                String cmd = "$J=G91 " + btn.axis + String(move, 2) + " F" + String(moveSpeed);
                grbl.sendCommand(cmd.c_str());
                
                updateDebugConsole("TX:" + cmd);
                
            } else {
                // 離された：元の色に戻す
                drawMyButton(btn.x, btn.y, btn.w, btn.h, btn.label, BTN_PRIMARY);
                updateStatusDisplay(); // ステータス更新
            }
            lastStates[btn.stateIdx] = now;
        }
    }

    // 4. 受信処理（GRBLからの現在位置などのレスポンス）
    if (grbl.grblavailable()) {
        const char* res = grbl.getNextLine();
        if (res[0] == '<') {
            grbl.parseGrblResponse(res); 
            updateStatusDisplay();
        } else if (strcmp(res, "ok") != 0) {
            updateDebugConsole(res); 
        }
    }

    // 共通の現在時刻を取得
    unsigned long currentMillis = millis();

    // --- ESC ボタン (移動量切替 / 長押しで終了) ---
    bool escNow = sw.isPressed(EXIT);
    static unsigned long lastEscClickTime = 0; // チャタリング防止用の最終操作時間記録

    if (escNow) {
        if (!escIsBeingPressed) {
            // 直前の操作から80ms以内の再入力を無視する（デバウンス）
            if (currentMillis - lastEscClickTime > 80) {
                escPressStartTime_ex = currentMillis;
                escIsBeingPressed = true;
                isEscLongTargetReached = false;
            }
        } else if (!isEscLongTargetReached && (currentMillis - escPressStartTime_ex > LONG_PRESS_MS)) {
            drawMyButton(75, 69, 20, 16, "ESC", BTN_SEND); // 長押し確定色
            isEscLongTargetReached = true;
        }
    } 
    else if (!escNow && escIsBeingPressed) {
        drawMyButton(75, 69, 20, 16, "ESC", BTN_SEND); 
        unsigned long duration = currentMillis - escPressStartTime_ex;

        if (duration > LONG_PRESS_MS) {
            // 【長押し】メニュー画面へ
            currentMode = MODE_MENU; 
            drawMenuUI();
            escIsBeingPressed = false;
            isEscLongTargetReached = false;
            lastEscClickTime = currentMillis; // 操作時間を更新
            delay(50); 
            return; 
        } else {
            // 【短押し】移動量の切り替え
            if (moveDistance == 0.01f)       moveDistance = 0.1f;
            else if (moveDistance == 0.1f)  moveDistance = 1.0f;
            else if (moveDistance == 1.0f)  moveDistance = 10.0f;
            else                            moveDistance = 0.01f;
           
            updateStatusDisplay();
        }
        drawMyButton(75, 69, 20, 16, "ESC", BTN_PRIMARY);
        escIsBeingPressed = false;
        isEscLongTargetReached = false;
        lastEscClickTime = currentMillis; // 操作時間を更新
    }

    // --- OKボタン (短押し：速度切替 / 長押し：スピンドル切替) ---
    bool okNow = sw.isPressed(OK);
    static unsigned long lastOkClickTime = 0; // チャタリング防止用の最終操作時間記録

    if (okNow) {
        if (!okIsBeingPressed) {
            // 直前の操作から80ms以内の再入力を無視する（デバウンス）
            if (currentMillis - lastOkClickTime > 80) {
                okPressStartTime = currentMillis;
                okIsBeingPressed = true;
                drawMyButton(40, 46, 20, 16, "OK", BTN_SEND); // 押下中の色
            }
        }
    } 
    else if (!okNow && okIsBeingPressed) {
        unsigned long duration = currentMillis - okPressStartTime;

        if (duration > LONG_PRESS_MS) {
            // 【長押し】スピンドル状態の反転
            if (!isSpindleOn) {
                grbl.sendCommand("M3 S200");
                updateDebugConsole("TX: M3 S200 (ON)");
                drawMyButton(72, 24, 28, 16, "SPDL", BTN_ENABLED);
                isSpindleOn = true;
            } else {
                grbl.sendCommand("M5");
                updateDebugConsole("TX: M5 (OFF)");
                drawMyButton(72, 24, 28, 16, "SPDL", BTN_DISABLED);
                isSpindleOn = false;
            }
            updateStatusDisplay();
        } else {
            // 【短押し】送り速度(F)の切り替え
            moveSpeed += 50;
            if (moveSpeed > 500) moveSpeed = 50;
            updateDebugConsole("Feed: " + String(moveSpeed));
            updateStatusDisplay();
        }
        
        // ボタン表示を元に戻す
        drawMyButton(40, 46, 20, 16, "OK", BTN_ENABLED);
        okIsBeingPressed = false;
        lastOkClickTime = currentMillis; // 操作時間を更新
    }

    static uint32_t lastUpdate = 0; // ステータス時間更新
    if (currentMillis - lastUpdate > 300) {
        updateStatusDisplay();
        lastUpdate = currentMillis;
    }
}
// 許可されたコマンドかどうかを判定する関数
bool isAllowed(char* line) {
    // 1. 先頭の空白をスキップ
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;

    // 2. 空行・コメント・特殊記号の除外
    if (*p == '\0' || *p == '(' || *p == '%' || *p == ';') return false;

    // 3. 末尾のゴミ（\r, \n, スペース, 制御文字）を後ろから削る
    int len = strlen(p);
    while (len > 0 && (unsigned char)p[len - 1] <= 0x20) {
        p[--len] = '\0';
    }

    // 4. 禁止コマンドの除外 (M6, M0など)
    if (strncmp(p, "M6", 2) == 0 || strncmp(p, "M0", 2) == 0) return false;

    // 5. ホワイトリスト照合
    bool allowed = false;
    // ※whiteListは外部で定義されている前提
    int listSize = sizeof(whiteList) / sizeof(whiteList[0]);
    for (int i = 0; i < listSize; i++) {
        int wLen = strlen(whiteList[i]);
        if (strncmp(p, whiteList[i], wLen) == 0) {
            allowed = true;
            break;
        }
    }

    // 6. 最終判定：許可されており、かつ空文字でないこと
    if (allowed && p[0] != '\0') {
        // 掃除した文字列を元の先頭に書き戻す
        if (p != line) memmove(line, p, strlen(p) + 1);
        return true;
    }
    return false;
}
//ＧーＣＯＤＥ送信関数
void processGCodeStreaming() {
    static char sdReadBuf[128];
    static int sdBufIdx = 0;

    // --- 1. GRBLからの応答チェック ---
    // ok または error が来たら次の行を送れるようにする
    while (Serial2.available()) {
        String res = Serial2.readStringUntil('\n');
        res.trim();
        if (res.equals("ok") || res.startsWith("error")) {
            waitingForOk = false;
            lastOkTime = millis();
            // errorが出た場合はシリアルにも出して確認できるようにする
            if (res.startsWith("error")) {
                //Serial.print(F("GRBL Error Response: "));
                //Serial.println(res);
            }
        }
    }

    // --- 2. 送信処理 ---
    if (isStreaming && !waitingForOk) {
        sdCtrl.activateBus();

        while (sdCtrl.file().available()) {
            char c = sdCtrl.file().read();

            // 改行を検知したら1行確定
            if (c == '\n' || c == '\r') {
                sdReadBuf[sdBufIdx] = '\0';
                
                if (sdBufIdx > 0) {
                    // クリーニングと判定を同時に行う
                    if (isAllowed(sdReadBuf)) {
                        // 【決まり事】printlnを使わず、自前でLF(\n)のみを付加して送る
                        Serial2.print(sdReadBuf);
                        Serial2.print('\n'); 
                        
                        // 表示用に保存
                        strncpy(lastSentGCode, sdReadBuf, sizeof(lastSentGCode)-1);
                        
                        waitingForOk = true;
                        lastOkTime = millis();
                        sdBufIdx = 0;
                        
                        // 送信したのでSPIバスを解放して一旦抜ける
                        SDControl::releaseAllSlots();
                        return; 
                    }
                }
                // 送信対象外、または空行ならバッファをリセットして次へ
                sdBufIdx = 0;
            } 
            else if (sdBufIdx < (int)sizeof(sdReadBuf) - 1) {
                sdReadBuf[sdBufIdx++] = c;
            }
        }

        // ファイル終了判定
        if (!sdCtrl.file().available()) {
            isStreaming = false;
            sdCtrl.closeCurrent();
            //Serial.println(F("Streaming Completed."));
            SDControl::releaseAllSlots();
        }
    }
}
//Ｇコード送信前の確認ダイアログの描画
void drawConfirmDialog(const char* filename) {
    // 画面中央に枠を描画
    uint16_t boxW = 120;
    uint16_t boxH = 40;
    uint16_t x = (160 - boxW) / 2; // 画面幅320と仮定
    uint16_t y = (128 - boxH) / 2; // 画面高240と仮定

    tft.fillRect(x, y, boxW, boxH, 0x0000); // 背景黒
    tft.drawRect(x, y, boxW, boxH, 0xFFFF); // 外枠白

    tft.setCursor(x + 38, y + 2);
    tft.setTextColor(0xFFE0); // 黄色
    tft.println(F("Run OK?"));
    
    tft.setCursor(x + 2, y + 15);
    tft.setTextColor(0xFFFF); // 白色
    tft.println(filename);

    tft.setCursor(x + 15, y + 28);
    tft.setTextColor(0x07E0); // 緑色
    tft.print(F("[OK]"));
    tft.setCursor(x + 70, y + 28);
    tft.setTextColor(0xF800); // 赤色
    tft.print(F("[ESC]"));
}
// Ｇコード送信中の画面描画とSDカードのファイルオープン処理
void drawSendingUI(const char* filename) {
 // 1. まず画面を描く（この間、GPIODはTFT制御で激しく動く）
    tft.fillScreen(0x0000);
    tft.drawRect(5, 115, 110, 10, 0xFFFF); // 進捗枠
    tft.drawRect(2, 25, 156, 60, 0x07E0);   // コンソール枠

    drawMyButton(5, 90, 70, 16, "HOLD (Z-)", BTN_DISABLED);
    drawMyButton(80, 90, 70, 16, "RUN  (OK)", BTN_ENABLED);
    
    // 2. 画面描画が終わってから、SDカードを「改めて」認識させる
    // sdCtrl.openFile の内部で _initBus() が呼ばれ、CS(PD12)が正しく制御されます
    if (sdCtrl.openFile(filename)) {
        tft.setCursor(15, 2);
        tft.setTextColor(0x07E0); // 緑
        tft.println(F(">>> RUNNING G-CODE <<<"));

        tft.setCursor(5, 15);
        tft.setTextColor(0xFFFF); // 白
        tft.print(F("File: "));
        tft.println(filename);
     
        tft.setCursor(5, 28);
        tft.setTextColor(0x7E0);
        tft.println(F("Console:"));

        tft.setCursor(10, 50);
        tft.setTextColor(0xFFFF);
        uint32_t fSize = sdCtrl.file().size();
        tft.printf("Size: %d bytes", fSize);
        
        sdCtrl.closeCurrent();
        SDControl::releaseAllSlots();
        SPI.endTransaction(); 
        delay(1); 
        
        
    } else {
        // 失敗した場合
        tft.setTextColor(0xF800); // 赤
        tft.setCursor(10, 90);
        tft.println(F("File Open Error!"));
        
        // 異常時はバスを解放して、TFT側の操作を安全にする
        SDControl::releaseAllSlots();
    }
   
}
// 送信開始（確認画面でOKを押した時に呼ぶ）
bool startGCodeStream(const char* filename) {
    
    char safeFileName[32]; 
    strncpy(safeFileName, filename, sizeof(safeFileName) - 1);
    safeFileName[sizeof(safeFileName) - 1] = '\0';

    digitalWrite(TFT_CS, HIGH); 
    SDControl::releaseAllSlots();
    delay(1); 

    sdCtrl.closeCurrent();

    // 成功・失敗を bool で返すように変更
    if (sdCtrl.openFile(safeFileName)) {
        if (sdCtrl.file().isOpen()) {
            totalFileBytes = sdCtrl.file().size(); 
            sentFileBytes = 0;
            lineIdx = 0; 
            memset(lineBuffer, 0, sizeof(lineBuffer));
            
            Serial2.println("");
            
            
            streamStartTime = millis();
            isWaitingInitialResponse = true;
            isStreaming = false;
            waitingForOk = false;

            SDControl::releaseAllSlots();
            return true; // 成功
        }
    }
    
    // 失敗時
    SDControl::releaseAllSlots();
    return false; // 失敗
}
// 送信停止・中断
void stopGCodeStream() {
    isStreaming = false;

    // 1. ファイルと物理バスのクリーンアップ（最優先）
    // SDカードを切り離すことで、TFT(パラレル)の動作を安定させます
    sdCtrl.closeCurrent();
    SDControl::releaseAllSlots();
    SPI.endTransaction(); 
    delay(1); 

    // 2. GRBLへの非常停止（ソフトリセット Ctrl+X）送信
    grbl.sendByte(0x18); 
    Serial2.flush();// 送信を確実に完了させる

    // 3. GRBLのリセット完了（応答）待ちタイムアウト設定
    uint32_t startTime = millis();
    const uint32_t RESET_TIMEOUT = 1500; // 1.5秒待機
    bool resetConfirmed = false;

    // GRBLがリセットされると何らかの文字列（welcomeメッセージ等）を返す
    while (millis() - startTime < RESET_TIMEOUT) {
        if (Serial2.available()) {
            // 受信バッファを読み飛ばし、反応があったことだけ確認
            while(Serial2.available()) { Serial2.read(); }
            resetConfirmed = true;
            break;
        }
        delay(1);
    }

    // 4. 管理用フラグのリセット
    waitingForOk = false;
    sentFileBytes = 0;
    totalFileBytes = 0;

    // 5. 結果に応じたUI表示
    tft.setCursor(10, 90);
    if (resetConfirmed) {
        // 正常に停止・リセットされた場合
        tft.fillRect(4, 27, 148, 57, 0x0000);// 前のメッセージを消す
        tft.setCursor(5,48);
        tft.setTextColor(0x07E0); // 緑
        tft.println(F("STOPPED & RESET OK "));
        //Serial2.println(F("G-Code Stream Stopped. GRBL Reset Confirmed."));
    } else {
        // リセットコマンドを送ったが反応がない場合
        tft.fillRect(4, 27, 148, 57, 0x0000);// 前のメッセージを消す
        tft.setCursor(5,48);
        tft.setTextColor(0xF800); // 赤
        tft.println(F("STOPPED: GRBL NO RES "));
        //Serial2.println(F("G-Code Stream Stopped. Warning: GRBL Reset Timeout!"));
        SDControl::releaseAllSlots();
    }
    // データのクリア
    sentFileBytes = 0;
    totalFileBytes = 0;
    
    // 文字列のクリア（String型なら ""、char配列なら [0]='\0'）
    memset(lastSentGCode, 0, sizeof(lastSentGCode));
    memset(lastGrblResponse, 0, sizeof(lastGrblResponse));

    // フラグのリセット
    waitingForOk = false;

   

    // 最後に再度バスの解放を確実に行う
    SDControl::releaseAllSlots();
}
//送受信の進捗と内容を画面に表示する関数
void updateSendingStatus(uint32_t current, uint32_t total, const char* lastSent, const char* lastRecv) {
    // 1. 進捗％の計算（変化時のみ描画）
    if (total == 0) total = 1;
    int percent = (int)((float)current / total * 100);
    if (!sdCtrl.file().available() || current >= (total - 2)) {
        percent = 100;
    }
    static int lastPercent = -1;

    if (percent != lastPercent) {
        int barWidth = (percent >= 100) ? 110 : (110 * percent / 100);

        tft.fillRect(5, 116, barWidth, 8, 0x07E0);

        tft.fillRect(130, 115, 40, 12, 0x0000);
        tft.setCursor(130, 115);
        tft.setTextColor(0xFFFF);
        tft.print(percent); tft.print(F("%"));
        lastPercent = percent;
    }

    // 2. 送受信情報の更新（変化時のみ描画：負荷軽減の肝）
    static char prevSent[32] = ""; // 前回の値を保持
    static char prevRecv[32] = "";

    // 文字列が前回と異なる場合のみ描画処理を行う
    if (strcmp(lastSent, prevSent) != 0 || strcmp(lastRecv, prevRecv) != 0) {
        // コンソール領域のクリア
        tft.fillRect(4, 27, 148, 57, 0x0000); 
        
        // --- TX (送信Gコード) の描画 ---
        tft.setCursor(5, 28);
        tft.setTextColor(0xFFE0); // 黄色
        tft.print(F("TX: "));
        
        // 最大3行分（最大60文字）の折り返し処理
        const char* p = lastSent;
        int len = strlen(lastSent);
        
        if (len > 20) {
            // 【1行目】前半20文字を表示
            for(int i = 0; i < 20; i++) tft.print(*p++);
            
            // 【2行目】10ピクセル下に移動して設定
            tft.setCursor(5, 38); 
            
            if (len > 45) {
                // 21〜45文字目を表示
                for(int i = 0; i < 25; i++) tft.print(*p++);
                
                // 【3行目】さらに10ピクセル下に移動して設定
                tft.setCursor(5, 48); 
                
                // 41文字目以降の残りを表示（もし60文字を超えるリスクがあれば、ここもforで20文字制限にできます）
                tft.println(p); 
            } else {
                // 45文字以下なら残りをすべて2行目に出して終了
                tft.println(p);
            }
        } else {
            // 20文字以下なら1行で終了
            tft.println(lastSent);
        }

        // --- RX (受信レスポンス) の描画 ---
        tft.setCursor(5, 60); // RXの開始位置
        tft.setTextColor(0x07FF); // 水色
        tft.print(F("RX: "));
        
        p = lastRecv;
        len = strlen(lastRecv);
        if (len > 22) {
            for(int i=0; i<22; i++) tft.print(*p++);
            tft.setCursor(5, 75);
            tft.println(p);
        } else {
            tft.println(lastRecv);
        }

        // 今回の値を保存
        strncpy(prevSent, lastSent, sizeof(prevSent)-1);
        strncpy(prevRecv, lastRecv, sizeof(prevRecv)-1);
    }
}
// Ｇコード送信中のメインループ処理
void handleSendingMode() {
    static uint32_t initStartTime = 0; 
    const uint32_t SEND_TIMEOUT_MS = 60000;
    const uint32_t INIT_TIMEOUT_MS = 5000; 

    // --- 1. 受信処理：リングバッファの更新 ---
    grbl.update(); 

    // --- 2. 受信解析：ok/error/MSGの監視 ---
    while (grbl.grblavailable()) {
        const char* res = grbl.getNextLine();
        if (res != NULL) {
            strncpy(lastGrblResponse, res, sizeof(lastGrblResponse) - 1);

            // 【超重要】プログラム終了メッセージを検知した瞬間に全てを遮断
            if (strstr(res, "[MSG:Pgm End]") != NULL) {
                //Serial.println(F(">>> GRBL PGM END: Shutting down stream immediately."));
                
                isStreaming = false;
                waitingForOk = false;
                lineIdx = 0; // 送信待ちバッファを強制クリア
                
                // 画面を100%にして終了通知
                updateSendingStatus(totalFileBytes, totalFileBytes, "FINISHED", res);
                
                stopGCodeStream(); // ファイルを閉じ、必要ならGRBLをソフトリセット
                
                delay(500); 
                currentMode = MODE_MENU;
                isFirstOpen = true;
                return; // これ以降の処理（送信など）を一切させない
            }

            // 正常応答
            if (strstr(res, "ok") != NULL) {
                waitingForOk = false;
                lastOkTime = millis();
            }
            // エラー応答
            else if (strstr(res, "error") != NULL) {
                //Serial.print(F("!!! GRBL Error: "));
                //Serial.println(res);
                isStreaming = false;
                waitingForOk = false;
                
                drawDetailedLog(res, 5, 72); // エラー内容を表示
                
                stopGCodeStream(); 
                delay(2000); 
                currentMode = MODE_MENU;
                isFirstOpen = true;
                return; 
            }
            
            if (res[0] == '<') {
                grbl.parseGrblResponse(res);
            }
        }
    }

    // --- 3. 初期化フェーズ ---
    if (isFirstOpen) {
        SDControl::releaseAllSlots();
        digitalWrite(TFT_CS, HIGH);
        delay(10);
        grbl.clearBuffer(); 
        isPaused = false;
        drawSendingUI(currentFileName);
        delay(500); 
        
        if (startGCodeStream(currentFileName)) {
            initStartTime = millis();
            isFirstOpen = false;
            return;
        } else {
            currentMode = MODE_MENU;
            isFirstOpen = true;
            return;
        }
    }

    // --- 4. GRBL開始待ち ---
    if (isWaitingInitialResponse) {
        if (!waitingForOk) {
            isWaitingInitialResponse = false;
            isStreaming = true;
            lastOkTime = millis();
            tft.fillRect(4, 27, 148, 57, 0x0000);
            tft.setCursor(5,30);
            tft.setTextColor(0x07E0);
            tft.println(F("GRBL Ready."));
        } else if (millis() - initStartTime > INIT_TIMEOUT_MS) {
            stopGCodeStream();
            isFirstOpen = true;
            currentMode = MODE_MENU;
            return;
        }
        return;
    }
    
    // --- 5. ストリーミングフェーズ ---
    if (isStreaming) {
        
        // 物理ボタン入力
        if (sw.isPressed(Z_DN) && !isPaused) {
            Serial2.print("!"); 
            isPaused = true;
            drawMyButton(5, 90, 70, 16, "HOLD (Z-)", BTN_ENABLED);
            drawMyButton(80, 90, 70, 16, "RUN  (OK)", BTN_DISABLED);
        }
        if (sw.isPressed(OK_BTN) && isPaused) {
            Serial2.print("~"); 
            isPaused = false;
            drawMyButton(5, 90, 70, 16, "HOLD (Z-)", BTN_DISABLED);
            drawMyButton(80, 90, 70, 16, "RUN  (OK)", BTN_ENABLED);
        }
        if (sw.isPressed(ESC_BTN)) {
            stopGCodeStream();
            currentMode = MODE_MENU;
            isFirstOpen = true;
            return;
        }

        // タイムアウト監視
        if (waitingForOk && (millis() - lastOkTime > SEND_TIMEOUT_MS)) {
            stopGCodeStream();
            isFirstOpen = true;
            currentMode = MODE_MENU;
            return;
        }

        // --- 次のGコード行の読み込みと送信 ---
        if (!waitingForOk && !isPaused) {
            sdCtrl.activateBus(); 

            while (sdCtrl.file().available()) {
                char c = sdCtrl.file().read();

                if (c == '\n' || c == '\r') {
                    lineBuffer[lineIdx] = '\0';
                    
                    if (lineIdx > 0) {
                        // 文字が含まれているか厳格チェック（error:2対策）
                        bool hasLetter = false;
                        for(int i=0; lineBuffer[i] != '\0'; i++) {
                            if(isAlpha(lineBuffer[i])) { hasLetter = true; break; }
                        }

                        if (hasLetter && isAllowed(lineBuffer)) {
                            Serial2.print(lineBuffer); 
                            Serial2.print('\n'); 

                            waitingForOk = true; 
                            lastOkTime = millis();
                            strncpy(lastSentGCode, lineBuffer, sizeof(lastSentGCode)-1);
                            sentFileBytes = sdCtrl.file().curPosition();
                            
                            lineIdx = 0; 
                            SDControl::releaseAllSlots();
                            return; 
                        }
                    }
                    lineIdx = 0; 
                } 
                else if (lineIdx < (int)sizeof(lineBuffer) - 1) {
                    lineBuffer[lineIdx++] = c;
                }
            }

            // ファイル末尾に達した場合
            if (!sdCtrl.file().available()) {
                // ファイル末尾だが [MSG:Pgm End] がまだ来ていない状態
                // 何も送らずに受信解析（上のセクション2）での終了を待つ
                static uint32_t fileEndWait = millis();
                if (millis() - fileEndWait > 5000) { // 5秒待ってもMSGが来ない場合の保険
                    isStreaming = false;
                    stopGCodeStream();
                    currentMode = MODE_MENU;
                    isFirstOpen = true;
                }
            }
        }

        // --- 6. 画面描画更新 ---
        static uint32_t lastUpdate = 0;
        if (isStreaming && (millis() - lastUpdate > 300)) {
            updateSendingStatus(sentFileBytes, totalFileBytes, lastSentGCode, lastGrblResponse);
            lastUpdate = millis();
        }
    }
}
// SDファイルリストのUI描画
void drawSDFileListUI() {
    // 1. 画面の初期化
    tft.fillScreen(0x0000);
    // 外枠の描画（160x128などのサイズに合わせて調整）
    tft.drawRect(2, 12, 156, 113, 0xFFFF);

    // 2. スロット名の表示
    tft.setCursor(5, 2);
    tft.setTextSize(1);
    tft.setTextColor(0x07E0); // 緑色
    
    // 現在のスロット名を表示
    tft.print(sdCtrl.getCurrentSlotName()); 

    // 3. ファイルリストとページの取得
    int count = sdCtrl.getFileCount();
    int selected = sdCtrl.getSelectedIndex();
    
    // 現在のインデックスから表示すべきページ番号を逆算 (1画面10個)
    // currentPage はグローバル変数
    currentPage = selected / ITEMS_PER_PAGE; 

    if (count == 0) {
        tft.setCursor(10, 30);
        tft.setTextColor(0xF800); // 赤色
        tft.println(F("No Files Found."));
    } else {
        // 現在のページに応じたループの開始位置と終了位置を計算
        int startIdx = currentPage * ITEMS_PER_PAGE;
        int endIdx = startIdx + ITEMS_PER_PAGE;
        if (endIdx > count) endIdx = count; // ファイル総数を超えないようにガード

        // 表示行（0〜9行目）のカウンター
        int displayRow = 0; 

        for (int i = startIdx; i < endIdx; i++) {
            // 表示開始位置の計算（行ごとに10ピクセルずつ下げる）
            tft.setCursor(5, 20 + (displayRow * 10));

            // --- 選択表示（カーソル） ---
            if (i == selected) {
                tft.setTextColor(0x0000, 0x07E0); // 文字黒、背景緑（反転）
                tft.print(F("> "));
            } else {
                tft.setTextColor(0xFFFF, 0x0000); // 文字白、背景黒
                tft.print(F("  "));
            }
            
            // --- ファイル名表示処理（安全な文字列操作） ---
            char* originalName = sdCtrl.getFileName(i);
            
            // 表示用の一時バッファ
            char displayName[22]; 
            const int MAX_CHAR = 18; // 表示可能な最大文字数

            if (originalName != nullptr) {
                if (strlen(originalName) > MAX_CHAR) {
                    // 名前が長すぎる場合は切り詰めて "..." を付与
                    strncpy(displayName, originalName, MAX_CHAR - 3);
                    displayName[MAX_CHAR - 3] = '\0';
                    strcat(displayName, "...");
                } else {
                    // 安全にコピー
                    strncpy(displayName, originalName, sizeof(displayName) - 1);
                    displayName[sizeof(displayName) - 1] = '\0';
                }
                tft.println(displayName);
            }

            displayRow++; // 画面上の次の行へ
        }

        // --- 4. ページ情報の表示（画面最下部の空きスペースを利用） ---
        int totalPages = (count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        tft.setCursor(100, 2); // スロット名の右側あたりに配置（座標は微調整してください）
        tft.setTextColor(0x7BDD); // 灰色
        tft.print(currentPage + 1);
        tft.print(F("/"));
        tft.print(totalPages);
        tft.print(F(" P"));
    }
}
// SDファイルリスト画面のボタン処理
void handleSDButtons() {
    
    // --- 0. 初回進入時の自動スキャン ---
    if (isFirstOpen) {
        isFirstOpen = false; // ループ防止
        
        tft.fillScreen(0x0000); 
        tft.setCursor(0, 0);
        tft.setTextColor(0xFFFF);
        tft.println(F("Scanning SD slots..."));

        bool foundA = false;
        bool foundB = false;

        // --- Slot A をチェック ---
        tft.setTextColor(0xFFFF);
        tft.print(F("Slot "));
        foundA = sdCtrl.scanFiles(SLOT_A);
        if (foundA) {
            tft.setTextColor(0x07E0); // 緑
            tft.println(F("OK"));
        } else {
            tft.setTextColor(0x7BDD); // 灰色
            tft.println(F("Empty"));
            tft.println(F("Press ESC to Menu."));
        

    }

        // --- 判定：どちらも無い場合 ---
        if (!foundA && !foundB) {
            tft.setCursor(0, 150);
            tft.setTextColor(0xF800); // 赤
            tft.println(F("Error: No SD Card found!"));
            tft.setTextColor(0xFFFF);
            tft.println(F("\nInsert SD and try again."));
            tft.println(F("Press ESC to Menu."));
            
            // 物理ピンを安全にHIGHにしておく
            SDControl::releaseAllSlots();
            
            // ここで return することで、以降の描画処理(drawSDFileListUI)を実行させない
            // あとは loop 内で ESC ボタンが押されるのを待つ
            return; 
        }

        // --- どちらか、あるいは両方ある場合の優先順位設定 ---
        if (foundA) {
            // Aがあるなら、起動直後はAを表示対象にする
            sdCtrl.scanFiles(SLOT_A);
        } else if (foundB) {
            // Aが無くてBがあるなら、Bを表示対象にする
            sdCtrl.scanFiles(SLOT_B);
        }

        delay(500);
        SDControl::releaseAllSlots();
        drawSDFileListUI();
        isConfirming = false;
        return;
    }
    
    // --- A. 確認ダイアログ表示中の処理 ---
    if (isConfirming) {
        if (sw.isPressed(OK_BTN)) {
            char* file = sdCtrl.getSelectedFileName();
            if (file) {
                strncpy(currentFileName, file, sizeof(currentFileName) - 1);
                currentFileName[sizeof(currentFileName) - 1] = '\0';
                
                tft.setCursor(10, 100);
                tft.setTextColor(0x07E0);
                tft.println(F("Loading..."));

                delay(100); 
                // 送信モードへ移る前にバスを完全に解放
                SDControl::releaseAllSlots();
                isConfirming = false;
                isFirstOpen = true; 
                currentMode = MODE_SENDING; 
                return; 
            }
        }
        if (sw.isPressed(ESC_BTN)) {
            isConfirming = false;
            SDControl::releaseAllSlots();
            drawSDFileListUI(); 
            delay(200);
        }
        return; 
    }

   
    // 上移動
    if (sw.isPressed(Y_UP)) {
        if (sdCtrl.getFileCount() > 0) {
            sdCtrl.selectPrev();
            // 描画のみ。SDカードへの通信は発生しないので releaseAllSlots は不要でも可
            drawSDFileListUI();
        }
        delay(150);
    }
    
    // 下移動
    if (sw.isPressed(Y_DOWN)) {
        if (sdCtrl.getFileCount() > 0) {
            sdCtrl.selectNext();
            drawSDFileListUI();
        }
        delay(150);
    }

    // 決定（ファイル選択）
    if (sw.isPressed(OK_BTN)) {
        char* file = sdCtrl.getSelectedFileName();
        if (file && strlen(file) > 0) {
            // ダイアログ表示前にバスを一度リセット
            SDControl::releaseAllSlots(); 
            drawConfirmDialog(file); 
            isConfirming = true;
            delay(200);
        }
    }

    // メニューに戻る
    if (sw.isPressed(ESC_BTN) && !backLastState) {
        sdCtrl.closeCurrent();
        SDControl::releaseAllSlots(); // 完全に通信を断つ
        currentMode = MODE_MENU;
        drawMenuUI();
        isFirstOpen = true; 
    }
    backLastState = sw.isPressed(ESC_BTN);
}
// GRBL設定画面の描画と操作
void drawSettingUI() {
    tft.fillScreen(0x0000); // 黒背景
    tft.drawRect(2, 12, 158, 100, 0xFFFF);
    tft.setCursor(20, 2);
    tft.setTextColor(0xFFE0);
    tft.println(" [ GRBL SETTINGS ]");
    //tft.drawFastHLine(0, 15, 128, 0xFFFF);
    tft.drawRect(2,113,158,15,0x07E0);

    for (int i = 0; i < TOTAL_SETTINGS; i++) {
        tft.setCursor(5, 18 + (i * 10));
        if (i == selectedIndex) {
            tft.setTextColor(0xFFE0); // 選択項目は黄色
            tft.print("> "); 
        } else {
            tft.setTextColor(0xFFFF);  // それ以外は白
            tft.print("  ");
        }
        tft.println(SETTINGS[i].name);
    }
    
}
//設定画面のレスポンスコンソール
void drawSetconsoleUI() {
    tft.fillRect(0, 0, 160, 110, 0x0000); // コンソール消去黒背景
    tft.drawRect(2, 12, 158, 100, 0xFFFF);//コンソール枠
    
    tft.setCursor(10, 2);
    tft.setTextColor(0x07FF);
    tft.print("[Response]");

    tft.setTextColor(0xFFE0);
    tft.drawRect(2,113,158,15,0x07E0);//コマンドコンソール枠
    consoleLineCount = 0;
 }
//コマンド送信後のレスポンスやエラーをコンソールエリアに表示する関数 
void displayLogPage(int page, int totalLines) {
    drawSetconsoleUI();
    
    int startLine = page * 8;
    int endLine = (startLine + 8 > totalLines) ? totalLines : startLine + 8;

    tft.setCursor(80, 2);
    tft.setTextColor(0xFFFF);
    tft.printf("PAGE %d/%d", page + 1, (totalLines + 7) / 8);

    for (int i = startLine; i < endLine; i++) {
        // リングバッファから行を取得（tailを起点に計算）
        const char* msg = grbl.getLineAt(i); 
        
        int currentY = logStartY + (consoleLineCount * lineHeight);
        tft.setCursor(logStartX, currentY);
        
        // 色分け判定
        if (strstr(msg, "error") != NULL || strstr(msg, "ALARM") != NULL) {
            tft.setTextColor(0xF800); // 赤
        } else {
            tft.setTextColor(0x07FF); // 水色
        }
        
        tft.printf("> %s", msg);
        consoleLineCount++;
    }

        
}
// GRBL設定画面のボタン処理
void handleSettingButtons() {
    // 【閲覧モード】受信完了後にUP/DOWNでページをめくる
    //
    if (isShowingConsole) {
        int maxPage = (totalCapturedLines > 0) ? (totalCapturedLines - 1) / 8 : 0;
        bool pageChanged = false;

        // Y_UPボタンで前のページ
        bool upNow = sw.isPressed(Y_UP);
        if (upNow && !upLastState) {
            if (currentLogPage > 0) {
                currentLogPage--;
                pageChanged = true;
            }
        }
        upLastState = upNow;

        // Y_DOWNボタンで次のページ
        bool downNow = sw.isPressed(Y_DOWN);
        if (downNow && !downLastState) {
            if (currentLogPage < maxPage) {
                currentLogPage++;
                pageChanged = true;
            }
        }
        downLastState = downNow;

        // 変更があれば再描画
        if (pageChanged) {
            displayLogPage(currentLogPage, totalCapturedLines);
        }

        // ESCで設定メニューに戻る
        if (sw.isPressed(ESC_BTN) && !backLastState) {
            grbl.clearBuffer();
            isShowingConsole = false;
            currentMode = MODE_SETTING;
            drawSettingUI();
        }
        backLastState = sw.isPressed(ESC_BTN);
        return; 
    }

    // 【設定選択モード】リストの上下移動
    bool upMenu = sw.isPressed(Y_UP);
    if (upMenu && !upLastState) {
        selectedIndex = (selectedIndex - 1 + TOTAL_SETTINGS) % TOTAL_SETTINGS;
        drawSettingUI();
    }
    upLastState = upMenu;

    bool downMenu = sw.isPressed(Y_DOWN);
    if (downMenu && !downLastState) {
        selectedIndex = (selectedIndex + 1) % TOTAL_SETTINGS;
        drawSettingUI();
    }
    downLastState = downMenu;

    // ESCで閲覧を終了して「設定リスト選択画面」に戻る
        if (sw.isPressed(ESC_BTN) && !backLastState) {
            grbl.clearBuffer();
            isShowingConsole = false;
            currentMode = MODE_MENU;
            drawMenuUI();
        }
        backLastState = sw.isPressed(ESC_BTN);
        //return;

    // 【送信実行】OKボタンでコマンド送信し、全レスポンスをバッファする
    if (sw.isPressed(OK_BTN) && !enterLastState) {
        const char* cmd = SETTINGS[selectedIndex].command;//コマンド選択変数
        
            while (Serial2.available()) {
            Serial2.read(); // 溜まっている古いデータをすべて捨てる
             }

        // コンソール表示
        tft.setCursor(5, 117);
        tft.setTextColor(0x07E0);
        tft.println("TX:");
        tft.setCursor(30, 117);
        tft.setTextColor(0x07E0);
        tft.println(cmd);

        // バッファをクリアして送信
        grbl.clearBuffer();
        if (strcmp(cmd, "SoftRESET") == 0){
             grbl.sendByte(0x18);
            }else{
                Serial2.println(cmd);
            }
        
        // --- 集中受信フェーズ ---
        // データが途切れるまで、または最大1.5秒間バッファに溜める
        uint32_t startTime = millis();
        while (millis() - startTime < 1200) {
            grbl.update(); // シリアルからリングバッファへ
            // シリアルにまだ生データがある場合は待ち時間をリセット
            if (Serial2.available()) {
                startTime = millis();
            }
        }

        // --- 閲覧フェーズへ移行 ---
        totalCapturedLines = grbl.getStoredLineCount(); // 受信した総行数を取得
        currentLogPage = 0;
        isShowingConsole = true;
        
        if (totalCapturedLines > 0) {
            displayLogPage(currentLogPage, totalCapturedLines);
        } else {
            // 返信がなかった場合
            drawSetconsoleUI();
            tft.setCursor(40, 55);
            tft.println("No Response.");
        }
    }
    enterLastState = sw.isPressed(OK_BTN);
}
//プローブモードＵＩ
void drawProbeUI() {
    tft.fillScreen(0x0000);
    
    // タイトル枠の描画
    tft.drawRect(2, 12, 156, 113, 0xFFFF);
    tft.setCursor(5, 2);
    tft.setTextSize(1);
    tft.setTextColor(0x07E0); // 緑文字
    tft.print(F("PROBE MENU"));

    // 各メニュー項目の文字列定義
    const char* itemLabels[PROBE_COUNT] = {
        "Z-Probe Touch",
        "X-Probe [ X+ ]",
        "X-Probe [ X- ]",
        "Y-Probe [ Y+ ]",
        "Y-Probe [ Y- ]",
        "XY Corner (Left-Down)",
        "Center Find (Inner)",
        "Center Find (Outer)",
        "Tool Dia: " // ここだけ特殊描画
    };

    for (int i = 0; i < PROBE_COUNT; i++) {
        // 各行のY座標（1行11ピクセルピッチ）
        int yPos = 16 + (i * 11);
        tft.setCursor(6, yPos);

        // 選択行のハイライト処理
        if (i == currentProbeItem) {
            if (i == PROBE_TOOL_DIA && isEditingToolDia) {
                tft.setTextColor(0x0000, 0xF800); // ツール径編集中の場合は背景を「赤」にして警告
            } else {
                tft.setTextColor(0x0000, 0x07E0); // 通常の選択は背景「緑」
            }
            tft.print(F(">"));
        } else {
            tft.setTextColor(0xFFFF, 0x0000); // 非選択は「白文字/黒背景」
            tft.print(F(" "));
        }

        // 文字列の描画
        if (i == PROBE_TOOL_DIA) {
            tft.print(itemLabels[i]);
            tft.print(toolDiameter, 1); // 小数点第1位まで表示 (例: 3.2mm)
            tft.print(F(" mm"));
        } else {
            tft.print(itemLabels[i]);
        }
    }
}
// ワーク径（内径/外径）入力のポップアップUI描画
void drawPopupUI() {
    // 画面中央付近にポップアップの枠を描画（サイズは液晶に合わせて調整してください）
    // 例：160x128の画面の場合のレイアウト例
    tft.fillRect(10, 30, 140, 75, 0x18E3); // 背景：ダークグレー
    tft.drawRect(10, 30, 140, 75, 0xFFFF); // 枠線：白

    tft.setTextSize(1);
    tft.setTextColor(0xFFFF);
    
    // モードに応じてタイトルを切り替え
    tft.setCursor(20, 40);
    if (pendingProbeItem == PROBE_CENTER_IN) {
        tft.print(F("Target Hole Dia"));
    } else {
        tft.print(F("Target Boss Dia"));
    }

    // 現在の入力値を大きく表示
    tft.setCursor(35, 60);
    tft.setTextSize(2);
    tft.setTextColor(0x07E0); // 数値は目立つ緑色など
    tft.print(workDiameter, 1);
    tft.print(F("mm"));

    // ガイドライン
    tft.setTextSize(1);
    tft.setTextColor(0xBDF7);
    tft.setCursor(15, 90);
    tft.print(F("[X]:+-[OK]:Run[ESC]:X"));
}
//プローブモードのボタン処理
void handleProbeButtons() {

    // 1. 初回進入時の画面描画
    if (isFirstOpen) {
        isFirstOpen = false;
        isEditingToolDia = false;
        isEditingWorkDia = false; // ★追加
        isProbeExecuting = false;
        drawProbeUI();
        return;
    }

    // 2. プローブ実行（G-Codeストリーミング）中の場合はボタン入力を制限
    if (isProbeExecuting) {
        // ※ 必要に応じてここでGRBLからの 'ok' 受信監視や緊急停止(ESC)のみ受け付ける処理を入れます
        return;
    }

    // --- A. ツール径の数値編集モード中の処理 ---
    if (isEditingToolDia) {
        if (sw.isPressed(X_RT)) { // 0.1mm増加
            toolDiameter += 0.1f;
            if (toolDiameter > 20.0f) toolDiameter = 20.0f;
            drawProbeUI();
            delay(100);
        }
        if (sw.isPressed(X_LF)) { // 0.1mm減少
            toolDiameter -= 0.1f;
            if (toolDiameter < 0.1f) toolDiameter = 0.1f;
            drawProbeUI();
            delay(100);
        }
        if (sw.isPressed(OK_BTN) || sw.isPressed(ESC_BTN)) { // 編集確定またはキャンセル
            isEditingToolDia = false;
            drawProbeUI();
            delay(200);
        }
        return;
    }

    // --- ★追加 B. ワーク径（ポップアップ）の数値編集モード中の処理 ---
    if (isEditingWorkDia) {
        // 既存のツール径編集と同様に、X+/X-（左右キー）で直径を増減
        if (sw.isPressed(X_RT)) { // 1.0mm増加（ワーク径は1mmステップが快適です）
            workDiameter += 1.0f;
            if (workDiameter > 500.0f) workDiameter = 500.0f; // 上限
            drawPopupUI(); // ★専用のポップアップ描画関数
            delay(100);
        }
        if (sw.isPressed(X_LF)) { // 1.0mm減少
            workDiameter -= 1.0f;
            if (workDiameter < 1.0f) workDiameter = 1.0f;   // 下限
            drawPopupUI();
            delay(100);
        }
        
        // 決定ボタン：数値を確定して、保留していたマクロを実行！
        if (sw.isPressed(OK_BTN)) {
            isEditingWorkDia = false;
            // 保留していた内径（CENTER_IN）または外径（CENTER_OUT）を実行
            executeProbeMacro(pendingProbeItem); 
            delay(200);
        }
        
        // ESCボタン：キャンセルして通常メニューに戻る
        if (sw.isPressed(ESC_BTN)) {
            isEditingWorkDia = false;
            drawProbeUI(); // 通常の画面に戻す
            delay(200);
        }
        return;
    }

    // --- C. 通常のメニュー選択モード中の処理 ---

    // メニュー選択：下移動
    if (sw.isPressed(Y_DOWN)) {
        currentProbeItem = (currentProbeItem + 1) % PROBE_COUNT;
        drawProbeUI();
        delay(150);
    }

    // メニュー選択：上移動
    if (sw.isPressed(Y_UP)) {
        currentProbeItem = (currentProbeItem - 1 + PROBE_COUNT) % PROBE_COUNT;
        drawProbeUI();
        delay(150);
    }

    // 決定ボタン
    if (sw.isPressed(OK_BTN)) {
        if (currentProbeItem == PROBE_TOOL_DIA) {
            // ツール径設定行なら、数値編集モードへ
            isEditingToolDia = true;
            drawProbeUI();
        } 
        // ★追加：もし選ばれたのが「内径中心(CENTER_IN)」または「外径中心(CENTER_OUT)」なら
        else if (currentProbeItem == PROBE_CENTER_IN || currentProbeItem == PROBE_CENTER_OUT) {
            isEditingWorkDia = true;          // ポップアップ編集モードをON
            pendingProbeItem = currentProbeItem; // どのマクロを走らせるかを記憶
            drawPopupUI();                     // ポップアップを画面に描画
        } 
        else {
            // それ以外（単軸プローブなど）なら、そのまま実行
            executeProbeMacro(currentProbeItem);
        }
        delay(200);
    }

    // ESCボタンでメインメニューに戻る
    if (sw.isPressed(ESC_BTN) && !backLastState) {
        currentMode = MODE_MENU;
        drawMenuUI();
        isFirstOpen = true;
    }
    backLastState = sw.isPressed(ESC_BTN);
}
// 初期設定と起動ロゴの表示
void setup() {

    Serial.begin(115200);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);
    Serial.setTimeout(1);


    tft.tftbegin();
    sw.swbegin();
 
    //sdCtrl.scanFiles(SLOT_A); // 初期スロットを指定
    // 起動ロゴを表示
    delay(150);
    digitalWrite(TFT_BL, HIGH);
    drawOpeningScreen();
    
    // メニュー画面を表示
    drawMenuUI();
    
}
// メインループ：モードごとの処理を呼び出す
void loop() {
   
    switch (currentMode) {
        case MODE_MENU:    handleMenuButtons();    break;
        case MODE_JOG:     handleJogButtons();    break;
        case MODE_SD:      handleSDButtons();     break;
        case MODE_SETTING: handleSettingButtons(); break;
        case MODE_SENDING: handleSendingMode();    break;
        case MODE_PROBE:   handleProbeButtons(); break;
    }
    handleProbeStreaming();
}
