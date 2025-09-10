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

#include <zuluide/control/display_state.h>

using namespace zuluide::control;

DisplayState::DisplayState(const StatusState &state) :
  currentMode(Mode::Status),
  statusState(state)
{}

DisplayState::DisplayState(MenuState &state) :
  currentMode(Mode::Menu),
  menuState(state)
{}

DisplayState::DisplayState(SelectState &state) :
  currentMode(Mode::Select),
  selectState(state)
{}

DisplayState::DisplayState() :
  currentMode(Mode::Splash),
  splashState(SplashState())
{}

DisplayState::DisplayState(EjectState& state) :
  currentMode(Mode::Eject),
  ejectState(state)
{}

DisplayState::DisplayState(SplashState& state) :
  currentMode(Mode::Splash),
  splashState(state)
{}


DisplayState::DisplayState(EjectPreventedState& state) :
  currentMode(Mode::EjectPrevented),
  ejectPreventedState(state)
{}

DisplayState::DisplayState(const DisplayState& state) :
  currentMode(state.currentMode),
  statusState(state.statusState),
  menuState(state.menuState),
  selectState(state.selectState),
  ejectState(state.ejectState),
  infoState(state.infoState),
  splashState(state.splashState),
  ejectPreventedState(state.ejectPreventedState)
{}

DisplayState& DisplayState::operator=(DisplayState&& src) {
  currentMode = src.currentMode;
  statusState = src.statusState;
  menuState = std::move(src.menuState);
  selectState = std::move(src.selectState);
  ejectState = std::move(src.ejectState);
  ejectPreventedState = std::move(src.ejectPreventedState);
  infoState = std::move(src.infoState);
  splashState = std::move(src.splashState);
  return *this;
}

DisplayState::DisplayState(DisplayState&& src) :
  currentMode(src.currentMode)  
{
  statusState = std::move(src.statusState);
  menuState = std::move(src.menuState);
  selectState = std::move(src.selectState);
  ejectState = std::move(src.ejectState);
  splashState = std::move(src.splashState);
  ejectPreventedState = std::move(src.ejectPreventedState);
}

DisplayState::DisplayState(InfoState &state) :
  currentMode(Mode::Info), infoState(state) {
}

Mode DisplayState::GetCurrentMode() const {
  return currentMode;
}

const MenuState& DisplayState::GetMenuState() const {
  return menuState;
}

const EjectState& DisplayState::GetEjectState() const {
  return ejectState;
}

const EjectPreventedState& DisplayState::GetEjectPreventedState() const {
  return ejectPreventedState;
}


const SelectState& DisplayState::GetSelectState() const {
  return selectState;
}

const StatusState& DisplayState::GetStatusState() const {
  return statusState;
}

const InfoState& DisplayState::GetInfoState() const {
  return infoState;
}

