/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "vidc/VidcDriver.hpp"

#include <cstring>

#include "utils/Log.hpp"

extern "C" {
#include "vidc_client.h"
#include "vidc_ioctl.h"
#include "vidc_types.h"
}

namespace qrb::video_v4l2
{
VidcDriver::VidcDriver() = default;

VidcDriver::~VidcDriver()
{
  if (session_) {
    close();
  }
}

bool VidcDriver::open()
{
  ioctl_callback_t cb{};
  cb.handler = &VidcDriver::bridgeCallback;
  cb.data = this;
  session_ = device_open(const_cast<char *>(VIDC_DRIVER_DEVICE), &cb);
  if (!session_) {
    LOGE("VidcDriver: device_open failed\n");
    mError = true;
    return false;
  }
  LOGI("VidcDriver: opened %s\n", VIDC_DRIVER_DEVICE);
  return true;
}

void VidcDriver::close()
{
  if (session_) {
    device_close(session_);
    session_ = nullptr;
  }
}

void VidcDriver::destroy()
{
  if (session_) {
    device_destroy(session_);
    session_ = nullptr;
  }
}

int VidcDriver::ioctl(uint32_t cmd, void * in, uint32_t in_sz, void * out, uint32_t out_sz)
{
  LOGI("VidcDriver: ioctl cmd=%u\n", cmd);
  int ret = device_ioctl(session_, cmd,
      reinterpret_cast<uint8 *>(in), in_sz,
      reinterpret_cast<uint8 *>(out), out_sz);
  if (ret != 0) {
    LOGE("VidcDriver: ioctl cmd=%u failed ret=%d\n", cmd, ret);
  }
  return ret;
}

int VidcDriver::ping(uint32_t cmd, void * buf, uint32_t sz)
{
  LOGI("VidcDriver: ping cmd=%u\n", cmd);
  return device_ping(session_, cmd, reinterpret_cast<uint8 *>(buf), sz);
}

void VidcDriver::registerCallback(Callback * cb)
{
  callback_ = cb;
}

int VidcDriver::bridgeCallback(uint8_t * msg, uint32_t length, void * cd)
{
  auto * self = reinterpret_cast<VidcDriver *>(cd);
  if (!self || !self->callback_ || !msg || length < sizeof(vidc_drv_msg_info_type)) {
    return -1;
  }

  auto * info = reinterpret_cast<vidc_drv_msg_info_type *>(msg);
  switch (info->event_type) {
    case VIDC_EVT_RESP_INPUT_DONE:
      LOGI("VidcDriver: INPUT_DONE\n");
      self->callback_->onInputDone(info->payload.frame_data);
      break;
    case VIDC_EVT_RESP_OUTPUT_DONE:
      LOGI("VidcDriver: OUTPUT_DONE\n");
      self->callback_->onOutputDone(info->payload.frame_data);
      break;
    default:
      LOGI("VidcDriver: event %#x data=%u\n", info->event_type, info->payload.event_data_1);
      self->callback_->onEvent(info->event_type, info->payload.event_data_1);
      break;
  }
  return 0;
}
}  // namespace qrb::video_v4l2
