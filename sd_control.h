// sd_control.h
#ifndef SD_CONTROL_H
#define SD_CONTROL_H

#include <SPI.h>
#include <SdFat.h>
#define CS_SLOT_A PC7
#define CS_SLOT_B PD12
#define MAX_FILES 40

enum SDSlot { SLOT_A = 0, SLOT_B = 1};

class SDControl {
public:
    SDControl();

    static void releaseAllSlots();
    FsFile& file() { return _currentFile; }
    bool scanFiles(SDSlot slot);
    bool openFile(const char* filename);
    void closeCurrent();
    
    // 選択操作（データ更新のみ行う）
    void selectNext();
    void selectPrev();

    // main.cppからデータを取得するための関数
    int getFileCount() { return _fileCount; }
    int getSelectedIndex() { return _selectedIndex; }
    char* getFileName(int index) { return _fileList[index]; }
    char* getSelectedFileName();

    uint32_t getFileSize();

    void activateBus(); // 通信を物理的に再開する関数（高速）

    bool switchSlot();
    SDSlot getCurrentSlot() { return _currentSlot; }
    const char* getCurrentSlotName();

    bool toggleSlotAndScan();
    
private:
    bool _initBus(SDSlot slot, bool forceReinit);
    
    SdFs _sdA; // スロットA専用の脳
    SdFs _sdB; // スロットB専用の脳
    // 操作するときは、今どっちを使っているかで切り替える
    SdFs* _currentSD;
    FsFile _currentFile;
    char _fileList[MAX_FILES][32]; // ファイル名リスト (40個×32バイト = 1,280バイト)
    int _fileCount;
    int _selectedIndex;
    SDSlot _currentSlot;
    bool _isInitializedA;
    bool _isInitializedB;
};

extern SDControl sdCtrl;

#endif