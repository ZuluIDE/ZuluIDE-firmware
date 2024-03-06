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

#pragma once

#include <Wire.h>
#include <zuluide/control/input_interface.h>
#include "ZuluIDE_platform_gpio.h"

#ifndef PCA9554_ADDR
#define PCA9554_ADDR 0x3F
#endif

namespace zuluide::control {
  class RotaryControl : public InputInterface {
  public:
    RotaryControl(int addr = PCA9554_ADDR);
    void SetI2c(TwoWire* i2c);
    virtual void SetReciever(InputReceiver* reciever);
    virtual void StartSendingEvents();
    virtual void StopSendingEvents();
    void Poll();

  private:
    uint8_t getValue();
    InputReceiver* inputReceiver;
    int pcaAddr;
    bool isSending;
    TwoWire *wire;
    bool buttonIsPressed(bool isDown, uint32_t* lastDownMillis, uint32_t checkTime);

    bool clockHigh;
    int tickCount;
    bool goingRight;

    uint32_t eject_btn_millis;
    uint32_t insert_btn_millis;
    uint32_t rotate_btn_millis;
    uint8_t lrmem;
    int32_t lrsum;
  };
}
