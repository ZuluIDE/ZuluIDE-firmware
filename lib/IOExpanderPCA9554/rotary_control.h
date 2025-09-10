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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
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

  enum rotary_direction_t {
    ROTARY_DIR_NONE = 0,
    ROTARY_DIR_CW   = 0x10,
    ROTARY_DIR_CCW  = 0x20
  };

  // first two bits hold state of input
  // 3rd bit holds rotation direction
  enum rotatry_state_t {
    ROTARY_TICK_000 = 0,
    ROTARY_LAST_CW_001,
    ROTARY_START_CW_010,
    ROTARY_CONT_CW_011,
    ROTARY_START_CCW_100,
    ROTARY_LAST_CCW_101,
    ROTARY_CONT_CCW_110,
  };

  class RotaryControl : public InputInterface {
  public:

    RotaryControl(int addr = PCA9554_ADDR);

    /*
      \param ticks how many ticks before registering a change
     */
    void SetTicks(uint8_t ticks);
    void SetI2c(TwoWire* i2c);
    virtual void SetReceiver(InputReceiver* receiver);
    virtual void StartSendingEvents();
    virtual void StopSendingEvents();
    virtual bool CheckForDevice() override;
    virtual bool GetDeviceExists() override;
    void Poll();

  private:
    uint8_t getValue();
    InputReceiver* inputReceiver;
    int pcaAddr;
    bool deviceExists;
    bool isSending;
    TwoWire *wire;
    bool buttonIsPressed(bool isDown, uint32_t* lastDownMillis, uint32_t checkTime);

    int tick_count;
    bool going_cw;

    uint8_t number_of_ticks;
    uint32_t eject_btn_millis;
    uint32_t insert_btn_millis;
    uint32_t rotate_btn_millis;
    uint8_t lrmem;
    int32_t lrsum;
    uint8_t rotary_state;

    static const uint8_t rotary_transition_lut[7][4];
  };
}
