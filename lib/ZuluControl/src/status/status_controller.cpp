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
#include "status_controller.h"

#include "ZuluIDE_log.h"
#include <ide_protocol.h>

#include <algorithm>
#include <utility>
#include <memory>

using namespace zuluide::status;
using namespace zuluide::images;

StatusController::StatusController() :
  isUpdating(false)
{
}

void StatusController::AddObserver(std::function<void(const SystemStatus&)> callback) {
  observers.push_back(callback);
}

void StatusController::EjectImage() {
  status.SetLoadedImage(nullptr);
  status.SetIsEject(true);
  notifyObservers();
}

void StatusController::BeginUpdate() {
  isUpdating = true;
}

void StatusController::EndUpdate() {
  isUpdating = false;
  notifyObservers();
}

void StatusController::UpdateDeviceStatus(std::unique_ptr<IDeviceStatus> updated) {
  status.SetDeviceStatus(std::move(updated));  
}

void StatusController::SetIsPrimary(bool isPrimary) {
  status.SetIsPrimary(isPrimary);
  notifyObservers();
}

void StatusController::notifyObservers() {
  if (!isUpdating) {
    std::for_each(observers.begin(), observers.end(), [this](auto observer) {
      // Make a copy so observers cannot mutate system state.
      // This may be overly conservative if we do not do multi-threaded work
      // and we do not mutate system state in observers. This could be easily
      // verified given this isn't a public API.
      observer(SystemStatus(status));
#ifndef CONTROL_CROSS_CORE_QUEUE
      ide_protocol_poll();
#endif
    });

    std::for_each(observerQueues.begin(), observerQueues.end(), [this](auto observer) {
      SystemStatus *update = new SystemStatus(status);
      observer->TryAdd(&update);
    });
  }
}

void StatusController::SetFirmwareVersion(std::string firmwareVersion) {
  status.SetFirmwareVersion(std::move(firmwareVersion));
  notifyObservers();
}

const SystemStatus& StatusController::GetStatus() {
  return status;
}

void StatusController::Reset() {
  updateQueue.Reset(sizeof(UpdateAction*), 5);
}

void StatusController::LoadImage(zuluide::images::Image i) {
  status.SetLoadedImage(std::make_unique<zuluide::images::Image>(i));
  notifyObservers();
}

void StatusController::LoadImageSafe(zuluide::images::Image i) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->ToLoad = std::make_unique<zuluide::images::Image>(i);
  actionToExecute->IsEject = false;
  if(!updateQueue.TryAdd(&actionToExecute)) {
    logmsg("Load image failed to enqueue.");
  }
}

void StatusController::EjectImageSafe() {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->IsEject = true;
  if(!updateQueue.TryAdd(&actionToExecute)) {
    logmsg("Eject image failed to enqueue.");
  }
}

void StatusController::ProcessUpdates() {
  UpdateAction* actionToExecute;
  if (updateQueue.TryRemove(&actionToExecute)) {
    // An action was on the queue, execute it.
    if (actionToExecute->ToLoad) {
      LoadImage(*actionToExecute->ToLoad);
    } else {
      if (actionToExecute->IsEject){
        EjectImage();
      }
    }
   
    delete(actionToExecute);
  }
}

void StatusController::AddObserver(zuluide::queue::SafeQueue* dest) {
  observerQueues.push_back(dest);
}

void StatusController::SetIsCardPresent(bool value) {
  status.SetIsCardPresent(value);
  if (!value) {
    status.SetLoadedImage(nullptr);
  }
  
  notifyObservers();
}


void StatusController::SetIsPreventRemovable(bool prevent)
{
  status.SetIsPreventRemovable(prevent);
}

bool StatusController::IsPreventRemovable()
{
  return status.IsPreventRemovable();
}

void StatusController::SetIsDeferred(bool defer)
{
  status.SetIsDeferred(defer);
  if (!defer)
    notifyObservers();
}

bool StatusController::IsDeferred()
{
  return status.IsDeferred();
}