/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "vidc/VidcCodec.hpp"

#include <chrono>
#include <cstring>

#include "utils/Log.hpp"
#include "v4l2/V4l2Memory.hpp"

extern "C" {
#include "vidc_ioctl.h"
#include "vidc_types.h"
}

namespace qrb::video_v4l2
{
namespace
{
// Extra buffers held on top of the driver-reported minimum, matching
// gst-plugin-vidc (NUM_OF_BACKBUFFER_IN / _OUT).
constexpr uint32_t kBackBufferIn = 4;
constexpr uint32_t kBackBufferOut = 6;

// Map a vidc_property_id_type to its symbol name for logging. The enum is not
// contiguous (encoder ids start at 0x100, decoder at 0x200), so we list the
// properties this library actually sets/gets rather than index an array.
const char * vidcPropName(vidc_property_id_type id)
{
#define VIDC_PROP_CASE(x) \
  case x:                 \
    return #x
  switch (id) {
    VIDC_PROP_CASE(VIDC_I_FRAME_SIZE);
    VIDC_PROP_CASE(VIDC_I_SESSION_CODEC);
    VIDC_PROP_CASE(VIDC_I_PROFILE);
    VIDC_PROP_CASE(VIDC_I_LEVEL);
    VIDC_PROP_CASE(VIDC_I_COLOR_FORMAT);
    VIDC_PROP_CASE(VIDC_I_BUFFER_REQUIREMENTS);
    VIDC_PROP_CASE(VIDC_I_BUFFER_ALLOC_MODE);
    VIDC_PROP_CASE(VIDC_I_FRAME_RATE);
    VIDC_PROP_CASE(VIDC_I_TARGET_BITRATE);
    VIDC_PROP_CASE(VIDC_I_ENC_RATE_CONTROL);
    default:
      return "VIDC_I_<unknown>";
  }
#undef VIDC_PROP_CASE
}
}  // namespace

VidcCodec::VidcCodec(CodecType type, Format compressed)
  : type_(type), compressedFormat_(compressed), state_(STOPPED)
{
  driver_ = std::make_shared<VidcDriver>();
}

bool VidcCodec::configure(const Setting & s)
{
  settings_[s.type] = s;
  return true;
}

bool VidcCodec::start()
{
  LOGI("VidcCodec: start\n");
  state_ = LOADING;

  if (!driver_->open()) {
    LOGE("VidcCodec: driver open failed\n");
    return false;
  }
  driver_->registerCallback(this);

  setSessionCodec();
  configureProperties();

  if (!allocateBuffers()) {
    LOGE("VidcCodec: allocateBuffers failed\n");
    return false;
  }
  LOGI("VidcCodec: buffers allocated (in=%zu out=%zu)\n", inputBuffers_.size(), outputBuffers_.size());

  state_ = LOADED;

  // This is an iris3-onwards driver: there is no separate LOAD_RESOURCES step.
  // START is issued per-port (input then output) as a regular ioctl carrying a
  // vidc_start_mode_type, and is acked by VIDC_EVT_RESP_START_INPUT_DONE /
  // _OUTPUT_DONE. (See gst-plugin-vidc GstClient::stateExecuting.)
  vidc_start_mode_type start_mode = VIDC_START_INPUT;
  armEvent(VIDC_EVT_RESP_START_INPUT_DONE);
  driver_->ioctl(VIDC_IOCTL_START, &start_mode, sizeof(start_mode), nullptr, 0);
  if (!waitForEvent()) {
    LOGE("VidcCodec: START input timed out\n");
    return false;
  }

  start_mode = VIDC_START_OUTPUT;
  armEvent(VIDC_EVT_RESP_START_OUTPUT_DONE);
  driver_->ioctl(VIDC_IOCTL_START, &start_mode, sizeof(start_mode), nullptr, 0);
  if (!waitForEvent()) {
    LOGE("VidcCodec: START output timed out\n");
    return false;
  }

  state_ = STARTED;
  LOGI("VidcCodec: started\n");

  std::lock_guard<std::mutex> lk(mutex_);
  for (auto & [idx, buf] : outputBuffers_) {
    feedOutputBuffer(buf);
  }

  return true;
}

bool VidcCodec::stop()
{
  LOGI("VidcCodec: stop\n");
  state_ = DRAINING;

  // If a port-reconfigure was in flight, let it finish before we tear the
  // session down. The reconfig thread issues its own STOP_OUTPUT/START_OUTPUT
  // and would race with the per-port stops below otherwise.
  if (reconfigThread_.joinable()) {
    reconfigThread_.join();
  }

  // Mirror of start(): STOP is per-port and acked by _STOP_INPUT_DONE /
  // _STOP_OUTPUT_DONE. There is no RELEASE_RESOURCES step on this driver.
  vidc_stop_mode_type stop_mode = VIDC_STOP_INPUT;
  armEvent(VIDC_EVT_RESP_STOP_INPUT_DONE);
  driver_->ioctl(VIDC_IOCTL_STOP, &stop_mode, sizeof(stop_mode), nullptr, 0);
  if (!waitForEvent()) {
    LOGE("VidcCodec: STOP input timed out\n");
    return false;
  }

  stop_mode = VIDC_STOP_OUTPUT;
  armEvent(VIDC_EVT_RESP_STOP_OUTPUT_DONE);
  driver_->ioctl(VIDC_IOCTL_STOP, &stop_mode, sizeof(stop_mode), nullptr, 0);
  if (!waitForEvent()) {
    LOGE("VidcCodec: STOP output timed out\n");
    return false;
  }

  freeBuffers();

  driver_->close();
  state_ = STOPPED;
  LOGI("VidcCodec: stopped\n");
  return true;
}

bool VidcCodec::seek()
{
  return false;
}

bool VidcCodec::pause()
{
  return false;
}

bool VidcCodec::queueBuffer(const std::shared_ptr<Buffer> & item)
{
  auto vidc_buf = std::dynamic_pointer_cast<VidcBuffer>(item);
  if (!vidc_buf) {
    vidc_buf = std::make_shared<VidcBuffer>(*item);
  }

  auto fd = vidc_buf->toFrameData(VIDC_BUFFER_INPUT);
  LOGI("VidcCodec::queueBuffer: queue input buffer index %zu (len %zu, eos %d)", vidc_buf->index,
      vidc_buf->length(), vidc_buf->isEOS());
  // Hold the caller's pool-aliased shared_ptr until INPUT_DONE arrives. The
  // BufferPool returns its slot to the free list via a custom deleter that
  // fires when the alias's refcount hits zero; without this map the alias
  // would die as soon as the caller's frame returns, the slot would go free,
  // and the same buffer would be re-acquired and re-submitted while the
  // driver still owned it (driver rejects with "inuse buffer ... for reuse").
  {
    std::lock_guard<std::mutex> lk(mutex_);
    inputInflight_[vidc_buf->index] = item;
  }
  int ret = driver_->ioctl(VIDC_IOCTL_EMPTY_INPUT_BUFFER, &fd, sizeof(fd), nullptr, 0);
  if (ret != 0) {
    std::lock_guard<std::mutex> lk(mutex_);
    inputInflight_.erase(vidc_buf->index);
    return false;
  }
  return true;
}

std::shared_ptr<Buffer> VidcCodec::acquireBuffer()
{
  // Caller acquires an input buffer to fill (raw frames for encode, compressed data for decode).
  std::shared_ptr<Buffer> buf;
  if (inputPool_) {
    inputPool_->acquire(buf);
  } else {
    LOGE("VidcCodec::acquireBuffer: inputPool_ is null (codec not started?)");
  }
  if (not buf) {
    LOGE("VidcCodec::acquireBuffer: returning null buffer");
  }
  return buf;
}

bool VidcCodec::dispatchBuffer(const std::shared_ptr<Buffer> & item)
{
  auto cb = notifier.lock();
  if (cb) {
    return cb->onBufferAvailable(item);
  }
  return false;
}

bool VidcCodec::onAcquireBuffer(std::shared_ptr<Buffer> & item)
{
  item = acquireBuffer();
  return item != nullptr;
}

bool VidcCodec::onQueueBuffer(const std::shared_ptr<Buffer> & item)
{
  return queueBuffer(item);
}

bool VidcCodec::onConfigure(const Setting & s)
{
  return configure(s);
}

bool VidcCodec::onStart()
{
  return start();
}

bool VidcCodec::onStop()
{
  return stop();
}

bool VidcCodec::onPause()
{
  return pause();
}

bool VidcCodec::onSeek()
{
  return seek();
}

void VidcCodec::onInputDone(const vidc_frame_data_type & frame)
{
  size_t idx = static_cast<size_t>(frame.frm_clnt_data);
  // Drop the in-flight reference outside the lock so the BufferPool deleter
  // (which itself takes the pool lock) does not run with mutex_ held.
  // Erasing the entry runs the deleter only if no other ref still pins the
  // alias — that's exactly the contract we want: the slot returns to the
  // pool when both the driver AND every caller are done with it.
  std::shared_ptr<Buffer> inflight;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = inputBuffers_.find(idx);
    if (it != inputBuffers_.end()) {
      it->second->fromFrameData(frame);
    } else {
      LOGW("VidcCodec::onInputDone: unknown input buffer index %zu", idx);
    }
    auto fit = inputInflight_.find(idx);
    if (fit != inputInflight_.end()) {
      inflight = std::move(fit->second);
      inputInflight_.erase(fit);
    }
  }
  LOGI("VidcCodec::onInputDone: released input buffer index %zu", idx);
}

