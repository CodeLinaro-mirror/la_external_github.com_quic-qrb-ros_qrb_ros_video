/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#include "Handler.hpp"

#include "Log.hpp"
#include "Looper.hpp"

namespace qrb::video_v4l2
{
Handler::Handler()
{
  if (looper == nullptr) {
    looper = std::make_shared<Looper>();
  }
}

Handler::Handler(const Handler * another)
{
  if (another == nullptr || another->looper == nullptr) {
    looper = std::make_shared<Looper>();
  } else if (looper == nullptr) {
    looper = another->looper;
  }
}

std::shared_ptr<Message> Handler::obtainMessage(int what)
{
  auto msg = std::make_shared<Message>(what);

  msg->handler = shared_from_this();
  return msg;
}

int32_t Handler::sendMessage(std::shared_ptr<Message> & message)
{
  if (not(message->flags & Flags::SYNC)) {
    message->flags = Flags::SYNC;
    LOGI("send msg in sync %x", message->flags);
  }
  sendMessageAsync(message);
  int32_t result = 0;

  if (message->flags & Flags::SYNC) {
    LOGI("wait for msg done");
    auto future = message->promise.get_future();
    if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
      result = future.get();
    } else {
      LOGE("wait for msg done timeout");
      result = 0;
    }
  }

  return result;
}

bool Handler::sendMessageAsync(std::shared_ptr<Message> & message)
{
  if (message->handler.use_count() == 0) {
    message->handler = shared_from_this();
  }

  bool ret = looper->sendMessage(message);
  return ret;
}

bool Handler::finishMessage(const std::shared_ptr<Message> & msg, value_type ret)
{
  if (msg->flags & Handler::Flags::SYNC) {
    msg->promise.set_value(ret);
  }

  return true;
}
}  // namespace qrb::video_v4l2
