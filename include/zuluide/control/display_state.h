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

#include <memory>
#include <zuluide/images/image.h>

using namespace zuluide::images;

namespace zuluide::control {
  enum class Mode { Status, Menu, Eject, Select, NewImage };

  class StatusState {
  public:
    StatusState (int imgNameOffset = 0);
    StatusState (const StatusState& src);
    int GetImageNameOffset () const;
    void IncrementImageNameOffset();
    void DecrementImageNameOffset();
    void ResetImageNameOffset();
    StatusState& operator=(const StatusState& src);
  private:
    int imageNameOffset;
  };

  class MenuState {
  public:
    enum class Entry { Eject, Select, New, Back };
    MenuState (Entry value = Entry::Eject);
    MenuState (const MenuState& src);
    Entry GetCurrentEntry () const;
    void MoveToNextEntry();
    void MoveToPreviousEntry();
    MenuState& operator=(const MenuState& src);
  private:
    Entry currentEntry;
  };

  class EjectState {
  public:
    enum class Entry { Eject, Back };
    EjectState (Entry value = Entry::Eject);
    EjectState (const EjectState& src);
    Entry GetCurrentEntry () const;
    void MoveToNextCurrentEntry();
    EjectState& operator=(const EjectState& src);
  private:
    Entry currentEntry;
  };

  class SelectState {
  public:
    SelectState (int imageNameOffset = 0);
    SelectState (const SelectState& src);
    int GetImageNameOffset () const;
    void SetImageNameOffset(int value);
    SelectState& operator=(const SelectState& src);
    void SetCurrentImage(std::unique_ptr<zuluide::images::Image> image);
    const zuluide::images::Image& GetCurrentImage() const;
    bool HasCurrentImage() const;
    bool IsShowingBack() const;
    void SetIsShowingBack(bool value);
  private:
    int imageNameOffset;
    std::unique_ptr<zuluide::images::Image> currentImage;
    bool isShowingBack;
  };

  class NewImageState {
  public:
    NewImageState (int imgIndex = 0);
    NewImageState (const NewImageState& src);
    int GetImageIndex () const;
    NewImageState& operator++(int);
    NewImageState& operator--(int);
    NewImageState& operator=(const NewImageState& src);
  private:
    int imageIndex;
  };

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
