/**
 * ZuluIDE™ - Copyright (c) 2025 Rabbit Hole Computing™
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

#include <zuluide/observable.h>
#include <zuluide/observable_safe.h>
#include <pico/util/queue.h>

#include <functional>
#include <memory>
#include <vector>
#include <string>

namespace zuluide::pipe {

  enum class response_status_t {None, End, More};
 
  class FilenameResponse{
    public:
    FilenameResponse();
    FilenameResponse(const FilenameResponse& src);
    FilenameResponse(FilenameResponse&& src);
    FilenameResponse& operator= (FilenameResponse&& src);
    FilenameResponse& operator= (const FilenameResponse& src);

    void SetFilename(const std::string&& value);

    void SetStatus(const response_status_t value);
    
    const std::string GetFilename();
    const response_status_t GetStatus();
    private:
    response_status_t status;
    std::string filename;
  };
}
