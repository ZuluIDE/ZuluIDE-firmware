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
#include "scrolling_text.h"
using namespace zuluide;

#ifndef SCROLL_INTERVAL_MS
#define SCROLL_INTERVAL_MS 60
#endif

#ifndef SCROLL_START_DELAY_MS
#define SCROLL_START_DELAY_MS 1000
#endif

ScrollingText::ScrollingText(Adafruit_SSD1306 *g, Rectangle bnds) : bounds(bnds),
								    toDisplay(""),
								    graph(g),
								    startScrollingAfter(at_the_end_of_time),
								    scrollText(false),
								    reverseScroll(false),
								    isDirty(false),
								    centerStationaryText(false),
								    isStationaryText(false) {
}

void ScrollingText::SetToDisplay(const char* toDisp) {
  toDisplay = std::string(toDisp);
  int16_t x = 0, y = 0;
  graph->getTextBounds(toDisplay.c_str(), 0 ,0, &x, &y, &toDispSize.width, &toDispSize.height);
  isStationaryText = toDispSize.width <= bounds.size.width;
  
  // When changing the text, don't start scrolling immediately.
  Reset();
}

void ScrollingText::Reset() {
  startScrollingAfter = make_timeout_time_ms(SCROLL_START_DELAY_MS);
  scrollText = false;
  reverseScroll = false;
  imageNameOffsetPixels = 0;
  isDirty = true;
}

bool ScrollingText::CheckAndUpdateScrolling(absolute_time_t now) {
  // Return false
  if (isStationaryText || (!scrollText && absolute_time_diff_us (now, startScrollingAfter) > 0)) {
    auto returnValue = isDirty;
    isDirty = false;
    return returnValue;
  }

  // Update the offset prior to calling display.
  scrollText = true;
  if (reverseScroll) {
    imageNameOffsetPixels--;
    if (imageNameOffsetPixels == 0) {
      scrollText = false;
      reverseScroll = false;
      startScrollingAfter = make_timeout_time_ms(SCROLL_START_DELAY_MS);
    }
  } else {
    imageNameOffsetPixels++;
    if (imageNameOffsetPixels > toDispSize.width - bounds.size.width) {
      // The text scrolled too far, reset.
      reverseScroll = true;
      scrollText = false;
      startScrollingAfter = make_timeout_time_ms(SCROLL_START_DELAY_MS);
      return false;
    }
  }

  return true;
}

void ScrollingText::Display() {
  if (isStationaryText) {
    auto dispBox = bounds.MakeCentered(toDispSize);
    graph->setCursor(dispBox.topLeft.x, dispBox.topLeft.y);
    graph->print(toDisplay.c_str());
  } else {
    auto left = bounds.topLeft.x - imageNameOffsetPixels;
    // Move the cursor.
    graph->setCursor(left, bounds.topLeft.y);
    
    // Print the text
    graph->print(toDisplay.c_str());
  }
}

void ScrollingText::SetCenterStationaryText(bool value) {
  centerStationaryText = value;
}
