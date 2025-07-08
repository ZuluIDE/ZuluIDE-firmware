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

#include "select_controller.h"
#include "zuluide/control/select_controller_src_type.h"
#include "std_display_controller.h"
#include "zuluide/pipe/image_request.h"
#include "ZuluIDE_log.h"

using namespace zuluide::control;
using namespace zuluide::pipe;

DisplayState SelectController::Reset() {
  state = SelectState();
  auto currentStatus = controller->GetCurrentStatus();
  ImageRequest<select_controller_source_t> reset_request;
  reset_request.SetType(image_request_t::Reset);
  imageRequestPipe->RequestImageSafe(reset_request);

  ImageRequest<select_controller_source_t> request;
  if (currentStatus.HasLoadedImage()) {
    // Lets try to move the iterator to the currently selected image.
    request.SetCurrentFilename(std::make_unique<std::string>(currentStatus.GetLoadedImage().GetFilename()));
    request.SetType(image_request_t::Current);
    imageRequestPipe->RequestImageSafe(request);
  }
  else
  {
    request.SetType(image_request_t::First);
    imageRequestPipe->RequestImageSafe(request);
  }

  return DisplayState(state);
}

SelectController::SelectController(StdDisplayController* cntrlr, zuluide::status::DeviceControlSafe* statCtrlr,
                                    ImageRequestPipe<select_controller_source_t>* imReqPipe, ImageResponsePipe<select_controller_source_t> * imResPipe) :
  UIControllerBase(cntrlr), statusController(statCtrlr), imageRequestPipe(imReqPipe), imageResponsePipe(imResPipe) {
  imageResponsePipe->AddObserver([&](const ImageResponse<select_controller_source_t>& t){SetImageEntry(t);});
}

void SelectController::IncrementImageNameOffset() {
  auto value = state.GetImageNameOffset();
  if (!state.IsShowingBack() && state.HasCurrentImage() && value + 1 < state.GetCurrentImage().GetFilename().length()) {
    state.SetImageNameOffset(value + 1);
    controller->UpdateState(state);
  }
}

void SelectController::DecreaseImageNameOffset() {
  auto value = state.GetImageNameOffset();
  if (value > 0) {
    state.SetImageNameOffset(value - 1);
    controller->UpdateState(state);
  }
}

void SelectController::ResetImageNameOffset() {
  state.SetImageNameOffset(0);
  controller->UpdateState(state);
}

void SelectController::SelectImage() {
  if (state.IsShowingBack()) {
    controller->SetMode(Mode::Menu);
  } else if (state.HasCurrentImage()) {
    statusController->LoadImageSafe(state.GetCurrentImage());
  }
  controller->SetMode(Mode::Status);
  ImageRequest image_request = ImageRequest<select_controller_source_t>();
  image_request.SetType(image_request_t::Cleanup);
  imageRequestPipe->RequestImageSafe(image_request);
}

void SelectController::ChangeToMenu() {
  controller->SetMode(Mode::Menu);
  ImageRequest image_request = ImageRequest<select_controller_source_t>();
  image_request.SetType(image_request_t::Cleanup);
  imageRequestPipe->RequestImageSafe(image_request);
}

void SelectController::GetNextImageEntry() {
  ImageRequest image_request = ImageRequest<select_controller_source_t>();
  image_request.SetType(image_request_t::Next);
  imageRequestPipe->RequestImageSafe(image_request);
}

void SelectController::GetPreviousImageEntry() {
  ImageRequest image_request = ImageRequest<select_controller_source_t>();
  image_request.SetType(image_request_t::Prev);
  imageRequestPipe->RequestImageSafe(image_request);
}


void SelectController::SetImageEntry(const ImageResponse<select_controller_source_t>& response)
{
  static image_request_t last_request = image_request_t::Current;
  std::unique_ptr<zuluide::images::Image> image = std::make_unique<zuluide::images::Image>(response.GetImage());

  if (response.GetStatus() == response_status_t::None)
  {
    state.SetCurrentImage(nullptr);
    state.SetIsShowingBack(true);
    state.SetAtStart(false);
    state.SetAtEnd(false);
    return;
  }

  switch(response.GetRequest().GetType())
  {
    case image_request_t::Next:
      if(state.AtEnd() && !state.IsShowingBack())
      {
        state.SetIsShowingBack(true);
      }
      else if (state.IsShowingBack())
      {
        ImageRequest image_request = ImageRequest<select_controller_source_t>();
        image_request.SetType(image_request_t::First);
        imageRequestPipe->RequestImageSafe(image_request);
      }
      else
      {
        state.SetCurrentImage(std::move(image));
        state.SetIsShowingBack(false);
      }
      break;

    case image_request_t::Prev:
      if(state.AtStart() && !state.IsShowingBack())
      {
          state.SetIsShowingBack(true);
          state.SetAtEnd(true);
      }
      else if (state.IsShowingBack())
      {
        ImageRequest<select_controller_source_t> image_request = ImageRequest<select_controller_source_t>();
        image_request.SetType(image_request_t::Last);
        imageRequestPipe->RequestImageSafe(image_request);
      }
      else
      {
        state.SetCurrentImage(std::move(image));
        state.SetIsShowingBack(false);
      }
        break;

    case image_request_t::First:
      state.SetCurrentImage(std::move(image));
      state.SetIsShowingBack(false);
      break;

    case image_request_t::Last:
      state.SetCurrentImage(std::move(image));
      state.SetIsShowingBack(false);
      break;

    case image_request_t::Current:
      state.SetCurrentImage(std::move(image));
      state.SetIsShowingBack(false);
      break;

    case image_request_t::Cleanup:
    case image_request_t::Reset:
    default:
      logmsg("SelectController::SetImageEntry: No handler for request");
  }
  state.SetAtStart(response.IsFirst());
  state.SetAtEnd(response.IsLast());
  controller->UpdateState(state);
}