/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef QRB_VIDEO_V4L2__VIDCCODEC_HPP_
#define QRB_VIDEO_V4L2__VIDCCODEC_HPP_

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "BufferChannel.hpp"
#include "BufferPool.hpp"
#include "VideoCodec.hpp"
#include "vidc/VidcBuffer.hpp"
#include "vidc/VidcDriver.hpp"

namespace qrb::video_v4l2
{
class VidcCodec : public VideoCodec,
                  public ChannelCB,
                  public VidcDriver::Callback,
                  public std::enable_shared_from_this<VidcCodec>
{
public:
  enum State
  {
    STOPPED,
    LOADING,
    LOADED,
    STARTED,
    PAUSED,
    FLUSHING,
    DRAINING,
    RECONFIGURING,
  };

  enum
  {
    MSG_FLUSH_PORT,
    MSG_RECONFIGURE_PORT,
    MSG_FEED_OUTPUT_BUFFER,
  };

  explicit VidcCodec(CodecType type, Format compressed);
  ~VidcCodec() override = default;

  bool configure(const Setting & s) override;
  bool start() override;
  bool stop() override;
  bool seek() override;
  bool pause() override;
  bool queueBuffer(const std::shared_ptr<Buffer> & item) override;
  std::shared_ptr<Buffer> acquireBuffer() override;
  bool dispatchBuffer(const std::shared_ptr<Buffer> & item) override;

  bool onAcquireBuffer(std::shared_ptr<Buffer> & item) override;
  bool onQueueBuffer(const std::shared_ptr<Buffer> & item) override;
  bool onConfigure(const Setting & s) override;
  bool onStart() override;
  bool onStop() override;
  bool onPause() override;
  bool onSeek() override;

  void onInputDone(const vidc_frame_data_type & frame) override;
  void onOutputDone(const vidc_frame_data_type & frame) override;
  void onEvent(vidc_event_type event, uint32_t data) override;

protected:
  virtual void setSessionCodec() = 0;
  virtual void configureProperties() = 0;
  virtual bool allocateBuffers();
  virtual bool freeBuffers();
  virtual bool queryBufferRequirements(vidc_buffer_type bt, uint32_t & count, uint32_t & size);
  // Default no-op. VidcDecoder overrides to handle VIDC_EVT_OUTPUT_RECONFIG:
  // STOP_OUTPUT → free old → re-query → alloc new → SET_BUFFER → START_OUTPUT
  // → re-feed. Encoders typically don't see this event.
  virtual void reconfigureOutputPort() {}

  bool setProperty(vidc_property_id_type id, void * payload, uint32_t size);
  bool getProperty(vidc_property_id_type id, vidc_buffer_type bt, void * payload_out,
      uint32_t size);
  // Re-arm asyncEvent_ for the next ioctl-ack handshake and record which
  // specific VIDC_EVT_RESP_* the caller is waiting for. Only that exact event
  // will fulfill the promise in onEvent(); duplicate or stray acks (e.g. a
  // bunched VIDC_EVT_RESP_START arriving alongside the per-port _DONE, or a
  // late flush ack) are ignored instead of throwing future_error.
  void armEvent(vidc_event_type ev);
  bool waitForEvent();
  bool feedOutputBuffer(std::shared_ptr<VidcBuffer> buf);

  std::shared_ptr<VidcDriver> driver_;
  std::shared_ptr<BufferPool> inputPool_;
  std::shared_ptr<BufferPool> outputPool_;
  CodecType type_;
  Format compressedFormat_;
  std::atomic<State> state_;
  std::mutex mutex_;
  std::promise<bool> asyncEvent_;
  // The vidc_event_type the caller is currently waiting for, or -1 when no
  // handshake is in flight. Stored as int so we have a sentinel value distinct
  // from every legal vidc_event_type. Written by callers under armEvent(),
  // consumed (and reset to -1) by onEvent() on the driver poll thread.
  std::atomic<int> expectedEvent_{-1};
  std::map<size_t, std::shared_ptr<VidcBuffer>> inputBuffers_;
  std::map<size_t, std::shared_ptr<VidcBuffer>> outputBuffers_;
  // In-flight input buffers held by the driver. Holds a strong reference to
  // the BufferPool alias so its custom deleter does not return the slot to
  // the pool until the driver has actually returned the buffer via INPUT_DONE.
  std::map<size_t, std::shared_ptr<Buffer>> inputInflight_;
  std::map<uint32_t, Setting> settings_;
  // Worker thread that runs reconfigureOutputPort() out of the driver poll
  // thread, so the STOP_OUTPUT / START_OUTPUT acks can be delivered while we
  // wait on them.
  std::thread reconfigThread_;
};
}  // namespace qrb::video_v4l2

#endif  // QRB_VIDEO_V4L2__VIDCCODEC_HPP_
