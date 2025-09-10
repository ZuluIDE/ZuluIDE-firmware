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

#include "menu_widget.h"

using namespace zuluide;

#define MENU_OFFSET 1

static const char* toString(const zuluide::control::MenuState::Entry value);

MenuWidget::MenuWidget(Adafruit_SSD1306 *g, Rectangle b, Size cb) :
  Widget(g, b)
{
  charBounds = cb;
}

void MenuWidget::Display () {
  graph->setTextColor(WHITE, BLACK);

  DrawCenteredTextAt ("-- Menu --", 0);

  const char* selectMenuText = toString(zuluide::control::MenuState::Entry::Select);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Select) {
    graph->setTextColor(BLACK, WHITE);
  }
  
  auto entrySize = MeasureText (selectMenuText);
  graph->setCursor(bounds.size.width * .25 - entrySize.width / 2, 8 + MENU_OFFSET);
  graph->print(selectMenuText);
  graph->setTextColor(WHITE, BLACK);

  selectMenuText = toString(zuluide::control::MenuState::Entry::Eject);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Eject) {
    graph->setTextColor(BLACK, WHITE);
  }

  entrySize = MeasureText (selectMenuText);
  graph->setCursor(bounds.size.width * .75 - entrySize.width / 2, 8 + MENU_OFFSET);
  graph->print(selectMenuText);
  graph->setTextColor(WHITE, BLACK);

  selectMenuText = toString(zuluide::control::MenuState::Entry::Info);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Info) {
    graph->setTextColor(BLACK, WHITE);
  }

  entrySize = MeasureText (selectMenuText);
  graph->setCursor(bounds.size.width * .25 - entrySize.width / 2, 24 + MENU_OFFSET);
  graph->print(selectMenuText);
  graph->setTextColor(WHITE, BLACK);

  selectMenuText = toString(zuluide::control::MenuState::Entry::Back);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Back) {
    graph->setTextColor(BLACK, WHITE);
  }

  entrySize = MeasureText (selectMenuText);
  graph->setCursor(bounds.size.width * .75 - entrySize.width / 2, 24 + MENU_OFFSET);
  graph->print(selectMenuText);
}

static const char* toString(const zuluide::control::MenuState::Entry value) {
  switch (value) {
  case zuluide::control::MenuState::Entry::Eject:
    return "[ EJECT ]";
  case zuluide::control::MenuState::Entry::Select:
    return "[ SELECT ]";
  case zuluide::control::MenuState::Entry::Back:
    return "[ BACK ]";
  case zuluide::control::MenuState::Entry::Info:
    return "[ INFO ]";
  default:
    return "ERROR";
  }
}
