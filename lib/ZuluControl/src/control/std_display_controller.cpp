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
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the ZuluIDE Hardware Support Library Exception
 * (GPL-3.0_HSL_Exception.md), as published by Rabbit Hole Computing™.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include "std_display_controller.h"
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

UIControllerBase* StdDisplayController::getControllerByMode(const Mode mode) {
  switch (mode) {  
  case Mode::Status: {
    return &statusController;
  }

  case Mode::Menu: {
    return &menuController;
  }

  case Mode::Eject: {
    return &ejectController;
  }

  case Mode::EjectPrevented: {
    EjectPreventedState empty;
    UpdateState(empty);
    ejectPreventedController.SetState(empty);
    return &ejectPreventedController;
  }
    
  case Mode::Select: {
    return &selectController;
  }

  case Mode::Info: {
    return &infoController;
  }

  case Mode::Splash:
  default: {
    return &splashController;
  }
  }
}

Mode StdDisplayController::GetMode() const
{
  return currentState.GetCurrentMode();
}

void StdDisplayController::SetMode(Mode newMode)
{
  current = getControllerByMode(newMode);
  auto x = current->Reset();
  currentState = std::move(x);
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

void StdDisplayController::UpdateState(EjectState& newState)
{
  // Copy the new state into a new memory location.
  currentState = std::move(DisplayState(newState));
  notifyObservers();
}

void StdDisplayController::UpdateState(EjectPreventedState& newState)
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

EjectPreventedController& StdDisplayController::GetEjectPreventedController() {
  return ejectPreventedController;
}

SelectController& StdDisplayController::GetSelectController() {
  return selectController;
}

InfoController& StdDisplayController::GetInfoController() {
  return infoController;
}

SplashController& StdDisplayController::GetSplashController() {
  return splashController;
}

StdDisplayController::StdDisplayController(zuluide::status::StatusController* statCtrlr, zuluide::pipe::ImageRequestPipe<select_controller_source_t>* imRqPipe,zuluide::pipe::ImageResponsePipe<select_controller_source_t>* imRsPipe) : 
                      statusController(this),
                      statController(statCtrlr),
                      current(NULL),
                      menuController(this, statController),
                      ejectController(this, statController),
                      ejectPreventedController(this, statController),
                      selectController(this, statController, imRqPipe, imRsPipe),
                      infoController(this),
                      splashController(this) {
}

zuluide::status::StatusController& StdDisplayController::GetStatController() {
  return *statController;
}

void StdDisplayController::ProcessSystemStatusUpdate(zuluide::status::SystemStatus& newStatus) {
  currentStatus = newStatus;
  if (current) {
    current->SystemStatusUpdated(currentStatus);
  }
}

const zuluide::status::SystemStatus& StdDisplayController::GetCurrentStatus() const {
  return currentStatus;
}
