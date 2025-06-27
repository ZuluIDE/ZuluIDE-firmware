
/**
 * ZuluIDE™ - Copyright (c) 2025 Rabbit Hole Computing™
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

#include "zuluide/pipe/image_request_pipe.h"


#include "ZuluIDE_log.h"

#include <algorithm>
#include <utility>
#include <memory>

using namespace zuluide::pipe;

ImageRequestPipe::ImageRequestPipe() :
  isUpdating(false)
{
}

void ImageRequestPipe::AddObserver(std::function<void(const ImageRequest&)> callback) {
  observers.push_back(callback);
}

void ImageRequestPipe::BeginUpdate() {
  isUpdating = true;
}

void ImageRequestPipe::EndUpdate() {
  isUpdating = false;
  notifyObservers();
}

void ImageRequestPipe::notifyObservers() {
  if (!isUpdating) {
    std::for_each(observers.begin(), observers.end(), [this](auto observer) {
      // Make a copy so observers cannot mutate system state.
      // This may be overly conservative if we do not do multi-threaded work
      // and we do not mutate system state in observers. This could be easily
      // verified given this isn't a public API.
      observer(*filenameRequest);
    });

    // std::for_each(observerQueues.begin(), observerQueues.end(), [this](auto observer) {
    //   FilenameRequest *update = new FilenameRequest(filenameRequest);
    //   queue_try_add(observer, &update);
    // });
  }
}

// void ImageRequestPipe::SetFirmwareVersion(std::string firmwareVersion) {
//   status.SetFirmwareVersion(std::move(firmwareVersion));
//   notifyObservers();
// }

// const SystemStatus& ImageRequestPipe::GetStatus() {
//   return status;
// }

void ImageRequestPipe::Reset() {
   queue_init(&updateQueue, sizeof(ImageRequest*), 5);
}

// void ImageRequestPipe::LoadImage(zuluide::images::Image i) {
//   status.SetLoadedImage(std::make_unique<zuluide::images::Image>(i));
//   notifyObservers();
// }

// void ImageRequestPipe::LoadImageSafe(zuluide::images::Image i) {
//   UpdateAction* actionToExecute = new UpdateAction();
//   actionToExecute->ToLoad = std::make_unique<zuluide::images::Image>(i);
//   if(!queue_try_add(&updateQueue, &actionToExecute)) {
//     logmsg("Load image failed to enqueue.");
//   }
// }

// void ImageRequestPipe::EjectImageSafe() {
//   UpdateAction* actionToExecute = new UpdateAction();
//   actionToExecute->Eject = std::make_unique<bool>(true);
//   if(!queue_try_add(&updateQueue, &actionToExecute)) {
//     logmsg("Eject image failed to enqueue.");
//   }
// }

void ImageRequestPipe::RequestImageSafe(ImageRequest image_request) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->requestFilename = std::make_unique<ImageRequest>(image_request);
  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Requesting filename action failed to enqueue.");
  }
}

void ImageRequestPipe::ProcessUpdates() {
  UpdateAction* actionToExecute;
  if (queue_try_remove(&updateQueue, &actionToExecute)) {
    // An action was on the queue, execute it.
    if (actionToExecute->requestFilename) {
      filenameRequest = std::move(actionToExecute->requestFilename);
      notifyObservers();
    }
    delete(actionToExecute);
  }
}

// void ImageRequestPipe::HostProcessUpdates() {
//   UpdateAction* actionToExecute;
//   if (queue_try_remove(&updateQueue, &actionToExecute)) {
//     // An action was on the queue, execute it.
//     if (actionToExecute->ToLoad) {
//       LoadImage(*actionToExecute->ToLoad);
//     }

//     if (actionToExecute->Eject && *actionToExecute->Eject) {
//       EjectImage();
//     }

//     if (actionToExecute->requestFilename) {
//       switch(*actionToExecute->requestFilename) {
//         case filename_request_action_t::Start:
          
//         break;
//         case filename_request_action_t::Next:
//         break;
//         default:
//           // do nothing
//       }
//     }
   
//     delete(actionToExecute);
//   }
// }

// void ImageRequestPipe::ClientProcessUpdates() {
//   ReceiveAction* clientActionToExecute;
//   if (queue_try_remove(&receiveQueue, &clientActionToExecute)) {
//     // An action was on the queue, execute it.
//     if (clientActionToExecute->SendFilename) {
//       switch(clientActionToExecute->SendFilename->state) {
//         case filename_send_state_t::First:
//           // \todo send filename to i2c client
//           break;
//         case filename_send_state_t::Continue:
//           // \todo send next filename
//           break;
//         case filename_send_state_t::EndOfList:
//           // \todo end receive before next request
//           break;
//         case filename_send_state_t::Empty:
//           break;
//         default:
//           // do nothing
//       }
//     }
//     delete(clientActionToExecute)
//   }
// }

void ImageRequestPipe::AddObserver(queue_t* dest) {
  observerQueues.push_back(dest);
}

// void ImageRequestPipe::SetIsCardPresent(bool value) {
//   status.SetIsCardPresent(value);
//   if (!value) {
//     status.SetLoadedImage(nullptr);
//   }
  
//   notifyObservers();
// }
