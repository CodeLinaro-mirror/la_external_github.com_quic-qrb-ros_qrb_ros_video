/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#include "Memory.hpp"

namespace qrb::video_v4l2
{
MemoryView Memory::view() const
{
  return allocation->view;
}

size_t Memory::size() const
{
  return allocation->size;
}

int Memory::fd() const
{
  return allocation->fd;
}

void Memory::map()
{
  int fd = allocation->fd;
  if (fd == -1) {
    return;
  }
  size_t size = allocation->size;
  off_t offset = allocation->offset;
  base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
}

void Memory::unmap()
{
  if (base != nullptr) {
    munmap(base, allocation->size);
    base = nullptr;
  }
}
}  // namespace qrb::video_v4l2
