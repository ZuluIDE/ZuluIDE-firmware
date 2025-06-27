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
#include <pico/util/queue.h>
#include <zuluide/images/image_iterator.h>

#include <functional>
#include <memory>
#include <vector>

namespace zuluide::pipe {

  class ImageResponsePipe : public Observable<ImageResponse>
  {
  public:
    ImageResponsePipe();
    /**
     * Functions to call once image data has safely received from the core with SD access
     */
    void AddObserver(std::function<void(const ImageResponse& current)> callback);
    void BeginUpdate();
    void EndUpdate();
    void Reset();
    /**
     * A call back to handle image requests, run via the Image Request Pipe
     */
    void HandleRequest(zuluide::pipe::ImageRequest& current);
    /**
     * Attempts to queue the requested image from the imageIterator wth SD access to the other core
     */
    void ResponseImageSafe(ImageResponse image_response);
    /**
     * Attempts to remove the requested image sent from queue to send to observers on the core without SD access
     */
    void ProcessUpdates();

  private:
    bool isUpdating;
    void notifyObservers();
    std::vector<std::function<void(const ImageResponse&)>> observers;
    std::unique_ptr<ImageResponse> imageResponse;

    /***
        Stores updates that come from another thread. These are processed through class to ProcessUpdates.
     **/
    queue_t updateQueue;

    /***
     * Image iterator
     */
    zuluide::images::ImageIterator imageIterator;

    /***
        Simple class for storing updates.
     **/
    class UpdateAction {
    public:
      std::unique_ptr<ImageResponse> responseImage;
    };

  };
}
