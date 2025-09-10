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
#include "image_request_src_tag.h"

#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace zuluide::pipe {
  
enum class image_request_t {First, Next, Prev, Current, Last, Cleanup, Reset, Empty};
template<typename SrcType>
class ImageRequest{
  public:
  ImageRequest();
  ImageRequest(image_request_t request, SrcType source);
  ImageRequest(const ImageRequest& src);
  ImageRequest(ImageRequest&& src);
  ImageRequest& operator= (ImageRequest&& src);
  ImageRequest& operator= (const ImageRequest& src);

  void RequestFilenamesSafe(image_request_t action);

  void SetType(const image_request_t value);
  const image_request_t GetType() const;
  void SetCurrentFilename(std::unique_ptr<std::string> fn);
  const std::string GetCurrentFilename() const;
  inline SrcType GetSource() const {return source;}
  inline void SetSource(const SrcType value){source = value;}


  private:
  SrcType source;
  image_request_t type;
  std::unique_ptr<std::string> currentFilename;
};

template <typename SrcType>
ImageRequest<SrcType>::ImageRequest()
{
  
}

template <typename SrcType>
ImageRequest<SrcType>::ImageRequest(image_request_t request, SrcType source) : source(source), type(request)
{
}

template <typename SrcType>
ImageRequest<SrcType>::ImageRequest(const ImageRequest<SrcType>& src)
  : source(src.source), type(src.type)
{
  if (src.currentFilename)
    currentFilename = std::make_unique<std::string>(*src.currentFilename);
}

template <typename SrcType>
ImageRequest<SrcType>::ImageRequest(ImageRequest<SrcType>&& src)
  : source(src.source), type(src.type)
{
  currentFilename = std::move(src.currentFilename); 
}

template <typename SrcType>
ImageRequest<SrcType>& ImageRequest<SrcType>::operator= (ImageRequest<SrcType>&& src) {
  type = src.type;
  source = src.source;
  currentFilename = std::move(src.currentFilename);
  return *this;
}

template <typename SrcType>
ImageRequest<SrcType>& ImageRequest<SrcType>::operator= (const ImageRequest<SrcType>& src) {
  type = src.type;
  source = src.source;
  currentFilename = std::make_unique<std::string>(*currentFilename);
  return *this;
}

template <typename SrcType>
void ImageRequest<SrcType>::SetType(const image_request_t value) {
  type = value;
}

template <typename SrcType>
const image_request_t ImageRequest<SrcType>::GetType() const
{
  return type;
}

template <typename SrcType>
void ImageRequest<SrcType>::SetCurrentFilename(std::unique_ptr<std::string> fn)
{
  currentFilename = std::move(fn);
}

template <typename SrcType>
const std::string ImageRequest<SrcType>::GetCurrentFilename() const
{
  if (currentFilename)
    return *currentFilename;
  return std::string("");
}

}
