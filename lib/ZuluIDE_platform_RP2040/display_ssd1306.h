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
#include <Adafruit_SSD1306.h>
#include "ZuluIDE_platform_gpio.h"
#include <zuluide/status/system_status.h>
#include <zuluide/control/display_state.h>
#include <memory>
#include <pico/time.h>

using namespace zuluide::status;

namespace zuluide {
  class DisplaySSD1306 {
  public:
    DisplaySSD1306();
    void init(TwoWire* wire);
    virtual void HandleUpdate(const SystemStatus& current);
    void HandleUpdate(const zuluide::control::DisplayState& current);
    /***
	Called in a polling fashion in order to allow the display to animate
	itself (e.g., scrolling text.)
     */
    void Refresh();
  private:
    TwoWire* m_wire;
    Adafruit_SSD1306 graph;
    uint8_t m_i2c_addr;
    uint16_t lineHeight;
    uint16_t lineLength;
    uint16_t lineCount;
    uint16_t centerBase;
    absolute_time_t nextRefresh;
    uint16_t imageNameWidthPixels;
    uint16_t imageNameOffsetPixels;
    /***
	Indicates whether to scroll the text automatically or to use the manual rotary encoder scrolling.
     */
    bool scrollText;
    std::unique_ptr<zuluide::control::DisplayState> currentDispState;
    std::unique_ptr<SystemStatus> currentSysStatus;
    void updateDisplay();
    void displayStatus(bool isRefresh);
    void displayMenu();
    void displayEject();
    void displaySelect(bool isRefresh);
    void displayInfo(bool isRefresh);    
    uint8_t cdrom_loaded[16*4] =
      {
0x0,0xf,0xe0,0x0,
0x1,0xff,0xff,0x0,
0x7,0xff,0xff,0xc0,
0x1f,0xff,0xff,0xf0,
0x3f,0xff,0xff,0xf8,
0x3f,0xf8,0x3f,0xf8,
0x7f,0xe0,0xf,0xfc,
0x7f,0xe0,0xf,0xfc,
0x7f,0xe0,0xf,0xfc,
0x3f,0xf8,0x3f,0xf8,
0x3f,0xff,0xff,0xf8,
0x1f,0xff,0xff,0xf0,
0x7,0xff,0xff,0xc0,
0x1,0xff,0xff,0x0,
0x0,0xf,0xe0,0x0,
0x0,0x0,0x0,0x0
};

    uint8_t cdrom_empty[16*4] =
{
0x0,0x0,0x0,0x0,
0x0,0xf,0xf0,0x0,
0x0,0xc0,0x3,0x0,
0x2,0x0,0x0,0x40,
0x8,0x0,0x0,0x10,
0x10,0x0,0x0,0x8,
0x20,0x1,0x80,0x4,
0x20,0x3,0xc0,0x4,
0x20,0x1,0x80,0x4,
0x20,0x0,0x0,0x4,
0x10,0x0,0x0,0x8,
0x8,0x0,0x0,0x10,
0x2,0x0,0x0,0x40,
0x0,0xc0,0x3,0x0,
0x0,0xf,0xf0,0x0,
0x0,0x0,0x0,0x0
};

  };
}

#define SELECT_IMAGE_MENU_TEXT "-- Select Image --"

#define INFO_MENU_TEXT "-- About --"
#define ZULUIDE_TITLE "ZuluIDE"
