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
#include <memory>
#include "ZuluIDE_log.h"
#include <string>

using namespace zuluide::images;

static bool is_valid_filename(const char *name);
static bool fileIsValidImage(FsFile& file, const char* fileName);

ImageIterator::ImageIterator() :
  fileCount (0), isEmpty(true)
{
}

int ImageIterator::GetFileCount() {
  return fileCount;
}

bool ImageIterator::IsEmpty() {
  return isEmpty;
}

/***
    Gets the current image in the iteration.
    requires: Previous call to MoveNext to have returned true.
 */
Image ImageIterator::Get() {
  return Image(std::string(candidate), candidateSizeInBytes);
}

bool ImageIterator::MoveNext()
{
  return Move();
}

bool ImageIterator::MovePrevious()
{
  return Move(false);
}

/***
    Moves to the next image in the root folder.
    requires: IsEmpty == false
 */
bool ImageIterator::Move(bool forward) {
  char current_candidate[MAX_FILE_PATH + 1] = {0};
  char prev_candidate[MAX_FILE_PATH + 1] = {0};
  char result_candidate[MAX_FILE_PATH + 1] = {0};

  // Grab the filename of the current image or reset if the file is no longer valid
  if (candidate[0] && currentFile.open(&root, candidate, O_RDONLY))
    currentFile.getName(prev_candidate, sizeof(prev_candidate));
  else
    Reset();

  currentFile.close();
  root.rewindDirectory();

  uint32_t next = curIdx;
  uint32_t matchingIdx = curIdx;
  int maxIterations = fileCount;
  bool first_search = !prev_candidate[0];

  do {
    maxIterations--;
    // Check the file.
    if (currentFile.openNext(&root, O_RDONLY)) {
      memset(current_candidate, 0, sizeof(current_candidate));
      currentFile.getName(current_candidate, sizeof(current_candidate));
      bool isValid = fileIsValidImage(currentFile, current_candidate);
      next = currentFile.dirIndex();
      currentFile.close();

      if (!isValid)
        continue;

      if (forward)
      {
        if (prev_candidate[0] && strcasecmp(current_candidate, prev_candidate) <= 0)
        {
          // Alphabetically before the previous image
          continue;
        }

        if (result_candidate[0] && strcasecmp(current_candidate, result_candidate) > 0)
        {
          // Alphabetically later than current result
          continue;
        }
      }
      else
      {
        if (prev_candidate[0] && strcasecmp(current_candidate, prev_candidate) >= 0)
        {
          // Alphabetically after the previous image
          continue;
        }

        if (result_candidate[0] && strcasecmp(current_candidate, result_candidate) < 0)
        {
          // Alphabetically before than current result
          continue;
        }
      }
      matchingIdx = next;
      memcpy(result_candidate, current_candidate, sizeof(current_candidate));
    }
  } while (maxIterations > 0);  // This counter prevents an infinite loop if something goes wrong.

  if (result_candidate[0] && currentFile.open(&root, result_candidate, O_RDONLY))
  {
    currentFile.getName(candidate, sizeof(candidate));
    candidateSizeInBytes = currentFile.fileSize();
    currentFile.close();
    currentIsLast = (matchingIdx == lastIdx);
    currentIsFirst = (matchingIdx == firstIdx);

    if (!first_search && curIdx == matchingIdx)
      return false;

    curIdx = matchingIdx;
    return true;
  }
  return false;
}

bool ImageIterator::MoveFirst()
{
  if (currentFile.open(&root, firstIdx, O_RDONLY))
  {
    currentFile.getName(candidate, sizeof(candidate));
    candidateSizeInBytes = currentFile.fileSize();
    curIdx = currentFile.dirIndex();
    currentIsLast = (lastIdx == firstIdx);
    currentIsFirst = true;
    currentFile.close();
    return true;
  }
  return false;
}


bool ImageIterator::MoveLast()
{
  if (currentFile.open(&root, lastIdx, O_RDONLY))
  {
    currentFile.getName(candidate, sizeof(candidate));
    candidateSizeInBytes = currentFile.fileSize();
    curIdx = currentFile.dirIndex();
    currentIsLast = lastIdx;
    currentIsFirst = (lastIdx == firstIdx);
    currentFile.close();
    return true;
  }
  return false;
}

