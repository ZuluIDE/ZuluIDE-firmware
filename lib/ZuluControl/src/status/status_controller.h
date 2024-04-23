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
#include <zuluide/observable_safe.h>
#include <zuluide/status/device_control_safe.h>
#include <pico/util/queue.h>

#include <functional>
#include <memory>
#include <vector>

namespace zuluide::status {

  class StatusController : public Observable<SystemStatus>, public ObservableSafe<SystemStatus>, public DeviceControlSafe
  {
  public:
    StatusController();
    void AddObserver(std::function<void(const SystemStatus& current)> callback);
    void AddObserver(queue_t* dest);
    void LoadImage(zuluide::images::Image i);
    void EjectImage();
    void BeginUpdate();
    void EndUpdate();
    void UpdateDeviceStatus(std::unique_ptr<IDeviceStatus> updated);
    void SetIsPrimary(bool value);
    void SetFirmwareVersion(std::string firmwareVersion);
    const SystemStatus& GetStatus();
    void Reset();
    virtual void LoadImageSafe(zuluide::images::Image i);
    virtual void EjectImageSafe();
    void ProcessUpdates();
    void SetIsCardPresent(bool value);
  private:
    bool isUpdating;
    void notifyObservers();
    std::vector<std::function<void(const SystemStatus&)>> observers;
    SystemStatus status;
    /***
        Stores queues where updated system status pointers are copied.
     **/
    std::vector<queue_t*> observerQueues;
    /***
        Stores updates that come from another thread. These are processed through calss to ProcessUpdates.
     **/
    queue_t updateQueue;

    /***
        Simple class for storing updates. As we currently only have the load or eject image updates,
        this class is overly simple.
     **/
    class UpdateAction {
    public:
      /***
          If the value is null, this is an eject.
       **/
      std::unique_ptr<zuluide::images::Image> ToLoad;
    };
  };
  
}
