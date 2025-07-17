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

#include "zuluide/pipe/image_request.h"

#include "zuluide/images/image.h"

#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>

namespace zuluide::pipe {

  enum class response_status_t {None, End, More};
  template <typename SrcType>
  class ImageResponse{
    public:
    ImageResponse();
    ImageResponse(const ImageResponse& src);
    ImageResponse(ImageResponse&& src);
    ImageResponse& operator= (ImageResponse&& src);
    ImageResponse& operator= (const ImageResponse& src);

    void SetImage(std::unique_ptr<zuluide::images::Image>&& value);
    void SetStatus(const response_status_t value);
    void SetRequest(std::unique_ptr<ImageRequest<SrcType>>&& value);
    void SetIsLast(const bool value);
    void SetIsFirst(const bool value);
    
    const zuluide::images::Image GetImage() const;
    const response_status_t GetStatus() const;
    const ImageRequest<SrcType> GetRequest() const;
    const bool IsLast() const;
    const bool IsFirst() const;

    private:
    response_status_t status;
    std::unique_ptr<zuluide::images::Image> image;
    std::unique_ptr<ImageRequest<SrcType>> request;
    bool isFirst;
    bool isLast;

  };

template <typename SrcType>
ImageResponse<SrcType>::ImageResponse () : status(response_status_t::None), image(nullptr), isFirst(false), isLast(false){
}

template <typename SrcType>
ImageResponse<SrcType>::ImageResponse(const ImageResponse& src) : status(src.status), isFirst(src.isFirst), isLast(src.isLast) {
  image = std::make_unique<zuluide::images::Image>(*src.image);
  request = std::make_unique<ImageRequest<SrcType>>(ImageRequest<SrcType>(*src.request));
}

template <typename SrcType>
ImageResponse<SrcType>::ImageResponse(ImageResponse<SrcType>&& src) : status(src.status), isFirst(src.isFirst), isLast(src.isLast) {
  image = std::move(src.image);
  request = std::move(src.request);
}

template <typename SrcType>
ImageResponse<SrcType>& ImageResponse<SrcType>::operator= (ImageResponse<SrcType>&& src)  {
  status = src.status;
  isFirst = src.isFirst;
  isLast = src.isLast;
  image = std::move(src.image);
  request = std::move(src.request);
  return *this;
}

template <typename SrcType>
ImageResponse<SrcType>& ImageResponse<SrcType>::operator= (const ImageResponse<SrcType>& src) {
  status = src.status;
  isFirst = src.isFirst;
  isLast = src.isLast;
  image = std::make_unique<zuluide::images::Image>(*src.image);
  request = std::make_unique<ImageRequest<SrcType>>(ImageRequest<SrcType>(*src.request));
  return *this;
}

template <typename SrcType>
void ImageResponse<SrcType>::SetImage(std::unique_ptr<zuluide::images::Image>&& value) {
  image = std::move(value);
}

template <typename SrcType>
void ImageResponse<SrcType>::SetStatus(response_status_t value)
{
  status = value;
}

template <typename SrcType>
void ImageResponse<SrcType>::SetRequest(std::unique_ptr<ImageRequest<SrcType>>&& value)
{
  request = std::move(value);
}

template <typename SrcType>
void ImageResponse<SrcType>::SetIsLast(bool value)
{
  isLast = value;
}

template <typename SrcType>
void ImageResponse<SrcType>::SetIsFirst(bool value)
{
  isFirst = value;
}

template <typename SrcType>
const zuluide::images::Image ImageResponse<SrcType>::GetImage() const
{
  return *image;
}


template <typename SrcType>
const response_status_t ImageResponse<SrcType>::GetStatus() const
{
  return status;
}

template <typename SrcType>
const ImageRequest<SrcType> ImageResponse<SrcType>::GetRequest() const
{
  if (request)
    return *request; 

  ImageRequest<SrcType> empty_request;
  empty_request.SetType(image_request_t::Empty);
  return empty_request;
}

template <typename SrcType>
const bool ImageResponse<SrcType>::IsLast() const
{
  return isLast;
}

template <typename SrcType>
const bool ImageResponse<SrcType>::IsFirst() const
{
  return isFirst;
}


}
