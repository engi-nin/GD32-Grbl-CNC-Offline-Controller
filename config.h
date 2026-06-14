#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =================================================================
// 1. システム・バージョン情報
// =================================================================
const char* const VERSION_STR = "Ver 1.0";

// =================================================================
// 2. 物理ピン配置 (Pin Assignments)
// =================================================================
#define TFT_BL         PD13    // TFT バックライトピン

// ソフトSPI (SDカード用) ピン配置
#define SD_MOSI_PIN    PB15
#define SD_MISO_PIN    PB14
#define SD_SCK_PIN     PB13
#define SD_CS_PIN      PC7

// GRBLシリアル通信ピン (HardwareSerial)
#define GRBL_TX_PIN    PA2
#define GRBL_RX_PIN    PA3

// =================================================================
// 3. キー入力・ボタンマッピング設定
// =================================================================
#define ESC_BTN        EXIT    // 物理スイッチのキャンセル割り当て
#define OK_BTN         OK      // 物理スイッチの決定割り当て

#define Y_PL           Y_UP    // Ｙ＋方向ボタン
#define Y_DN           Y_DOWN  // Ｙ－方向ボタン
#define X_LF           X_DOWN  // Ｘ－方向ボタン
#define X_RT           X_UP    // Ｘ＋方向ボタン
#define Z_PL           Z_UP    // Ｚ＋方向ボタン
#define Z_DN           Z_DOWN  // Ｚ－方向ボタン

const int LONG_PRESS_MS = 800;        // 長押しと判定する時間（ミリ秒）
const unsigned long LONG_PRESS_TIME = 1000; // ポップアップ等での長押し確定時間

// =================================================================
// 4. UI・グラフィックデザイン定数
// =================================================================
#define BTN_W          20      // 標準のボタン横幅
#define BTN_H          16      // 標準のボタン高さ
#define BTN_R          5       // ボタン角の丸み
#define BTN_Z_W        20      // Z軸等の細長いボタン用横幅

const int ITEMS_PER_PAGE = 10; // SDカード等の1画面あたり最大表示数
const int lineHeight     = 10; // コンソールログの1行の高さ
const int logStartX      = 5;  // ログの描画開始X座標
const int logStartY      = 18; // ログの描画開始Y座標
enum DisplayMode { MODE_MENU,MODE_JOG, MODE_SD, MODE_SETTING ,MODE_SENDING,MODE_PROBE};
// =================================================================
// 5. CNC・プローブ（探査）初期設定パラメータ
// =================================================================
const float DEFAULT_TOOL_DIAMETER = 3.0f;  // デフォルトのツール径 (mm)
const float DEFAULT_PROBE_FEEDRATE = 40.0f; // タッチ探査時のフィードスピード (F)

// 外径・内径探査の安全逃げ・潜り設定
const float DEFAULT_OUTER_CLEARANCE_X = 40.0f; // 開始点から左右に逃げる距離 (mm)
const float DEFAULT_OUTER_CLEARANCE_Y = 40.0f; // 開始点から前後に逃げる距離 (mm)
const float DEFAULT_OUTER_PROBE_DEPTH = 15.0f; // タッチのためにZ軸が潜る深さ (mm)


#endif // CONFIG_H