void VidcCodec::onOutputDone(const vidc_frame_data_type & frame)
{
  size_t idx = static_cast<size_t>(frame.frm_clnt_data);
  std::shared_ptr<VidcBuffer> buf;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = outputBuffers_.find(idx);
    if (it == outputBuffers_.end()) {
      LOGW("VidcCodec::onOutputDone: unknown output buffer index %zu", idx);
      return;
    }
    buf = it->second;
  }
  buf->fromFrameData(frame);
  LOGI("VidcCodec::onOutputDone: output buffer index %zu (eos %d, bytesused %zu)", idx,
      buf->isEOS(), buf->bytesused);
  // Only dispatch buffers that carry real data. The driver delivers a final
  // zero-length buffer with VIDC_FRAME_FLAG_EOS to mark end-of-stream; that
  // marker is a completion signal, not a frame, and must not be published
  // (publishing it races node teardown -> rclcpp "invalid publisher id").
  // Mirrors gst-plugin-vidc EVENT_OUTPUTS_DONE, which only pushes downstream
  // when (fd > 0 && size > 0) and otherwise treats EOS purely as a signal.
  if (buf->bytesused > 0) {
    dispatchBuffer(buf);
  }
  // Re-queue the output buffer unless EOS or we are tearing the port down
  // for a reconfigure (the buffers about to be FREE_BUFFER'd should not be
  // re-fed).
  if (!buf->isEOS() && state_ == STARTED) {
    feedOutputBuffer(buf);
  }
}

