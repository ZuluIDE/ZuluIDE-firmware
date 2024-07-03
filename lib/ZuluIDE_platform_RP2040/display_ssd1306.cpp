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

#define IMAGE_NAME_SPACING 5

static const char* toString(const zuluide::control::MenuState::Entry value);
static const char* toString(const zuluide::control::EjectState::Entry value);
static uint16_t centerText(const char* text, Adafruit_SSD1306& graph);
static uint16_t centerText(const std::string& text, Adafruit_SSD1306& graph);
static void truncate(std::string& toProcess);
static std::string makeImageSizeStr(uint64_t size);

DisplaySSD1306::DisplaySSD1306() : nextRefresh(at_the_end_of_time), scrollText(true)
{
}

void DisplaySSD1306::init(TwoWire* wire) {
  m_wire = wire;
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
      displayStatus(false);
      break;
    }
    case zuluide::control::Mode::Menu:
      displayMenu();
      break;
    case zuluide::control::Mode::Eject:
      displayEject();
      break;
    case zuluide::control::Mode::Select:
      displaySelect(false);
      break;
    case zuluide::control::Mode::NewImage:
      break;
    case zuluide::control::Mode::Info: {
      displayInfo(false);
      break;
    }
    }

    // Set the time of the next refresh.
    nextRefresh = make_timeout_time_ms(SCROLL_INTERVAL_MS);
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

void DisplaySSD1306::displayStatus(bool isRefresh) {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);
  graph.setCursor(32, centerBase);
  if (currentSysStatus->HasLoadedImage()) {
    int offset = currentDispState->GetStatusState().GetImageNameOffset();
    const char* filename = currentSysStatus->GetLoadedImage().GetFilename().c_str();
    if (!isRefresh) {
      // If this isn't a refresh, then store the width of the image name.
      int16_t x=0, y=0;
      uint16_t h=0;
      graph.getTextBounds(filename, 0 ,0, &x, &y, &imageNameWidthPixels, &h);
      imageNameWidthPixels += IMAGE_NAME_SPACING;
      imageNameOffsetPixels = 0;
    }

    if (scrollText) {
      auto left = 32 - imageNameOffsetPixels;
      // Move the cursor.
      graph.setCursor(left, centerBase);

      // Print the text
      graph.print(filename);

      // Calculate tail location for wrap-around.
      auto tail = 32 - imageNameOffsetPixels + imageNameWidthPixels + IMAGE_NAME_SPACING;
      if (tail < 128) {
	graph.setCursor(tail, centerBase);

	// Print the text
	graph.print(filename);
      }

      imageNameOffsetPixels++;
      if (imageNameOffsetPixels >= imageNameWidthPixels + IMAGE_NAME_SPACING) {
	imageNameOffsetPixels = 0;
      }
    } else if (offset < currentSysStatus->GetLoadedImage().GetFilename().length()) {
      graph.print(filename + offset);
    } else {
      graph.print(filename + (strlen(filename) - 1));
    }

    // Black out the icon area.
    graph.fillRect(0, 0, 32, 32, BLACK);

    // Draw the icon.
    graph.drawBitmap(0, 8, cdrom_loaded, 32, 16, WHITE);

    auto size = currentSysStatus->GetLoadedImage().GetFileSizeBytes();
    if (size != 0) {
      auto sizeStr = makeImageSizeStr(size);
      auto toShow = sizeStr.c_str();
      graph.setCursor(32, centerBase + lineHeight);
      graph.print(toShow);
    }
  } else if (!currentSysStatus->IsCardPresent()) {
    graph.drawBitmap(0, 8, cdrom_empty, 32, 16, WHITE);
    graph.print("[NO SD CARD]");
  } else {
    graph.drawBitmap(0, 8, cdrom_empty, 32, 16, WHITE);
    graph.print("[NO IMAGE]");
  }

  // Display primary/secondary
  int16_t x=0, y=0;
  uint16_t h=0, w=0;
  const char* text = currentSysStatus->IsPrimary() ? "pri" : "sec";
  graph.getTextBounds(text, 0 ,0, &x, &y, &w, &h);
  graph.setCursor(128 - w, 0);
  graph.print(text);

  graph.display();
}

#define MENU_OFFSET 1