bool ImageIterator::MoveToFile(char *filename)
{
  if (currentFile.open(&root, filename, O_RDONLY))
  {
    currentFile.getName(candidate, sizeof(candidate));
    candidateSizeInBytes = currentFile.fileSize();
    curIdx = currentFile.dirIndex();
    currentIsLast = (curIdx == lastIdx);
    currentIsFirst = (curIdx == firstIdx);
    currentFile.close();
    return true;
  }
  return false;
}

void ImageIterator::Cleanup() {
  if (currentFile.isOpen()) {
    currentFile.close();
  }

  if (root.isOpen()) {
    root.close();
  }
}

void ImageIterator::Reset() {
  Cleanup();

  if (!root.open("/")) {
    logmsg("Failed to open root directory.");
  }

  if (root.isOpen()) {
    FsFile curFile;
    bool oneIsValid = false;
    firstIdx = 0;
    lastIdx = 0;
    char curFilePath[MAX_FILE_PATH+1];
    char firstFilename[MAX_FILE_PATH+1] = {0};
    char lastFilename[MAX_FILE_PATH+1] = {0};
    // Walk the directory to count the number of files.
    while (curFile.openNext(&root, O_RDONLY)) {
      fileCount++;

      // Get the file name and check that it is valid..
      memset(curFilePath, 0, sizeof(curFilePath));
      curFile.getName(curFilePath, sizeof(curFilePath));
      if (fileIsValidImage(curFile, curFilePath)) {
        oneIsValid = true;
        if (!firstFilename[0] || strcasecmp(firstFilename, curFilePath) > 0)
        {
          memcpy(firstFilename, curFilePath, sizeof(firstFilename));
          firstIdx = curFile.dirIndex();
        }

        if (!lastFilename[0] || strcasecmp(lastFilename, curFilePath) < 0)
        {
          memcpy(lastFilename, curFilePath, sizeof(lastFilename));
          lastIdx = curFile.dirIndex();
        }
      }

      curFile.close();
    }

    isEmpty = !oneIsValid;
    curIdx = firstIdx;
    currentIsFirst = false;
    currentIsLast = false;
  }
}

bool ImageIterator::IsFirst() {
  return currentIsFirst;
}

bool ImageIterator::IsLast() {
  return currentIsLast;
}

static bool fileIsValidImage(FsFile& file, const char* fileName) {
  // Skip directories.
  if (file.isDirectory()) {
    return false;
  }

  // If the file name is bad, skip it.
  return is_valid_filename(fileName);
}

/***
    Predicate for checking filenames.
 */
static bool is_valid_filename(const char *name)
{
  if (strncasecmp(name, "ice5lp1k_top_bitmap.bin", sizeof("ice5lp1k_top_bitmap.bin")) == 0){
    // Ignore FPGA bitstream
    return false;
  }

  if (!isalnum(name[0])) {
    // Skip names beginning with special character
    return false;
  }

  if (strncasecmp(name, "zulu", 4) == 0) {
    // Ignore all files that start with "zulu"
    return false;
  }

  // Check file extension
  const char *extension = strrchr(name, '.');
  if (extension) {
      const char *ignore_exts[] = {
        ".cue", ".txt", ".rtf", ".md", ".nfo", ".pdf", ".doc", ".ini",
        NULL
      };
      const char *archive_exts[] = {
        ".tar", ".tgz", ".gz", ".bz2", ".tbz2", ".xz", ".zst", ".z",
        ".zip", ".zipx", ".rar", ".lzh", ".lha", ".lzo", ".lz4", ".arj",
        ".dmg", ".hqx", ".cpt", ".7z", ".s7z",
        NULL
          };

      for (int i = 0; ignore_exts[i]; i++) {
        if (strcasecmp(extension, ignore_exts[i]) == 0) {
          // ignore these without log message
          return false;
        }
      }

      for (int i = 0; archive_exts[i]; i++) {
        if (strcasecmp(extension, archive_exts[i]) == 0) {
          logmsg("-- Ignoring compressed file ", name);
          return false;
        }
      }
  }

  return true;
}
