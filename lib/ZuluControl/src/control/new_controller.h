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

#include <zuluide/control/display_state.h>
#include "../status/status_controller.h"
#include "ui_controller_base.h"

namespace zuluide::control {
  class StdDisplayController;
  /**
     Controls state when the UI is creating a new image.
   */
  class NewController : public UIControllerBase<NewImageState> {
  public:
    NewController(StdDisplayController* cntrlr, zuluide::status::StatusController* statCtrlr);
    void IncrementImageIndex();
    void DecreaseImageIndex();
    void ResetImageIndex();
    void CreateAndSelect();
    virtual void Reset(const NewImageState& newState);
  private:
    NewImageState state;
    zuluide::status::StatusController *statusController;
  };
}
