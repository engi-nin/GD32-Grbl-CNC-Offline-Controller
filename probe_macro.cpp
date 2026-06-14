#include <Arduino.h>
#include "probe_macro.h"    // 自身のヘッダ（構造体や関数の宣言）
#include "config.h"         // ピン配置や定数マクロ
#include "tft_control.h"    // TFT操作用クラスの型定義
#include "switch_control.h" // スイッチ操作用クラスの型定義
#include "grbl_serial.h"    // GRBL通信用クラスの型定義

// =================================================================
// 1. 他のファイル（main.cpp）で定義されているグローバルインスタンスの参照
// =================================================================
extern TFTControl tft;
extern SwitchControl sw;
extern DisplayMode currentMode;
extern bool isFirstOpen;

// =================================================================
// 2. probe_macro.h で宣言した変数の実体定義
// =================================================================
ProbeContext probeCtx;
bool isProbeExecuting = false;
int currentProbeItem = 0;
bool isEditingToolDia = false;

float toolDiameter = DEFAULT_TOOL_DIAMETER;
float probeFeedRate = DEFAULT_PROBE_FEEDRATE;
float outerClearanceX = DEFAULT_OUTER_CLEARANCE_X;
float outerClearanceY = DEFAULT_OUTER_CLEARANCE_Y;
float outerProbeDepth = DEFAULT_OUTER_PROBE_DEPTH;

// =================================================================
// 3. 内部管理用スタティック変数群
// =================================================================
static int currentProbeMode = PROBE_Z;
static const char *probeCmds[40]; 
static int probeCmdCount = 0;
static int probeCurrentLine = 0;

static bool isProbeWaitingOk = false;
static unsigned long probeLastOkTime = 0;

// 送信用に加工したGコード文字列を保持する一時バッファ
static String dynamicCmdBuffer = "";