void VidcCodec::onEvent(vidc_event_type event, uint32_t data)
{
  switch (event) {
    case VIDC_EVT_RESP_LOAD_RESOURCES:
    case VIDC_EVT_RESP_START:
    case VIDC_EVT_RESP_START_INPUT_DONE:
    case VIDC_EVT_RESP_START_OUTPUT_DONE:
    case VIDC_EVT_RESP_STOP:
    case VIDC_EVT_RESP_STOP_INPUT_DONE:
    case VIDC_EVT_RESP_STOP_OUTPUT_DONE:
    case VIDC_EVT_RESP_PAUSE:
    case VIDC_EVT_RESP_DRAIN:
    case VIDC_EVT_RESP_RELEASE_RESOURCES:
    case VIDC_EVT_RESP_FLUSH_INPUT_DONE:
    case VIDC_EVT_RESP_FLUSH_OUTPUT_DONE: {
      // Multiple distinct VIDC_EVT_RESP_* may be delivered for one ioctl
      // (e.g. VIDC_EVT_RESP_START bunched with the per-port _DONE), and stale
      // acks can arrive after the caller's waitForEvent() already returned.
      // Only fulfill the promise for the exact event the caller armed for;
      // anything else is silently dropped. Without this gate the second
      // set_value on the same promise throws future_error and aborts.
      int expected = expectedEvent_.load(std::memory_order_acquire);
      if (expected != static_cast<int>(event)) {
        LOGI("VidcCodec::onEvent: ignoring event %d (expected %d, data=%u)",
            static_cast<int>(event), expected, data);
        break;
      }
      expectedEvent_.store(-1, std::memory_order_release);
      asyncEvent_.set_value(data == VIDC_ERR_NONE);
      break;
    }
    case VIDC_EVT_OUTPUT_RECONFIG:
      // The driver tells us the output port needs to be re-sized (typical
      // after the decoder parses SPS/PPS and the actual resolution differs
      // from the placeholder we used at start). Hand off to a worker thread
      // because reconfigureOutputPort() issues STOP_OUTPUT / START_OUTPUT
      // ioctls and waits on their acks - and those acks are delivered on
      // *this* poll thread, so doing the work inline would deadlock.
      if (reconfigThread_.joinable()) {
        // Previous reconfig already finished; reap it before starting a new one.
        reconfigThread_.join();
      }
      reconfigThread_ = std::thread([this] { reconfigureOutputPort(); });
      break;
    case VIDC_EVT_ERR_HWFATAL:
      LOGE("VidcCodec: HWFATAL error\n");
      driver_->mError = true;
      break;
    default:
      break;
  }
}

