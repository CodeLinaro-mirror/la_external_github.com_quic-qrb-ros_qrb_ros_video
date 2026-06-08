/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef QRB_VIDEO_V4L2__VIDCENCODER_HPP_
#define QRB_VIDEO_V4L2__VIDCENCODER_HPP_

#include <memory>
#include <unordered_map>

#include "BufferChannel.hpp"
#include "VideoCodec.hpp"
#include "vidc/VidcCodec.hpp"

namespace qrb::video_v4l2
{
class VidcEncoder : public VidcCodec
{
public:
  explicit VidcEncoder(Format compressed);
  ~VidcEncoder() override = default;

  static std::shared_ptr<Client> create(Format compressed);

  bool configure(const Setting & s) override;
  bool start() override;
  bool stop() override;

protected:
  void setSessionCodec() override;
  void configureProperties() override;

private:
  Profile profile_{};
  Level level_{};
  Bitrate bitrate_{};
  Framerate framerate_{};
  Resolution resolution_{};
  Format pixelFormat_ = Format::NV12;

  std::unordered_map<uint32_t, uint32_t> avcProfileMap_;
  std::unordered_map<uint32_t, uint32_t> hevcProfileMap_;
  std::unordered_map<uint32_t, uint32_t> avcLevelMap_;
  std::unordered_map<uint32_t, uint32_t> hevcLevelMap_;
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__VIDCENCODER_HPP_
