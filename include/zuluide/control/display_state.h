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

#pragma once

#include <zuluide/images/image.h>
#include "status_state.h"
#include "menu_state.h"
#include "eject_state.h"
#include "select_state.h"
#include "new_image_state.h"
#include "about_state.h"

using namespace zuluide::images;

namespace zuluide::control {
  enum class Mode { Status, Menu, Eject, Select, NewImage };

  class DisplayState {
  public:
    DisplayState(const StatusState &state);
    DisplayState(MenuState &state);
    DisplayState(SelectState &state);
    DisplayState(NewImageState &state);
    DisplayState(EjectState &state);
    DisplayState();
    DisplayState(const DisplayState& state);
    DisplayState(DisplayState&& src);
    DisplayState& operator=(DisplayState&& state);
    Mode GetCurrentMode() const;
    const MenuState& GetMenuState() const;
    const EjectState& GetEjectState() const;
    const SelectState& GetSelectState() const;
    const StatusState& GetStatusState() const;
  private:
    Mode currentMode;
    StatusState statusState;
    MenuState menuState;
    SelectState selectState;
    NewImageState newImageState;
    EjectState ejectState;
  };
}
