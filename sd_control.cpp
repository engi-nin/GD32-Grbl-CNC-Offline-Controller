#include <SdFat.h>
#include "sd_control.h"

extern SoftSpiDriver<PB14, PB15, PB13> softSpi; 

SDControl::SDControl() : _fileCount(0), _selectedIndex(0), _currentSlot(SLOT_A), _isInitializedA(false), _isInitializedB(false) {
    _currentSD = &_sdA; 
}

void SDControl::releaseAllSlots() {
    pinMode(CS_SLOT_A, OUTPUT);
    pinMode(CS_SLOT_B, OUTPUT);
    digitalWrite(CS_SLOT_A, HIGH);
    digitalWrite(CS_SLOT_B, HIGH);
}
    
void SDControl::activateBus() {
    releaseAllSlots();
    delayMicroseconds(10); 
}

bool SDControl::_initBus(SDSlot slot, bool forceReinit) {
    _currentSD = (slot == SLOT_A) ? &_sdA : &_sdB;

    // ファイルが開いているなら閉じる
    if (_currentFile.isOpen()) {
        _currentFile.close();
    }

    releaseAllSlots();
    delay(20); 

    uint8_t targetPin = (slot == SLOT_A) ? CS_SLOT_A : CS_SLOT_B;
 
    // 安定性を考慮し、2MHzで初期化
    SdSpiConfig config(targetPin, SHARED_SPI, SD_SCK_MHZ(2), &softSpi);
    
    // 毎回 .end() を呼んでバスの競合を防ぐ
    _currentSD->end();
    
    // フラグでスキップせず、毎回確実に begin() を通す
    // (SdFatの begin() は安全に再初期化を行ってくれます)
    if (!_currentSD->begin(config)) {
        if (slot == SLOT_A) _isInitializedA = false;
        else _isInitializedB = false;
        return false; 
    }

    if (slot == SLOT_A) _isInitializedA = true;
    else _isInitializedB = true;

    _currentSlot = slot;
    return true;
}

bool SDControl::scanFiles(SDSlot slot) {
    _fileCount = 0;
    _selectedIndex = 0;
    for (int i = 0; i < MAX_FILES; i++) _fileList[i][0] = '\0';

    // スロットを切り替えて初期化
    if (!_initBus(slot, false)) {
        return false; 
    }

    FsFile root;
    // 修正: _currentSD->open() を使用してルートディレクトリを開く
    if (!root.open(_currentSD->vol(), "/", O_RDONLY)) { 
        // ボリューム指定でのオープンが失敗する場合のフォールバック
        if (!root.open(_currentSD, "/", O_RDONLY)) {
            return false;
        }
    }

    FsFile entry;
    while (entry.openNext(&root, O_RDONLY)) {
        if (!entry.isDir() && !entry.isHidden()) {
            entry.getName(_fileList[_fileCount], sizeof(_fileList[0]));
            _fileCount++;
        }
        entry.close();
        if (_fileCount >= MAX_FILES) break;
    }
    root.close();

    if (_fileCount == 0) {
        cid_t cid;
        return _currentSD->card()->readCID(&cid);
    }
    return true;
}

void SDControl::selectNext() {
    if (_fileCount > 0) _selectedIndex = (_selectedIndex + 1) % _fileCount;
}

void SDControl::selectPrev() {
    if (_fileCount > 0) _selectedIndex = (_selectedIndex - 1 + _fileCount) % _fileCount;
}

char* SDControl::getSelectedFileName() {
    if (_fileCount == 0) return nullptr;
    return _fileList[_selectedIndex];
}

uint32_t SDControl::getFileSize() {
    if (_currentFile.isOpen()) {
        return _currentFile.size();
    }
    return 0;
}

bool SDControl::openFile(const char* filename) {
    if (_currentFile.isOpen()) {
        _currentFile.close();
    }
    
    // スロットのバスと初期化状態を確認
    if (!_initBus(_currentSlot, false)) return false;

    // 修正: _currentSD のオブジェクトから直接 open する
    _currentFile = _currentSD->open(filename, O_RDONLY);
    
    return _currentFile.isOpen();
}

void SDControl::closeCurrent() {
    if (_currentFile.isOpen()) {
        _currentFile.close();
    }
}

const char* SDControl::getCurrentSlotName() {
    return (_currentSlot == SLOT_A) ? "Slot A" : "Slot B"; 
}

bool SDControl::switchSlot() {
    SDSlot nextSlot = (_currentSlot == SLOT_A) ? SLOT_B : SLOT_A;
    return scanFiles(nextSlot);
}

bool SDControl::toggleSlotAndScan() {
    return switchSlot();
}

// ==========================================
// 💡 安全なファイル操作ラッパー
// ==========================================

int SDControl::readChar() {
    if (_currentFile && _currentFile.isOpen()) {
        return _currentFile.read();
    }
    return -1;
}

int SDControl::availableBytes() {
    if (_currentFile && _currentFile.isOpen()) {
        return _currentFile.available();
    }
    return 0;
}

SDControl sdCtrl;
