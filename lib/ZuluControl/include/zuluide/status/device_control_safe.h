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

#include <zuluide/images/image.h>

namespace zuluide::status {

  /***
      Provides multi-core safe interface for changing state of the device. Any changes from a UI running on a concurrent core should go through
      this interface instead of directly through the status controller. The status control is the final point through which all device status updates
      should go into. From there they go back out to observers.
   **/
  class DeviceControlSafe {
  public:
    virtual void LoadImageSafe(zuluide::images::Image i) = 0;
    virtual void EjectImageSafe() = 0;
  };    
}
