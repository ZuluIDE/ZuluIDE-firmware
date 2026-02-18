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

#include <zuluide/images/image_iterator.h>
#include <memory>
#include <ZuluIDE_log.h>
#include <ZuluIDE_config.h>
#include <string>
#include <scp/SharedCUEParser.h>

using namespace zuluide::images;

FsFile ImageIterator::root;
char ImageIterator::tmpFilePath[MAX_FILE_PATH + 1];
FsFile ImageIterator::tmpFsFile;


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

Image ImageIterator::QuickGet(const char *filename)
{
  bool found_file = false;
  // Grab the filename of the current image or reset if the file is no longer valid
  if (filename != nullptr && filename[0] && currentFile.open(filename, O_RDONLY))
  {
    found_file = true;
  }
  else
  {
    currentFile.close();
    root.close();
    root.open("/");
  }
  bool isValid = false;
  while ((found_file || currentFile.openNext(&root, O_RDONLY))
          && currentFile.getName(candidate, sizeof(candidate)) < sizeof(candidate) - 1) {

      isValid = fileIsValidImage(currentFile, candidate);

      if (isValid)
          break;
      else if (found_file)
          break;
      currentFile.close();
  }
  if (!isValid)
  {
    currentFile.close();
    return Image("");
  }

  if ( currentFile.isOpen())
  {
    if (currentFile.isDirectory()) {
      // Indicates probable multi-part bin/cue.
      if(!FetchSizeFromCueFile()) {
        logmsg("Failed to fetch bin/cue size.");
      }
    } else {
      candidateSizeInBytes = currentFile.fileSize();
    }
  }
  currentFile.close();
  candidateImageType = Image::InferImageTypeFromFileName(candidate);
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
  static char current_candidate[MAX_FILE_PATH + 1];
  static char prev_candidate[MAX_FILE_PATH + 1];
  static char result_candidate[MAX_FILE_PATH + 1];

  prev_candidate[0] = '\0';
  result_candidate[0] = '\0';
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
    if (currentFile.openNext(&root, O_RDONLY) &&
        currentFile.getName(current_candidate, sizeof(current_candidate)) < sizeof(current_candidate) - 1) {
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

void ImageIterator::Reset(bool warning) {
  Cleanup();
  fileCount = 0;

  if (!root.open("/")) {
    return;
  }

  static FsFile curFile;
  bool oneIsValid = false;
  firstIdx = 0;
  lastIdx = 0;
  static char curFilePath[MAX_FILE_PATH+1];
  static char firstFilename[MAX_FILE_PATH+1];
  static char lastFilename[MAX_FILE_PATH+1];
  firstFilename[0] = '\0';
  lastFilename[0] = '\0';
  memset(candidate, 0, sizeof(candidate));
  candidateImageType = Image::ImageType::unknown;

  // Walk the directory to count the number of files.
  while (curFile.openNext(&root, O_RDONLY)) {
    fileCount++;

    // Get the file name and check that it is valid..
    memset(curFilePath, 0, sizeof(curFilePath));
    size_t filenameLen = curFile.getName(curFilePath, sizeof(curFilePath));
    if (filenameLen < sizeof(curFilePath) - 1 && fileIsValidImage(curFile, curFilePath, warning)) {
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

bool ImageIterator::IsFirst() {
  return currentIsFirst;
}

bool ImageIterator::IsLast() {
  return currentIsLast;
}

bool ImageIterator::folderContainsCueSheet(FsFile &dir)
{
  FsFile file;
  dir.rewindDirectory();
  while (file.openNext(&dir, O_RDONLY))
  {
    size_t filename_len = file.getName(tmpFilePath, sizeof(tmpFilePath));
    if (filename_len < sizeof(tmpFilePath) -1 && // filename not truncated
        filename_len > 4 &&
        (strncasecmp(tmpFilePath + strlen(tmpFilePath) - 4, ".cue", 4) == 0))
    {
      file.close();
      return true;
    }
  }
  file.close();
  return false;
}

bool ImageIterator::fileIsValidImage(FsFile& file, const char* fileName, bool warning) {
  if (file.isHidden())
    return false;

  // Directories are allowed if they contain a .cue sheet and the first character is alphanumeric
  if (file.isDirectory())
  {
    if (!isalnum(fileName[0]))
    {
      if (warning) logmsg("-- Ignoring directory \"",fileName,"\", first character is not alphanumeric");
      return false;
    }
    if (!folderContainsCueSheet(file))
    {
      if (warning) logmsg("-- Ignoring directory \"",fileName,"\", no .cue file found within or .cue filename exceeds max length ", MAX_FILE_PATH - 1);
      return false;
    }
    return true;
  }

  // If the file name is bad, skip it.
  return is_valid_filename(fileName, warning);
}

/***
    Predicate for checking filenames.
 */
bool ImageIterator::is_valid_filename(const char *name, bool warning)
{
  if (strcasecmp(name, "ice5lp1k_top_bitmap.bin") == 0){
    // Ignore FPGA bitstream
    return false;
  }

  if (strcasecmp(name, "sniff.dat") == 0) {
    if (warning) logmsg("-- Ignore bus sniffer output file \"sniff.dat\"");
    return false;
  }

  if (strncasecmp(name, CREATEFILE, strlen(CREATEFILE)) == 0) {
    if (warning) logmsg("-- Ignoring \"", name, "\" with prefix \"", CREATEFILE,"\", used to create images");
    return false;
  }


  if (!isalnum(name[0])) {
    // Skip names beginning with special character
    if (warning) logmsg("-- Ignoring \"", name, "\", first character is not alphanumeric");
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
          bool isCue = (i == 0); // Do not warn if file extension is .cue 
          if (warning && !isCue) logmsg("-- Ignoring \"", name, "\", file extension ",ignore_exts[i]," is in the reject list");
          return false;
        }
      }

      for (int i = 0; archive_exts[i]; i++) {
        if (strcasecmp(extension, archive_exts[i]) == 0) {
          if (warning) logmsg("-- Ignoring \"", name, "\", compressed files with extension ", archive_exts[i]," are rejected");
          return false;
        }
      }
  }

  return true;
}

bool ImageIterator::searchForCueSheetFile(FsFile *directory, FsFile *outputFile) {
  directory->rewindDirectory();
  while (outputFile->openNext(directory, O_RDONLY)) {
    size_t nameLen = outputFile->getName(tmpFilePath, sizeof(tmpFilePath));
    if (nameLen < sizeof(tmpFilePath) - 1 &&
      nameLen > 4 &&
      strncasecmp(tmpFilePath + strlen(tmpFilePath) - 4, ".cue", 4) == 0) {
      return true;
    }
  }
  outputFile->close();
  return false;
}

bool ImageIterator::FetchSizeFromCueFile() {
  if (!parseMultiPartBinCueSize) {
    return false;
  }
  SharedCUEParser parser;
  if (!searchForCueSheetFile(&currentFile, parser.get_cue_file())) {
    logmsg("---- Unable to find CUE sheet.");
    return false;
  }


  if (parser.get_cue_file()->size() > (uint64_t)parser.max_cue_sheet_size()) {
    static char cuesheet_name[MAX_FILE_PATH + 1];
    parser.get_cue_file()->getName(cuesheet_name, sizeof(cuesheet_name));
    logmsg("---- CUE sheet: ", cuesheet_name," too large to fit in ", (int) parser.max_cue_sheet_size(), " byte cache");
    parser.get_cue_file()->close();
    return false;
  }

  parser.load_updated_cue();
  auto currentTrack = parser.next_track(0);
  uint64_t totalSize = 0;
  std::string currentfilename = "";
  // Check each track. Whenever a file changes, sum its size.
  while (currentTrack) {
    if (currentfilename != currentTrack->filename) {
      // This track file has not be summed yet.
      if (tmpFsFile.open(&currentFile, currentTrack->filename, O_RDONLY)) {
        totalSize += tmpFsFile.fileSize();
        tmpFsFile.close();
        currentfilename = currentTrack->filename;
      } else {
        logmsg("Failed to read ", currentTrack->filename, "\"");
        // If we cannot open a track file, we cannot proceed in a meaningful way.
        tmpFsFile.close();
        return false;
      }
    }
    
    currentTrack = parser.next_track(totalSize);
  }
    
  candidateImageType = Image::ImageType::cdrom;
  candidateSizeInBytes = totalSize;
  tmpFsFile.close();
  return true;
}
