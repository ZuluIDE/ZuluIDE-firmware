/**
 * ZuluIDE™ - Copyright (c) 2024 Rabbit Hole Computing™
 *
 * ZuluIDE™ firmware is licensed under the GPL version 3 or any later version. 
 *
 * https://www.gnu.org/licenses/gpl-3.0.html
 * ----
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include "rotary_control.h"

using namespace zuluide::control;

static volatile bool g_control_input_flag = false;

#ifndef DEBOUNCE_IN_MS
#define DEBOUNCE_IN_MS 20
#endif

void RotaryControl::SetReciever(InputReceiver* receiver) {
  inputReceiver = receiver;
}

void RotaryControl::StartSendingEvents() {  
  eject_btn_millis = insert_btn_millis = 0;
  isSending = true;
  lrmem = 3;
  lrsum = 0;
}

void RotaryControl::StopSendingEvents() {
  isSending = false;
}

bool RotaryControl::CheckForDevice()
{
  wire->begin();
  wire->setTimeout(1, false);
  wire->beginTransmission(pcaAddr);
  deviceExists = 0 == wire->endTransmission();
  return deviceExists;
}

RotaryControl::RotaryControl(int addr) :
  pcaAddr(addr), isSending(false), clockHigh(false), tickCount(0), deviceExists(false) {
}

uint8_t RotaryControl::getValue() {
  uint8_t input_byte = 0xFF;

  wire->beginTransmission(pcaAddr);
  wire->write(0);
  wire->endTransmission();

  wire->requestFrom(pcaAddr, 1);
  while(wire->available()) {
    input_byte = wire->read();
  }
  
  return input_byte;
}

void RotaryControl::SetI2c(TwoWire* i2c) {
  wire = i2c;
}

void RotaryControl::Poll() {  
  if(!deviceExists || !isSending) {
    return;
  }
  
  uint8_t input_byte = getValue();
  uint32_t check_time = millis();
  bool ejectButtonIsDown = ((1 << EXP_EJECT_PIN) & input_byte) != 0;
  bool insertButtonIsPressed = ((1 << EXP_INSERT_PIN) & input_byte) != 0;  
  bool rotateButton = ((1 << EXP_ROT_PIN) & input_byte) != 0;  

  if (buttonIsPressed(ejectButtonIsDown, &eject_btn_millis, check_time)) {
    // Eject button was pressed succsfully.
    inputReceiver->PrimaryButtonPressed();
  }

  if (buttonIsPressed(insertButtonIsPressed, &insert_btn_millis, check_time)) {
    // Insert button was pressed succsfully.
    inputReceiver->SecondaryButtonPressed();
  }

  if (buttonIsPressed(rotateButton, &rotate_btn_millis, check_time)) {
    inputReceiver->RotaryButtonPressed();
  }

  // Current state of the encoder pins.
  int8_t l = 1 & (input_byte >> EXP_ROT_A_PIN);
  if(!clockHigh && l > 0) {
    // Clock went high.
    clockHigh = true;
  } else if (clockHigh && l == 0) {
    // Clock dropped.
    int8_t r = 1 & (input_byte >> EXP_ROT_B_PIN);
    clockHigh = false;
    if (r == 0) {
      if (goingRight) {
        if (tickCount < 3) {          
          tickCount++;
        } else {
          inputReceiver->RotaryUpdate(-1);
          tickCount = 0;
        }
      } else {
        tickCount = 1;
        goingRight = true;        
      }
    } else {
      if (!goingRight) {
        if (tickCount < 3) {          
          tickCount++;
        } else {
          inputReceiver->RotaryUpdate(1);
          tickCount = 0;
        }
      } else {
        tickCount = 1;
        goingRight = false;        
      }
    }   
  }
}

bool RotaryControl::buttonIsPressed(bool isDown, uint32_t* lastDownMillis, uint32_t checkTime) {
  if (isDown) {
    if (*lastDownMillis == 0) {
      // First instance of the button going down, record this time.
      *lastDownMillis = checkTime;
    }
    
    return false;
  } else {
    bool isPressed = *lastDownMillis != 0 && *lastDownMillis - checkTime > DEBOUNCE_IN_MS;
    // Reset our timestamp for this button.
    *lastDownMillis = 0;

    return isPressed;    
  }
}
