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

#include <zuluide/control/display_state.h>

using namespace zuluide::control;

StatusState::StatusState (int imgNameOffset) : imageNameOffset(imgNameOffset) { }

StatusState::StatusState (const StatusState& src) : imageNameOffset(src.imageNameOffset) { }

int StatusState::GetImageNameOffset () const {
  return imageNameOffset;
}

void StatusState::IncrementImageNameOffset() {
  imageNameOffset++;
}

void StatusState::DecrementImageNameOffset() {
  imageNameOffset--;
  if (imageNameOffset < 0) {
    imageNameOffset = 0;
  }
}

void StatusState::ResetImageNameOffset() {
  imageNameOffset = 0;
}

StatusState& StatusState::operator=(const StatusState& src) {
  imageNameOffset = src.imageNameOffset;
  return *this;
}

MenuState::MenuState (MenuState::Entry value) : currentEntry(value) { }

MenuState::MenuState (const MenuState &src) : currentEntry(src.currentEntry) { }

MenuState::Entry MenuState::GetCurrentEntry () const {
  return currentEntry;
}

MenuState& MenuState::operator=(const MenuState& src) {
  currentEntry = src.currentEntry;
  return *this;
}

void MenuState::MoveToNextEntry() {
  switch (currentEntry) {
  case Entry::Eject: {
    currentEntry = Entry::New;
    break;
  }
    
  case Entry::Select: {
    currentEntry = Entry::Eject;
    break;
  }
    
  case Entry::New: {
    currentEntry = Entry::Back;
    break;
  }
    
  case Entry::Back: {
    currentEntry = Entry::Select;
    break;
  }
  }
}

void MenuState::MoveToPreviousEntry() {
  switch (currentEntry) {
  case Entry::Eject: {
    currentEntry = Entry::Back;
    break;
  }
    
  case Entry::Select: {
    currentEntry = Entry::Eject;
    break;
  }
    
  case Entry::New: {
    currentEntry = Entry::Select;
    break;
  }
    
  case Entry::Back: {
    currentEntry = Entry::New;
    break;
  }
  }
}

SelectState::SelectState (int imgNameOffset) : imageNameOffset(imgNameOffset), isShowingBack(false) {}

SelectState::SelectState (const SelectState& src) : imageNameOffset(src.imageNameOffset), isShowingBack(src.isShowingBack) {
  if (src.currentImage) {
    currentImage = std::make_unique<Image>(*src.currentImage);
  }
}

int SelectState::GetImageNameOffset () const {
  return imageNameOffset;
}

SelectState& SelectState::operator=(const SelectState& src) {
  imageNameOffset = src.imageNameOffset;
  isShowingBack = src.isShowingBack;
  if (src.currentImage) {
    currentImage = std::make_unique<Image>(*src.currentImage);
  } else {
    currentImage = nullptr;
  }
  
  return *this;
}

void SelectState::SetImageNameOffset(int value) {
  imageNameOffset = value;
}

void SelectState::SetCurrentImage(std::unique_ptr<zuluide::images::Image> image) {
  currentImage = std::move(image);
}

const zuluide::images::Image& SelectState::GetCurrentImage() const {
  return *currentImage;
}

bool SelectState::HasCurrentImage() const {
  return currentImage ? true : false;
}

bool SelectState::IsShowingBack() const {
  return isShowingBack;
}

void SelectState::SetIsShowingBack(bool value) {
  isShowingBack = value;
}

NewImageState::NewImageState (int imgIndex) : imageIndex (imgIndex) {}

NewImageState::NewImageState (const NewImageState& src) : imageIndex(src.imageIndex) {}

int NewImageState::GetImageIndex () const {
  return imageIndex;
}

NewImageState& NewImageState::operator=(const NewImageState& src) {
  imageIndex = src.imageIndex;
  return *this;
}

NewImageState& NewImageState::operator++(int) {
  imageIndex++;
  return *this;
}

NewImageState& NewImageState::operator--(int) {
  imageIndex--;
  return *this;
}

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

DisplayState::DisplayState(NewImageState &state) :
  currentMode(Mode::NewImage),
  newImageState(state)
{}

DisplayState::DisplayState() :
  currentMode(Mode::Eject),
  ejectState(EjectState())
{}

DisplayState::DisplayState(EjectState& state) :
  currentMode(Mode::Eject),
  ejectState(state)
{}


DisplayState::DisplayState(const DisplayState& state) :
  currentMode(state.currentMode),
  statusState(state.statusState),
  menuState(state.menuState),
  selectState(state.selectState),
  newImageState(state.newImageState),
  ejectState(state.ejectState)
{}

DisplayState& DisplayState::operator=(DisplayState&& src) {
  currentMode = src.currentMode;
  statusState = src.statusState;
  menuState = std::move(src.menuState);
  selectState = std::move(src.selectState);
  newImageState = std::move(src.newImageState);
  ejectState = std::move(src.ejectState);
  return *this;
}

DisplayState::DisplayState(DisplayState&& src) :
  currentMode(src.currentMode)  
{
  statusState = std::move(src.statusState);
  menuState = std::move(src.menuState);
  selectState = std::move(src.selectState);
  newImageState = std::move(src.newImageState);
  ejectState = std::move(src.ejectState);
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

const SelectState& DisplayState::GetSelectState() const {
  return selectState;
}

const StatusState& DisplayState::GetStatusState() const {
  return statusState;
}

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
