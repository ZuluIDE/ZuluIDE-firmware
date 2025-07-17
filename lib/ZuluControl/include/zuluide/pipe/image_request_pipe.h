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

#pragma once

#include <zuluide/observable.h>
#include <pico/util/queue.h>
#include "zuluide/pipe/image_request.h"
#include "zuluide/pipe/image_response_pipe.h"
#include "ZuluIDE_log.h"

#include <algorithm>
#include <utility>

#include <functional>
#include <memory>
#include <vector>

namespace zuluide::pipe {

template <typename SrcType>
class ImageRequestPipe : public Observable<ImageRequest<SrcType>>
{
public:
  ImageRequestPipe();
  /**
   * Callback to process image request should be the image response pipe
   */
  void AddObserver(std::function<void(const ImageRequest<SrcType>& current)> callback);
  void BeginUpdate();
  void EndUpdate();
  /**
   * Should be run on the core without SD access, attempts to queue the requested image action
   */
  void RequestImageSafe(ImageRequest<SrcType> image_request);
  /**
   * Initialized the queue
   */
  void Reset();
  /**
   * To be run on the core with SD access, attempt to remove requests from the other core
   */
  void ProcessUpdates();
private:
  bool isUpdating;
  std::unique_ptr<ImageRequest<SrcType>> imageRequest;
  void notifyObservers();

  std::vector<std::function<void(const ImageRequest<SrcType>&)>> observers;

  /***
      Stores updates that come from another thread. These are processed through class to ProcessUpdates.
    **/
  queue_t updateQueue;

  /***
      Simple class for storing updates.
    **/
  class UpdateAction {
  public:
    /***
        If the value is null, ignore.
      **/
    std::unique_ptr<ImageRequest<SrcType>> requestImage;
  };
};

template <typename SrcType>
ImageRequestPipe<SrcType>::ImageRequestPipe() :
  isUpdating(false)
{
}

template <typename SrcType>
void ImageRequestPipe<SrcType>::AddObserver(std::function<void(const ImageRequest<SrcType>&)> callback) {
  observers.push_back(callback);
}

template <typename SrcType>
void ImageRequestPipe<SrcType>::BeginUpdate() {
  isUpdating = true;
}

template <typename SrcType>
void ImageRequestPipe<SrcType>::EndUpdate() {
  isUpdating = false;
  notifyObservers();
}

template <typename SrcType>
void ImageRequestPipe<SrcType>::notifyObservers() {
  if (!isUpdating) {
    std::for_each(observers.begin(), observers.end(), [this](auto observer) {
      // Make a copy so observers cannot mutate system state.
      // This may be overly conservative if we do not do multi-threaded work
      // and we do not mutate system state in observers. This could be easily
      // verified given this isn't a public API.
      observer(ImageRequest<SrcType>(*imageRequest));
    });
  }
}

template <typename SrcType>
void ImageRequestPipe<SrcType>::Reset() {
   queue_init(&updateQueue, sizeof(ImageRequest<SrcType>*), 5);
}

template <typename SrcType>
void ImageRequestPipe<SrcType>::RequestImageSafe(ImageRequest<SrcType> image_request) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->requestImage = std::make_unique<ImageRequest<SrcType>>(image_request);

  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Requesting image action failed to enqueue.");
  }
}

template <typename SrcType>
void ImageRequestPipe<SrcType>::ProcessUpdates() {
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
  
}
