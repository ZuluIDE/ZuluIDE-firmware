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

#include "widget.h"
#include "scrolling_text.h"

namespace zuluide {
  class StatusWidget : public Widget {
  public:
    StatusWidget(Adafruit_SSD1306 *graph, Rectangle bounds, Size charBounds);
    virtual bool Refresh ();
    virtual void Display ();
    virtual void Update (const zuluide::status::SystemStatus &status);
    virtual void Update (const zuluide::control::DisplayState &disp);
  private:
    ScrollingText imagename;
    ScrollingText deferred_load;
    Size charBounds;
    uint8_t cdrom_loaded[27] = {
	0x01, 0xc0, 0x00, 0x1f, 0xfc, 0x00, 0x7f, 0xff, 0x00, 0x7f, 0x7f, 0x00, 0xfe, 0x3f, 0x80, 0x7f, 
	0x7f, 0x00, 0x7f, 0xff, 0x00, 0x1f, 0xfc, 0x00, 0x01, 0xc0, 0x00
    };
    
    uint8_t cdrom_empty[27] = {
      0x01, 0xc0, 0x00, 0x18, 0x0c, 0x00, 0x60, 0x03, 0x00, 0x40, 0x81, 0x00, 0x81, 0x40, 0x80, 0x40, 
      0x81, 0x00, 0x60, 0x03, 0x00, 0x18, 0x0c, 0x00, 0x01, 0xc0, 0x00
    };

    uint8_t zipdrive_empty[27] = {
      0x0f, 0xfc, 0x00, 0x72, 0x13, 0x80, 0x92, 0x12, 0x40, 0x91, 0xe2, 0x40, 0x90, 0x02, 0x40, 0x90, 
      0x02, 0x40, 0x90, 0x02, 0x40, 0x90, 0x02, 0x40, 0xff, 0xff, 0xc0
    };
      
    uint8_t zipdrive_loaded[27] = {
      0x0f, 0xfc, 0x00, 0x72, 0x13, 0x80, 0x92, 0x12, 0x40, 0x91, 0xe2, 0x40, 0x90, 0x02, 0x40, 0x91, 
      0xe2, 0x40, 0x91, 0xe2, 0x40, 0x90, 0x02, 0x40, 0xff, 0xff, 0xc0
    };
  };
}
