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
    SplashState empty;
    UpdateState(empty);
    current = &splashController;
    splashController.Reset(empty);
    break;
  }

  case Mode::Status: {
    StatusState empty;
    UpdateState(empty);
    current = &statusController;
    statusController.Reset(empty);
    break;
  }

  case Mode::Menu: {
    MenuState empty(MenuState::Entry::Select);
    UpdateState(empty);
    current = &menuController;
    menuController.Reset(empty);
    break;
  }

  case Mode::Eject: {
    EjectState empty;
    UpdateState(empty);
    current = &ejectController;
    ejectController.Reset(empty);
    break;
  }

  case Mode::Select: {
    SelectState empty;
    UpdateState(empty);
    current = &selectController;
    selectController.Reset(empty);
    break;
  }

  case Mode::NewImage: {
    NewImageState empty;
    UpdateState(empty);
    current = &newController;
    newController.Reset(empty);
    break;
  }

  case Mode::Info: {
    InfoState empty;
    UpdateState(empty);
    current = &infoController;
    infoController.Reset(empty);
    break;
  }
  }
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

void StdDisplayController::UpdateState(SplashState& newState)
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
  return statusController;
}

MenuController& StdDisplayController::GetMenuController() {
  return menuController;
}

EjectController& StdDisplayController::GetEjectController() {
  return ejectController;
}

SelectController& StdDisplayController::GetSelectController() {
  return selectController;
}

NewController& StdDisplayController::GetNewController() {
  return newController;
}

InfoController& StdDisplayController::GetInfoController() {
  return infoController;
}

SplashController& StdDisplayController::GetSplashController() {
  return splashController;
}

StdDisplayController::StdDisplayController(zuluide::status::StatusController* statCtrlr) : statController(statCtrlr),
											   current(NULL),
											   menuController(this),
											   ejectController(this, statController),
											   selectController(this, statController),
											   newController(this, statController),
											   infoController(this),
											   splashController(this),
											   statusController(this) {
}

zuluide::status::StatusController& StdDisplayController::GetStatController() {
  return *statController;
}

void StdDisplayController::ProcessSystemStatusUpdate(zuluide::status::SystemStatus& currentStatus) {
  if (current) {
    current->SystemStatusUpdated(currentStatus);
  }
}