bool VidcCodec::allocateBuffers()
{
  uint32_t in_count = 0, in_size = 0;
  uint32_t out_count = 0, out_size = 0;

  // Tell the driver we will be supplying our own dmabuf-backed buffers.
  // Without this, VIDC_I_BUFFER_ALLOC_MODE defaults to STATIC (driver
  // allocates internally) and SET_BUFFER below will leave the dynamic pool
  // capacity at 0 - the first ETB then fails with
  // "Dyn_buffer_mgmt Buffer pool is full" / VIDC_ERR_BAD_PARAM.
  // Mirrors gst-plugin-vidc GstClient::initialize. The property must be
  // configured before any SET_BUFFER call (driver docstring on
  // VIDC_I_BUFFER_ALLOC_MODE: "Reconfigurable: Only between
  // vidc_initialize() and before buffer allocation").
  vidc_buffer_alloc_mode_type alloc_mode{};
  alloc_mode.buf_mode = VIDC_BUFFER_MODE_DYNAMIC;
  alloc_mode.buf_type = VIDC_BUFFER_INPUT;
  setProperty(VIDC_I_BUFFER_ALLOC_MODE, &alloc_mode, sizeof(alloc_mode));
  alloc_mode.buf_type = VIDC_BUFFER_OUTPUT;
  setProperty(VIDC_I_BUFFER_ALLOC_MODE, &alloc_mode, sizeof(alloc_mode));

  if (!queryBufferRequirements(VIDC_BUFFER_INPUT, in_count, in_size)) {
    return false;
  }
  if (!queryBufferRequirements(VIDC_BUFFER_OUTPUT, out_count, out_size)) {
    return false;
  }

  inputPool_ = std::make_shared<BufferPool>();
  outputPool_ = std::make_shared<BufferPool>();

  // In DYNAMIC buffer mode the driver registers each buffer the first time
  // it is submitted via EMPTY_INPUT_BUFFER / FILL_OUTPUT_BUFFER - calling
  // SET_BUFFER here actually corrupts the dynamic pool: it pre-fills
  // pool->validated up to count, and when the matching ETB/FTB then arrives
  // its handle is rejected with "Dyn_buffer_mgmt Buffer pool is full".
  // The reference vidc_test_app (vidcioctl.cpp::useBuffers) explicitly skips
  // SET_BUFFER on a dynamic port; we do the same. We still allocate the
  // dmabufs locally so the pool/notifier plumbing stays the same.
  for (uint32_t i = 0; i < in_count; i++) {
    auto buf = VidcBuffer::create(in_size);
    buf->index = i;
    inputBuffers_[i] = buf;
    inputPool_->registerBuffer(buf);
  }

  for (uint32_t i = 0; i < out_count; i++) {
    auto buf = VidcBuffer::create(out_size);
    buf->index = i;
    outputBuffers_[i] = buf;
    outputPool_->registerBuffer(buf);
  }

  return true;
}

bool VidcCodec::freeBuffers()
{
  // In DYNAMIC buffer mode the driver has no SET_BUFFER -> FREE_BUFFER
  // protocol to honor: buffers are registered lazily on the first ETB/FTB
  // and released either as INPUT_DONE / OUTPUT_DONE arrive, or when the
  // session is torn down via device_close(). Calling VIDC_IOCTL_FREE_BUFFER
  // here for buffers the firmware has not yet returned (typically the
  // surplus FTBs queued at start() that were never consumed) racks up
  // "buffer still with core" failures (AEE_EBADPARM = 0x80000002) on the
  // driver side - benign, but very noisy. The reference vidc_test_app
  // skips SET_BUFFER on dynamic ports (see start() comment); we mirror
  // that for FREE_BUFFER. device_close() reclaims everything regardless.
  inputBuffers_.clear();
  outputBuffers_.clear();
  inputPool_.reset();
  outputPool_.reset();
  return true;
}

