#ifndef PROBE_MACRO_H
#define PROBE_MACRO_H

#include <Arduino.h>
#include "config.h" // 設定パラメータを使用するため
void drawMenuUI();
// =================================================================
// 1. プローブ専用の列挙型 (Enums)
// =================================================================
// どのサブメニューが選択されているか
enum ProbeSubMode {
    PROBE_Z = 0,
    PROBE_X_PLUS,
    PROBE_X_MINUS,
    PROBE_Y_PLUS,
    PROBE_Y_MINUS,
    PROBE_XY_CORNER,  // 左下ゼロ設定
    PROBE_CENTER_IN,  // 円形内径中心
    PROBE_CENTER_OUT, // 円形外径中心
    PROBE_TOOL_DIA,   // ツール径設定項目
    PROBE_COUNT       // 総項目数
};

// 1行ずつの同期送信（完全ピンポン）を制御するステート
enum ProbeState {
    STATE_IDLE,
    STATE_X_ORG1,   STATE_X_ORG2, 
    STATE_Z1_RUN,     STATE_Z2_RUN,
    STATE_Y_ORG1,  STATE_Y_ORG2,
    STATE_X2_PRB,  STATE_X2_SLOW,  STATE_Y2_PRB,  STATE_Y2_SLOW,
    STATE_OUT_X1_RUN, STATE_OUT_X2_RUN, STATE_OUT_Y1_RUN, STATE_OUT_Y2_RUN
};

// =================================================================
// 2. 状態管理用の構造体 (Context)
// =================================================================
struct ProbeContext {
    ProbeState activeState = STATE_IDLE;
    
    // マクロ開始時の初期座標
    float startX = 0.0f; float startY = 0.0f; float startZ = 0.0f;
    
    // PRB応答から抽出した最新の接触座標
    float currentPrbX = 0.0f; float currentPrbY = 0.0f; float currentPrbZ = 0.0f;
    
    // 各壁面のタッチ座標記録
    float ORGX1 = 0.0f; float ORGY1 = 0.0f; // 原点設定用の最初のタッチ座標
    float ORGX2 = 0.0f; float ORGY2 = 0.0f; // コーナー設定用のタッチ座標
    float wallX1 = 0.0f; float wallX2 = 0.0f;
    float wallY1 = 0.0f; float wallY2 = 0.0f;
    float wallZ1 = 0.0f; float wallZ2 = 0.0f;
    
    // コーナー、および計算された中心座標
    float cornerX1 = 0.0f; float cornerY1 = 0.0f;
    float calculatedCenterX = 0.0f; float calculatedCenterY = 0.0f;
};

// =================================================================
// 3. 外部ファイル（main.cpp等）に公開する動的変数・関数の宣言
// =================================================================
// 実体は「probe_macro.cpp」側に置き、main側からもアクセス可能にする(extern)
extern ProbeContext probeCtx;
extern bool isProbeExecuting;
extern int currentProbeItem;
extern float toolDiameter;
extern bool isEditingToolDia;
extern bool isEditingWorkDia;  
extern int pendingProbeItem; 
extern float workDiameter;

// メインから呼び出すコア関数
void executeProbeMacro(int mode);   // プローブ開始トリガー
void handleProbeStreaming();       // loop()内で常に回す通信ロジック

#endif // PROBE_MACRO_H