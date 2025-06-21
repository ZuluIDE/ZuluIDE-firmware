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
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#pragma once

#include <functional>
#include <algorithm>
#include <pico/util/queue.h>
#include <zuluide/observable_ui_safe.h>
#include <zuluide/observable_safe.h>

namespace zuluide {
  
  /***
  Resends updates to from one observable (via the observablesafe interface) and then resends the update to
  oberservers of this instance when calls to ProcessUpdate execute. The purpose of this class to safely move updates from
  one core to another.
  **/
  template <class T> class ObserverTransfer : public ObservableUISafe<T>
  {
    public:
    ObserverTransfer() : discardOldMessages(false) {
      queue_init(&updateQueue, sizeof(T*), 5);
    };
    
    virtual void AddObserver(std::function<void(const T& current)> callback) {
      observers.push_back(callback);
    };
    
    /***
    Connects to the observable instance that is the source of items to send.
    **/
    void Initialize(ObservableSafe<T> &toWatch, bool discardOldMsgs = false) {
      discardOldMessages = discardOldMsgs;
      toWatch.AddObserver(&updateQueue);
    };
    
    /***
    This function dispatches update to its observers. Calls should be made periodically from the thread upon which
    you want the updates to execute.
    **/
    bool ProcessUpdate() {
      T* item;
      
      // Throw away outdated messages to get to the latest.
      while (discardOldMessages && queue_get_level(&updateQueue) > 1 && queue_try_remove(&updateQueue, &item)) {
        delete(item);
      }
      
      if (queue_try_remove(&updateQueue, &item)) {
        
        std::for_each(observers.begin(), observers.end(), [this, item](auto observer) {
          observer(*item);
        });
        
        delete(item);
        return true;
      } else {
        return false;
      }
    };
    private:
    queue_t updateQueue;
    std::vector<std::function<void(const T& current)>> observers;
    bool discardOldMessages;
  };
}
