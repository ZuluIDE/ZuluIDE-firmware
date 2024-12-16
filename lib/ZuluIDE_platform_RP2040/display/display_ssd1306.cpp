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

#include "display_ssd1306.h"
#include "ZuluIDE_log.h"

using namespace zuluide;
using namespace zuluide::status;

#ifndef SS1306_ADDR
#define SS1306_ADDR 0x3c
#endif

#ifndef SCROLL_INTERVAL_MS
#define SCROLL_INTERVAL_MS 60
#endif

#ifndef SCROLL_START_DELAY_MS
#define SCROLL_START_DELAY_MS 1000
#endif

#define WIDTH 128
#define HEIGHT 32

DisplaySSD1306::DisplaySSD1306() : nextRefresh(at_the_end_of_time)
{
}

void DisplaySSD1306::init(TwoWire* wire) {
  m_wire = wire;
  graph = Adafruit_SSD1306(WIDTH, HEIGHT, m_wire, -1, 400000, 100000);
  m_i2c_addr = SS1306_ADDR;

  if(graph.begin(SSD1306_SWITCHCAPVCC, m_i2c_addr, false, false)) {
    int16_t x=0, y=0;
    uint16_t h=0, w=0;
    graph.getTextBounds("W", 0 ,0, &x, &y, &w, &h);
    wBounds = {h, w};
    graph.setTextWrap(false);

    currentWidget = std::make_unique<zuluide::SplashWidget>(&graph, Rectangle{{0,0}, {WIDTH, HEIGHT}});
    updateDisplay();
  } else {
    logmsg("gfx.begin failed.");
  }
}

void DisplaySSD1306::HandleUpdate(const SystemStatus& current) {
  if (currentWidget) {
    currentWidget->Update(current);
  }

  currentSysStatus = std::make_unique<SystemStatus>(current);
  updateDisplay();
}

void DisplaySSD1306::HandleUpdate(const zuluide::control::DisplayState& current) {
  // Create the correct widget and assigned it to currentWidget, if there is a change in state..
  if (currentDispState->GetCurrentMode() != current.GetCurrentMode()) {
    switch (current.GetCurrentMode()) {
    case zuluide::control::Mode::Splash: {
      currentWidget = std::make_unique<zuluide::SplashWidget>(&graph, Rectangle{{0,0}, {WIDTH, HEIGHT}});
      break;
    }
    case zuluide::control::Mode::Status: {
      currentWidget = std::make_unique<zuluide::StatusWidget>(&graph, Rectangle{{0,0}, {WIDTH, HEIGHT}}, wBounds);
      break;
    }
    case zuluide::control::Mode::Eject:
      currentWidget = std::make_unique<zuluide::EjectWidget>(&graph, Rectangle{{0,0}, {WIDTH, HEIGHT}}, wBounds);
      break;
    case zuluide::control::Mode::Info: {
      currentWidget = std::make_unique<zuluide::InfoWidget>(&graph, Rectangle{{0,0}, {WIDTH, HEIGHT}}, wBounds);
      break;
    }
    case zuluide::control::Mode::Menu: {
      currentWidget = std::make_unique<zuluide::MenuWidget>(&graph, Rectangle{{0,0}, {WIDTH, HEIGHT}}, wBounds);
      break;
    }
    case zuluide::control::Mode::Select: {
      currentWidget = std::make_unique<zuluide::SelectWidget>(&graph, Rectangle{{0,0}, {WIDTH, HEIGHT}}, wBounds);
      break;
    }
    default: {
      currentWidget = nullptr;
      break;
    }
    }
  }

  if (currentWidget) {
    currentWidget->Update(current);
    if (currentSysStatus) {
      currentWidget->Update(*currentSysStatus);
    }
  }

  currentDispState = std::make_unique<zuluide::control::DisplayState>(current);
  updateDisplay();
}

void DisplaySSD1306::updateDisplay() {
    graph.clearDisplay();
    currentWidget->Display();
    graph.display();
    nextRefresh = make_timeout_time_ms(SCROLL_INTERVAL_MS);
}

void DisplaySSD1306::Refresh() {
  if (absolute_time_diff_us (get_absolute_time(), nextRefresh) > 0) {
    return;
  }

  nextRefresh = make_timeout_time_ms(SCROLL_INTERVAL_MS);

  if (currentDispState && currentSysStatus && currentWidget->Refresh()) {
    graph.clearDisplay();
    currentWidget->Display();
    graph.display();    
  }
}
