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

#include "select_controller.h"
#include "std_display_controller.h"
#include "ZuluIDE_log.h"

using namespace zuluide::control;

void SelectController::Reset(const SelectState& newState) {
  imgIterator.Reset();
  state = newState;
  GetNextImageEntry();
}

SelectController::SelectController(StdDisplayController* cntrlr, zuluide::status::DeviceControlSafe* statCtrlr) :
  controller(cntrlr), statusController(statCtrlr), imgIterator(true) {  
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
  imgIterator.Cleanup();
}

void SelectController::ChangeToMenu() {
  controller->SetMode(Mode::Menu);
  imgIterator.Cleanup();
}

void SelectController::GetNextImageEntry() {
  logmsg("GetNextImageEntry Core ", (int)get_core_num());
  if (imgIterator.IsLast() && !state.IsShowingBack()) {
    // We are currently on the last item, show the back.
    state.SetIsShowingBack(true);
    state.SetCurrentImage(nullptr);
  } else if (imgIterator.MoveNext()) {
    state.SetCurrentImage(std::make_unique<Image>(imgIterator.Get()));
    state.SetIsShowingBack(false);
  } else {
    // Otherwise, we have no images on the card.
    state.SetIsShowingBack(true);
  }  

  controller->UpdateState(state);
}

void SelectController::GetPreviousImageEntry() {
  if (imgIterator.IsFirst() && !state.IsShowingBack()) {
    // We are currently on the last item, show the back.
    state.SetIsShowingBack(true);
    state.SetCurrentImage(nullptr);
  } else if (imgIterator.MoveNext()) {
    state.SetCurrentImage(std::make_unique<Image>(imgIterator.Get()));
    state.SetIsShowingBack(false);
  } else {
    // Otherwise, we have no images on the card.
    state.SetIsShowingBack(true);
  }

  controller->UpdateState(state);
}