bool VidcCodec::queryBufferRequirements(vidc_buffer_type bt, uint32_t & count, uint32_t & size)
{
  vidc_buffer_reqmnts_type req{};
  req.buf_type = bt;

  uint8_t buffer[256] = {};
  auto * prop = reinterpret_cast<vidc_drv_property_type *>(buffer);
  prop->prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
  prop->prop_hdr.size = sizeof(vidc_buffer_reqmnts_type);
  std::memcpy(prop->payload, &req, sizeof(req));
  uint32_t bytes = sizeof(vidc_property_hdr_type) + sizeof(vidc_buffer_reqmnts_type);

  int ret = driver_->ioctl(VIDC_IOCTL_GET_PROPERTY, prop, bytes,
      reinterpret_cast<uint8 *>(&req), sizeof(req));
  if (ret != 0) {
    LOGE("VidcCodec: queryBufferRequirements failed for buf_type %d (ioctl ret %d)", bt, ret);
    return false;
  }

  // A plain GET only populates the read-only min_count; actual_count stays 0
  // until it is negotiated. Mirror gst-plugin-vidc: take min_count, add the
  // back-buffers, and SET the chosen actual_count back to the driver.
  uint32_t backBuffers = (bt == VIDC_BUFFER_OUTPUT) ? kBackBufferOut : kBackBufferIn;
  req.actual_count = req.min_count + backBuffers;
  LOGI("VidcCodec: buffer requirements buf_type %d: min_count %u, actual_count %u, size %u", bt,
      req.min_count, req.actual_count, req.size);
  if (!setProperty(VIDC_I_BUFFER_REQUIREMENTS, &req, sizeof(req))) {
    LOGE("VidcCodec: failed to set buffer requirements for buf_type %d", bt);
    return false;
  }

  count = req.actual_count;
  size = req.size;
  return true;
}

bool VidcCodec::setProperty(vidc_property_id_type id, void * payload, uint32_t size)
{
  // The driver reads the payload INLINE right after the header, even though
  // vidc_drv_property_type::payload is declared as a pointer. Match the
  // gst-plugin-vidc wrapper: copy the payload bytes after the header and send
  // sizeof(header) + payload (NOT sizeof(prop), which would send a pointer).
  uint8_t buffer[256] = {};
  auto * prop = reinterpret_cast<vidc_drv_property_type *>(buffer);
  prop->prop_hdr.prop_id = id;
  prop->prop_hdr.size = size;
  std::memcpy(prop->payload, payload, size);
  uint32_t bytes = sizeof(vidc_property_hdr_type) + size;
  int ret = driver_->ioctl(VIDC_IOCTL_SET_PROPERTY, prop, bytes, nullptr, 0);
  if (ret != 0) {
    LOGE("VidcCodec: SET_PROPERTY %s (id=0x%x) failed ret=%d", vidcPropName(id), id, ret);
    return false;
  }
  LOGI("VidcCodec: SET_PROPERTY %s (id=0x%x) ok", vidcPropName(id), id);
  return true;
}

bool VidcCodec::getProperty(vidc_property_id_type id, vidc_buffer_type bt, void * payload_out,
    uint32_t size)
{
  uint8_t buffer[256] = {};
  auto * prop = reinterpret_cast<vidc_drv_property_type *>(buffer);
  prop->prop_hdr.prop_id = id;
  prop->prop_hdr.size = size;
  std::memcpy(prop->payload, payload_out, size);
  uint32_t bytes = sizeof(vidc_property_hdr_type) + size;
  return driver_->ioctl(VIDC_IOCTL_GET_PROPERTY, prop, bytes,
             reinterpret_cast<uint8 *>(payload_out), size) == 0;
}

bool VidcCodec::waitForEvent()
{
  auto fut = asyncEvent_.get_future();
  auto status = fut.wait_for(std::chrono::milliseconds(2500));
  if (status != std::future_status::ready) {
    // Clear the gate so a late ack doesn't fulfill a promise we've abandoned.
    expectedEvent_.store(-1, std::memory_order_release);
    return false;
  }
  return fut.get();
}

void VidcCodec::armEvent(vidc_event_type ev)
{
  // Order matters: install the new promise first, then publish the expected
  // event id with release semantics. onEvent() loads expectedEvent_ with
  // acquire and only touches asyncEvent_ after seeing the new id, so a stray
  // ack that races with arming is dropped on the old (-1) gate rather than
  // landing on the fresh promise.
  asyncEvent_ = std::promise<bool>{};
  expectedEvent_.store(static_cast<int>(ev), std::memory_order_release);
}

bool VidcCodec::feedOutputBuffer(std::shared_ptr<VidcBuffer> buf)
{
  auto fd = buf->toFrameData(VIDC_BUFFER_OUTPUT);
  return driver_->ioctl(VIDC_IOCTL_FILL_OUTPUT_BUFFER, &fd, sizeof(fd), nullptr, 0) == 0;
}
}  // namespace qrb::video_v4l2
