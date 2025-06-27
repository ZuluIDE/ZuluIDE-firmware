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
#include <zuluide/observable_safe.h>
#include <pico/util/queue.h>
#include <zuluide/images/image_iterator.h>

#include <functional>
#include <memory>
#include <vector>

namespace zuluide::pipe {

  class ImageResponsePipe : public Observable<ImageResponse>, public ObservableSafe<ImageResponse>
  {
  public:
    ImageResponsePipe();
    void AddObserver(std::function<void(const ImageResponse& current)> callback);
    void AddObserver(queue_t* dest);
    void BeginUpdate();
    void EndUpdate();
    void Reset();
    void HandleRequest(zuluide::pipe::ImageRequest& current);
    void SendResponse(std::unique_ptr<ImageResponse> response);
    void ResponseFilenamesSafe(ImageResponse filename_request);
    void ProcessUpdates();

  private:
    bool isUpdating;
    void notifyObservers();
    std::vector<std::function<void(const ImageResponse&)>> observers;
    std::unique_ptr<ImageResponse> imageResponse;
    /***
        Stores queues where updatefilenameResponsed system status pointers are copied.
     **/
    std::vector<queue_t*> observerQueues;
    /***
        Stores updates that come from another thread. These are processed through class to ProcessUpdates.
     **/
    queue_t updateQueue;

    /***
        Receives updates that come from another thread in the opposite direction of the updateQueue 
     */
    queue_t receiveQueue;

    /***
     * Image iterator
     */
    zuluide::images::ImageIterator imageIterator;

    /***
        Simple class for storing updates. As we currently only have the load or eject image updates,
        this class is overly simple.
     **/
    class UpdateAction {
    public:
      /***
          If the value is null, this is an eject.
       **/
      std::unique_ptr<ImageResponse> responseFilename;
    };

  };
}
