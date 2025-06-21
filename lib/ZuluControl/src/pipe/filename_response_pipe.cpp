
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

#include "zuluide/pipe/filename_response_pipe.h"
#include "ZuluIDE_log.h"

#include <algorithm>
#include <utility>
#include <memory>

using namespace zuluide::pipe;

FilenameResponsePipe::FilenameResponsePipe() :
  isUpdating(false)
{
  imageIterator.Reset();
}

void FilenameResponsePipe::HandleUpdate(const zuluide::pipe::FilenameRequest& current)
{
  FilenameResponse response;
  const filename_request_t request = current.GetRequest();
  bool more = false;
  if ( request == filename_request_t::Next)
  {
    {
      more = imageIterator.MoveNext();
      if(imageIterator.IsEmpty())
      {
        response.SetStatus(response_status_t::None);
        response.SetFilename(std::move(std::string("Empty filename on next")));
      }
      else
      {
        response.SetStatus(more ? response_status_t::More : response_status_t::End);
        response.SetFilename(std::move(imageIterator.Get().GetFilename()));
      }
    }
  }
  else if (request == zuluide::pipe::filename_request_t::Start)
  {
    imageIterator.Reset();
    if(imageIterator.IsEmpty())
    {
      response.SetStatus(response_status_t::None);
      response.SetFilename(std::move(std::string("Empty filename on Start")));
    }
    else
    {
      more = imageIterator.MoveNext();
      if (more)
      {
        
        response.SetStatus(more ? response_status_t::More : response_status_t::End);
        response.SetFilename(std::move(imageIterator.Get().GetFilename()));
      }
    }
  }
  logmsg("Response: Found this filename, ", response.GetFilename().c_str(), request == filename_request_t::Next ? " on next" :" on a first");
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->responseFilename = std::make_unique<FilenameResponse>(response);
  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Responding filename action failed to enqueue.");
  }
}

void FilenameResponsePipe::AddObserver(std::function<void(const FilenameResponse&)> callback) {
  observers.push_back(callback);
}

void FilenameResponsePipe::BeginUpdate() {
  isUpdating = true;
}

void FilenameResponsePipe::EndUpdate() {
  isUpdating = false;
  notifyObservers();
}

void FilenameResponsePipe::notifyObservers() {
  if (!isUpdating) {
    std::for_each(observers.begin(), observers.end(), [this](auto observer) {
      // Make a copy so observers cannot mutate system state.
      // This may be overly conservative if we do not do multi-threaded work
      // and we do not mutate system state in observers. This could be easily
      // verified given this isn't a public API.
      observer(FilenameResponse(filenameResponse));
    });

    std::for_each(observerQueues.begin(), observerQueues.end(), [this](auto observer) {
      FilenameResponse *update = new FilenameResponse(filenameResponse);
      queue_try_add(observer, &update);
    });
  }
}

void FilenameResponsePipe::Reset() {
   queue_init(&updateQueue, sizeof(UpdateAction*), 5);
}

void FilenameResponsePipe::ResponseFilenamesSafe(FilenameResponse filename_request) {
  UpdateAction* actionToExecute = new UpdateAction();
  actionToExecute->responseFilename = std::make_unique<FilenameResponse>(filename_request);
  if(!queue_try_add(&updateQueue, &actionToExecute)) {
    logmsg("Responding filename action failed to enqueue.");
  }
}

void FilenameResponsePipe::ProcessUpdates() {
  UpdateAction* actionToExecute;
  if (queue_try_remove(&updateQueue, &actionToExecute)) {
    // An action was on the queue, execute it.
    if (actionToExecute) {
      std::unique_ptr<FilenameResponse> response = std::move(actionToExecute->responseFilename);
      actionToExecute->responseFilename = nullptr;
      switch (response->GetStatus())
      {
        case response_status_t::More:
          logmsg("Response Status: There are more filenames, curent: ", response->GetFilename().c_str());
          break;
        case response_status_t::End:
          logmsg("Response Status: The last filename: ", response->GetFilename().c_str());
          break;
        case response_status_t::None:
          logmsg("Response Status: No files");
          break;
      }
    }
    delete(actionToExecute);
  }
}


void FilenameResponsePipe::AddObserver(queue_t* dest) {
  observerQueues.push_back(dest);
}
