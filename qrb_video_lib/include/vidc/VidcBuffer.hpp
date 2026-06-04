/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef QRB_VIDEO_V4L2__VIDCBUFFER_HPP_
#define QRB_VIDEO_V4L2__VIDCBUFFER_HPP_

#include <memory>

#include "Buffer.hpp"

extern "C" {
#include "vidc_types.h"
}

namespace qrb::video_v4l2
{
class VidcBuffer : public Buffer
{
public:
  VidcBuffer() = default;
  explicit VidcBuffer(const Buffer & o) : Buffer(o) {}
  ~VidcBuffer() override = default;

  static std::shared_ptr<VidcBuffer> create(size_t size);

  void * mappedAddr() const { return memory ? memory->data() : nullptr; }

  vidc_frame_data_type toFrameData(vidc_buffer_type buf_type) const;
  vidc_buffer_info_type toBufferInfo(vidc_buffer_type buf_type) const;
  void fromFrameData(const vidc_frame_data_type & fd);
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__VIDCBUFFER_HPP_
