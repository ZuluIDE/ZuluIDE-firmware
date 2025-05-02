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

#include "menu_controller.h"
#include "std_display_controller.h"

using namespace zuluide::control;

MenuController::MenuController(StdDisplayController* cntrlr, zuluide::status::DeviceControlSafe* statCtrlr) : UIControllerBase(cntrlr), statusController (statCtrlr) {
}

void MenuController::MoveToNextEntry() {
  state.MoveToNextEntry();
  controller->UpdateState(state);
}

void MenuController::MoveToPreviousEntry() {
  state.MoveToPreviousEntry();
  controller->UpdateState(state);
}

void MenuController::ChangeToSelectedEntry() {
  switch (state.GetCurrentEntry()) {
  case MenuState::Entry::Eject: {
    if (statusController->IsPreventRemovable())
      controller->SetMode(Mode::EjectPrevented);
    else
      controller->SetMode(Mode::Eject);
    break;
  }
    
  case MenuState::Entry::Select: {
    controller->SetMode(Mode::Select);
    break;
  }

  case MenuState::Entry::Info: {
    controller->SetMode(Mode::Info);
    break;
  }

  case MenuState::Entry::Back: {
    controller->SetMode(Mode::Status);
    break;
  }
  }
}

DisplayState MenuController::Reset() {
  state = MenuState();
  return DisplayState(state);
}
