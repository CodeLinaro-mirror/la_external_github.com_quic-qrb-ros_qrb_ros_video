/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#ifndef QRB_VIDEO_V4L2__BUFFER_HPP_
#define QRB_VIDEO_V4L2__BUFFER_HPP_

#include "Memory.hpp"

namespace qrb::video_v4l2
{
class Buffer : std::enable_shared_from_this<Buffer>
{
public:
  Buffer() = default;

  virtual ~Buffer() = default;

  static std::shared_ptr<Buffer> create(std::shared_ptr<Allocation> & allocation);

  explicit Buffer(const Buffer & o)
  {
    this->type = o.type;
    this->flags = o.flags;
    this->timestamp = o.timestamp;
    this->index = o.index;
    this->sequence = o.sequence;
    this->memory = o.memory;
    this->bytesused = o.bytesused;
    this->offset = o.offset;
  }

  enum class Flags
  {
    EOS = 1 << 0,
  };

  MemoryView view();

  void release()
  {
    // Force release all refer count.
    shared_from_this().reset();
  }

  bool setTimestamp(const timeval time)
  {
    timestamp = time;
    return true;
  }

  [[nodiscard]] timeval getTimestamp() const { return timestamp; }

  size_t size() const { return memory->size(); }

  size_t length() const { return bytesused; }

  void setEOS()
  {
    if ((flags & Flags::EOS) == 0) {
      flags = Flags::EOS;
    }
  }

  bool isEOS() const { return (flags & Flags::EOS) != 0; }

  void map() const { memory->map(); }

  void unmap() const { memory->unmap(); }

  void * data() const { return memory->data(); }

  int fd() const { return memory->fd(); }

  Memory::Type type;
  Flags flags;
  timeval timestamp;
  size_t index;
  size_t sequence;
  size_t bytesused;
  off_t offset;
  std::shared_ptr<Memory> memory;

  friend class BufferPool;
  friend std::underlying_type_t<Flags> operator&(const Flags a, const Flags b)
  {
    return static_cast<std::underlying_type_t<Flags>>(a) &
           static_cast<std::underlying_type_t<Flags>>(b);
  }

  friend Flags operator|(const Flags a, const Flags b)
  {
    return static_cast<Flags>(static_cast<std::underlying_type_t<Flags>>(a) |
                              static_cast<std::underlying_type_t<Flags>>(b));
  }
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__BUFFER_HPP_
