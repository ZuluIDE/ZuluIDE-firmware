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

#include <zuluide/pipe/image_response.h>
#include <algorithm>


using namespace zuluide::pipe;

ImageResponse::ImageResponse () : status(response_status_t::None), image(nullptr), isFirst(false), isLast(false){
}

ImageResponse::ImageResponse(const ImageResponse& src) : status(src.status), isFirst(src.isFirst), isLast(src.isLast) {
  image = std::make_unique<zuluide::images::Image>(*src.image);
}

ImageResponse::ImageResponse(ImageResponse&& src) : status(src.status), isFirst(src.isFirst), isLast(src.isLast) {
  image = std::move(src.image);
}

ImageResponse& ImageResponse::operator= (ImageResponse&& src)  {
  status = src.status;
  isFirst = src.isFirst;
  isLast = src.isLast;
  image = std::move(src.image);
  return *this;
}


ImageResponse& ImageResponse::operator= (const ImageResponse& src) {
  status = src.status;
  isFirst = src.isFirst;
  isLast = src.isLast;
  image = std::make_unique<zuluide::images::Image>(*src.image);
  return *this;
}

void ImageResponse::SetImage(std::unique_ptr<zuluide::images::Image>&& value) {
  image = std::move(value);
}

void ImageResponse::SetStatus(response_status_t value)
{
  status = value;
}

void ImageResponse::SetRequest(std::unique_ptr<ImageRequest>&& value)
{
  request = std::move(value);
}

void ImageResponse::SetIsLast(bool value)
{
  isLast = value;
}

void ImageResponse::SetIsFirst(bool value)
{
  isFirst = value;
}

const zuluide::images::Image ImageResponse::GetImage() const
{
  return *image;
}


const response_status_t ImageResponse::GetStatus() const
{
  return status;
}

const ImageRequest ImageResponse::GetRequest() const
{
  return *request;
}

const bool ImageResponse::IsLast() const
{
  return isLast;
}

const bool ImageResponse::IsFirst() const
{
  return isFirst;
}
