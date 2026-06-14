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
    // 強制再初期化、またはスロットが変わる場合のみend()と再begin()を行う
    if (forceReinit || (slot == SLOT_A && !_isInitializedA) || (slot == SLOT_B && !_isInitializedB)) {
        
        // ファイルが開いているなら閉じる
        if (_currentFile) _currentFile.close();

        if (slot == SLOT_A) {
            _sdA.end();
            _isInitializedA = false;
        } else {
            _sdB.end();
            _isInitializedB = false;
        }

        pinMode(CS_SLOT_A, OUTPUT);
        pinMode(CS_SLOT_B, OUTPUT);
        digitalWrite(CS_SLOT_A, HIGH);
        digitalWrite(CS_SLOT_B, HIGH);
        
        delay(50); 

        _currentSD = (slot == SLOT_A) ? &_sdA : &_sdB;
        uint8_t targetPin = (slot == SLOT_A) ? CS_SLOT_A : CS_SLOT_B;
     
        // 安定性を考慮し、最初は2MHz程度で試すのをおすすめします
        SdSpiConfig config(targetPin, SHARED_SPI, SD_SCK_MHZ(2), &softSpi);
        
        if (!_currentSD->begin(config)) {
            return false; 
        }

        if (slot == SLOT_A) _isInitializedA = true;
        else _isInitializedB = true;
    }

    // スロット切り替えにともなうポインタの更新
    _currentSD = (slot == SLOT_A) ? &_sdA : &_sdB;
    _currentSlot = slot;
    return true;
}

bool SDControl::scanFiles(SDSlot slot) {
    _fileCount = 0;
    _selectedIndex = 0;
    for (int i = 0; i < 20; i++) _fileList[i][0] = '\0';

    // scanFilesのときは「カードが差し替えられた可能性」を考慮し、強制的に初期化をやり直す
    if (!_initBus(slot, true)) {
        return false; 
    }

    FsFile root;
    if (!root.open(_currentSD, "/", O_RDONLY)) { 
        return false;
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
        // 安全な方法でカード生存チェック（CID構造体のポインタを渡すか、単にtrueを返す）
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
    if (_currentFile) _currentFile.close();
    
    // スロットが変わっていなければ、重いSPI初期化をスキップして即ファイルを開く
    if (!_initBus(_currentSlot, false)) return false;

    if (!_currentFile.open(_currentSD, filename, O_RDONLY)) {
        return false;
    }
    return _currentFile.isOpen();
}

void SDControl::closeCurrent() {
    if (_currentFile) {
        _currentFile.close();
    }
}

const char* SDControl::getCurrentSlotName() {
    return (_currentSlot == SLOT_A) ? "Slot A" : "Slot B"; // 明示的な名前に変更
}

bool SDControl::switchSlot() {
    SDSlot nextSlot = (_currentSlot == SLOT_A) ? SLOT_B : SLOT_A;
    return scanFiles(nextSlot);
}

bool SDControl::toggleSlotAndScan() {
    return switchSlot();
}
SDControl sdCtrl;