/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#ifndef QRB_VIDEO_V4L2__MEMORY_HPP_
#define QRB_VIDEO_V4L2__MEMORY_HPP_

#include <sys/mman.h>

#include <cstdint>
#include <memory>

namespace qrb::video_v4l2
{
enum
{
  BUFFER_MAX_PLANES = 4
};

enum class Format
{
  NV12,
  NV12C,
  P010,
  TP10C,
  H264,
  HEVC,
};

struct Allocation;
struct MemoryView;

struct Memory
{
  enum class Type
  {
    Linear,
    Graphics,
  };

  explicit Memory(const std::shared_ptr<Allocation> & allocation)
    : allocation(allocation), base(nullptr)
  {
  }

  Memory() = delete;

  virtual ~Memory() = default;

  static constexpr Type determineType(const Format & format)
  {
    switch (format) {
      case Format::NV12:
      case Format::NV12C:
      case Format::P010:
      case Format::TP10C:
        return Type::Graphics;
      case Format::H264:
      case Format::HEVC:
      default:
        return Type::Linear;
    }
  }

  [[nodiscard]] MemoryView view() const;

  virtual void map();

  virtual void unmap();

  virtual void * data() { return base; }

  size_t size() const;

  int fd() const;

  std::shared_ptr<Allocation> getAllocation() { return allocation; }

protected:
  std::shared_ptr<Allocation> allocation;
  void * base;
};

constexpr size_t boundary(size_t length, size_t align)
{
  return length % align ? length + align - length % align : length;
}

struct PlaneView
{
  size_t size;
  uint32_t stride;
  uint32_t scanline;
  void * base;
};

struct MemoryView
{
  Memory::Type type;
  Format pixelfmt;
  uint32_t width;
  uint32_t height;
  uint32_t num_planes;
  PlaneView planes[BUFFER_MAX_PLANES];

  static MemoryView create(Format f, uint32_t w, uint32_t h)
  {
    MemoryView view;
    view.pixelfmt = f;
    view.width = w;
    view.height = h;

    view.type = Memory::determineType(f);
    view.determinePlanes();
    return view;
  }

  void determinePlanes()
  {
    switch (pixelfmt) {
      case Format::NV12: {
        num_planes = 2;
        planes[0].stride = boundary(width, 128);
        planes[0].scanline = boundary(height, 32);
        planes[0].size = planes[0].stride * planes[0].scanline;
        planes[1].stride = planes[0].stride;
        planes[1].scanline = planes[0].scanline / 2;
        planes[1].size = planes[1].stride * planes[1].scanline;
      } break;
      // case Format::NV12C:
      // case Format::TP10C:
      case Format::P010: {
        num_planes = 2;
        planes[0].stride = boundary(width, 256);
        planes[0].scanline = boundary(height, 32);
        planes[0].size = planes[0].stride * planes[0].scanline;
        planes[1].stride = planes[0].stride;
        planes[1].scanline = planes[0].scanline / 2;
        planes[1].size = planes[1].stride * planes[1].scanline;
      } break;
      case Format::H264:
      case Format::HEVC:
      default:
        num_planes = 1;
        break;
    }
  }
};

struct Allocation
{
  enum class Usage : uint32_t
  {
    MMAP = 1 << 0,
    DMABUF = 1 << 1,
  };

  virtual ~Allocation() = default;

  friend std::underlying_type_t<Usage> operator&(const Usage a, const Usage b)
  {
    return static_cast<std::underlying_type_t<Usage>>(a) &
           static_cast<std::underlying_type_t<Usage>>(b);
  }

  int fd = -1;
  size_t size;
  off_t offset;
  Usage flags;
  MemoryView view;
};

class Allocator
{
public:
  Allocator() = default;

  virtual ~Allocator() = default;

  virtual std::shared_ptr<Allocation> allocate() = 0;

  virtual void set(const MemoryView & view) = 0;

protected:
  Memory::Type type;
  MemoryView view;
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__MEMORY_HPP_
