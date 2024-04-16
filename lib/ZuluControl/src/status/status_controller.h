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

#include <zuluide/status/system_status.h>
#include <zuluide/status/device_status.h>
#include <zuluide/images/image.h>
#include <zuluide/images/image_iterator.h>
#include <zuluide/observable.h>

#include <functional>
#include <memory>
#include <vector>

namespace zuluide::status {

  class StatusController : public Observable<SystemStatus>
  {
  public:
    StatusController();
    void AddObserver(std::function<void(const SystemStatus& current)> callback);    
    void LoadImage(zuluide::images::Image i);
    void EjectImage();
    void BeginUpdate();
    void EndUpdate();
    void UpdateDeviceStatus(std::unique_ptr<IDeviceStatus> updated);
    void SetIsPrimary(bool value);
    void SetFirmwareVersion(std::string firmwareVersion);
    const SystemStatus& GetStatus();
    void Reset();

  private:
    bool isUpdating;
    void notifyObservers();
    std::vector<std::function<void(const SystemStatus&)>> observers;
    SystemStatus status;
    
  };
  
}
