#ifndef SORA_HWENC_V4L2_V4L2_RUNNER_H_
#define SORA_HWENC_V4L2_V4L2_RUNNER_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

// Linux
#include <linux/videodev2.h>

// WebRTC
#include <rtc_base/platform_thread.h>

namespace sora {

template <class T>
class ConcurrentQueue {
 public:
  void push(T t) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(t);
  }
  std::optional<T> pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T t = queue_.front();
    queue_.pop();
    return t;
  }
  bool empty() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }
  size_t size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = std::queue<T>();
  }

 private:
  std::queue<T> queue_;
  std::mutex mutex_;
};

class V4L2Runner {
 public:
  ~V4L2Runner();

  static std::shared_ptr<V4L2Runner> Create(
      std::string name,
      int fd,
      int src_count,
      int src_memory,
      int dst_memory,
      std::function<void()> on_change_resolution = nullptr);

  // 出力された capture バッファ (v4l2_buffer) と、そのバッファをデバイスに戻す
  // ための on_next を受け取るコールバック
  //
  // このコールバックでは、必要な処理が終わったら最後に必ず on_next を呼ぶこと。
  typedef std::function<void(v4l2_buffer*, std::function<void()>)>
      OnCompleteCallback;

  // 入力フレームをデバイスに投入し、それに対応する capture バッファが出力された
  // ときに呼ぶ on_complete を登録する
  //
  // v4l2_buf の timestamp にはデキュー時の健全性の確認に利用するため、
  // 必ず適切な値を設定しておくこと
  //
  // 失敗した場合は WEBRTC_VIDEO_CODEC_ERROR を返す。このとき v4l2_buf の出力
  // バッファは再利用可能に戻してあり、on_complete は呼ばれない (そのフレームの
  // 結果は得られないので、必要なら呼び出し側で再投入すること)
  int Enqueue(v4l2_buffer* v4l2_buf, OnCompleteCallback on_complete);

  // 利用可能な出力バッファのインデックスを取り出す
  //
  // 取得したインデックスは Enqueue に渡すこと。利用可能なバッファが無い場合は
  // std::nullopt を返す
  std::optional<int> PopAvailableBufferIndex();

 private:
  // v4l2_buf のタイムスタンプを us に変換する
  static int64_t BufferTimestampUs(const v4l2_buffer* v4l2_buf);

  // 利用中の出力バッファを利用可能に戻す
  //
  // 出力バッファを利用可能にする経路はこの関数に統一する
  void ReturnAvailableOutputBuffer(int index);

  // Enqueue で登録した 1 フレーム分の情報
  struct Registration {
    int64_t timestamp_us;
    OnCompleteCallback on_complete;
  };

  // デバイスが出力した capture バッファをデバイスに戻す
  //
  // on_complete に渡す on_next と、対応する登録が無かった場合の再エンキューで使う
  static void EnqueueCaptureBuffer(int fd, v4l2_buffer* v4l2_buf);

  void PollProcess();

 private:
  std::string name_;
  int fd_;
  int src_count_;
  int src_memory_;
  int dst_memory_;
  std::function<void()> on_change_resolution_;

  ConcurrentQueue<int> output_buffers_available_;
  ConcurrentQueue<Registration> on_completes_;
  std::atomic<bool> abort_poll_;
  webrtc::PlatformThread thread_;
};

}  // namespace sora

#endif
