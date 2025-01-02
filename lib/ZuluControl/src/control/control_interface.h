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

#include <zuluide/control/input_interface.h>
#include "std_display_controller.h"
#include "../status/status_controller.h"

namespace zuluide::control {
  /***
      Takes input (via the InputReceiver interface) and traslates this into updates to the display via the display controller. 
   */
  class ControlInterface : public InputReceiver {
  public:
    void SetDisplayController(StdDisplayController* dispController);
    virtual void RotaryUpdate(int offset);
    virtual void RotaryButtonPressed();
    virtual void PrimaryButtonPressed();
    virtual void SecondaryButtonPressed();
    void HandleSystemStatusUpdate(const zuluide::status::SystemStatus& current);
  private:
    StdDisplayController* displayController;    
    void handleDisplayStateUpdate(const DisplayState& current);
    zuluide::status::SystemStatus currentStatus;
    zuluide::status::StatusController* statusController;
    Mode currentDisplayMode;    
  };
}
