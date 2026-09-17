#include "sora/hwenc_v4l2/v4l2_runner.h"

// Linux
#include <poll.h>
#include <sys/ioctl.h>

// WebRTC
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>
#include <rtc_base/time_utils.h>

namespace sora {

V4L2Runner::~V4L2Runner() {
  abort_poll_ = true;
  thread_.Finalize();
}

std::shared_ptr<V4L2Runner> V4L2Runner::Create(
    std::string name,
    int fd,
    int src_count,
    int src_memory,
    int dst_memory,
    std::function<void()> on_change_resolution) {
  auto p = std::make_shared<V4L2Runner>();
  p->name_ = name;
  p->fd_ = fd;
  p->src_count_ = src_count;
  p->src_memory_ = src_memory;
  p->dst_memory_ = dst_memory;

  if (on_change_resolution) {
    v4l2_event_subscription sub = {};
    sub.type = V4L2_EVENT_SOURCE_CHANGE;
    if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
      RTC_LOG(LS_ERROR) << "Failed to subscribe to V4L2_EVENT_SOURCE_CHANGE";
      return nullptr;
    }
    p->on_change_resolution_ = on_change_resolution;
  }

  p->abort_poll_ = false;
  p->thread_ = webrtc::PlatformThread::SpawnJoinable(
      [p = p.get()]() { p->PollProcess(); }, "PollThread",
      webrtc::ThreadAttributes().SetPriority(webrtc::ThreadPriority::kHigh));
  for (int i = 0; i < src_count; i++) {
    p->ReturnAvailableOutputBuffer(i);
  }
  return p;
}

