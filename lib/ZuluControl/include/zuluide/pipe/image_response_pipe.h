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

#include <zuluide/pipe/image_response.h>
#include <zuluide/pipe/image_request.h>
#include <zuluide/observable.h>
#include <zuluide/queue/safe_queue.h>
#include <zuluide/images/image_iterator.h>
#include "zuluide/pipe/image_response_pipe.h"
#include <ide_protocol.h>
#include "ZuluIDE_log.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace zuluide::pipe {

template<typename SrcType>
class ImageResponsePipe : public Observable<ImageResponse<SrcType>>
{
  public:
    ImageResponsePipe();
    /**
     * Functions to call once image data has safely received from the core with SD access
     */
    void AddObserver(std::function<void(const ImageResponse<SrcType>& current)> callback);
    void BeginUpdate();
    void EndUpdate();
    void Reset();
    /**
     * A call back to handle image requests, run via the Image Request Pipe
     */
    void HandleRequest(ImageRequest<SrcType>& current);
    /**
     * Attempts to queue the requested image from the imageIterator wth SD access to the other core
     */
    void ResponseImageSafe(ImageResponse<SrcType> image_response);
    /**
     * Attempts to remove the requested image sent from queue to send to observers on the core without SD access
     */
    void ProcessUpdates();

  private:
    bool isUpdating;
    void notifyObservers();
    std::vector<std::function<void(const ImageResponse<SrcType>&)>> observers;
    std::unique_ptr<ImageResponse<SrcType>> imageResponse;

    /***
        Stores updates that come from another thread. These are processed through class to ProcessUpdates.
      **/
    zuluide::queue::SafeQueue updateQueue;

    /***
     * Image iterator
     */
    zuluide::images::ImageIterator imageIterator;

    /***
        Simple class for storing updates.
      **/
    class UpdateAction {
    public:
      std::unique_ptr<ImageResponse<SrcType>> responseImage;
    };

};

template <typename SrcType>
ImageResponsePipe<SrcType>::ImageResponsePipe() : isUpdating(false) 
{
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::HandleRequest(ImageRequest<SrcType>& current)
{
  std::unique_ptr<ImageResponse<SrcType>> response = std::make_unique<ImageResponse<SrcType>>();
  image_request_t request = current.GetType();

  switch(request)
  {
    case image_request_t::Next:
      imageIterator.MoveNext();
      if(imageIterator.IsEmpty())
      {
        response->SetStatus(response_status_t::None);
      }
      else
      {
        response->SetStatus(imageIterator.IsLast()? response_status_t::End : response_status_t::More);
        response->SetImage(std::move(std::make_unique<zuluide::images::Image>(imageIterator.Get())));
      }
      break;
    case image_request_t::Prev:
      imageIterator.MovePrevious();
      if(imageIterator.IsEmpty())
      {
        response->SetStatus(response_status_t::None);
      }
      else
      {
        response->SetStatus(imageIterator.IsFirst() ? response_status_t::End : response_status_t::More);
        response->SetImage(std::make_unique<zuluide::images::Image>(imageIterator.Get()));
      }
      break;
    case image_request_t::First:
      imageIterator.Reset();
      if(imageIterator.IsEmpty())
      {
        response->SetStatus(response_status_t::None);
      }
      else
      {
        imageIterator.MoveNext();
        response->SetStatus(imageIterator.IsLast() ? response_status_t::End : response_status_t::More);
        response->SetImage(std::make_unique<zuluide::images::Image>(imageIterator.Get()));
      }
      break;
    case image_request_t::Last:
      imageIterator.MoveLast();
      if (imageIterator.IsEmpty())
      {
        response->SetStatus(response_status_t::None);
      }
      else
      {
        response->SetStatus(imageIterator.IsFirst() ? response_status_t::End : response_status_t::More);
        response->SetImage(std::make_unique<zuluide::images::Image>(imageIterator.Get()));
      }
      break;
   case image_request_t::Current:
      if (!current.GetCurrentFilename().empty())
      {
        imageIterator.MoveToFile(current.GetCurrentFilename().c_str());
        if (imageIterator.IsEmpty())
        {
          response->SetStatus(response_status_t::None);
        }
        else
        {
          response->SetStatus(imageIterator.IsLast() ? response_status_t::End : response_status_t::More);
          response->SetImage(std::move(std::make_unique<zuluide::images::Image>(imageIterator.Get())));
        }
      }
      else
      {
        // Move to first file if current image string is empty
        imageIterator.MoveFirst();
        if (imageIterator.IsEmpty())
        {
          response->SetStatus(response_status_t::None);
        }
        else
        {
          response->SetStatus(imageIterator.IsLast() ? response_status_t::End : response_status_t::More);
          response->SetImage(std::make_unique<zuluide::images::Image>(imageIterator.Get()));
        }
      }
      break;
    // The following don't get queued for processing
    case image_request_t::Cleanup:
      dbgmsg("Image response pipe is cleaning up");
      imageIterator.Cleanup();
      return;
    case image_request_t::Reset:
      dbgmsg("Image response pipe is resetting");
      imageIterator.Reset();
      return;
    case image_request_t::Empty:
      dbgmsg("Requesting image was emtpy and doesn't have a source");
      return;
  }
  
  // All request but clean up and reset get passed

  response->SetIsFirst(imageIterator.IsFirst());
  response->SetIsLast(imageIterator.IsLast());
  response->SetRequest(std::move(std::make_unique<ImageRequest<SrcType>>(current)));
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->responseImage = std::move(response);
  if(!updateQueue.TryAdd(&actionToExecute)) {
    logmsg("Responding image action failed to enqueue.");
  }
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::AddObserver(std::function<void(const ImageResponse<SrcType>&)> callback) {
  observers.push_back(callback);
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::BeginUpdate() {
  isUpdating = true;
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::EndUpdate() {
  isUpdating = false;
  notifyObservers();
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::notifyObservers() {
  if (!isUpdating) {
    std::for_each(observers.begin(), observers.end(), [this](auto observer) {
      // Make a copy so observers cannot mutate system state.
      // This may be overly conservative if we do not do multi-threaded work
      // and we do not mutate system state in observers. This could be easily
      // verified given this isn't a public API.
      observer(ImageResponse<SrcType>(*imageResponse));
#ifndef CONTROL_CROSS_CORE_QUEUE
          ide_protocol_poll();
#endif
    });
  }
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::Reset() {
   updateQueue.Reset(sizeof(UpdateAction*), 5);
   imageIterator.Reset();
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::ResponseImageSafe(ImageResponse<SrcType> image_response) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->responseImage = std::make_unique<ImageResponse<SrcType>>(image_response);
  if(!updateQueue.TryAdd(&actionToExecute)) {
    logmsg("Responding image action failed to enqueue.");
  }
}

template<typename SrcType>
void ImageResponsePipe<SrcType>::ProcessUpdates() {
  UpdateAction* actionToExecute;
  if (updateQueue.TryRemove(&actionToExecute)) {
    // An action was on the queue, execute it.
    if (actionToExecute) {
      imageResponse = std::move(actionToExecute->responseImage);
      actionToExecute->responseImage = nullptr;
      notifyObservers();
    }
    delete(actionToExecute);
  }
}
}
