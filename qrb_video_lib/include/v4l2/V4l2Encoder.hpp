/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#ifndef QRB_VIDEO_V4L2__V4L2ENCODER_HPP_
#define QRB_VIDEO_V4L2__V4L2ENCODER_HPP_

#include "BufferChannel.hpp"
#include "V4l2Codec.hpp"

namespace qrb::video_v4l2
{
class V4l2Encoder : public V4l2Codec
{
public:
  explicit V4l2Encoder(const Format & compressed)
    : V4l2Codec(CodecType::VideoEncoder, std::string("/dev/video33"))
  {
    compressedFormat = compressed;
  };
  ~V4l2Encoder() override = default;

  static std::shared_ptr<Client> create(const Format & compressed);

  bool configure(const Setting & s) override;

  bool start() override;

  bool stop() override;

private:
  void setCodecFormat();
  bool drain() override;
};
}  // namespace qrb::video_v4l2
#endif  // QRB_VIDEO_V4L2__V4L2ENCODER_HPP_
