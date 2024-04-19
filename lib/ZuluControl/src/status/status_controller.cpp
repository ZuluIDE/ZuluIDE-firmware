
#include "status_controller.h"

#include "ZuluIDE_log.h"

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
    });

    std::for_each(observerQueues.begin(), observerQueues.end(), [this](auto observer) {
      SystemStatus *update = new SystemStatus(status);
      queue_try_add(observer, &update);
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
  queue_init(&updateQueue, sizeof(UpdateAction*), 5);
}

void StatusController::LoadImage(zuluide::images::Image i) {
  status.SetLoadedImage(std::make_unique<zuluide::images::Image>(i));
  notifyObservers();
}

void StatusController::LoadImageSafe(zuluide::images::Image i) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->ToLoad = std::make_unique<zuluide::images::Image>(i);
  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Load image failed to enqueue.");
  }
}

void StatusController::EjectImageSafe() {
  UpdateAction* actionToExecute = new UpdateAction();
  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Eject image failed to enqueue.");
  }
}

void StatusController::ProcessUpdates() {
  UpdateAction* actionToExecute;
  if (queue_try_remove(&updateQueue, &actionToExecute)) {
    // An action was on the queue, execute it.
    if (actionToExecute->ToLoad) {
      LoadImage(*actionToExecute->ToLoad);
    } else {
      EjectImage();
    }
   
    delete(actionToExecute);
  }
}

void StatusController::AddObserver(queue_t* dest) {
  observerQueues.push_back(dest);
}
