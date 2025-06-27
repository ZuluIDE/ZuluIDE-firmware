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

#include <functional>
#include <memory>
#include <vector>

namespace zuluide::pipe {

  class ImageRequestPipe : public Observable<ImageRequest>
  {
  public:
    ImageRequestPipe();
    /**
     * Callback to process image request should be the image response pipe
     */
    void AddObserver(std::function<void(const ImageRequest& current)> callback);
    void BeginUpdate();
    void EndUpdate();
    /**
     * Should be run on the core without SD access, attempts to queue the requested image action
     */
    void RequestImageSafe(ImageRequest image_request);
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
    std::unique_ptr<ImageRequest> imageRequest;
    void notifyObservers();

    std::vector<std::function<void(const ImageRequest&)>> observers;

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
      std::unique_ptr<ImageRequest> requestImage;
    };
  };

  
}