void DisplaySSD1306::displayMenu() {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);

  int16_t x=0, y=0;
  uint16_t h=0, w=0;

  graph.getTextBounds("-- Menu --", 0 ,0, &x, &y, &w, &h);
  graph.setCursor((128 - w) / 2, 0);
  graph.print("-- Menu --");

  const char* selectMenuText = toString(zuluide::control::MenuState::Entry::Select);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Select) {
    graph.setTextColor(BLACK, WHITE);
  }
  graph.getTextBounds(selectMenuText, 0 ,0, &x, &y, &w, &h);
  graph.setCursor(32 - w / 2, 8 + MENU_OFFSET);
  graph.print(selectMenuText);
  graph.setTextColor(WHITE, BLACK);

  selectMenuText = toString(zuluide::control::MenuState::Entry::Eject);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Eject) {
    graph.setTextColor(BLACK, WHITE);
  }

  graph.getTextBounds(selectMenuText, 0 ,0, &x, &y, &w, &h);
  graph.setCursor(96 - w / 2, 8 + MENU_OFFSET);
  graph.print(selectMenuText);
  graph.setTextColor(WHITE, BLACK);

  selectMenuText = toString(zuluide::control::MenuState::Entry::Info);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Info) {
    graph.setTextColor(BLACK, WHITE);
  }

  graph.getTextBounds(selectMenuText, 0 ,0, &x, &y, &w, &h);
  graph.setCursor(32 - w / 2, 24 + MENU_OFFSET);
  graph.print(selectMenuText);
  graph.setTextColor(WHITE, BLACK);

  selectMenuText = toString(zuluide::control::MenuState::Entry::Back);
  if (currentDispState->GetMenuState().GetCurrentEntry() == zuluide::control::MenuState::Entry::Back) {
    graph.setTextColor(BLACK, WHITE);
  }

  graph.getTextBounds(selectMenuText, 0 ,0, &x, &y, &w, &h);
  graph.setCursor(96 - w / 2, 24 + MENU_OFFSET);
  graph.print(selectMenuText);
  graph.setTextColor(WHITE, BLACK);

  graph.display();
}

void DisplaySSD1306::displayEject() {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);

  int16_t x=0, y=0;
  uint16_t h=0, w=0;
  graph.getTextBounds("-- Confirm Eject --", 0 ,0, &x, &y, &w, &h);
  graph.setCursor((128 - w) / 2, 0);
  graph.print("-- Confirm Eject --");

  const char* selectMenuText = " Yes ";
  if (currentDispState->GetEjectState().GetCurrentEntry() == zuluide::control::EjectState::Entry::Eject) {
    graph.setTextColor(BLACK, WHITE);
  }
  graph.getTextBounds(selectMenuText, 0 ,0, &x, &y, &w, &h);
  graph.setCursor(32 - w / 2, 16 + MENU_OFFSET);
  graph.print(selectMenuText);
  graph.setTextColor(WHITE, BLACK);

  selectMenuText = " No ";
  if (currentDispState->GetEjectState().GetCurrentEntry() == zuluide::control::EjectState::Entry::Back) {
    graph.setTextColor(BLACK, WHITE);
  }
  graph.getTextBounds(selectMenuText, 0 ,0, &x, &y, &w, &h);
  graph.setCursor(96 - w / 2, 16 + MENU_OFFSET);
  graph.print(selectMenuText);
  graph.setTextColor(WHITE, BLACK);

  graph.display();
}

void DisplaySSD1306::displaySelect(bool isRefresh) {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);

  int16_t x=0, y=0;
  uint16_t h=0, w=0;
  graph.getTextBounds(SELECT_IMAGE_MENU_TEXT, 0 ,0, &x, &y, &w, &h);
  graph.setCursor((128 - w) / 2, 0);
  graph.print(SELECT_IMAGE_MENU_TEXT);

  const char* toShow;
  if (!currentDispState->GetSelectState().IsShowingBack() && currentDispState->GetSelectState().HasCurrentImage()) {
    auto img = currentDispState->GetSelectState().GetCurrentImage();

    if (!isRefresh) {
      // If this isn't a refresh, then store the width of the image name.
      int16_t x=0, y=0;
      uint16_t h=0;
      graph.getTextBounds(img.GetFilename().c_str(), 0 ,0, &x, &y, &imageNameWidthPixels, &h);
      imageNameWidthPixels += IMAGE_NAME_SPACING;
      imageNameOffsetPixels = 0;
    }

    if (scrollText) {
      auto left = 0 - imageNameOffsetPixels;
      // Move the cursor.
      graph.setCursor(left, centerBase);

      // Print the text
      graph.print(img.GetFilename().c_str());

      // Calculate tail location for wrap-around.
      auto tail = 0 - imageNameOffsetPixels + imageNameWidthPixels + IMAGE_NAME_SPACING;
      if (tail < 128) {
	graph.setCursor(tail, centerBase);

	// Print the text
	graph.print(img.GetFilename().c_str());
      }

      imageNameOffsetPixels++;
      if (imageNameOffsetPixels >= imageNameWidthPixels + IMAGE_NAME_SPACING) {
	imageNameOffsetPixels = 0;
      }
    } else {
      graph.setCursor(centerText(img.GetFilename(), graph), centerBase);

      auto offset = currentDispState->GetSelectState().GetImageNameOffset();
      auto filename = img.GetFilename().c_str();
      if (offset < img.GetFilename().length()) {
	graph.print(filename + offset);
      } else {
	graph.print(filename + (img.GetFilename().length() - 1));
      }
    }

    auto size = img.GetFileSizeBytes();
    if (size != 0) {
      auto sizeStr = makeImageSizeStr(size);
      toShow = sizeStr.c_str();
      graph.setCursor(centerText(toShow, graph), centerBase + h);
      graph.print(toShow);
    }
  } else {
    toShow = "[Back]";
    graph.setCursor(centerText(toShow, graph), centerBase);
    graph.print(toShow);
  }

  graph.display();
}

