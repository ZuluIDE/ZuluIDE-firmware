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

#include <zuluide/control/eject_state.h>

using namespace zuluide::control;

EjectState::EjectState (Entry value)
  : currentEntry(value) {
}

EjectState::EjectState (const EjectState& src)
  : currentEntry(src.currentEntry) {
}

EjectState::Entry EjectState::GetCurrentEntry () const {
  return currentEntry;
}

void EjectState::MoveToNextCurrentEntry() {
  if (currentEntry == Entry::Eject) {
    currentEntry = Entry::Back;
  } else {
    currentEntry = Entry::Eject;
  }
}

EjectState& EjectState::operator=(const EjectState& src) {
  currentEntry = src.currentEntry;
  return *this;
}
