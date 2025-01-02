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

#include "splash_widget.h"
#include "ZuluIDE_log.h"

using namespace zuluide;

SplashWidget::SplashWidget(Adafruit_SSD1306 *g, Rectangle b) :
  Widget(g, b)
{
}

void SplashWidget::Update (const zuluide::status::SystemStatus &status) {
  Widget::Update(status);
}

void SplashWidget::Update (const zuluide::control::DisplayState &disp) {
  Widget::Update(disp);
}

bool SplashWidget::Refresh () {
  return false;
}

void SplashWidget::Display () {
  graph->drawBitmap(0, 0, logo, 128, 32, WHITE);
}
