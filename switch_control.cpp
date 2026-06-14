// switch_control.cpp
#include "switch_control.h"

void SwitchControl::swbegin() {
    for(int i=0; i<BTN_COUNT; i++) pinMode(pins[i], INPUT_PULLUP);
}

bool SwitchControl::isPressed(Button btn) {
    return digitalRead(pins[btn]) == LOW; // PULLUPなのでLOWで検知
}