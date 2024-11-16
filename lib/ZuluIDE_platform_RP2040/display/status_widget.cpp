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

#include "status_widget.h"

static std::string makeImageSizeStr(uint64_t size);

using namespace zuluide;

StatusWidget::StatusWidget(Adafruit_SSD1306 *g, Rectangle b, Size cb) :
  Widget(g, b),
  imagename {g, b.MakeCentered({b.size.width, cb.height})},
  deferred_load {g, b.MakeCenteredAt(24, {b.size.width, cb.height})}
{
  charBounds = cb;
}

void StatusWidget::Update (const zuluide::status::SystemStatus& status) {
  if (!currentSysStatus || !currentSysStatus->LoadedImagesAreEqual(status)) {    
    const char* filename = status.HasLoadedImage() ? status.GetLoadedImage().GetFilename().c_str() : "";
    imagename.SetToDisplay(filename);

    const char* message = status.IsDeferred() ? "To load image, eject device from host system" : "";
    deferred_load.SetToDisplay(message);
  }

  Widget::Update(status);
}

void StatusWidget::Update (const zuluide::control::DisplayState &disp) {
  imagename.Reset();
  deferred_load.Reset();
  Widget::Update(disp);
}

bool StatusWidget::Refresh () {
  bool update = imagename.CheckAndUpdateScrolling(get_absolute_time());
  update = deferred_load.CheckAndUpdateScrolling(get_absolute_time()) || update;
  return update;
}

void StatusWidget::Display () {
  graph->setTextColor(WHITE, BLACK);

  if (currentSysStatus->HasLoadedImage()) {
    imagename.Display();
    deferred_load.Display();

    auto size = currentSysStatus->GetLoadedImage().GetFileSizeBytes();
    if (size != 0) {
      auto sizeStr = makeImageSizeStr(size);
      auto toShow = sizeStr.c_str();
      DrawCenteredTextAt (toShow, 0);
    }

    // Draw the icon.
    auto dev_icon = currentSysStatus->GetDeviceType() == drive_type_t::DRIVE_TYPE_ZIP100 ? zipdrive_loaded : cdrom_loaded;
    graph->drawBitmap(0, 0, dev_icon, 18, 9, WHITE);

  } else if (!currentSysStatus->IsCardPresent()) {
    auto dev_icon = currentSysStatus->GetDeviceType() == drive_type_t::DRIVE_TYPE_ZIP100 ? zipdrive_empty : cdrom_empty;
    graph->drawBitmap(0, 0, dev_icon, 18, 9, WHITE);

    DrawCenteredText("[NO SD CARD]");
  } else {
    auto dev_icon = currentSysStatus->GetDeviceType() == drive_type_t::DRIVE_TYPE_ZIP100 ? zipdrive_empty : cdrom_empty;
    graph->drawBitmap(0, 0, dev_icon, 18, 9, WHITE);
    
    DrawCenteredText("[NO IMAGE]");
  }

  // Display primary/secondary
  const char* text = currentSysStatus->IsPrimary() ? "pri" : "sec";
  auto textSize = MeasureText(text);
  graph->setCursor(bounds.Right() - textSize.width, 0);
  graph->print(text);
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
