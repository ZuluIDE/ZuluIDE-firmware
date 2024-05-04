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
#include <memory>
#include <vector>

#include "device_status.h"
#include "../images/image.h"

namespace zuluide::status {

  class SystemStatus {
  public:
    SystemStatus();
    SystemStatus(const SystemStatus& src);
    SystemStatus(SystemStatus&& src);
    SystemStatus& operator= (SystemStatus&& src);
    SystemStatus& operator= (const SystemStatus& src);
    void SetDeviceStatus(std::unique_ptr<IDeviceStatus> value);
    void SetIsPrimary(bool value);
    bool IsPrimary() const;    
    template <class T> const T& GetDeviceStatus() const;
    drive_type_t GetDeviceType() const;
    
    void SetFirmwareVersion(std::string&& frmVer);
    void SetLoadedImage(std::unique_ptr<zuluide::images::Image> image);
    const zuluide::images::Image& GetLoadedImage() const;
    bool HasLoadedImage() const;
    bool LoadedImagesAreEqual(const SystemStatus& src) const;
    bool HasCurrentImage() const;
    const std::string& GetFirmwareVersion() const;

    bool IsCardPresent() const;
    void SetIsCardPresent(bool value);

    std::string ToJson() const;
  private:
    std::unique_ptr<IDeviceStatus> primary;
    std::string firmwareVersion;
    std::unique_ptr<zuluide::images::Image> loadedImage;
    bool isPrimary;
    bool isCardPresent;
  };
}
