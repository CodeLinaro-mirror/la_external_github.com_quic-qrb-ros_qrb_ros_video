/*
**************************************************************************************************
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
**************************************************************************************************
*/

#include "Log.hpp"

namespace qrb::video_v4l2
{
std::once_flag Log::flag;
uint32_t Log::debug_level;
};  // namespace qrb::video_v4l2