// =================================================================
// 4. マクロ構築関数 (executeProbeMacro)
// =================================================================
void probeActiveUI() {
    tft.fillScreen(0x0000);
    tft.drawRect(2, 12, 156, 113, 0xFFFF);
    tft.setCursor(40, 2);
    tft.setTextColor(0xF800);
    tft.print(F("PROBING ACTIVE"));

    tft.fillRect(5, 20, 150, 15, 0x0000);
    tft.setCursor(5, 20);
    tft.setTextColor(0xF800); // 赤
    tft.print(F("tool:"));
    tft.print(toolDiameter, 3);
    tft.print(F(" mm"));
}
void executeProbeMacro(int mode)
{
    probeCmdCount = 0;
    probeCurrentLine = 0;
    isProbeWaitingOk = false;

    switch (mode)
    {
    case PROBE_Z:
        probeCmds[probeCmdCount++] = "D_Z1_RUN";   // 高速探査開始
        probeCmds[probeCmdCount++] = "D_Z2_MOVE";  // 2mm上に復帰
        probeCmds[probeCmdCount++] = "D_Z2_RUN";   // 微速探査開始
        probeCmds[probeCmdCount++] = "D_Z_FINISH"; // Z0登録＆上空退避
        break;

    case PROBE_X_PLUS:
        probeCmds[probeCmdCount++] = "D_X_PLUS_RUN1";   // ① 高速プローブ
        probeCmds[probeCmdCount++] = "D_X_PLUS_RESET1"; // ② 1回目のゼロリセット ★追加
        probeCmds[probeCmdCount++] = "D_X_PLUS_MOV";    // ③ -2mm戻り
        probeCmds[probeCmdCount++] = "D_X_PLUS_RUN2";   // ④ 微速プローブ
        probeCmds[probeCmdCount++] = "D_X_PLUS_RESET2"; // ⑤ 2回目のゼロリセット ★追加
        break;

    case PROBE_X_MINUS:
        probeCmds[probeCmdCount++] = "D_X_MINUS_RUN1";   // ① 高速プローブ
        probeCmds[probeCmdCount++] = "D_X_MINUS_RESET1"; // ② 1回目のゼロリセット ★追加
        probeCmds[probeCmdCount++] = "D_X_MINUS_MOV";    // ③ +2mm戻り
        probeCmds[probeCmdCount++] = "D_X_MINUS_RUN2";   // ④ 微速プローブ
        probeCmds[probeCmdCount++] = "D_X_MINUS_RESET2"; // ⑤ 2回目のゼロリセット ★追加
        break;

    case PROBE_Y_PLUS:
        probeCmds[probeCmdCount++] = "D_Y_PLUS_RUN1";   // ① 高速プローブ
        probeCmds[probeCmdCount++] = "D_Y_PLUS_RESET1"; // ② 1回目のゼロリセット ★追加
        probeCmds[probeCmdCount++] = "D_Y_PLUS_MOV";    // ③ -2mm戻り
        probeCmds[probeCmdCount++] = "D_Y_PLUS_RUN2";   // ④ 微速プローブ
        probeCmds[probeCmdCount++] = "D_Y_PLUS_RESET2"; // ⑤ 2回目のゼロリセット ★追加
        break;

    case PROBE_Y_MINUS:
        probeCmds[probeCmdCount++] = "D_Y_MINUS_RUN1";   // ① 高速プローブ
        probeCmds[probeCmdCount++] = "D_Y_MINUS_RESET1"; // ② 1回目のゼロリセット ★追加
        probeCmds[probeCmdCount++] = "D_Y_MINUS_MOV";    // ③ +2mm戻り
        probeCmds[probeCmdCount++] = "D_Y_MINUS_RUN2";   // ④ 微速プローブ
        probeCmds[probeCmdCount++] = "D_Y_MINUS_RESET2"; // ⑤ 2回目のゼロリセット ★追加
        break;

    case PROBE_XY_CORNER:
        probeCmds[probeCmdCount++] = "D_IN_X_START"; // スタート位置の確定
        // ─── X軸プローブシーケンス ───
        probeCmds[probeCmdCount++] = "D_CR_X1";       // 1回目：X軸高速探査開始
        probeCmds[probeCmdCount++] = "D_CR_X_RESET1"; // ★追加：1回目高速タッチ位置をその場で暫定X0にリセット
        probeCmds[probeCmdCount++] = "D_CR_X_MOV";    // 絶対座標 G0 X-2.000 で確実に2mm固定値退避
        probeCmds[probeCmdCount++] = "D_CR_X2";       // 2回目：絶対座標 X3.000 に向けて精密微速探査
        probeCmds[probeCmdCount++] = "D_CR_X_RESET2"; // ★追加：2回目精密タッチ位置をその場で本当の【X0.0】に確定
        probeCmds[probeCmdCount++] = "D_CR_X_ESC";    // X0から「工具半径＋5mm」分、安全に左へ逃げる（固定値移動）

        // ─── Y軸への回り込みシーケンス ───
        probeCmds[probeCmdCount++] = "D_CR_Y1"; // Y軸のスタート位置（手前側）まで下がる
        probeCmds[probeCmdCount++] = "D_CR_Y2"; // ワークの角を安全に回り込んで、Y軸探査の正面位置へ移動

        // ─── Y軸プローブシーケンス ───
        probeCmds[probeCmdCount++] = "D_CR_Y3";       // 1回目：Y軸高速探査開始
        probeCmds[probeCmdCount++] = "D_CR_Y_RESET1"; // 1回目高速タッチ位置をその場で暫定Y0にリセット
        probeCmds[probeCmdCount++] = "D_CR_Y_MOV";    // 絶対座標 G0 Y-2.000 で確実に2mm固定値退避
        probeCmds[probeCmdCount++] = "D_CR_Y4";       // 2回目：絶対座標 Y3.000 に向けて精密微速探査
        probeCmds[probeCmdCount++] = "D_CR_Y_RESET2"; // ★追加：2回目精密タッチ位置をその場で本当の【Y0.0】に確定

        // ─── 最終原点オフセット設定 ───
        probeCmds[probeCmdCount++] = "D_CR_FINAL_OFFSET"; // ★変更：壁面(0,0)から工具半径分マイナスしてワークの角を原点化
        break;

    case PROBE_CENTER_IN:
        probeCmdCount = 0; // カウンタの初期化（必要に応じて）

        // ─── X軸センター探査 ───
        probeCmds[probeCmdCount++] = "D_IN_X_START";       // スタート位置の確定
        probeCmds[probeCmdCount++] = "D_IN_X1_PRB";        // 左壁（X-）へ高速探査
        probeCmds[probeCmdCount++] = "D_CR_X_RESET1";          // プローブＸタッチ位置をその場で暫定X0にリセット
        probeCmds[probeCmdCount++] = "D_IN_X1_BAK";        // 【G90】絶対座標で右(X+)へ2mm戻る (G0 X2.000)
        probeCmds[probeCmdCount++] = "D_IN_X1_SLOW";       // 【G90】左(X-)へ5mm追い込んで微速精密探査 (G38.2 X-5.000)
        probeCmds[probeCmdCount++] = "D_IN_X2_MOV";        // 【G90】登録直径のマイナス3mmの位置まで右(X+)に安全大ジャンプ
        probeCmds[probeCmdCount++] = "D_IN_X2_PRB";        // 【G90】ジャンプ先からさらに右(X+)へ高速探査
        probeCmds[probeCmdCount++] = "D_IN_X2_BAK";        // 【新規】高速タッチ位置の絶対座標を一時変数に格納
        probeCmds[probeCmdCount++] = "D_IN_X2_SLOW";       // 【G90】右(X+)へ微速精密探査（戻った位置から+5mm押し込み）
        probeCmds[probeCmdCount++] = "D_IN_CX";            // 【G90】格納した右壁の距離の半分（中心）へ左(X-)にジャンプ移動
        probeCmds[probeCmdCount++] = "D_IN_X_FINISH"; // 【新規】着地したX軸の中心を完全に「X0」に設定

        // ─── Y軸センター探査 ───
        probeCmds[probeCmdCount++] = "D_IN_Y1_PRB";     // 手前壁（Y-）へ高速探査
        probeCmds[probeCmdCount++] = "D_CR_Y_RESET1";
        probeCmds[probeCmdCount++] = "D_IN_Y1_BAK";     // 【G90】絶対座標で奥(Y+)へ2mm戻る (G0 Y2.000)
        probeCmds[probeCmdCount++] = "D_IN_Y1_SLOW";    // 【G90】手前(Y-)へ5mm追い込んで微速精密探査 (G38.2 Y-5.000)
        probeCmds[probeCmdCount++] = "D_IN_Y2_MOV";     // 【G90】登録直径のマイナス3mmの位置まで奥(Y+)に安全大ジャンプ
        probeCmds[probeCmdCount++] = "D_IN_Y2_PRB";     // 【G90】ジャンプ先からさらに奥(Y+)へ高速探査
        probeCmds[probeCmdCount++] = "D_IN_Y2_BAK";     // 【新規】高速タッチ位置の絶対座標を一時変数に格納
        probeCmds[probeCmdCount++] = "D_IN_Y2_SLOW";    // 【G90】奥(Y+)へ微速精密探査（戻った位置から+5mm押し込み）
        probeCmds[probeCmdCount++] = "D_IN_CY";         // 【G90】格納した奥壁の距離の半分（中心）へ手前(Y-)にジャンプ移動

        // ─── 最終原点確定 ───
        probeCmds[probeCmdCount++] = "D_IN_FINISH"; // 【指示通り】最終着地した中心のその場を、本当のY0（完了）としてリセット！
        break;

    case PROBE_CENTER_OUT:
        probeCmdCount = 0;

        // ─── X軸ボス探査 ───
        probeCmds[probeCmdCount++] = "D_OUT_START";         // 手動開始位置（X-側）
        probeCmds[probeCmdCount++] = "D_OUT_X1_PRB";        // X+方向に高速プローブ
        probeCmds[probeCmdCount++] = "D_CR_X_RESET1";
        probeCmds[probeCmdCount++] = "D_OUT_X1_BAK";        // X-2mmバック
        probeCmds[probeCmdCount++] = "D_OUT_X1_SLOW";       // X+微速プローブ
        probeCmds[probeCmdCount++] = "D_OUT_X1_ESC_X";      // X-2mm退避
        probeCmds[probeCmdCount++] = "D_OUT_X1_UP";         // Z+10mm上昇（逃げ）
        probeCmds[probeCmdCount++] = "D_OUT_X2_JUMP";       // ワークを跨いで右側(X+)へジャンプ移動
        probeCmds[probeCmdCount++] = "D_OUT_X2_DN";         // Z-10mm下降
        probeCmds[probeCmdCount++] = "D_OUT_X2_PRB";        // X-方向に高速プローブ
        probeCmds[probeCmdCount++] = "D_OUT_X2_BAK";        // 高速タッチ座標を一時退避
        probeCmds[probeCmdCount++] = "D_OUT_X2_SLOW";       // X-微速プローブ
        probeCmds[probeCmdCount++] = "D_OUT_X2_ESC";        // X+2mm退避
        probeCmds[probeCmdCount++] = "D_OUT_X2_UP";         // Z+10mm上昇
        probeCmds[probeCmdCount++] = "D_OUT_CX";            // 計算されたX軸中心へジャンプ
        probeCmds[probeCmdCount++] = "D_OUT_X_FINISH"; // 着地したX軸中心を完全に「X0」に設定

        // ─── Y軸ボス探査 ───
        probeCmds[probeCmdCount++] = "D_OUT_Y1_JUMP";    // X中心から手前側(Y-)の場外へジャンプ
        probeCmds[probeCmdCount++] = "D_OUT_Y2_DN";      // Z-10mm下降
        probeCmds[probeCmdCount++] = "D_OUT_Y1_PRB";     // Y+方向に高速プローブ
        probeCmds[probeCmdCount++] = "D_CR_Y_RESET1";
        probeCmds[probeCmdCount++] = "D_OUT_Y1_BAK";     // Y-2mmバック
        probeCmds[probeCmdCount++] = "D_OUT_Y1_SLOW";    // Y微速プローブ
        probeCmds[probeCmdCount++] = "D_OUT_Y1_ESC_Y";   // Y-2mmバック
        probeCmds[probeCmdCount++] = "D_OUT_Y1_UP";      // Z+10mm上昇
        probeCmds[probeCmdCount++] = "D_OUT_Y2_JUMP";    // ワークを跨いで奥側(Y+)の場外へジャンプ
        probeCmds[probeCmdCount++] = "D_OUT_Y2_DN2";     // Z-10mm下降
        probeCmds[probeCmdCount++] = "D_OUT_Y2_PRB";     // Y-方向に高速プローブ
        probeCmds[probeCmdCount++] = "D_OUT_Y2_BAK";     // 高速タッチ座標を一時退避
        probeCmds[probeCmdCount++] = "D_OUT_Y2_SLOW";    // 微速Y-プローブ
        probeCmds[probeCmdCount++] = "D_OUT_Y2_ESC_Y2";  // 精密確定した奥壁の座標を格納
        probeCmds[probeCmdCount++] = "D_OUT_Y2_UP";      // Z+10mm上昇
        probeCmds[probeCmdCount++] = "D_OUT_CY";         // Yの格納した距離の半分（中心）へ移動
        probeCmds[probeCmdCount++] = "D_OUT_FINISH";     // 最終中心着地、Y軸をゼロリセット（完了）
        break;

    case PROBE_TOOL_DIA:
    default:
        return;
    }

    currentProbeMode = mode;
    isProbeExecuting = true;
    probeLastOkTime = millis();

    probeActiveUI();

}

