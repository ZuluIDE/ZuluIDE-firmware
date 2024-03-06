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

static const char* toString(const zuluide::control::MenuState::Entry value);
static const char* toString(const zuluide::control::EjectState::Entry value);
static uint16_t centerText(const char* text, Adafruit_SSD1306& graph);

DisplaySSD1306::DisplaySSD1306()
{
}

void DisplaySSD1306::init(TwoWire* wire) {
  m_wire = wire;
  m_wire->begin();
  graph = Adafruit_SSD1306(128, 32, m_wire, -1, 400000, 100000);
  m_i2c_addr = SS1306_ADDR;

  if(graph.begin(SSD1306_SWITCHCAPVCC, m_i2c_addr, false, false)) {
    int16_t x=0, y=0;
    uint16_t h=0, w=0;
    graph.getTextBounds("W", 0 ,0, &x, &y, &w, &h);
    if(h > 0 && w > 0) {
      lineHeight = h;
      lineCount = 32 / h;
      lineLength = 128 / w;
      centerBase = (32 - h) / 2;
    }
    logmsg("lineHeight: ", lineHeight, " lineCount: ", lineCount);
    graph.setTextWrap(false);

    graph.clearDisplay();
    graph.setTextColor(WHITE, BLACK);
    graph.setCursor(0, lineCount / 2);    
    graph.print("Initializing");
    graph.display();
  } else {
    logmsg("gfx.begin failed.");
  }
}

void DisplaySSD1306::HandleUpdate(const SystemStatus& current) {
  currentSysStatus = std::make_unique<SystemStatus>(current);
  updateDisplay();
}

void DisplaySSD1306::HandleUpdate(const zuluide::control::DisplayState& current) {
  currentDispState = std::make_unique<zuluide::control::DisplayState>(current);
  updateDisplay();
}

void DisplaySSD1306::updateDisplay() {
  if (currentDispState && currentSysStatus) {
    switch (currentDispState->GetCurrentMode()) {
    case zuluide::control::Mode::Status: {
      displayStatus();
      break;
    }
    case zuluide::control::Mode::Menu:
      displayMenu();
      break;
    case zuluide::control::Mode::Eject:
      displayEject();
      break;
    case zuluide::control::Mode::Select:
      displaySelect();
      break;
    case zuluide::control::Mode::NewImage:
      break;
    }
  } else {
    // Show loading message.
    if (!currentDispState && !currentSysStatus) {
      logmsg("Received an update when display and system status is not set.");
    } else if (!currentDispState) {
      logmsg("Received an update when display is not set.");
    } else {
      logmsg("Received an update when system status is not set.");
    }
  }
}

void DisplaySSD1306::displayStatus() {
  graph.clearDisplay();  
  graph.setTextColor(WHITE, BLACK);
  graph.setCursor(32, centerBase);
  if (currentSysStatus->HasLoadedImage()) {
    graph.drawBitmap(0, 8, cdrom_loaded, 32, 16, WHITE);
    int offset = currentDispState->GetStatusState().GetImageNameOffset();
    const char* filename = currentSysStatus->GetLoadedImage().GetFilename().c_str();
    if (offset < strlen(filename)) {
      graph.print(filename + offset);
    } else {
      graph.print(filename + (strlen(filename) - 1));
    }
  } else {
    graph.drawBitmap(0, 8, cdrom_empty, 32, 16, WHITE);
    graph.print("[NO IMAGE]");
  }
  
  graph.display();
}

void DisplaySSD1306::displayMenu() {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);

  const char* menuText = toString(currentDispState->GetMenuState().GetCurrentEntry());
  graph.setCursor(centerText(menuText, graph), centerBase);
  graph.print(menuText);
  graph.display();
}

void DisplaySSD1306::displayEject() {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);

  const char* menuText = toString(currentDispState->GetEjectState().GetCurrentEntry());
  graph.setCursor(centerText(menuText, graph), centerBase);
  graph.print(menuText);

  graph.display();
}

void DisplaySSD1306::displaySelect() {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);

  const char* toShow;
  if (!currentDispState->GetSelectState().IsShowingBack() && currentDispState->GetSelectState().HasCurrentImage()) {
    toShow = currentDispState->GetSelectState().GetCurrentImage().GetFilename().c_str();
  } else {
    toShow = "[Back]";
  }

  graph.setCursor(centerText(toShow, graph), centerBase);
  graph.print(toShow);

  graph.display();
}

static uint16_t centerText(const char* text, Adafruit_SSD1306& graph) {
  int16_t x=0, y=0;
  uint16_t h=0, w=0;
  graph.getTextBounds(text, 0 ,0, &x, &y, &w, &h);

  if (w > 128) {
    // Ensure we do not return a negative number.
    w = 128;
  }

  return (128 - w) / 2;
}

static const char* toString(const zuluide::control::MenuState::Entry value) {
  switch (value) {
  case zuluide::control::MenuState::Entry::Eject:
    return "[ EJECT ]";
  case zuluide::control::MenuState::Entry::Select:
    return "[ SELECT ]";
  case zuluide::control::MenuState::Entry::New:
    return "[ NEW ]";
  case zuluide::control::MenuState::Entry::Back:
    return "[ BACK ]";
  default:
    return "ERROR";
  }
}

static const char* toString(const zuluide::control::EjectState::Entry value) {
  switch (value) {
  case zuluide::control::EjectState::Entry::Eject:
    return "Yes";
  default:
    return "No";
  }
}
