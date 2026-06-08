/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "vidc/VidcBuffer.hpp"

#include "Memory.hpp"
#include "v4l2/V4l2Memory.hpp"

extern "C" {
#include "vidc_types.h"
}

namespace qrb::video_v4l2
{
std::shared_ptr<VidcBuffer> VidcBuffer::create(size_t size)
{
  auto allocator = std::make_shared<DmabufAllocator>();
  MemoryView view{};
  view.pixelfmt = Format::H264;
  view.type = Memory::Type::Linear;
  view.num_planes = 1;
  view.planes[0].size = size;
  view.planes[0].stride = 0;
  view.planes[0].scanline = 0;
  allocator->set(view);

  auto allocation = allocator->allocate();
  auto buf = std::make_shared<VidcBuffer>();
  buf->memory = std::make_shared<Memory>(allocation);
  buf->type = Memory::Type::Linear;
  buf->bytesused = 0;
  buf->offset = 0;
  buf->timestamp = {};
  buf->memory->map();
  return buf;
}

vidc_frame_data_type VidcBuffer::toFrameData(vidc_buffer_type buf_type) const
{
  vidc_frame_data_type fd{};
  // The buffer is registered by handle via SET_BUFFER (see toBufferInfo), so
  // we do NOT set frame_addr here - the driver uses its kernel-side mapping.
  // metadata_handle must match the extradata_buf_handle that was registered
  // in toBufferInfo(), which is 0 (no metadata buffer). The driver compares
  // the two and rejects the submission on any mismatch.
  fd.alloc_len = static_cast<uint32>(memory->size());
  fd.data_len = static_cast<uint32>(bytesused);
  fd.offset = 0;
  fd.timestamp =
      static_cast<vidc_timestamp_type>(timestamp.tv_sec * 1000000LL + timestamp.tv_usec);
  fd.flags = isEOS() ? VIDC_FRAME_FLAG_EOS : 0;
  fd.frm_clnt_data = static_cast<uint64>(index);
  fd.input_tag = static_cast<unsigned long>(index);
  fd.buf_type = buf_type;
  fd.frame_handle = static_cast<pmem_handle_t>(memory->fd());
  fd.metadata_handle = 0;
  fd.alloc_metadata_len = 0;
  fd.non_contiguous_metadata = false;
  return fd;
}

vidc_buffer_info_type VidcBuffer::toBufferInfo(vidc_buffer_type buf_type) const
{
  // SET_BUFFER / FREE_BUFFER register the buffer by its dma-buf handle (fd);
  // the driver maps it, so buf_addr is left null. No extradata buffers are
  // used, so contiguous is true. (Mirrors gst-plugin-vidc useBuffer/freeBuffer.)
  vidc_buffer_info_type info{};
  info.buf_type = buf_type;
  info.contiguous = true;
  info.buf_size = static_cast<uint32>(memory->size());
  info.buf_addr = nullptr;
  info.buf_handle = static_cast<pmem_handle_t>(memory->fd());
  return info;
}

void VidcBuffer::fromFrameData(const vidc_frame_data_type & fd)
{
  bytesused = fd.data_len;
  // Convert microseconds back to timeval
  vidc_timestamp_type usec = fd.timestamp;
  timestamp.tv_sec = static_cast<time_t>(usec / 1000000LL);
  timestamp.tv_usec = static_cast<suseconds_t>(usec % 1000000LL);
  if (fd.flags & VIDC_FRAME_FLAG_EOS) {
    setEOS();
  }
}
}  // namespace qrb::video_v4l2
