/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "Decoder.hpp"

#include "V4l2Decoder.hpp"
#ifdef ENABLE_VIDC_BACKEND
#include "vidc/VidcDecoder.hpp"
#endif

namespace qrb::video_v4l2
{
std::shared_ptr<Client> Decoder::create(std::string mime)
{
  Format f = mime == MIME_H264 ? Format::H264 : Format::HEVC;
#ifdef ENABLE_VIDC_BACKEND
  return VidcDecoder::create(f);
#else
  return V4l2Decoder::create(f);
#endif
}
}  // namespace qrb::video_v4l2
