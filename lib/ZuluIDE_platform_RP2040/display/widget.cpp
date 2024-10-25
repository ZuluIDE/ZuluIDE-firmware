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

#include "widget.h"

using namespace zuluide;

Widget::Widget (Adafruit_SSD1306 *g, Rectangle b) : graph(g), bounds(b) { }

void Widget::DrawCenteredText (const char* text) {
  // Make a text box centered inside of the bounds.
  auto textBox = bounds.MakeCentered(MeasureText(text));

  // Update cursor and print.
  graph->setCursor(textBox.topLeft.x, textBox.topLeft.y);
  graph->print(text);
}

void Widget::DrawCenteredTextAt (const char* text, int y) {
  // Make a text box centered inside of the bounds.
  auto textBox = bounds.MakeCenteredAt(y, MeasureText(text));

  // Update cursor and print.
  graph->setCursor(textBox.topLeft.x, textBox.topLeft.y);
  graph->print(text);
}

Size Widget::MeasureText (const char* text) {
  int16_t x=0, y=0;
  uint16_t h=0, w=0;
  // Measure the text.
  graph->getTextBounds(text, 0 ,0, &x, &y, &w, &h);
  return {w,  h};
}

void Widget::Update (const zuluide::status::SystemStatus& status) {
  currentSysStatus = std::make_unique<zuluide::status::SystemStatus> (status);
}

void Widget::Update (const zuluide::control::DisplayState &disp) {
  currentDispState = std::make_unique<zuluide::control::DisplayState> (disp);
}

bool Widget::Refresh () {
  return false;
}
