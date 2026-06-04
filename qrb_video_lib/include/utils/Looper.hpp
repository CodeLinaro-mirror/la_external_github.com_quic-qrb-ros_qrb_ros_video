/*
 **************************************************************************************************
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **************************************************************************************************
 */

#ifndef QRB_VIDEO_V4L2__LOOPER_HPP_
#define QRB_VIDEO_V4L2__LOOPER_HPP_

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "Handler.hpp"

namespace qrb::video_v4l2
{
class Looper
{
public:
  Looper();
  ~Looper();

  virtual bool sendMessage(const std::shared_ptr<Message> & msg);
  virtual bool handleMessage(const std::shared_ptr<Message> & msg);

private:
  std::thread looper;
  std::vector<std::shared_ptr<Message>> messages;
  std::mutex lock;
  std::condition_variable cv;
  bool running = false;

  void loop();
};

}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__LOOPER_HPP_
