
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
      observer(*imageRequest);
    });
  }
}

void ImageRequestPipe::Reset() {
   queue_init(&updateQueue, sizeof(ImageRequest*), 5);
}

void ImageRequestPipe::RequestImageSafe(ImageRequest image_request) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->requestImage = std::make_unique<ImageRequest>(image_request);
  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Requesting image action failed to enqueue.");
  }
}

void ImageRequestPipe::ProcessUpdates() {
  UpdateAction* actionToExecute;
  if (queue_try_remove(&updateQueue, &actionToExecute)) {
    // An action was on the queue, execute it.
    if (actionToExecute->requestImage) {
      imageRequest = std::move(actionToExecute->requestImage);
      notifyObservers();
    }
    delete(actionToExecute);
  }
}