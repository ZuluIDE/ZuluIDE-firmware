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

#include "select_widget.h"

#define SELECT_IMAGE_MENU_TEXT "-- Select Image --"

static void truncate(std::string& toProcess);
static std::string makeImageSizeStr(uint64_t size);

using namespace zuluide;

SelectWidget::SelectWidget(Adafruit_SSD1306 *g, Rectangle b, Size cb) :
  Widget(g, b),
  image {g, b.MakeCentered({b.size.width, cb.height})}
{
  charBounds = cb;
  image.SetCenterStationaryText(true);
}

void SelectWidget::Update (const zuluide::control::DisplayState &disp) {
  if (!disp.GetSelectState().IsShowingBack() && disp.GetSelectState().HasCurrentImage()) {
    auto img = disp.GetSelectState().GetCurrentImage();
    image.SetToDisplay(img.GetFilename().c_str());
  } else {
    image.SetToDisplay("[Back]");
  }

  Widget::Update(disp);
}

bool SelectWidget::Refresh () {
  return image.CheckAndUpdateScrolling(get_absolute_time());
}

void SelectWidget::Display () {
  graph->setTextColor(WHITE, BLACK);
  
  DrawCenteredTextAt (SELECT_IMAGE_MENU_TEXT, 0);

  image.Display();

  if (!currentDispState->GetSelectState().IsShowingBack() && currentDispState->GetSelectState().HasCurrentImage()) {
    auto img = currentDispState->GetSelectState().GetCurrentImage();

    auto size = img.GetFileSizeBytes();
    if (size != 0) {
      auto sizeStr = makeImageSizeStr(size);      
      auto toShow = sizeStr.c_str();
      DrawCenteredTextAt(toShow, bounds.topLeft.y + bounds.size.height / 2 + charBounds.height);
    }
  }
}

static void truncate(std::string& toProcess) {
  auto idx = toProcess.find('.');
  if (idx != std::string::npos && idx + 2 < toProcess.length()) {
    toProcess.erase(idx+2);
  }
}

static std::string makeImageSizeStr(uint64_t size) {
  if (size > 1073741824) {
    auto sizeStr = std::to_string(size / 1073741824.0d);
    truncate(sizeStr);
    sizeStr += " GB";
    return sizeStr;
  } else if (size > 1048576) {
    auto sizeStr = std::to_string(size / 1048576.0d);
    truncate(sizeStr);
    sizeStr += " MB";
    return sizeStr;
  } else {
    auto sizeStr = std::to_string(size);
    truncate(sizeStr);
    sizeStr += " B";
    return sizeStr;
  }
}
