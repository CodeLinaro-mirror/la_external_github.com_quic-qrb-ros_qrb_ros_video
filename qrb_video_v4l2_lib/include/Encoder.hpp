/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef QRB_VIDEO_V4L2__VIDEOENCODER_HPP_
#define QRB_VIDEO_V4L2__VIDEOENCODER_HPP_

#include "BufferChannel.hpp"
#include "v4l2/V4l2Encoder.hpp"

namespace qrb::video_v4l2
{
class Encoder
{
public:
  Encoder() = default;
  ~Encoder() = default;

  static std::shared_ptr<Client> create(std::string mime);
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__VIDEOENCODER_HPP_
