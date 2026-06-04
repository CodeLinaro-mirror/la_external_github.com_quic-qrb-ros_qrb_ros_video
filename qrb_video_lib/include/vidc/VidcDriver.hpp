/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef QRB_VIDEO_V4L2__VIDCDRIVER_HPP_
#define QRB_VIDEO_V4L2__VIDCDRIVER_HPP_

#include <cstdint>

extern "C" {
#include "vidc_client.h"
#include "vidc_ioctl.h"
#include "vidc_types.h"
}

namespace qrb::video_v4l2
{
class VidcDriver
{
public:
  class Callback
  {
  public:
    virtual ~Callback() = default;
    virtual void onInputDone(const vidc_frame_data_type & frame) = 0;
    virtual void onOutputDone(const vidc_frame_data_type & frame) = 0;
    virtual void onEvent(vidc_event_type event, uint32_t data) = 0;
  };

  VidcDriver();
  ~VidcDriver();

  bool open();
  void close();
  void destroy();
  int ioctl(uint32_t cmd, void * in, uint32_t in_sz, void * out = nullptr, uint32_t out_sz = 0);
  int ping(uint32_t cmd, void * buf, uint32_t sz);
  void registerCallback(Callback * cb);

  bool mError = false;

private:
  static int bridgeCallback(uint8_t * msg, uint32_t length, void * cd);

  ioctl_session_t * session_ = nullptr;
  Callback * callback_ = nullptr;
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__VIDCDRIVER_HPP_
