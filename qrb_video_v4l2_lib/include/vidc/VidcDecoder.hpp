/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef QRB_VIDEO_V4L2__VIDCDECODER_HPP_
#define QRB_VIDEO_V4L2__VIDCDECODER_HPP_

#include <memory>

#include "BufferChannel.hpp"
#include "VideoCodec.hpp"
#include "vidc/VidcCodec.hpp"

namespace qrb::video_v4l2
{
class VidcDecoder : public VidcCodec
{
public:
  explicit VidcDecoder(Format compressed);
  ~VidcDecoder() override = default;

  static std::shared_ptr<Client> create(Format compressed);

  bool configure(const Setting & s) override;
  bool start() override;
  bool stop() override;

protected:
  void setSessionCodec() override;
  void configureProperties() override;
  void reconfigureOutputPort() override;

private:
  Format outputPixelFormat_ = Format::NV12;
  Resolution resolution_{};
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__VIDCDECODER_HPP_
