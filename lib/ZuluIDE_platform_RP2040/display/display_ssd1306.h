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
#include "status_widget.h"
#include "info_widget.h"
#include "eject_widget.h"
#include "menu_widget.h"
#include "select_widget.h"

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
    absolute_time_t nextRefresh;
    Size wBounds;
    std::unique_ptr<zuluide::control::DisplayState> currentDispState;
    std::unique_ptr<SystemStatus> currentSysStatus;
    std::unique_ptr<zuluide::Widget> currentWidget;
    void updateDisplay();
  };
}
