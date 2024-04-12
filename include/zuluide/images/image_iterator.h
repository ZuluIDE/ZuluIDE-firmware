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

#pragma once

#include "image.h"
#include <memory>
#include <SdFat.h>

#define MAX_FILE_PATH 64

namespace zuluide::images {

  class ImageIterator
  {
  public:
    ImageIterator(bool rotate = false);
    Image Get();
    bool MoveNext();
    bool MovePrevious();
    bool IsEmpty();
    int GetFileCount();
    void Reset();
    bool IsFirst();
    bool IsLast();
  private:
    FsFile currentFile;
    FsFile root;
    char candidate[MAX_FILE_PATH];
    uint64_t candidateSizeInBytes;
    bool isOnImageFile;
    int fileCount;
    bool rotateIterator;
    bool isEmpty;
    uint32_t curIdx;
    uint32_t firstIdx;
    uint32_t lastIdx;
    bool currentIsFirst;
    bool currentIsLast;
  };
  
}
