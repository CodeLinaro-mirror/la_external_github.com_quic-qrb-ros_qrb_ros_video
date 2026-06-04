/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#ifndef QRB_VIDEO_V4L2__LOG_HPP_
#define QRB_VIDEO_V4L2__LOG_HPP_

#include <glog/logging.h>

#include <cstdarg>
#include <mutex>

namespace qrb::video_v4l2
{
class Log
{
public:
  Log() = default;
  ~Log() = default;

  constexpr static uint32_t ERROR = 0x1;
  constexpr static uint32_t WARNING = 0x2;
  constexpr static uint32_t INFO = 0x4;

  static std::once_flag flag;
  static uint32_t debug_level;

  static void init() __attribute__((constructor))
  {
    std::call_once(flag, []() {
      google::InitGoogleLogging("video_v4l2");
      google::LogToStderr();
      char * level = getenv("VIDEO_V4L2_DEBUG");
      if (level != nullptr)
        debug_level = strtoul(level, nullptr, 16);
      else
        debug_level = ERROR;
    });
  }

  static void logError(const char * file, int line, const char * fmt, ...)
  {
    if (debug_level & ERROR) {
      va_list args;
      va_start(args, fmt);
      google::LogMessage(file, line, google::GLOG_ERROR).stream() << format(fmt, args);
      va_end(args);
    }
  }

  static void logWarning(const char * file, int line, const char * fmt, ...)
  {
    if (debug_level & WARNING) {
      va_list args;
      va_start(args, fmt);
      google::LogMessage(file, line, google::GLOG_WARNING).stream() << format(fmt, args);
      va_end(args);
    }
  }

  static void logInfo(const char * file, int line, const char * fmt, ...)
  {
    if (debug_level & INFO) {
      va_list args;
      va_start(args, fmt);
      google::LogMessage(file, line, google::GLOG_INFO).stream() << format(fmt, args);
      va_end(args);
    }
  }

  static std::string format(const char * fmt, std::va_list args)
  {
    const int n = vsnprintf(nullptr, 0, fmt, args);
    std::string s;
    s.reserve(n);
    s.resize(n);
    vsnprintf(s.data(), n + 1, fmt, args);

    return s;
  }
};

#define LOGI(...) Log::logInfo(__FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...) Log::logWarning(__FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...) Log::logError(__FILE__, __LINE__, __VA_ARGS__)

}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__LOG_HPP_
