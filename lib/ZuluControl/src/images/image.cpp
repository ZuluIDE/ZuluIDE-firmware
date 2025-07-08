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
#include <string.h>
#include <ZuluIDE_config.h>

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

bool Image::operator==(const Image& other) const {
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

  case Image::ImageType::harddrive: {
    return "harddrive";
  }

  case Image::ImageType::removable: {
    return "removable";
  }

  case Image::ImageType::unknown:
  default: {
    return "unknown";
  }
  }
}

const std::string Image::ToJson() const {
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

const std::string Image::ToJson(const char* fieldName) const {
  std::string buffer = "\"";
  buffer.append(fieldName);
  buffer.append("\":");
  buffer.append(ToJson());
  return buffer;
}
drive_type_t Image::ToDriveType(const Image::ImageType toConvert)
{
  switch (toConvert) {
    case Image::ImageType::cdrom: {
      return DRIVE_TYPE_CDROM;
    }

    case Image::ImageType::zip100: {
      return DRIVE_TYPE_ZIP100;
    }

    case Image::ImageType::zip250: {
      return DRIVE_TYPE_ZIP250;
    }

    case Image::ImageType::removable: {
      return DRIVE_TYPE_REMOVABLE;
    }

    case Image::ImageType::harddrive: {
      return DRIVE_TYPE_RIGID;
    }

    case Image::ImageType::unknown:
    default: {
      // If nothing is found, default to a CDROM.
      return drive_type_t::DRIVE_TYPE_CDROM;
    }
  }
}

const char* Image::GetImagePrefix(const ImageType toConvert) {
  switch (toConvert) {
    case Image::ImageType::cdrom: {
      return "cdrm";
    }

    case Image::ImageType::zip100: {
      return "z100";
    }

    case Image::ImageType::zip250: {
      return "z250";
    }

    case Image::ImageType::zip750: {
      return "z750";
    }

    case Image::ImageType::harddrive: {
      return "hddr";
    }

    case Image::ImageType::removable: {
      return "remv";
    }

    case Image::ImageType::unknown:
    default: {
      return "unkn";
    }
    }
}

Image::ImageType Image::InferImageTypeFromImagePrefix(const char* prefix) {
  if (strncasecmp(prefix, "cdrm", sizeof("cdrm")) == 0) {
    return Image::ImageType::cdrom;
  } else if (strncasecmp(prefix, "zipd", sizeof("zipd")) == 0) {
    return Image::ImageType::zip100;
  } else if (strncasecmp(prefix, "z100", sizeof("z100")) == 0) {
    return Image::ImageType::zip100;
  } else if (strncasecmp(prefix, "z250", sizeof("z250")) == 0) {
    return Image::ImageType::zip250;
  } else if (strncasecmp(prefix, "remv", sizeof("remv")) == 0) {
    return Image::ImageType::removable;
  } else if (strncasecmp(prefix, "hddr", sizeof("hddr")) == 0) {
    return Image::ImageType::harddrive;
  } else {
    return Image::ImageType::unknown;
  }
}

Image::ImageType Image::InferImageTypeFromFileName(const char *filename) {
  auto returnValue = Image::ImageType::unknown;
  auto len = strnlen(filename, MAX_FILE_PATH);

  if (len > 3) {
    // Check the suffix to see if this is a cd-rom image type extension.
    if (strncasecmp(filename + len - 4, ".iso", sizeof(".iso")) == 0) {
      returnValue = Image::ImageType::cdrom;
    } else {
      // Check  prefix to see if this uses the ZuluIDE file-prefix format.
      char *prefix = (char *)calloc(4, sizeof(char));
      if (prefix) {
        strncpy(prefix, filename, 4);
        returnValue = Image::InferImageTypeFromImagePrefix(prefix);
      }
    }
  }

  return returnValue;
}

