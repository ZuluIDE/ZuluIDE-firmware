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
#include <memory>
#include <functional>
#include <vector>
#include <zuluide/observable.h>
#include "status_controller.h"
#include "menu_controller.h"
#include "eject_controller.h"
#include "select_controller.h"
#include "new_controller.h"
#include "info_controller.h"

namespace zuluide::control {
  
  class StdDisplayController : public Observable<DisplayState> {
  public:
    StdDisplayController(zuluide::status::StatusController* statCtrlr);
    void AddObserver(std::function<void(const DisplayState& current)> callback);
    Mode GetMode() const;
    zuluide::status::StatusController& GetStatController();
    zuluide::control::StatusController& GetStatusController();
    MenuController& GetMenuController();
    EjectController& GetEjectController();
    SelectController& GetSelectController();
    NewController& GetNewController();
    InfoController& GetInfoController();
    void UpdateState(StatusState& newState);
    void UpdateState(MenuState& newState);
    void UpdateState(SelectState& newState);
    void UpdateState(EjectState& newState);
    void UpdateState(NewImageState& newState);
    void UpdateState(InfoState& newState);
    void MoveToSplash();
    void SetMode(Mode value);   
  private:
    void notifyObservers();    
    std::vector<std::function<void(const DisplayState& current)>> observers;
    DisplayState currentState;
    std::unique_ptr<zuluide::control::StatusController> statusController;
    std::unique_ptr<MenuController> menuController;
    std::unique_ptr<EjectController> ejectController;
    std::unique_ptr<SelectController> selectController;
    std::unique_ptr<NewController> newController;
    zuluide::status::StatusController* statController;
    std::unique_ptr<InfoController> infoController;
    void systemStatusUpdated(const zuluide::status::SystemStatus& current);
  };
}
