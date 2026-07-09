/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#include "Looper.hpp"

namespace qrb::video_v4l2
{
Looper::Looper()
{
  running = true;
  looper = std::thread(&Looper::loop, this);
}

Looper::~Looper()
{
  quit();
}

void Looper::quit()
{
  running = false;
  cv.notify_all();

  if (!looper.joinable())
    return;

  if (looper.get_id() == std::this_thread::get_id()) {
    // Being torn down on our own thread: joining ourselves would throw
    // EDEADLK. Detach so the loop can unwind; running == false guarantees it
    // exits after the current message completes.
    looper.detach();
  } else {
    looper.join();
  }
}

bool Looper::sendMessage(const std::shared_ptr<Message> & msg)
{
  {
    std::lock_guard l(lock);
    messages.push_back(msg);
  }
  cv.notify_one();
  return true;
}

bool Looper::handleMessage(const std::shared_ptr<Message> & msg)
{
  bool ret = false;
  if (auto handler = msg->handler.lock()) {
    ret = handler->handleMessage(msg);
  }
  return ret;
}

void Looper::loop()
{
  while (running) {
    std::shared_ptr<Message> msg;

    // Scope for the lock to minimize critical section
    {
      std::unique_lock<std::mutex> lk(lock);
      cv.wait_for(
          lk, std::chrono::milliseconds(10), [this]() { return !messages.empty() || !running; });

      // Check if we're still running after wait
      if (!running)
        break;
      if (messages.empty())
        continue;

      msg = messages.front();
      messages.erase(messages.begin());
    }  // lock is released here

    // Process message outside the critical section
    if (msg) {
      handleMessage(msg);
    }
  }
}
}  // namespace qrb::video_v4l2
