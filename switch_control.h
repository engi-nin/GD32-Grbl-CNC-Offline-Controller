// switch_control.h
#pragma once
#include <Arduino.h>

enum Button { OK, Y_UP, Y_DOWN, X_UP, X_DOWN, EXIT, Z_UP, Z_DOWN, BTN_COUNT };

class SwitchControl {
public:
    void swbegin();
    bool isPressed(Button btn);
private:
    const uint8_t pins[BTN_COUNT] = {PB10, PC4, PC6, PB12, PB11, PC5, PB6, PB5};
};