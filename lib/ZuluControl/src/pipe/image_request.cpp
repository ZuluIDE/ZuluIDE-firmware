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

#include <zuluide/pipe/image_request.h>
#include <algorithm>


using namespace zuluide::pipe;

ImageRequest::ImageRequest()
{
}

ImageRequest::ImageRequest(const ImageRequest& src)
  : type(src.type)
{
  currentFilename = std::make_unique<std::string>(*src.currentFilename);
}

ImageRequest::ImageRequest(ImageRequest&& src)
  : type(src.type)
{
  currentFilename = std::move(src.currentFilename); 
}

ImageRequest& ImageRequest::operator= (ImageRequest&& src) {
  type = src.type;
  currentFilename = std::move(src.currentFilename);
  return *this;
}

ImageRequest& ImageRequest::operator= (const ImageRequest& src) {
  type = src.type;
  currentFilename = std::make_unique<std::string>(*currentFilename);
  return *this;
}

void ImageRequest::SetType(const image_request_t value) {
  type = value;
}

const image_request_t ImageRequest::GetType() const
{
  return type;
}

void ImageRequest::SetCurrentFilename(std::unique_ptr<std::string> fn)
{
  currentFilename = std::move(fn);
}

const std::string ImageRequest::GetCurrentFilename() const
{
  return *currentFilename;
}
