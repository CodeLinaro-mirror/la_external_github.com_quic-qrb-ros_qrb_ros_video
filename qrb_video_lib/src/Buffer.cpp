/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#include "Buffer.hpp"

namespace qrb::video_v4l2
{
std::shared_ptr<Buffer> Buffer::create(std::shared_ptr<Allocation> & allocation)
{
  auto buffer = std::make_shared<Buffer>();
  buffer->memory = std::make_shared<Memory>(allocation);
  buffer->type = allocation->view.type;
  return buffer;
}

MemoryView Buffer::view()
{
  return memory->view();
}
}  // namespace qrb::video_v4l2
