/**
 * ZuluIDE™ - Copyright (c) 2023 Rabbit Hole Computing™
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

#ifndef IDE_ACTIONS_H
#define IDE_ACTIONS_H

namespace zuluide {

  /***
   * Models an IDE device's state changes of interest to an external component. 
   */
  enum class DeviceActions {
    INQUERY,
    EJECT,
    IMAGE_LOAD_COMPLETE,
    LOAD_REQUEST,
    TEST_READY,
    SD_CARD_MOUNTED
  };
  
  inline const char* ToString(const DeviceActions& value) {
    switch (value) {
    case DeviceActions::INQUERY:
      return "INQUERY";
    case DeviceActions::EJECT:
      return "EJECT";
    case DeviceActions::IMAGE_LOAD_COMPLETE:
      return "IMAGE_LOAD_COMPLETE";
    case DeviceActions::LOAD_REQUEST:
      return "LOAD_REQUEST";
    case DeviceActions::TEST_READY:
      return "TEST_READY";
    default:
      return "UNKNOWN";
    }
  }

}

#endif
