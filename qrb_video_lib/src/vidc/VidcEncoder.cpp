/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "vidc/VidcEncoder.hpp"

#include <any>

extern "C" {
#include "vidc_ioctl.h"
#include "vidc_types.h"
}

namespace qrb::video_v4l2
{
VidcEncoder::VidcEncoder(Format compressed) : VidcCodec(CodecType::VideoEncoder, compressed)
{
  avcProfileMap_ = {
    { Profile::AVC_BASELINE, VIDC_PROFILE_H264_BASELINE },
    { Profile::AVC_CONSTRAINED_BASELINE, VIDC_PROFILE_H264_CONSTRAINED_BASE },
    { Profile::AVC_MAIN, VIDC_PROFILE_H264_MAIN },
    { Profile::AVC_HIGH, VIDC_PROFILE_H264_HIGH },
    { Profile::AVC_CONSTRAINED_HIGH, VIDC_PROFILE_H264_CONSTRAINED_HIGH },
  };

  hevcProfileMap_ = {
    { Profile::HEVC_MAIN, VIDC_PROFILE_HEVC_MAIN },
    { Profile::HEVC_MAIN10, VIDC_PROFILE_HEVC_MAIN10 },
  };

  avcLevelMap_ = {
    { Level::AVC_1_0, VIDC_LEVEL_H264_1 },
    { Level::AVC_1_B, VIDC_LEVEL_H264_1b },
    { Level::AVC_1_1, VIDC_LEVEL_H264_1p1 },
    { Level::AVC_1_2, VIDC_LEVEL_H264_1p2 },
    { Level::AVC_1_3, VIDC_LEVEL_H264_1p3 },
    { Level::AVC_2_0, VIDC_LEVEL_H264_2 },
    { Level::AVC_2_1, VIDC_LEVEL_H264_2p1 },
    { Level::AVC_2_2, VIDC_LEVEL_H264_2p2 },
    { Level::AVC_3_0, VIDC_LEVEL_H264_3 },
    { Level::AVC_3_1, VIDC_LEVEL_H264_3p1 },
    { Level::AVC_3_2, VIDC_LEVEL_H264_3p2 },
    { Level::AVC_4_0, VIDC_LEVEL_H264_4 },
    { Level::AVC_4_1, VIDC_LEVEL_H264_4p1 },
    { Level::AVC_4_2, VIDC_LEVEL_H264_4p2 },
    { Level::AVC_5_0, VIDC_LEVEL_H264_5 },
    { Level::AVC_5_1, VIDC_LEVEL_H264_5p1 },
    { Level::AVC_5_2, VIDC_LEVEL_H264_5p2 },
    { Level::AVC_6_0, VIDC_LEVEL_H264_6 },
  };

  hevcLevelMap_ = {
    { Level::HEVC_1_0, VIDC_LEVEL_HEVC_1 },
    { Level::HEVC_2_0, VIDC_LEVEL_HEVC_2 },
    { Level::HEVC_2_1, VIDC_LEVEL_HEVC_21 },
    { Level::HEVC_3_0, VIDC_LEVEL_HEVC_3 },
    { Level::HEVC_3_1, VIDC_LEVEL_HEVC_31 },
    { Level::HEVC_4_0, VIDC_LEVEL_HEVC_4 },
    { Level::HEVC_4_1, VIDC_LEVEL_HEVC_41 },
    { Level::HEVC_5_0, VIDC_LEVEL_HEVC_5 },
    { Level::HEVC_5_1, VIDC_LEVEL_HEVC_51 },
    { Level::HEVC_5_2, VIDC_LEVEL_HEVC_52 },
    { Level::HEVC_6_0, VIDC_LEVEL_HEVC_6 },
    { Level::HEVC_6_1, VIDC_LEVEL_HEVC_61 },
    { Level::HEVC_6_2, VIDC_LEVEL_HEVC_62 },
  };
}

std::shared_ptr<Client> VidcEncoder::create(Format compressed)
{
  auto encoder = std::make_shared<VidcEncoder>(compressed);
  auto client = std::make_shared<Client>();
  client->setChannelCB(encoder);
  encoder->setNotifier(client);
  return client;
}

bool VidcEncoder::configure(const Setting & s)
{
  switch (s.type) {
    case Setting::PROFILE:
      profile_ = std::any_cast<Profile>(s.data);
      break;
    case Setting::LEVEL:
      level_ = std::any_cast<Level>(s.data);
      break;
    case Setting::BITRATE:
      bitrate_ = std::any_cast<Bitrate>(s.data);
      break;
    case Setting::FRAMERATE:
      framerate_ = std::any_cast<Framerate>(s.data);
      break;
    case Setting::RESOLUTION:
      resolution_ = std::any_cast<Resolution>(s.data);
      break;
    case Setting::FORMAT:
      pixelFormat_ = std::any_cast<Format>(s.data);
      break;
    default:
      break;
  }
  return VidcCodec::configure(s);
}

void VidcEncoder::setSessionCodec()
{
  vidc_session_codec_type sc{};
  sc.session = VIDC_SESSION_ENCODE;
  sc.codec = (compressedFormat_ == Format::H264) ? VIDC_CODEC_H264 : VIDC_CODEC_HEVC;
  setProperty(VIDC_I_SESSION_CODEC, &sc, sizeof(sc));
}

void VidcEncoder::configureProperties()
{
  // Frame size. The buf_type field is required (vidc_buffer_type starts at
  // 0x1; the zero-initialised default is rejected as VIDC_ERR_BAD_PARAM).
  // Setting it on the input port specifies the input YUV dimensions; the
  // encoder reuses them for the output bitstream when no scaling is
  // configured (driver docstring on VIDC_I_FRAME_SIZE).
  vidc_frame_size_type fs{};
  fs.buf_type = VIDC_BUFFER_INPUT;
  fs.width = resolution_.width;
  fs.height = resolution_.height;
  setProperty(VIDC_I_FRAME_SIZE, &fs, sizeof(fs));

  // Frame rate. Q16 fixed-point: the driver expects numerator/denominator in
  // 16.16 format (gst-plugin-vidc GstClient::configureEncoder), and a raw
  // integer ratio is rejected with VIDC_ERR_FAIL. Output must be set before
  // input - the driver enforces this ordering and rejects an input-only
  // configure on an unconfigured output port.
  vidc_frame_rate_type fr{};
  fr.fps_numerator = framerate_.value * 0x10000;
  fr.fps_denominator = 0x10000;
  fr.buf_type = VIDC_BUFFER_OUTPUT;
  setProperty(VIDC_I_FRAME_RATE, &fr, sizeof(fr));
  fr.buf_type = VIDC_BUFFER_INPUT;
  setProperty(VIDC_I_FRAME_RATE, &fr, sizeof(fr));

  // Color format on input port
  vidc_color_format_config_type cf{};
  cf.buf_type = VIDC_BUFFER_INPUT;
  cf.color_format = (pixelFormat_ == Format::P010) ? VIDC_COLOR_FORMAT_NV12_P010 :
                                                     VIDC_COLOR_FORMAT_NV12;
  setProperty(VIDC_I_COLOR_FORMAT, &cf, sizeof(cf));

  // Profile
  vidc_profile_type prof{};
  if (compressedFormat_ == Format::H264) {
    auto it = avcProfileMap_.find(profile_.value);
    prof.profile = (it != avcProfileMap_.end()) ? it->second : VIDC_PROFILE_H264_BASELINE;
  } else {
    auto it = hevcProfileMap_.find(profile_.value);
    prof.profile = (it != hevcProfileMap_.end()) ? it->second : VIDC_PROFILE_HEVC_MAIN;
  }
  setProperty(VIDC_I_PROFILE, &prof, sizeof(prof));

  // Level
  vidc_level_type lvl{};
  if (compressedFormat_ == Format::H264) {
    auto it = avcLevelMap_.find(level_.value);
    lvl.level = (it != avcLevelMap_.end()) ? it->second : VIDC_LEVEL_H264_1;
  } else {
    auto it = hevcLevelMap_.find(level_.value);
    lvl.level = (it != hevcLevelMap_.end()) ? it->second : VIDC_LEVEL_HEVC_1;
  }
  setProperty(VIDC_I_LEVEL, &lvl, sizeof(lvl));

  // Target bitrate
  vidc_target_bitrate_type tb{};
  tb.target_bitrate = static_cast<uint32>(bitrate_.value);
  setProperty(VIDC_I_TARGET_BITRATE, &tb, sizeof(tb));

  // Rate control
  vidc_rate_control_mode_type rc;
  switch (bitrate_.mode) {
    case Bitrate::CBR:
      rc = VIDC_RATE_CONTROL_CBR_CFR;
      break;
    case Bitrate::VBR:
      rc = VIDC_RATE_CONTROL_VBR_CFR;
      break;
    default:
      rc = VIDC_RATE_CONTROL_OFF;
      break;
  }
  setProperty(VIDC_I_ENC_RATE_CONTROL, &rc, sizeof(rc));
}

bool VidcEncoder::start()
{
  return VidcCodec::start();
}

bool VidcEncoder::stop()
{
  // Drain before stopping so queued raw frames are fully encoded and emitted.
  // drain() is idempotent (guarded by drained_), so if an EOS input buffer
  // already triggered the drain via queueBuffer this is a no-op. publish=false:
  // stop() is teardown, so flush the driver's held buffers without dispatching
  // into a publisher that is being destroyed. DRAIN takes no payload and is
  // acked by VIDC_EVT_RESP_DRAIN; it is a regular ioctl on this driver (see
  // gst-plugin-vidc GstClient::stateIdle), not a ping.
  drain(false);
  return VidcCodec::stop();
}
}  // namespace qrb::video_v4l2
