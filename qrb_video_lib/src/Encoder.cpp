/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "Encoder.hpp"

#include "V4l2Encoder.hpp"
#ifdef ENABLE_VIDC_BACKEND
#include "vidc/VidcEncoder.hpp"
#endif

namespace qrb::video_v4l2
{
std::shared_ptr<Client> Encoder::create(std::string mime)
{
  Format f = mime == MIME_H264 ? Format::H264 : Format::HEVC;
#ifdef ENABLE_VIDC_BACKEND
  return VidcEncoder::create(f);
#else
  return V4l2Encoder::create(f);
#endif
}
}  // namespace qrb::video_v4l2
