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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#pragma once
#include <functional>
#include <algorithm>
#include <zuluide/observable_ui_safe.h>
#include <zuluide/observable_safe.h>
#include <zuluide/queue/safe_queue.h>
#include <ide_protocol.h>

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
      updateQueue.Reset(sizeof(T*), 5);
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
      while (discardOldMessages && updateQueue.GetLevel() > 1 && updateQueue.TryRemove(&item)) {
        delete(item);
      }
      
      if (updateQueue.TryRemove(&item)) {
        
        std::for_each(observers.begin(), observers.end(), [this, item](auto observer) {
          observer(*item);
#ifndef CONTROL_CROSS_CORE_QUEUE
          ide_protocol_poll();
#endif
        });
        
        delete(item);
        return true;
      } else {
        return false;
      }
    };
    private:
    zuluide::queue::SafeQueue updateQueue;
    std::vector<std::function<void(const T& current)>> observers;
    bool discardOldMessages;
  };
}
