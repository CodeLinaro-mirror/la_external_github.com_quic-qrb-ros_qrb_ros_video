/*
 **************************************************************************************************
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **************************************************************************************************
 */

#ifndef QRB_VIDEO_V4L2__LOOPER_HPP_
#define QRB_VIDEO_V4L2__LOOPER_HPP_

#include <atomic>
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

  // Stop the loop and join the worker thread. Safe to call from any thread and
  // idempotent. When invoked from the worker thread itself (i.e. the owning
  // object is being destroyed on the looper thread) it detaches instead of
  // self-joining, which would otherwise throw EDEADLK ("Resource deadlock
  // avoided") out of the destructor.
  void quit();

private:
  std::thread looper;
  std::vector<std::shared_ptr<Message>> messages;
  std::mutex lock;
  std::condition_variable cv;
  std::atomic<bool> running = false;
  void loop();
};

}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__LOOPER_HPP_
