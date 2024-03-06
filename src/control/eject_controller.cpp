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

#include "eject_controller.h"
#include "std_display_controller.h"

using namespace zuluide::control;

EjectController::EjectController(StdDisplayController* cntrlr, zuluide::status::StatusController* statCtrlr) :
  controller(cntrlr), statusController(statCtrlr) {
}

void EjectController::MoveToNextEntry() {
  state.MoveToNextCurrentEntry();
  controller->UpdateState(state);
}

void EjectController::MoveToPreviousEntry() {
  // There are only two entries.
  state.MoveToNextCurrentEntry();
  controller->UpdateState(state);
}

void EjectController::DoSelectedEntry() {
  if (state.GetCurrentEntry() == EjectState::Entry::Eject) {
    statusController->EjectImage();
  }
  
  controller->SetMode(Mode::Status);
}

void EjectController::Reset(const EjectState newState) {
  state = newState;
}