// =================================================================
// 5. プローブコマンドのストリーミングとレスポンス処理（ループ処理）
// =================================================================
void handleProbeStreaming()
{
    if (!isProbeExecuting)
        return;

    // 💡 緊急停止 (ESC)
    if (sw.isPressed(ESC_BTN))
    {
        Serial2.print("\x18"); // GRBLにソフトリセット
        isProbeExecuting = false;
        isProbeWaitingOk = false;
        probeCtx.activeState = STATE_IDLE;

        tft.fillRect(3, 15, 150, 100, 0x0000);
        tft.setCursor(5, 55);
        tft.setTextColor(0xF800);
        tft.println(F("STOPPING CNC..."));

        delay(800);
        currentMode = MODE_MENU;
        drawMenuUI();
        isFirstOpen = true;
        return;
    }

    const uint32_t PROBE_TIMEOUT_MS = 60000;
    grbl.update();

    // 💡 受信解析フェーズ
    if (grbl.grblavailable())
    {
        const char *res = grbl.getNextLine();
        if (res != NULL)
        {
            static char logBuffer[64];

            if (strncmp(res, "[PRB:", 5) == 0)
            {

                strncpy(logBuffer, res, 63);
                logBuffer[63] = '\0';
                for (int i = 0; i < 63; i++)
                {
                    if (logBuffer[i] == '\0')
                        break;
                    if (logBuffer[i] == '\r' || logBuffer[i] == '\n')
                    {
                        logBuffer[i] = '\0';
                        break;
                    }
                }
                tft.fillRect(5, 90, 150, 20, 0x0000);
                tft.setCursor(5, 90);
                tft.setTextColor(0x7BDD);// 水色
                const char* p = logBuffer;
                int len = strlen(logBuffer);
            
                if (len > 22) {
                    // 前半22文字を表示
                    for(int i = 0; i < 22; i++) tft.print(*p++);
                
                    // 💡改行して残りを表示（10ピクセル下げて、X座標は1行目と同じ15に揃える）
                    tft.setCursor(5, 100); 
                    tft.println(p); 
                    } else {
                    tft.println(logBuffer);
                }
               

                // "[PRB:" の直後（数字の先頭）の位置を特定する
                char *ptr = strstr(logBuffer, "[PRB:");

                if (ptr != NULL)
                {
                    ptr += 5; // "[PRB:" の5文字分を右に進めて、最初の数字の先頭を指す

                    // 1つ目の数値を抽出（読んだ後、ptrは自動的にカンマ「,」の位置に進む）
                    probeCtx.currentPrbX = strtof(ptr, &ptr);

                    if (*ptr == ',')
                        ptr++; // カンマを1文字スキップ

                    // 2つ目の数値を抽出（読んだ後、ptrは2つ目のカンマの位置に進む）
                    probeCtx.currentPrbY = strtof(ptr, &ptr);

                    if (*ptr == ',')
                        ptr++; // カンマを1文字スキップ

                    // 3つ目の数値を抽出
                    probeCtx.currentPrbZ = strtof(ptr, &ptr);

                   
                }

                switch (probeCtx.activeState)
                {
                case STATE_X_ORG1:
                    probeCtx.ORGX1 = probeCtx.currentPrbX;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_Y_ORG1:
                    probeCtx.ORGY1 = probeCtx.currentPrbY;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_Z1_RUN:
                    probeCtx.wallZ1 = probeCtx.currentPrbZ;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_Z2_RUN:
                    probeCtx.wallZ2 = probeCtx.currentPrbZ;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_X_ORG2:
                    probeCtx.ORGX2 = probeCtx.currentPrbX;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_Y_ORG2:
                    probeCtx.ORGY2 = probeCtx.currentPrbY;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_X2_PRB:
                    probeCtx.wallX1 = probeCtx.currentPrbX;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_X2_SLOW:
                    probeCtx.wallX2 = probeCtx.currentPrbX;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_Y2_PRB:
                    probeCtx.wallY1 = probeCtx.currentPrbY;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_Y2_SLOW:
                    probeCtx.wallY2 = probeCtx.currentPrbY;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_OUT_X1_RUN:
                    probeCtx.wallX1 = probeCtx.currentPrbX;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_OUT_X2_RUN:
                    probeCtx.wallX2 = probeCtx.currentPrbX;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_OUT_Y1_RUN:
                    probeCtx.wallY1 = probeCtx.currentPrbY;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                case STATE_OUT_Y2_RUN:
                    probeCtx.wallY2 = probeCtx.currentPrbY;
                    probeCtx.activeState = STATE_IDLE; // 本物が届いたので完了
                    break;

                default:
                    break;
                    
                }
            }

            // ok の受信 (G38等の待機時、本物が届いてIDLE化している時のみ進捗を上げる)
            if (strstr(res, "ok") != NULL)
            {
                if (probeCtx.activeState == STATE_IDLE)
                {
                    isProbeWaitingOk = false;
                    probeLastOkTime = millis();
                    probeCurrentLine++;
                }
            }

            // エラーやアラームの検出
            if (strstr(res, "error") != NULL || strstr(res, "ALARM") != NULL)
            {
                isProbeExecuting = false;
                isProbeWaitingOk = false;
                probeCtx.activeState = STATE_IDLE;

                tft.fillRect(5, 15, 150, 100, 0x0000);
                tft.setCursor(5, 55);
                tft.setTextColor(0xF800);
                tft.println(F("STATUS: FAILED!"));

                delay(2000);
                currentMode = MODE_MENU;
                drawMenuUI();
                isFirstOpen = true;
                return;
            }
        }
    }

    // タイムアウト監視
    if (isProbeWaitingOk && (millis() - probeLastOkTime > PROBE_TIMEOUT_MS))
    {
        isProbeExecuting = false;
        isProbeWaitingOk = false;
        probeCtx.activeState = STATE_IDLE;

        tft.fillRect(5, 15, 150, 100, 0x0000);
        tft.setCursor(5, 55);
        tft.setTextColor(0xF800);
        tft.println(F("TIMEOUT ERROR!"));

        delay(2000);
        currentMode = MODE_MENU;
        drawMenuUI();
        isFirstOpen = true;
        return;
    }

    // 💡 コマンド生成・送出フェーズ
    if (!isProbeWaitingOk)
    {
        if (probeCurrentLine < probeCmdCount)
        {
            const char *cmdToSend = probeCmds[probeCurrentLine];
            float toolRadius = toolDiameter / 2.0f;
            String fStr = " F" + String(probeFeedRate, 0);
            String fSlowStr = " F10"; // 微速探査フィード速度

            // 内径（IN）および外径（OUT）用の動的アプローチ変数
            float workRadius = workDiameter / 2.0f;
            float maxSearchStroke = workRadius + 5.0f;

            if (strncmp(cmdToSend, "D_", 2) == 0)
            {
                // ─── Z軸単軸プローブ ───
                if (strcmp(cmdToSend, "D_Z1_RUN") == 0)
                {
                    probeCtx.activeState = STATE_Z1_RUN;
                    dynamicCmdBuffer = "G38.2 Z" + String(-30.0f, 3) + fStr;
                }
                else if (strcmp(cmdToSend, "D_Z2_MOVE") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 Z" + String(probeCtx.wallZ1 + 2.0f, 3);
                }
                else if (strcmp(cmdToSend, "D_Z2_RUN") == 0)
                {
                    probeCtx.activeState = STATE_Z2_RUN;
                    dynamicCmdBuffer = "G38.2 Z" + String(probeCtx.wallZ1 - 5.0f, 3) + fSlowStr;
                }
                else if (strcmp(cmdToSend, "D_Z_FINISH") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 L20 P1 Z0.0";
                }

                // ─── X_PLUS プローブ (完全ピンポン方式・5ステップ) ───
                else if (strcmp(cmdToSend, "D_X_PLUS_RUN1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE; // 1回目は結果の保存を行わないためIDLEのまま
                    dynamicCmdBuffer = "G38.2 X" + String(30.0f, 3) + fStr;
                }
                else if (strcmp(cmdToSend, "D_X_PLUS_RESET1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 X0";
                }
                else if (strcmp(cmdToSend, "D_X_PLUS_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 X-2.000";
                }
                else if (strcmp(cmdToSend, "D_X_PLUS_RUN2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G38.2 X" + String(5.0f, 3) + fSlowStr;
                }
                else if (strcmp(cmdToSend, "D_X_PLUS_RESET2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 X0";
                }

                // ─── X_MINUS プローブ (完全ピンポン方式・5ステップ) ───
                else if (strcmp(cmdToSend, "D_X_MINUS_RUN1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE; // 1回目は結果の保存を行わないためIDLEのまま
                    dynamicCmdBuffer = "G38.2 X" + String(-30.0f, 3) + fStr;
                }
                else if (strcmp(cmdToSend, "D_X_MINUS_RESET1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 X0";
                }
                else if (strcmp(cmdToSend, "D_X_MINUS_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 X2.000";
                }
                else if (strcmp(cmdToSend, "D_X_MINUS_RUN2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G38.2 X" + String(-5.0f, 3) + fSlowStr;
                }
                else if (strcmp(cmdToSend, "D_X_MINUS_RESET2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 X0";
                }

                // ─── Y_PLUS プローブ (完全ピンポン方式・5ステップ) ───
                else if (strcmp(cmdToSend, "D_Y_PLUS_RUN1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE; // 1回目は結果の保存を行わないためIDLEのまま
                    dynamicCmdBuffer = "G38.2 Y" + String(30.0f, 3) + fStr;
                }
                else if (strcmp(cmdToSend, "D_Y_PLUS_RESET1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 Y0";
                }
                else if (strcmp(cmdToSend, "D_Y_PLUS_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 Y-2.000";
                }
                else if (strcmp(cmdToSend, "D_Y_PLUS_RUN2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G38.2 Y" + String(5.0f, 3) + fSlowStr;
                }
                else if (strcmp(cmdToSend, "D_Y_PLUS_RESET2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 Y0";
                }

                // ─── Y_MINUS プローブ (完全ピンポン方式・5ステップ) ───
                else if (strcmp(cmdToSend, "D_Y_MINUS_RUN1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE; // 1回目は結果の保存を行わないためIDLEのまま
                    dynamicCmdBuffer = "G38.2 Y" + String(-30.0f, 3) + fStr;
                }
                else if (strcmp(cmdToSend, "D_Y_MINUS_RESET1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 Y0";
                }
                else if (strcmp(cmdToSend, "D_Y_MINUS_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 Y2.000";
                }
                else if (strcmp(cmdToSend, "D_Y_MINUS_RUN2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G38.2 Y" + String(-5.0f, 3) + fSlowStr;
                }
                else if (strcmp(cmdToSend, "D_Y_MINUS_RESET2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 P1 L20 Y0";
                }

                // ─── PROBE_XY_CORNER ───
                else if (strcmp(cmdToSend, "D_CR_X1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    probeCtx.startX = probeCtx.currentPrbX;
                    probeCtx.startY = probeCtx.currentPrbY;
                    dynamicCmdBuffer = "G38.2 X" + String(30.0f, 3) + fStr;
                }
                else if (strcmp(cmdToSend, "D_CR_X_RESET1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 L20 P1 X0.0";
                }
                else if (strcmp(cmdToSend, "D_CR_X_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 X-2.000";
                }
                else if (strcmp(cmdToSend, "D_CR_X2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G38.2 X3.000" + fSlowStr;
                }
                else if (strcmp(cmdToSend, "D_CR_X_RESET2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 L20 P1 X0.0";
                }
                else if (strcmp(cmdToSend, "D_CR_X_ESC") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 X-" + String(3.0f, 3);
                }
                else if (strcmp(cmdToSend, "D_CR_Y1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 Y" + String(-10.0f, 3);
                }
                else if (strcmp(cmdToSend, "D_CR_Y2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 X" + String(5.0f, 3);
                }
                else if (strcmp(cmdToSend, "D_CR_Y3") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G38.2 Y" + String(30.0f, 3) + fStr;
                }
                else if (strcmp(cmdToSend, "D_CR_Y_RESET1") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 L20 P1 Y0.0";
                }
                else if (strcmp(cmdToSend, "D_CR_Y_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G0 Y-2.000";
                }
                else if (strcmp(cmdToSend, "D_CR_Y4") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G38.2 Y3.000" + fSlowStr;
                }
                else if (strcmp(cmdToSend, "D_CR_Y_RESET2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 L20 P1 Y0.0";
                }
                else if (strcmp(cmdToSend, "D_CR_FINAL_OFFSET") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G10 L20 P1 X" + String(5.0f - toolRadius, 3) + " Y-" + String(toolRadius, 3);
                }

                // ─── PROBE_CENTER_IN ───
                else if (strcmp(cmdToSend, "D_IN_X_START") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G21 G90 G10 L20 P1 X0.0 Y0.0 Z0.0"; // リセットしてからスタート
                }
                else if (strcmp(cmdToSend, "D_IN_X1_PRB") == 0)
                {
                    probeCtx.activeState = STATE_X_ORG1;//ORGX1へ原点保存
                    dynamicCmdBuffer = "G90 G38.2 X-" + String(maxSearchStroke, 3) + fStr; // Ｘ左側一度目のプロービング ワーク半径＋５ｍｍ
                }
                else if (strcmp(cmdToSend, "D_IN_X1_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String(2.0f, 3); // 相対位置でプロービングバック２ｍｍ
                }
                else if (strcmp(cmdToSend, "D_IN_X1_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_X_ORG2;//ORGX1へ微速位置保存
                    dynamicCmdBuffer = "G90 G38.2 X" + String(- 5.0f, 3) + fSlowStr; // 微速プロービングで精密タッチ
                }
                else if (strcmp(cmdToSend, "D_IN_X2_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String( workDiameter - toolDiameter - 3.0f, 3); // ワーク径-ツール径-３ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_IN_X2_PRB") == 0)
                {
                    probeCtx.activeState = STATE_X2_PRB;
                    dynamicCmdBuffer = "G90 G38.2 X" + String(workDiameter + 5.0f, 3) + fStr; // Ｘ右側一度目のプロービング
                }
                else if (strcmp(cmdToSend, "D_IN_X2_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String(- 2.0f, 3); // プロービングバック-2mm
                }
                else if (strcmp(cmdToSend, "D_IN_X2_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_X2_SLOW;
                    dynamicCmdBuffer = "G90 G38.2 X" + String(workDiameter + 5.0f, 3) + fSlowStr; // プロービングで精密タッチ
                }
                else if (strcmp(cmdToSend, "D_IN_CX") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X-" + String((probeCtx.wallX2 - probeCtx.ORGX2) / 2.0f, 3); // 相対位置でX軸中心へ移動
                }
                else if (strcmp(cmdToSend, "D_IN_X_FINISH") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G90 G10 L20 P1 X0.0"; // X軸中心をゼロリセット
                }
                else if (strcmp(cmdToSend, "D_IN_Y1_PRB") == 0)
                {
                    probeCtx.activeState = STATE_Y_ORG1;//ORGY1へ原点保存
                    dynamicCmdBuffer = "G90 G38.2 Y-" + String(maxSearchStroke, 3) + fStr; // Y-方向に高速プローブ
                }
                else if (strcmp(cmdToSend, "D_IN_Y1_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String(2.0f, 3); // プロービングバック2ｍｍ
                }
                else if (strcmp(cmdToSend, "D_IN_Y1_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_Y_ORG2;//ORGY2へ微速位置保存
                    dynamicCmdBuffer = "G90 G38.2 Y" + String( - 5.0f, 3) + fSlowStr; // Y-方向に微速プローブ
                }
                else if (strcmp(cmdToSend, "D_IN_Y2_MOV") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String( workDiameter - toolDiameter - 3.0f, 3); // Y+ワーク径-ツール径-３ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_IN_Y2_PRB") == 0)
                {
                    probeCtx.activeState = STATE_Y2_PRB;
                    dynamicCmdBuffer = "G90 G38.2 Y" + String( workDiameter + 5.0f, 3) + fStr; // Y+方向に高速プローブ
                }
                else if (strcmp(cmdToSend, "D_IN_Y2_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String(-2.0f, 3); // プロービングバック2ｍｍ
                }
                else if (strcmp(cmdToSend, "D_IN_Y2_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_Y2_SLOW;
                    dynamicCmdBuffer = "G90 G38.2 Y" + String(workDiameter + 5.0f, 3) + fSlowStr; // Y+方向に微速プローブ
                }
                else if (strcmp(cmdToSend, "D_IN_CY") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y-" + String((probeCtx.wallY2 - probeCtx.ORGY2) / 2.0f, 3);
                }
                else if (strcmp(cmdToSend, "D_IN_FINISH") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G90 G10 L20 P1 Y0.0"; // Y軸中心をゼロリセットして完了
                }

                // ─── PROBE_CENTER_OUT ───
                else if (strcmp(cmdToSend, "D_OUT_START") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G21 G90 G10 L20 P1 X0.0 Y0.0 Z0.0"; // スタート位置をゼロリセット
                }
                else if (strcmp(cmdToSend, "D_OUT_X1_PRB") == 0)
                {
                    probeCtx.activeState = STATE_X_ORG1;//ORGX1へ原点保存
                    dynamicCmdBuffer = "G90 G38.2 X" + String(maxSearchStroke, 3) + fStr; // Ｘ+左側一度目のプロービング
                }
                else if (strcmp(cmdToSend, "D_OUT_X1_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String(-2.0f, 3); // X-方向に2mm移動
                }
                else if (strcmp(cmdToSend, "D_OUT_X1_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_X_ORG2;
                    dynamicCmdBuffer = "G90 G38.2 X" + String( 5.0f, 3) + fSlowStr; // Ｘ+左側微速プロービング
                }
                else if (strcmp(cmdToSend, "D_OUT_X1_ESC_X") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String(-2.0f, 3); // X-方向に2mm移動
                }
                else if (strcmp(cmdToSend, "D_OUT_X1_UP") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Z" + String(10.0f, 3); // Z軸を上に10ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_X2_JUMP") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String( workDiameter + toolDiameter + 5.0f, 3); // X軸をワーク直径分移動
                }
                else if (strcmp(cmdToSend, "D_OUT_X2_DN") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Z" + String(-10.0f, 3);
                }
                else if (strcmp(cmdToSend, "D_OUT_X2_PRB") == 0)
                {
                    probeCtx.activeState = STATE_X2_PRB;
                    dynamicCmdBuffer = "G90 G38.2 X" + String( workDiameter - 5.0f, 3) + fStr; // Ｘ-右側一度目のプロービング
                }
                else if (strcmp(cmdToSend, "D_OUT_X2_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String( 2.0f, 3); // プロービングバック2ｍｍ
                }
                else if (strcmp(cmdToSend, "D_OUT_X2_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_X2_SLOW;
                    dynamicCmdBuffer = "G90 G38.2 X" + String(workDiameter - 5.0f, 3) + fSlowStr; // Ｘ-右側微速プロービング
                }
                else if (strcmp(cmdToSend, "D_OUT_X2_ESC") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X" + String( 2.0f, 3); // X+右側に2mm移動
                }
                else if (strcmp(cmdToSend, "D_OUT_X2_UP") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Z" + String(10.0f, 3); // Z軸を上に10ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_CX") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 X-" + String(((probeCtx.wallX2 - probeCtx.ORGX2) + 4.0f) / 2.0f, 3); // X軸中心へ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_X_FINISH") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G90 G10 L20 P1 X0.0"; // X軸中心をゼロリセット
                }
                else if (strcmp(cmdToSend, "D_OUT_Y1_JUMP") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y-" + String(maxSearchStroke, 3); // Y-方向にワーク半径＋５ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_DN") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Z" + String(-10.0f, 3); // Z軸を下に10ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y1_PRB") == 0)
                {
                    probeCtx.activeState = STATE_Y_ORG1;//ORGY1へ原点保存
                    dynamicCmdBuffer = "G90 G38.2 Y" + String(workRadius + 5.0f, 3) + fStr; // Y+方向に１回目プローブ
                }
                else if (strcmp(cmdToSend, "D_OUT_Y1_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String(-2.0f, 3); // Y-方向に2mm移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y1_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_Y_ORG2;//ORGY2へ微速位置保存
                    dynamicCmdBuffer = "G90 G38.2 Y" + String( 5.0f, 3) + fSlowStr; // Y+方向に微速プローブ
                }
                else if (strcmp(cmdToSend, "D_OUT_Y1_ESC_Y") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String(-2.0f, 3); // Y-方向に2mm移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y1_UP") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Z" + String(10.0f, 3); // Z軸を上に10ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_JUMP") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String(workDiameter + toolDiameter + 5.0f, 3); // ワーク直径＋ツール径＋5mmY+方向に移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_DN2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Z" + String(-10.0f, 3); // Z軸を下に10ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_PRB") == 0)
                {
                    probeCtx.activeState = STATE_Y2_PRB;
                    dynamicCmdBuffer = "G90 G38.2 Y-" + String( workDiameter - 5.0f, 3) + fStr; // Y-方向に１回目のプロービング
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_BAK") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String( 2.0f, 3); // Y軸をwallY1 + 2.0fの位置に移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_SLOW") == 0)
                {
                    probeCtx.activeState = STATE_Y2_SLOW;
                    dynamicCmdBuffer = "G90 G38.2 Y-" + String(workDiameter - 5.0f, 3) + fSlowStr; // Y-方向に微速プロービング
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_ESC_Y2") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y" + String( 2.0f, 3); // Y+方向にwallY2 + 2.0fの位置に移動
                }
                else if (strcmp(cmdToSend, "D_OUT_Y2_UP") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Z" + String(10.0f, 3); // Z軸を上に10ｍｍ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_CY") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G91 G0 Y-" + String(((probeCtx.wallY2 - probeCtx.ORGY2) + 4.0f) / 2.0f, 3); // Y軸中心へ移動
                }
                else if (strcmp(cmdToSend, "D_OUT_FINISH") == 0)
                {
                    probeCtx.activeState = STATE_IDLE;
                    dynamicCmdBuffer = "G90 G10 L20 P1 Y0.0"; // Y軸中心をゼロリセットして完了
                }

                cmdToSend = dynamicCmdBuffer.c_str();
            }

            // TFT表示の更新
            tft.fillRect(5, 30, 150, 30, 0x0000);
            tft.setCursor(5, 30);
            tft.setTextColor(0x07E0);
            tft.print(F("Send ("));
            tft.print(probeCurrentLine + 1);
            tft.print(F("/"));
            tft.print(probeCmdCount);
            tft.println(F("):"));

            tft.setCursor(5, 40);
            tft.setTextColor(0xFFFF);
            const char* p = cmdToSend;
            int len = strlen(cmdToSend);
            
            if (len > 22) {
                // 前半22文字を表示
                for(int i = 0; i < 22; i++) tft.print(*p++);
                
                // 💡改行して残りを表示（10ピクセル下げて、X座標は1行目と同じ15に揃える）
                tft.setCursor(5, 50); 
                tft.println(p); 
            } else {
                tft.println(cmdToSend);
            }

            // シリアル送信
            Serial2.println(cmdToSend);

            isProbeWaitingOk = true;
            probeLastOkTime = millis();
        }
        else
        {
            // 💡 全ステップ完了処理
            if (currentProbeMode == PROBE_CENTER_IN || currentProbeMode == PROBE_CENTER_OUT)
            {
                Serial2.println("G10 L20 P1 X0.0 Y0.0");
                delay(50);
            }

            isProbeExecuting = false;
            isProbeWaitingOk = false;
            probeCtx.activeState = STATE_IDLE;

            tft.fillRect(3, 15, 154, 100, 0x0000);
            tft.setCursor(5, 55);
            tft.setTextColor(0x07E0);
            tft.println(F("STATUS: SUCCESS!"));

            delay(1500);
            currentMode = MODE_MENU;
            drawMenuUI();
            isFirstOpen = true;
        }
    }
}