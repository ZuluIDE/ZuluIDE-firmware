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

#include <string>
#include <stdint.h>
#include <zuluide/ide_drive_type.h>

namespace zuluide::images {

  class Image
  {
  public:
    enum class ImageType {
      cdrom,
      zip100,
      zip250,
      zip750,
      removable,
      harddrive,
      unknown
    };

    Image(std::string filename, uint64_t sizeInBytes = 0);
    Image(std::string filename, ImageType imageType, uint64_t sizeInBytes = 0);

    const std::string& GetFilename() const;
    ImageType GetImageType();
    bool operator==(const Image& other) const;
    const uint64_t GetFileSizeBytes() const;

    const std::string ToJson() const;
    const std::string ToJson(const char* fieldName) const;
    static drive_type_t ToDriveType(const ImageType toConvert);
    static const char* GetImagePrefix(const ImageType toConvert);
    static ImageType InferImageTypeFromImagePrefix(const char* prefix);
    static ImageType InferImageTypeFromFileName(const char *filename);

  private:
    std::string filenm;
    ImageType imgType;
    uint64_t fileSizeBytes;
  };

}
