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
  fileCount (0), isEmpty(true), parseMultiPartBinCueSize(true)
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
  return Image(std::string(candidate), candidateImageType, candidateSizeInBytes);
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
    if (currentFile.isDirectory()) {
      // Indicates probable multi-part bin/cue.
      if(!FetchSizeFromCueFile()) {
        logmsg("Failed to fetch bin/cue size.");
      }
    } else {
      candidateSizeInBytes = currentFile.fileSize();
    }
    
    currentFile.close();
    currentIsLast = (matchingIdx == lastIdx);
    currentIsFirst = (matchingIdx == firstIdx);

    if (!first_search && curIdx == matchingIdx)
      return false;

    curIdx = matchingIdx;
    candidateImageType = Image::InferImageTypeFromFileName(candidate);
    return true;
  }
  return false;
}

bool ImageIterator::MoveFirst()
{  
  if (currentFile.open(&root, firstIdx, O_RDONLY))
  {
    currentFile.getName(candidate, sizeof(candidate));
    if (currentFile.isDirectory()) {
      // Indicates probable multi-part bin/cue.
      if(!FetchSizeFromCueFile()) {
  logmsg("Failed to fetch bin/cue size.");
      }
    } else {
      candidateSizeInBytes = currentFile.fileSize();
    }
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
    if (currentFile.isDirectory()) {
      // Indicates probable multi-part bin/cue.
      if(!FetchSizeFromCueFile()) {
  logmsg("Failed to fetch bin/cue size.");
      }
    } else {
      candidateSizeInBytes = currentFile.fileSize();
    }
    
    curIdx = currentFile.dirIndex();
    currentIsLast = lastIdx;
    currentIsFirst = (lastIdx == firstIdx);
    currentFile.close();
    return true;
  }
  return false;
}

bool ImageIterator::MoveToFile(const char *filename)
{
  if (currentFile.open(&root, filename, O_RDONLY))
  {
    currentFile.getName(candidate, sizeof(candidate));
    if (currentFile.isDirectory()) {
      // Indicates probable multi-part bin/cue.
      if(!FetchSizeFromCueFile()) {
        logmsg("Failed to fetch bin/cue size.");
      }
    } else {
      candidateSizeInBytes = currentFile.fileSize();
    }
    
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
  fileCount = 0;

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
    memcpy(candidate, 0, sizeof(candidate));
    candidateImageType = Image::ImageType::unknown;

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

static bool folderContainsCueSheet(FsFile &dir)
{
  FsFile file;
  char filename[MAX_FILE_PATH + 1];
  while (file.openNext(&dir, O_RDONLY))
  {
    if (file.getName(filename, sizeof(filename)) &&
        (strncasecmp(filename + strlen(filename) - 4, ".cue", 4) == 0))
    {
      file.close();
      return true;
    }
  }
  file.close();
  return false;
}

static bool fileIsValidImage(FsFile& file, const char* fileName) {
  // Directories are allowed if they contain a .cue sheet
  if (file.isDirectory() && !folderContainsCueSheet(file)) {
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

bool tryReadQueueSheet(FsFile &cuesheetfile, char* cuesheet) {
  if (!cuesheetfile.isOpen()) {
    logmsg("---- Failed to load CUE sheet.");
    return false;
  }
  
  if (cuesheetfile.size() > MAX_CUE_SHEET_SIZE) {
    logmsg("---- WARNING: CUE sheet length ", (int)cuesheetfile.size(), " exceeds maximum ",
      (int)sizeof(cuesheet), " bytes");
    return false;
  }

  auto bytesread = cuesheetfile.read(cuesheet, MAX_CUE_SHEET_SIZE);
  if (bytesread < 0) {
    logmsg("---- Failed to read CUE sheet");
    return false;
  } else if (bytesread < MAX_CUE_SHEET_SIZE) {
    cuesheet[bytesread+1] = 0;
  }
  return true;
}

bool searchForCueSheetFile(FsFile *directory, FsFile &outputFile) {
  while (outputFile.openNext(directory, O_RDONLY)) {
    char filename[MAX_FILE_PATH + 1];
    if (outputFile.getName(filename, sizeof(filename)) &&
      strncasecmp(filename + strlen(filename) - 4, ".cue", 4) == 0) {

      return true;
    }

    outputFile.close();
  }

  return false;
}

bool ImageIterator::FetchSizeFromCueFile() {
  if (!parseMultiPartBinCueSize) {
    return false;
  }
  
  FsFile file;
  if (!searchForCueSheetFile(&currentFile, file)) {
    logmsg("Unabled find CUE sheet.");
    return false;
  }
    
  if (!tryReadQueueSheet(file, cuesheet)) {
    logmsg("Failed to read the queuesheet into memory.");
    file.close();
    return false;
  }
  char dirname[MAX_FILE_PATH + 1];
  currentFile.getName(dirname, sizeof(dirname));
  
  CUEParser parser(cuesheet);
  parser.restart();
  auto current = parser.next_track(0);
  uint64_t totalSize = 0;
  std::string currentfilename = "";
  // Check each track. Whenever a file changes, sum its size.
  while (current) {
    if (currentfilename != current->filename) {
      // This track file has not be summed yet.
      FsFile trackFile;
      if (trackFile.open(&currentFile, current->filename, O_RDONLY)) {
        totalSize += trackFile.fileSize();
        trackFile.close();
        currentfilename = current->filename;
      } else {
        logmsg("Failed to read \"", dirname, "/", current->filename, "\"");
        // If we cannot open a track file, we cannot proceed in a meaningful way.
        return false;
      }
    }
    
    current = parser.next_track(totalSize);
  }
    
  candidateImageType = Image::ImageType::cdrom;
  candidateSizeInBytes = totalSize;
  file.close();
  return true;
}
