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

namespace zuluide::pipe {

  enum class response_status_t {None, End, More};
 
  class ImageResponse{
    public:
    ImageResponse();
    ImageResponse(const ImageResponse& src);
    ImageResponse(ImageResponse&& src);
    ImageResponse& operator= (ImageResponse&& src);
    ImageResponse& operator= (const ImageResponse& src);

    void SetImage(std::unique_ptr<zuluide::images::Image>&& value);
    void SetStatus(const response_status_t value);
    void SetRequest(std::unique_ptr<ImageRequest>&& value);
    void SetIsLast(const bool value);
    void SetIsFirst(const bool value);
    
    const zuluide::images::Image GetImage() const;
    const response_status_t GetStatus() const;
    const ImageRequest GetRequest() const;
    const bool IsLast() const;
    const bool IsFirst() const;

    private:
    response_status_t status;
    std::unique_ptr<zuluide::images::Image> image;
    std::unique_ptr<ImageRequest> request;
    bool isLast;
    bool isFirst;
  };
}
