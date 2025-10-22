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

#pragma once

#include "image.h"
#include <memory>
#include <SdFat.h>
#include <CUEParser.h>

#define MAX_CUE_SHEET_SIZE 4096
// Maximum path length for files on SD card
#define MAX_FILE_PATH 255

namespace zuluide::images {

  class ImageIterator
  {
  public:
    ImageIterator();
    Image Get();
    // Find the next image
    // return false if there is no image or there is no next image
    bool MoveNext();
    // Find the previous image
    // return false if there is no image or there is no next image
    bool MovePrevious();
    // Move to the first image
    // return false if there are no images
    bool MoveFirst();
    // Move to the last image
    // return false if the are no images
    bool MoveLast();
    // Sets Interator to file
    // return true if successful
    bool MoveToFile(const char *filename);
    bool IsEmpty();
    int GetFileCount();
    void Reset(bool warning = false);
    bool IsFirst();
    bool IsLast();
    void Cleanup();
    /***
	[En/Dis]able parsing bin cue sheets to check size of multi-part bin/cue images.
	Defaults to true.
     **/
    void SetParseMultiPartBinCueSize(bool value);
  private:
    bool Move(bool forward = true);
    bool FetchSizeFromCueFile();
    FsFile currentFile;
    FsFile root;
    char candidate[MAX_FILE_PATH + 1];
    uint64_t candidateSizeInBytes;
    Image::ImageType candidateImageType;
    bool isOnImageFile;
    int fileCount;
    bool rotateIterator;
    bool isEmpty;
    uint32_t curIdx;
    uint32_t firstIdx;
    uint32_t lastIdx;
    bool currentIsFirst;
    bool currentIsLast;
    bool parseMultiPartBinCueSize;
    static char cuesheet[MAX_CUE_SHEET_SIZE];
  };
  
}
