/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "vidc/VidcDecoder.hpp"

#include <any>

#include "utils/Log.hpp"

extern "C" {
#include "vidc_ioctl.h"
#include "vidc_types.h"
}

namespace qrb::video_v4l2
{
VidcDecoder::VidcDecoder(Format compressed) : VidcCodec(CodecType::VideoDecoder, compressed) {}

std::shared_ptr<Client> VidcDecoder::create(Format compressed)
{
  auto decoder = std::make_shared<VidcDecoder>(compressed);
  auto client = std::make_shared<Client>();
  client->setChannelCB(decoder);
  decoder->setNotifier(client);
  return client;
}

bool VidcDecoder::configure(const Setting & s)
{
  if (s.type == Setting::FORMAT) {
    outputPixelFormat_ = std::any_cast<Format>(s.data);
  }
  return VidcCodec::configure(s);
}

void VidcDecoder::setSessionCodec()
{
  vidc_session_codec_type sc{};
  sc.session = VIDC_SESSION_DECODE;
  sc.codec = (compressedFormat_ == Format::H264) ? VIDC_CODEC_H264 : VIDC_CODEC_HEVC;
  setProperty(VIDC_I_SESSION_CODEC, &sc, sizeof(sc));
}

void VidcDecoder::configureProperties()
{
  vidc_color_format_config_type cf{};
  cf.buf_type = VIDC_BUFFER_OUTPUT;
  cf.color_format = (outputPixelFormat_ == Format::P010) ? VIDC_COLOR_FORMAT_NV12_P010 :
                                                           VIDC_COLOR_FORMAT_NV12;
  setProperty(VIDC_I_COLOR_FORMAT, &cf, sizeof(cf));
}

bool VidcDecoder::start()
{
  return VidcCodec::start();
}

bool VidcDecoder::stop()
{
  return VidcCodec::stop();
}

void VidcDecoder::reconfigureOutputPort()
{
  // Mirrors gst-plugin-vidc's EVENT_RECONFIG handler in gstqvidcvdec.c:
  //   stop output -> drop old pool -> re-query reqs -> alloc & SET_BUFFER
  //   new pool -> start output -> FILL_OUTPUT_BUFFER each.
  // The only thing we don't replicate is mWaitLastFlagToReconfig: the per-
  // port STOP_OUTPUT_DONE on this driver is delivered after the driver has
  // drained pending output buffers, so the explicit LAST_FLAG handshake is
  // not required for an initial post-headers reconfigure.
  LOGI("VidcDecoder: reconfigureOutputPort begin\n");

  // Suppress feedOutputBuffer() in onOutputDone while we tear the port down.
  state_ = RECONFIGURING;

  // 1. Stop the output port and wait for the ack from the driver poll thread.
  vidc_stop_mode_type stop_mode = VIDC_STOP_OUTPUT;
  armEvent(VIDC_EVT_RESP_STOP_OUTPUT_DONE);
  driver_->ioctl(VIDC_IOCTL_STOP, &stop_mode, sizeof(stop_mode), nullptr, 0);
  if (!waitForEvent()) {
    LOGE("VidcDecoder: STOP output timed out during reconfigure\n");
    return;
  }

  // 2. Free the old output buffers and drop the pool. Hold the codec mutex
  // so concurrent dispatchBuffer / pool acquires see a consistent state.
  {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto & [idx, buf] : outputBuffers_) {
      vidc_buffer_info_type info = buf->toBufferInfo(VIDC_BUFFER_OUTPUT);
      driver_->ioctl(VIDC_IOCTL_FREE_BUFFER, &info, sizeof(info), nullptr, 0);
    }
    outputBuffers_.clear();
    outputPool_.reset();
  }

  // 3. Re-query buffer requirements - count and size may have changed (this
  // is the whole point of the reconfigure event).
  uint32_t count = 0, size = 0;
  if (!queryBufferRequirements(VIDC_BUFFER_OUTPUT, count, size)) {
    LOGE("VidcDecoder: queryBufferRequirements failed during reconfigure\n");
    return;
  }

  // 4. Allocate fresh buffers. SET_BUFFER is intentionally NOT called: in
  // DYNAMIC buffer mode the driver registers each buffer when it first sees
  // it via FILL_OUTPUT_BUFFER below. Calling SET_BUFFER here would corrupt
  // the dynamic pool (see VidcCodec::allocateBuffers).
  {
    std::lock_guard<std::mutex> lk(mutex_);
    outputPool_ = std::make_shared<BufferPool>();
    for (uint32_t i = 0; i < count; i++) {
      auto buf = VidcBuffer::create(size);
      buf->index = i;
      outputBuffers_[i] = buf;
      outputPool_->registerBuffer(buf);
    }
  }

  // 5. Start the output port back up.
  vidc_start_mode_type start_mode = VIDC_START_OUTPUT;
  armEvent(VIDC_EVT_RESP_START_OUTPUT_DONE);
  driver_->ioctl(VIDC_IOCTL_START, &start_mode, sizeof(start_mode), nullptr, 0);
  if (!waitForEvent()) {
    LOGE("VidcDecoder: START output timed out during reconfigure\n");
    return;
  }

  state_ = STARTED;

  // 6. Feed all the new output buffers so the driver has somewhere to put
  // decoded frames.
  std::lock_guard<std::mutex> lk(mutex_);
  for (auto & [idx, buf] : outputBuffers_) {
    feedOutputBuffer(buf);
  }
  LOGI("VidcDecoder: reconfigureOutputPort done (count=%u size=%u)\n", count, size);
}
}  // namespace qrb::video_v4l2
