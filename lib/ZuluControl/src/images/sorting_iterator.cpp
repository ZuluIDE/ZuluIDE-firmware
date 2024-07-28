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

#include <zuluide/images/image_iterator.h>
#include <zuluide/images/sorting_iterator.h>
#include <memory>
#include "ZuluIDE_log.h"
#include <string>

using namespace zuluide::images;

SortingIterator::SortingIterator(ImageIterator&& source) : source (std::move(source)), isLast(false) {
}

Image SortingIterator::Get() {
  return *candidate;
}

bool SortingIterator::MoveNext() {
  // Start searching from the begining of the iterator.
  source.Reset();

  std::unique_ptr<Image> next;
  while (source.MoveNext()) {
    Image&& current = source.Get();
    if (!next) {
      next = std::make_unique<Image>(current);
    } else if (current < *next && (!candidate ||  *candidate < current)) {
      // Current is smaller than next, but bigger than candidate (or candidate doesn't exist b/c this is our first iteration).
      next = std::make_unique<Image>(current);
    }
  }

  if (next) {
    candidate = std::make_unique<Image>(*next);
    return true;
  } else {   
    return false;
  }
}

bool SortingIterator::IsEmpty() {
  return source.IsEmpty();
}

int SortingIterator::GetFileCount() {
  return source.GetFileCount();
}

void SortingIterator::Cleanup() {
  source.Cleanup();
}
