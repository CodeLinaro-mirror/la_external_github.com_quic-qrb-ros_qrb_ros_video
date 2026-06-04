/*
 **************************************************************************************************
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **************************************************************************************************
 */

#ifndef QRB_VIDEO_V4L2__BUFFERPOOL_HPP_
#define QRB_VIDEO_V4L2__BUFFERPOOL_HPP_

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>

#include "Buffer.hpp"

namespace qrb::video_v4l2
{
class BufferPool
{
public:
  explicit BufferPool() : count(0), capacity(32) {}

  explicit BufferPool(Memory::Type type) : type(type), count(0), capacity(32) {}

  virtual ~BufferPool()
  {
    using namespace std::chrono_literals;
    while (free.size() < count) {
      std::this_thread::sleep_for(500ms);
    }
    free.clear();
    used.clear();
    buffers.clear();
  }

  bool setCapacity(size_t new_size)
  {
    bool success = false;
    if (new_size > capacity || count < new_size) {
      capacity = new_size;
      success = true;
    }
    return success;
  }

  void acquire(std::shared_ptr<Buffer> & b)
  {
    auto deleter = [&](Buffer * ptr) { release(ptr->index); };

    std::lock_guard l(lock);
    if (not free.empty()) {
      auto index = free.front();
      used.push_back(index);
      free.pop_front();
      auto pick = buffers[index];
      // alias the buffer and return back when alias use count become zero.
      b.reset(pick.get(), deleter);
    } else if (allocator && count <= capacity) {
      auto allocation = allocator->allocate();
      auto newBuffer = Buffer::create(allocation);
      newBuffer->index = count++;
      used.push_back(newBuffer->index);
      buffers[newBuffer->index] = newBuffer;
      b.reset(newBuffer.get(), deleter);
    } else {
      // TODO: Error
    }
  }

  void release(const size_t index)
  {
    std::lock_guard l(lock);
    auto found = std::find(used.begin(), used.end(), index);
    if (found != used.end()) {
      used.erase(found);
      free.push_back(index);
    } else {
      // TODO: Error
    }
  }

  bool registerBuffer(const std::shared_ptr<Buffer> & b)
  {
    std::lock_guard l(lock);
    buffers[b->index] = b;
    free.push_back(b->index);
    count++;
    return true;
  }

  bool unregisterBuffer(const std::shared_ptr<Buffer> & b)
  {
    std::lock_guard l(lock);
    auto found = std::find(free.begin(), free.end(), b->index);
    if (found != free.end()) {
      free.erase(found);
      buffers.erase(b->index);
      if (count > 0)
        count--;
      return true;
    }
    return false;
  }

  bool registerAllocator(const std::shared_ptr<Allocator> & a)
  {
    if (not allocator)
      allocator = a;
    return true;
  }

private:
  std::mutex lock;
  std::deque<int> free;
  std::deque<int> used;
  std::map<size_t, std::shared_ptr<Buffer>> buffers;
  Memory::Type type;
  size_t count;
  size_t capacity;

  std::shared_ptr<Allocator> allocator;
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__BUFFERPOOL_HPP_
