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

#include "info_widget.h"

#define INFO_MENU_TEXT "-- About --"

#define ZULUIDE_TITLE "ZuluIDE"

using namespace zuluide;

InfoWidget::InfoWidget(Adafruit_SSD1306 *g, Rectangle b, Size cb) :
  Widget(g, b),
  firmwareversion {g, b.MakeCenteredAt(b.Bottom() - cb.height - 1, {b.size.width, cb.height})}
{
  charBounds = cb;
}

void InfoWidget::Update (const zuluide::status::SystemStatus &status) {
  auto firmwareVersion = status.GetFirmwareVersion().c_str();
  firmwareversion.SetToDisplay(firmwareVersion);
  
  Widget::Update(status);
}

void InfoWidget::Update (const zuluide::control::DisplayState &disp) {
  firmwareversion.Reset();
  Widget::Update(disp);
}

bool InfoWidget::Refresh () {
  return firmwareversion.CheckAndUpdateScrolling(get_absolute_time());
}

void InfoWidget::Display () {
  graph->setTextColor(WHITE, BLACK);

  DrawCenteredTextAt (INFO_MENU_TEXT, 0);
  DrawCenteredText (ZULUIDE_TITLE);
  firmwareversion.Display();
}
