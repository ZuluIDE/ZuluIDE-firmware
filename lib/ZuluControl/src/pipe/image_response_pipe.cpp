
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

#include "zuluide/pipe/image_response_pipe.h"
#include "ZuluIDE_log.h"

#include <algorithm>
#include <utility>
#include <memory>

using namespace zuluide::pipe;

ImageResponsePipe::ImageResponsePipe() :
  isUpdating(false)
{
  imageIterator.Reset();
}

void ImageResponsePipe::HandleRequest(zuluide::pipe::ImageRequest& current)
{
  std::unique_ptr<ImageResponse> response = std::make_unique<ImageResponse>();
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
        response->SetImage(std::make_unique<zuluide::images::Image>(imageIterator.Get()));
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
          response->SetImage(std::make_unique<zuluide::images::Image>(imageIterator.Get()));
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
    case image_request_t::Cleanup:
      imageIterator.Cleanup();
      break;
    case image_request_t::Reset:
      imageIterator.Reset();
      break;
  }
  
  // All request but clean up and reset get passed
  if (request != image_request_t::Cleanup && request != image_request_t::Reset)
  {
    response->SetIsFirst(imageIterator.IsFirst());
    response->SetIsLast(imageIterator.IsLast());
    response->SetRequest(std::move(std::make_unique<ImageRequest>(current)));
    UpdateAction* actionToExecute = new UpdateAction();
    actionToExecute->responseImage = std::move(response);
    if(!queue_try_add(&updateQueue, &actionToExecute)) {
      logmsg("Responding image action failed to enqueue.");
    }
  }
}

void ImageResponsePipe::AddObserver(std::function<void(const ImageResponse&)> callback) {
  observers.push_back(callback);
}

void ImageResponsePipe::BeginUpdate() {
  isUpdating = true;
}

void ImageResponsePipe::EndUpdate() {
  isUpdating = false;
  notifyObservers();
}

void ImageResponsePipe::notifyObservers() {
  if (!isUpdating) {
    std::for_each(observers.begin(), observers.end(), [this](auto observer) {
      // Make a copy so observers cannot mutate system state.
      // This may be overly conservative if we do not do multi-threaded work
      // and we do not mutate system state in observers. This could be easily
      // verified given this isn't a public API.
      observer(*imageResponse);
    });
  }
}

void ImageResponsePipe::Reset() {
   queue_init(&updateQueue, sizeof(UpdateAction*), 5);
   imageIterator.Reset();
}

void ImageResponsePipe::ResponseImageSafe(ImageResponse image_response) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->responseImage = std::make_unique<ImageResponse>(image_response);
  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Responding image action failed to enqueue.");
  }
}

void ImageResponsePipe::ProcessUpdates() {
  UpdateAction* actionToExecute;
  if (queue_try_remove(&updateQueue, &actionToExecute)) {
    // An action was on the queue, execute it.
    if (actionToExecute) {
      imageResponse = std::move(actionToExecute->responseImage);
      actionToExecute->responseImage = nullptr;
    }
    notifyObservers();
    delete(actionToExecute);
  }
}