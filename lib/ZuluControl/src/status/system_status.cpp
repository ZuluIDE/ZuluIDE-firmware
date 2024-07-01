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

#include <zuluide/status/system_status.h>
#include <algorithm>


using namespace zuluide::status;

SystemStatus::SystemStatus()
{
}

SystemStatus::SystemStatus(const SystemStatus& src)
  : firmwareVersion(src.firmwareVersion), isCardPresent(src.isCardPresent), isPrimary(src.isPrimary)
{
  if (src.primary) {
    primary = std::move(src.primary->Clone());
  }

  if (src.loadedImage) {
    loadedImage = std::make_unique<zuluide::images::Image>(*src.loadedImage);
  }
}

SystemStatus::SystemStatus(SystemStatus&& src)
{
  firmwareVersion = std::move(src.firmwareVersion);
  primary = std::move(src.primary);
  loadedImage = std::move(src.loadedImage);
  isCardPresent = src.isCardPresent;
  isPrimary = src.isPrimary;
}

SystemStatus& SystemStatus::operator= (SystemStatus&& src) {
  firmwareVersion = std::move(src.firmwareVersion);
  primary = std::move(src.primary);
  loadedImage = std::move(src.loadedImage);
  isCardPresent = src.isCardPresent;
  isPrimary = src.isPrimary;
  return *this;
}

SystemStatus& SystemStatus::operator= (const SystemStatus& src) {
  firmwareVersion = src.firmwareVersion;
    
  if (src.primary) {
    primary = std::move(src.primary->Clone());
  }

  if (src.loadedImage) {
    loadedImage = std::make_unique<zuluide::images::Image>(*src.loadedImage);
  }

  isCardPresent = src.isCardPresent;
  isPrimary = src.isPrimary;

  return *this;
}

void SystemStatus::SetDeviceStatus(std::unique_ptr<IDeviceStatus> value) {
  primary = std::move(value);
}

void SystemStatus::SetFirmwareVersion(std::string&& frmVer) {
  firmwareVersion = std::move(frmVer);
}

void SystemStatus::SetLoadedImage(std::unique_ptr<zuluide::images::Image> image) {
  loadedImage = std::move(image);
}

const zuluide::images::Image& SystemStatus::GetLoadedImage() const {
  return *loadedImage;
}

bool SystemStatus::HasLoadedImage() const {
  return loadedImage ? true : false;
}

bool SystemStatus::LoadedImagesAreEqual(const SystemStatus& src) const {
  if (loadedImage && src.loadedImage) {
    return *loadedImage == *src.loadedImage;
  }

  return false;
}

bool SystemStatus::IsPrimary() const {
  return isPrimary;
}

void SystemStatus::SetIsPrimary(bool value) {
  isPrimary = value;
}

template <class T> const T& SystemStatus::GetDeviceStatus() const {
  return *primary;
}

drive_type_t SystemStatus::GetDeviceType() const {
  return primary->GetDriveType();
}

const std::string& SystemStatus::GetFirmwareVersion() const {
  return firmwareVersion;
}

bool SystemStatus::IsCardPresent() const {
  return isCardPresent;
}

void SystemStatus::SetIsCardPresent(bool value) {
  isCardPresent = value;
}

static const char* toString(bool value) {
  if (value) {
    return "true";
  } else {
    return "false";
  }
}

static void outputField(std::string& output, const char* fieldName, const std::string& value) {
  output.append("\"");
  output.append(fieldName);
  output.append("\":\"");
  output.append(value);
  output.append("\"");
}

static void outputField(std::string& output, const char* fieldName, const char* value) {
  output.append("\"");
  output.append(fieldName);
  output.append("\":\"");
  output.append(value);
  output.append("\"");
}

static void outputField(std::string& output, const char* fieldName, bool value) {
  outputField(output, fieldName, toString(value));
}


std::string SystemStatus::ToJson() const {
  std::string output = "{";
  outputField(output, "isPrimary", isPrimary);
  output.append(",");
  outputField(output, "isCardPresent", isCardPresent);
  output.append(",");
  outputField(output, "fwVer", firmwareVersion);
  if (loadedImage) {
    output.append(",");
    output.append(loadedImage->ToJson("image"));
  }
  
  output.append("}");
  
  return output;
}
