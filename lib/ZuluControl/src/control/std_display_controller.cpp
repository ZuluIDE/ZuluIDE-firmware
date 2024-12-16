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

#include "std_display_controller.h"
#include "new_controller.h"
#include "eject_controller.h"
#include "menu_controller.h"
#include "info_controller.h"
#include "ZuluIDE_log.h"

#include <functional>
#include <algorithm>

using namespace zuluide::control;

void StdDisplayController::AddObserver(std::function<void(const DisplayState& current)> callback) {
  observers.push_back(callback);
}

void StdDisplayController::SetMode(Mode newMode)
{
  switch (newMode) {
  case Mode::Splash: {
    MoveToSplash();
    break;
  }
    
  case Mode::Status: {
    StatusState empty;
    UpdateState(empty);
    statusController->Reset(empty);
    break;
  }
    
  case Mode::Menu: {
    MenuState empty(MenuState::Entry::Select);
    UpdateState(empty);
    menuController->Reset(empty);
    break;
  }
    
  case Mode::Eject: {
    EjectState empty;
    UpdateState(empty);
    ejectController->Reset(empty);
    break;
  }
    
  case Mode::Select: {
    SelectState empty;
    UpdateState(empty);
    selectController->Reset(empty);
    break;
  }
    
  case Mode::NewImage: {
    NewImageState empty;
    UpdateState(empty);
    newController->Reset(empty);
    break;
  }

  case Mode::Info: {
    InfoState empty;
    UpdateState(empty);
    infoController->Reset(empty);
    break;
  }
  }
}

void StdDisplayController::MoveToSplash() {
  currentState = std::move(DisplayState());
  notifyObservers();
}

void StdDisplayController::UpdateState(StatusState& newState)
{
  // Copy the new state into a new memory location.
  currentState = std::move(DisplayState(newState));
  notifyObservers();
}

void StdDisplayController::UpdateState(MenuState& newState)
{
  // Copy the new state into a new memory location.
  currentState = std::move(DisplayState(newState));
  notifyObservers(); 
}

void StdDisplayController::UpdateState(SelectState& newState)
{
  // Copy the new state into a new memory location.
  currentState = std::move(DisplayState(newState));
  notifyObservers();  
}

void StdDisplayController::UpdateState(NewImageState& newState)
{
  // Copy the new state into a new memory location.
  currentState = std::move(DisplayState(newState));
  notifyObservers();
}

void StdDisplayController::UpdateState(EjectState& newState)
{
  // Copy the new state into a new memory location.
  currentState = std::move(DisplayState(newState));
  notifyObservers();
}

void StdDisplayController::UpdateState(InfoState& newState)
{
  // Copy the new state into a new memory location.
  currentState = std::move(DisplayState(newState));
  notifyObservers();
}

void StdDisplayController::notifyObservers() {
  std::for_each(observers.begin(), observers.end(), [this](auto observer) {
    // Make a copy so observers cannot mutate system state.
    // This may be overly conservative if we do not do multi-threaded work
    // and we do not mutate system state in observers. This could be easily
    // verified given this isn't a public API.
    observer(DisplayState(currentState));
  });  
}

StatusController& StdDisplayController::GetStatusController() {
  return *statusController;
}

MenuController& StdDisplayController::GetMenuController() {
  return *menuController;
}

EjectController& StdDisplayController::GetEjectController() {
  return *ejectController;
}

SelectController& StdDisplayController::GetSelectController() {
  return *selectController;
}

NewController& StdDisplayController::GetNewController() {
  return *newController;
}

InfoController& StdDisplayController::GetInfoController() {
  return *infoController;
}

StdDisplayController::StdDisplayController(zuluide::status::StatusController* statCtrlr) : statController(statCtrlr) {
  statusController = std::make_unique<StatusController>(this);
  menuController = std::make_unique<MenuController>(this);
  ejectController = std::make_unique<EjectController>(this, statController);
  selectController = std::make_unique<SelectController>(this, statController);
  newController = std::make_unique<NewController>(this, statController);
  infoController = std::make_unique<InfoController>(this);
}

zuluide::status::StatusController& StdDisplayController::GetStatController() {
  return *statController;
}