int V4L2Runner::Enqueue(v4l2_buffer* v4l2_buf, OnCompleteCallback on_complete) {
  // VIDIOC_QBUF より先に登録する。デバイスは投入された入力をすぐ処理できるため、
  // 登録が後だと対応する capture バッファを先に取り出してしまうことがある
  on_completes_.push(Registration{BufferTimestampUs(v4l2_buf), on_complete});
  if (ioctl(fd_, VIDIOC_QBUF, v4l2_buf) < 0) {
    RTC_LOG(LS_ERROR) << __func__ << "  Failed to queue output buffer: error="
                      << strerror(errno);
    on_completes_.pop();
    // VIDIOC_QBUF に失敗したバッファはデバイスに渡っていないので、DQBUF では
    // 返却されない。そのままでは利用可能なバッファが減り続けるため、ここで
    // 再利用可能に戻す
    ReturnAvailableOutputBuffer(v4l2_buf->index);
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

std::optional<int> V4L2Runner::PopAvailableBufferIndex() {
  return output_buffers_available_.pop();
}

void V4L2Runner::ReturnAvailableOutputBuffer(int index) {
  output_buffers_available_.push(index);
}

int64_t V4L2Runner::BufferTimestampUs(const v4l2_buffer* v4l2_buf) {
  return (int64_t)v4l2_buf->timestamp.tv_sec * webrtc::kNumMicrosecsPerSec +
         v4l2_buf->timestamp.tv_usec;
}

// デバイスが出力した capture バッファをデバイスに戻す。
//
// on_complete に渡す on_next と、対応する登録が無かった場合の再エンキューで使う。
void V4L2Runner::EnqueueCaptureBuffer(int fd, v4l2_buffer* v4l2_buf) {
  // 呼び出し元の v4l2_buf が持つ planes は呼び出し元のスタックを指しているため、
  // この関数のスタックに用意した planes を指し直してから QBUF する
  v4l2_buffer v4l2_capture_buf = *v4l2_buf;
  v4l2_plane planes[VIDEO_MAX_PLANES] = {};
  v4l2_capture_buf.m.planes = planes;
  if (ioctl(fd, VIDIOC_QBUF, &v4l2_capture_buf) < 0) {
    RTC_LOG(LS_ERROR) << "Failed to enqueue capture buffer: error="
                      << strerror(errno);
  }
}

void V4L2Runner::PollProcess() {
  while (true) {
    RTC_LOG(LS_VERBOSE) << "[POLL][" << name_ << "] Start poll";
    pollfd p = {fd_, POLLIN | POLLPRI, 0};
    int ret = poll(&p, 1, 500);
    if (abort_poll_ && output_buffers_available_.size() == src_count_) {
      break;
    }
    if (ret == -1) {
      RTC_LOG(LS_ERROR) << "[POLL][" << name_
                        << "] unexpected error ret=" << ret;
      break;
    }
    if (p.revents & POLLPRI) {
      RTC_LOG(LS_VERBOSE) << "[POLL][" << name_ << "] Polled POLLPRI";
      if (on_change_resolution_) {
        v4l2_event event = {};
        if (ioctl(fd_, VIDIOC_DQEVENT, &event) < 0) {
          RTC_LOG(LS_ERROR)
              << "[POLL][" << name_ << "] Failed dequeing an event";
          break;
        }
        if (event.type == V4L2_EVENT_SOURCE_CHANGE &&
            (event.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION) != 0) {
          RTC_LOG(LS_VERBOSE) << "On change resolution";
          on_change_resolution_();
        }
      }
    }
    if (p.revents & POLLIN) {
      RTC_LOG(LS_VERBOSE) << "[POLL][" << name_ << "] Polled POLLIN";
      v4l2_buffer v4l2_buf = {};
      v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      v4l2_buf.memory = src_memory_;
      v4l2_buf.length = 1;
      v4l2_plane planes[VIDEO_MAX_PLANES] = {};
      v4l2_buf.m.planes = planes;
      RTC_LOG(LS_VERBOSE) << "[POLL][" << name_ << "] DQBUF output";
      int ret = ioctl(fd_, VIDIOC_DQBUF, &v4l2_buf);
      if (ret != 0) {
        RTC_LOG(LS_ERROR) << "[POLL][" << name_
                          << "] Failed to dequeue output buffer: error="
                          << strerror(errno);
      } else {
        ReturnAvailableOutputBuffer(v4l2_buf.index);
      }

      v4l2_buf = {};
      memset(planes, 0, sizeof(planes));
      v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      v4l2_buf.memory = V4L2_MEMORY_MMAP;
      v4l2_buf.length = 1;
      v4l2_buf.m.planes = planes;
      RTC_LOG(LS_VERBOSE) << "[POLL][" << name_ << "] DQBUF capture";
      ret = ioctl(fd_, VIDIOC_DQBUF, &v4l2_buf);
      if (ret != 0) {
        RTC_LOG(LS_ERROR) << "[POLL][" << name_
                          << "] Failed to dequeue capture buffer: error="
                          << strerror(errno);
      } else {
        if (abort_poll_) {
          break;
        }

        // タイムスタンプが一致する登録を探す。通常は on_completes_ の先頭の要素と一致するが、
        //
        // - QBUF していないのに DQBUF が発生される
        // - QBUF した要素がスキップされる
        //
        // というケースが発生した場合の復帰用となる。
        // （仕様上そのようなことは起きるはずはないが念のため）。
        // 一致しない登録は破棄する。
        int64_t capture_timestamp_us = BufferTimestampUs(&v4l2_buf);
        std::optional<Registration> registration = on_completes_.pop();
        int discard_count = 0;
        int64_t discarded_timestamp_us = 0;
        while (registration &&
               registration->timestamp_us != capture_timestamp_us) {
          if (discard_count == 0) {
            discarded_timestamp_us = registration->timestamp_us;
          }
          discard_count++;
          registration = on_completes_.pop();
        }
        if (discard_count > 0) {
          RTC_LOG(LS_WARNING)
              << "[POLL][" << name_ << "] Pairing is out of sync. Discarded "
              << discard_count
              << " frame(s): capture timestamp_us=" << capture_timestamp_us
              << " discarded timestamp_us=" << discarded_timestamp_us;
        }

        int fd = fd_;
        if (!registration) {
          // 一致する登録が無いので、この capture バッファをデバイスに戻す
          RTC_LOG(LS_WARNING)
              << "[POLL][" << name_ << "] on_completes_ is empty."
              << " Requeue the capture buffer:"
              << " capture timestamp_us=" << capture_timestamp_us;
          EnqueueCaptureBuffer(fd, &v4l2_buf);
        } else {
          registration->on_complete(&v4l2_buf, [fd, v4l2_buf]() mutable {
            EnqueueCaptureBuffer(fd, &v4l2_buf);
          });
        }
      }
      RTC_LOG(LS_VERBOSE) << "[POLL][" << name_ << "] Completed POLLIN";
    }
  }
}

}  // namespace sora
