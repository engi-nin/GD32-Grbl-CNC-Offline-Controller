# GD32-Grbl-CNC-Offline-Controller

<img width="742" height="425" alt="offline_F" src="https://github.com/user-attachments/assets/3b464e44-e479-4c2b-a59c-647a911e2f9a" />


## 概要
GD32F303VCT6を搭載した、ＧＲＢＬ‐ＣＮＣ向けのオフライン制御端末用ファームウェアです。

＊＊注意＊＊
GD32F303VCT6以外のマイコンやＴＦＴの種類によって正常に動かない可能性があります

GRBL1.1用のコントローラーとシリアル通信を行い、PCレスでの操作を実現します。
主な操作
ＸＹＺ軸のＪＯＧ操作、0.01,0.1,1,10mmの４段階、速度は５０ステップで５０～５００mm／min、スピンドルＯＮ、ＯＦＦ
＄コマンド出力、$H,$X$$,$C,$SLP,$G,$10,Ctrl+X
ＳＤカードからのファイル読み出し、Ｇコード出力、一時停止機能付き
ＸＹＺのゼロセット、ゼロ位置移動
プローブ動作、Ｚ、Ｘ＋、Ｘ－、Ｙ＋、Ｙ－、左下コーナーゼロセット、穴中心ゼロセット、軸中心ゼロセット


## ハードウェア仕様
- **MCU:** GD32F303VCT6
- **Display:** 1.8 inch TFT (ST7735)
- **Interface:** SPI / UART / SD Card

<img width="1851" height="1003" alt="offline_B" src="https://github.com/user-attachments/assets/69d65736-e0b9-4434-892f-5018ef693eb8" />

### ピンアサイン（主要部分）
| 機能 | ピン番号 | 備考 |
タクトスイッチ
PB10(47)[OK]
PC4(33)[Y+]
PC6(63)[Y-]
PB12(51)[X+]
PB11(48)[X-]
PC5(35)[EXIT]
PB6(92)[Z+]
PB5(91)[Z-]

ＵＳＢ
PA11(70)USBDM
PA12(71)USBDP

SD SLOT
MOSI PB15(54)　　
MISO PB14(53) 　　
SD_SCK  PB13(52)  
CS_SLOT_A PC7(64)　　microSD
CS_SLOT_B PD12(59) 　ＳＤ

シリアル端子
PA2(25)USART_1TX
PA3(26)USART_1RX

(液晶表示はビットバンニングで動作確認できている)
1.8inchTFT

#define TFT_CS   PD7(88)　　チップセレクト
#define TFT_DC   PD11(58)　　データＨ／コマンドＬ切替
#define TFT_RST  PE1(90)　　リセット
#define TFT_WR   PD5(86)　　書き込み信号
#define TFT_RD   PD4(85)　　読み出し信号
#define TFT_BL   PD13(60) // バックライト
// データバス (D0-D7) の変則配置をここで定義
#define D0_PIN   PD14(61)　データビット０
#define D1_PIN   PD15(62)　データビット１
#define D2_PIN   PD0(81)　　データビット２
#define D3_PIN   PD1(82)　　データビット３
#define D4_PIN   PE7(38)　　データビット４
#define D5_PIN   PE8(39)　　データビット５
#define D6_PIN   PE9(40)　　データビット６
#define D7_PIN   PE10(41)　　データビット７

## 開発環境
- PlatformIO
- GD32 Arduino Core (または SPL)

## 使い方
1. PlatformIOでこのフォルダを開きます。
2. `platformio.ini` の設定を確認してください。
3. ビルドしてターゲットボードに書き込みます。

https://youtu.be/XpaJLY-EAXQ?si=iFZQxmoBzAtlG87i

## ライセンス
MIT License 
