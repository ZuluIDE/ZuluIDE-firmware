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

SelectState::SelectState (int imgNameOffset) : imageNameOffset(imgNameOffset), isShowingBack(false), atEnd(false), atStart(false) {}

SelectState::SelectState (const SelectState& src) : imageNameOffset(src.imageNameOffset), isShowingBack(src.isShowingBack), atEnd(src.atEnd), atStart(src.atStart) {
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
  atEnd = src.atEnd;
  atStart = src.atStart;
  
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

bool SelectState::AtEnd() const {
  return atEnd;
}

void SelectState::SetAtEnd(bool value) {
  atEnd = value;
}

bool SelectState::AtStart() const {
  return atStart;
}

void SelectState::SetAtStart(bool value) {
  atStart = value;
}
