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

#include "control_interface.h"
#include "ZuluIDE_log.h"
#include "eject_controller.h"
#include "menu_controller.h"
#include "select_controller.h"
#include "status_controller.h"
#include "new_controller.h"

using namespace zuluide::control;

void ControlInterface::SetDisplayController(StdDisplayController* dispController) {
  displayController = dispController;
  displayController->AddObserver([&](auto cur) { handleDisplayStateUpdate(cur); });
}

void ControlInterface::RotaryUpdate(int offset) {
  logmsg("Rotary update received: ", offset);
  switch(currentDisplayMode) {
  case Mode::Eject: {
    displayController->GetEjectController().MoveToNextEntry();
    break;
  }
    
  case Mode::Status: {
    int cur = offset;
    while (cur > 0) {
      displayController->GetStatusController().DecreaseImageNameOffset();
      cur--;
    }
    
    while (cur < 0) {
      displayController->GetStatusController().IncrementImageNameOffset();
      cur++;
    }
    
    break;
  }

  case Mode::Menu: {
    int cur = offset;
    while (cur > 0) {
      displayController->GetMenuController().MoveToNextEntry();
      cur--;
    }
    
    while (cur < 0) {
      displayController->GetMenuController().MoveToPreviousEntry();
      cur++;
    }
    
    break;
  }

  case Mode::Select: {
    int cur = offset;
    displayController->GetSelectController().ResetImageNameOffset();
    while (cur > 0) {
      displayController->GetSelectController().GetNextImageEntry();
      cur--;
    }
    
    while (cur < 0) {
      displayController->GetSelectController().GetPreviousImageEntry();
      cur++;
    }
    
    break;
  }

  case Mode::NewImage: {
    displayController->GetNewController().CreateAndSelect();
    break;
  }

  case Mode::Info: {
    int cur = offset;
    while (cur > 0) {
      displayController->GetInfoController().DecreaseFirmwareOffset();
      cur--;
    }
    
    while (cur < 0) {
      displayController->GetInfoController().IncrementFirmwareOffset();
      cur++;
    }
    
    break;
  }
  }
}

void ControlInterface::RotaryButtonPressed() {
  logmsg("Rotary Button Pressed");
  switch(currentDisplayMode) {
  case Mode::Status: {
    displayController->GetStatusController().ChangeToMenu();
    break;
  }

  case Mode::Menu: {
    displayController->GetMenuController().ChangeToSelectedEntry();
    break;
  }

  case Mode::Eject: {
    displayController->GetEjectController().DoSelectedEntry();
    break;
  }

  case Mode::Select: {
    displayController->GetSelectController().SelectImage();
    break;
  }

  case Mode::NewImage: {
    displayController->GetNewController().CreateAndSelect();
    break;
  }

  case Mode::Info: {
    displayController->SetMode(Mode::Status);
    break;
  }
  }
}

void ControlInterface::PrimaryButtonPressed() {
  logmsg("Primary Button Pressed");
  switch(currentDisplayMode) {
  case Mode::Select: {
    displayController->GetSelectController().DecreaseImageNameOffset();
    break;
  }
  default:
    break;      
  }
}

void ControlInterface::SecondaryButtonPressed() {
  logmsg("Secondary Button Pressed");
  switch(currentDisplayMode) {
  case Mode::Select: {
    displayController->GetSelectController().IncrementImageNameOffset();
    break;
  }

  case Mode::Status: {
    if (currentStatus.HasLoadedImage()) {
      displayController->SetMode(Mode::Eject);
    }
    
    break;
  }
    
  default:
    break;      
  }
}

void ControlInterface::handleSystemStatusUpdate(const zuluide::status::SystemStatus& current) {
  // Make a copy and move into member.
  currentStatus = std::move(zuluide::status::SystemStatus(current));
}

void ControlInterface::SetStatusController(zuluide::status::StatusController* statController) {
  statController->AddObserver([&](auto cur) { handleSystemStatusUpdate(cur); });
  statusController = statController;
}

void ControlInterface::handleDisplayStateUpdate(const DisplayState& current) {
  currentDisplayMode = current.GetCurrentMode();
}
