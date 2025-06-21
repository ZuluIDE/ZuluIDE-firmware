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

#include <zuluide/pipe/filename_response.h>
#include <algorithm>


using namespace zuluide::pipe;

FilenameResponse::FilenameResponse () : status(response_status_t::None), filename("") {
}

FilenameResponse::FilenameResponse(const FilenameResponse& src) : status(src.status), filename(src.filename) {
}

FilenameResponse::FilenameResponse(FilenameResponse&& src) : status(src.status) {
  filename = std::move(src.filename);
}

FilenameResponse& FilenameResponse::operator= (FilenameResponse&& src)  {
  status = src.status;
  filename = std::move(filename);
  return *this;
}


FilenameResponse& FilenameResponse::operator= (const FilenameResponse& src) {
  status = src.status;
  filename = src.filename;
  return *this;
}

void FilenameResponse::SetFilename(const std::string&& value) {
  filename = std::move(value);
}

void FilenameResponse::SetStatus(response_status_t value)
{
  status = value;
}

 const std::string FilenameResponse::GetFilename()
 {
  return filename;
 }


const response_status_t FilenameResponse::GetStatus()
{
  return status;
}