void DisplaySSD1306::displayInfo(bool isRefresh) {
  graph.clearDisplay();
  graph.setTextColor(WHITE, BLACK);

  int16_t x=0, y=0;
  uint16_t h=0, w=0;
  graph.getTextBounds(INFO_MENU_TEXT, 0 ,0, &x, &y, &w, &h);
  graph.setCursor((128 - w) / 2, 0);
  graph.print(INFO_MENU_TEXT);

  graph.setCursor(centerText(ZULUIDE_TITLE, graph), centerBase);
  graph.print(ZULUIDE_TITLE);

  auto firmwareVersion = currentSysStatus->GetFirmwareVersion().c_str();
  if (!isRefresh) {
    // If this isn't a refresh, then store the width of the image name.
    int16_t x=0, y=0;
    uint16_t h=0;
    graph.getTextBounds(firmwareVersion, 0 ,0, &x, &y, &imageNameWidthPixels, &h);
    imageNameWidthPixels += IMAGE_NAME_SPACING;
    imageNameOffsetPixels = 0;
  }

  if (scrollText) {
    auto left = 0 - imageNameOffsetPixels;
    // Move the cursor.
    graph.setCursor(left, centerBase + h);

    // Print the text
    graph.print(firmwareVersion);

    // Calculate tail location for wrap-around.
    auto tail = 0 - imageNameOffsetPixels + imageNameWidthPixels + IMAGE_NAME_SPACING;
    if (tail < 128) {
      graph.setCursor(tail, centerBase + h);

      // Print the text
      graph.print(firmwareVersion);
    }

    imageNameOffsetPixels++;
    if (imageNameOffsetPixels >= imageNameWidthPixels + IMAGE_NAME_SPACING) {
      imageNameOffsetPixels = 0;
    }
  } else {
    graph.setCursor(centerText(firmwareVersion, graph), centerBase + h);

    auto offset = currentDispState->GetInfoState().GetFirmwareOffset();
    if (offset < currentSysStatus->GetFirmwareVersion().length()) {
      graph.print(firmwareVersion + offset);
    } else {
      graph.print(firmwareVersion + (currentSysStatus->GetFirmwareVersion().length() - 1));
    }
  }

  graph.display();
}

void DisplaySSD1306::Refresh() {
  if (absolute_time_diff_us (get_absolute_time(), nextRefresh) > 0) {
    return;
  }

  nextRefresh = make_timeout_time_ms(SCROLL_INTERVAL_MS);

  if (currentDispState && currentSysStatus) {
    switch (currentDispState->GetCurrentMode()) {
    case zuluide::control::Mode::Status: {
      displayStatus(true);
      break;
    }
    case zuluide::control::Mode::Menu:
      displayMenu();
      break;
    case zuluide::control::Mode::Eject:
      displayEject();
      break;
    case zuluide::control::Mode::Select:
      displaySelect(true);
      break;
    case zuluide::control::Mode::NewImage:
      break;
    case zuluide::control::Mode::Info: {
      displayInfo(true);
      break;
    }
    }
  }
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

static uint16_t centerText(const std::string& text, Adafruit_SSD1306& graph) {
  return centerText(text.c_str(), graph);
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
  case zuluide::control::MenuState::Entry::Info:
    return "[ INFO ]";
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
