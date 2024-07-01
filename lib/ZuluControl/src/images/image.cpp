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

#include <zuluide/images/image.h>

using namespace zuluide::images;

Image::Image(std::string filename, uint64_t sizeInBytes)
  : filenm(filename), imgType(Image::ImageType::unknown), fileSizeBytes(sizeInBytes)
{
}

Image::Image(std::string filename, Image::ImageType imageType, uint64_t sizeInBytes)
  : filenm(filename), imgType(imageType), fileSizeBytes(sizeInBytes)
{
}

const std::string& Image::GetFilename() const {
  return filenm;
}

Image::ImageType Image::GetImageType() {
  return imgType;
}

bool Image::operator==(const Image& other) {
  return false;
}

const uint64_t Image::GetFileSizeBytes() const {
  return fileSizeBytes;
}

static void outputField(std::string& output, const char* fieldName, const std::string& value) {
  output.append("\"");
  output.append(fieldName);
  output.append("\":\"");
  output.append(value);
  output.append("\"");
}

static void outputField(std::string& output, const char* fieldName, uint64_t value) {
  outputField(output, fieldName, std::to_string(value));
}

static const char* toString(Image::ImageType type) {
  switch (type) {
  case Image::ImageType::cdrom: {
    return "cdrom";
  }
    
  case Image::ImageType::zip100: {
    return "zip100";
  }

  case Image::ImageType::zip250: {
    return "zip250";
  }

  case Image::ImageType::zip750: {
    return "zip750";
  }

  case Image::ImageType::generic: {
    return "generic";
  }

  case Image::ImageType::unknown:
  default: {
    return "unknown";
  }
  }
}

std::string Image::ToJson() {
  std::string buffer;
  buffer.append("{");
  outputField(buffer, "filename", filenm);
  buffer.append(",");
  outputField(buffer, "size", fileSizeBytes);
  buffer.append(",");
  outputField(buffer, "type", toString(imgType));
  buffer.append("}");
  return buffer;
}

std::string Image::ToJson(const char* fieldName) {
  std::string buffer = "\"";
  buffer.append(fieldName);
  buffer.append("\":");
  buffer.append(ToJson());
  return buffer;
}
