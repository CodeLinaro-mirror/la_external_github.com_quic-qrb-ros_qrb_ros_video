/*
 **************************************************************************************************
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **************************************************************************************************
 */

#ifndef QRB_VIDEO_V4L2__V4L2DRIVER_HPP_
#define QRB_VIDEO_V4L2__V4L2DRIVER_HPP_
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>

#define INPUT_PLANES 1
#define INPUT_MPLANE V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
#define OUTPUT_MPLANE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE

namespace qrb::video_v4l2
{
class V4l2Driver
{
public:
  class Callback
  {
  public:
    virtual ~Callback() = default;

    virtual bool onV4l2BufferDone(v4l2_buffer * buffer) = 0;
    virtual bool onV4l2EventDone(v4l2_event * event) = 0;
    virtual bool onV4l2Error(error_t error) = 0;
  };

  explicit V4l2Driver(std::string & name);

  void start();
  void stop();
  void pause();
  void resume();

  int streamOn(int port) const;
  int streamOff(int port) const;
  int getFormat(v4l2_format * fmt) const;
  int setCodecPixelFmt(uint32_t planeType, uint32_t codecPixFmt) const;
  int getSelection(v4l2_selection * sel) const;
  int getControl(v4l2_control * ctrl) const;
  int setControl(v4l2_control * ctrl) const;
  int reqBufs(v4l2_requestbuffers * reqbufs) const;
  int queueBuf(v4l2_buffer * buf) const;
  int decCommand(v4l2_decoder_cmd * cmd) const;
  int encCommand(v4l2_encoder_cmd * cmd) const;
  int setFormat(v4l2_format * fmt) const;
  int setParam(v4l2_streamparm * sparm) const;
  int setSelection(v4l2_selection * sel) const;
  int queryCapabilities(v4l2_capability * caps) const;
  int queryMenu(v4l2_querymenu * querymenu) const;
  int queryControl(v4l2_queryctrl * ctrl) const;
  int enumFormat(v4l2_fmtdesc * fmtdesc) const;
  int enumFramesize(v4l2_frmsizeenum * frmsize) const;
  int enumFrameInterval(v4l2_frmivalenum * fival) const;
  int subscribeEvent(unsigned int event_type) const;
  int unsubscribeEvent(unsigned int event_type) const;
  int registerCallbacks(Callback * cb);

  bool mError = false;

private:
  int devfd;
  std::string devName;
  std::thread pollThread;
  std::atomic<bool> pollThreadRunning;
  std::atomic<bool> pollThreadPaused;
  std::condition_variable pauseNotifier;
  std::mutex pauseMutex;

  Callback * callback = nullptr;

  void threadLoop();
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__V4L2DRIVER_HPP_
