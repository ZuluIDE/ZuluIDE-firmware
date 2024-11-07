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

#include <Adafruit_SSD1306.h>
#include <zuluide/control/display_state.h>
#include <zuluide/status/system_status.h>
#include <memory>
#include "dimensions.h"

namespace zuluide {
  class Widget {
  public:    
    virtual bool Refresh ();
    virtual void Display () = 0;
    virtual void Update (const zuluide::status::SystemStatus &status);
    virtual void Update (const zuluide::control::DisplayState &disp);
    void DrawCenteredText (const char* text);
    void DrawCenteredTextAt (const char* text, int y);
    Size MeasureText (const char* text);
  protected:
    Widget (Adafruit_SSD1306 *g, Rectangle b);
    Adafruit_SSD1306 *graph;
    Rectangle bounds;
    std::unique_ptr<zuluide::status::SystemStatus> currentSysStatus;
    std::unique_ptr<zuluide::control::DisplayState> currentDispState;
  };
}
