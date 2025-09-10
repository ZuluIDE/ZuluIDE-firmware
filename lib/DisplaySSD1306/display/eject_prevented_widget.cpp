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

#include "eject_prevented_widget.h"
#include "ZuluIDE_log.h"

#define EJECT_MENU_TEXT "-- Host Eject Only --"
#define MENU_OFFSET 1

using namespace zuluide;

EjectPreventedWidget::EjectPreventedWidget(Adafruit_SSD1306 *g, Rectangle b, Size cb) :
  Widget(g, b)
{
  charBounds = cb;
}

void EjectPreventedWidget::Display () {
  graph->setTextColor(WHITE, BLACK);

  DrawCenteredTextAt (EJECT_MENU_TEXT, 0);

  const char* selectMenuText = " Back ";
  graph->setTextColor(BLACK, WHITE);
  DrawCenteredTextAt (selectMenuText, 16 + MENU_OFFSET);
  graph->setTextColor(WHITE, BLACK);
}
