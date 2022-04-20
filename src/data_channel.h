#ifndef SORA_DATA_CHANNEL_H_
#define SORA_DATA_CHANNEL_H_

#include <map>
#include <memory>
#include <set>

// Boost
#include <boost/asio.hpp>

// WebRTC
#include <api/data_channel_interface.h>

namespace sora {

class DataChannelObserver {
 public:
  ~DataChannelObserver() {}
  virtual void OnStateChange(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) = 0;
  virtual void OnMessage(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
      const webrtc::DataBuffer& buffer) = 0;
};

// 複数の DataChannel を纏めてコールバックで受け取るためのクラス
class DataChannel {
  struct Thunk : webrtc::DataChannelObserver,
                 std::enable_shared_from_this<Thunk> {
    DataChannel* p;
    rtc::scoped_refptr<webrtc::DataChannelInterface> dc;
    void OnStateChange() override { p->OnStateChange(shared_from_this()); }
    void OnMessage(const webrtc::DataBuffer& buffer) override {
      p->OnMessage(shared_from_this(), buffer);
    }
    void OnBufferedAmountChange(uint64_t previous_amount) override {
      p->OnBufferedAmountChange(shared_from_this(), previous_amount);
    }
  };

 public:
  DataChannel(boost::asio::io_context& ioc,
              std::weak_ptr<DataChannelObserver> observer)
      : ioc_(&ioc), timer_(ioc), observer_(observer) {}
  bool IsOpen(std::string label) const {
    return labels_.find(label) != labels_.end();
  }
  void Send(std::string label, const webrtc::DataBuffer& data) {
    auto it = labels_.find(label);
    if (it == labels_.end()) {
      return;
    }
    if (!data.binary) {
      std::string str((const char*)data.data.cdata(),
                      (const char*)data.data.cdata() + data.size());
      RTC_LOG(LS_INFO) << "Send DataChannel label=" << label << " data=" << str;
    }
    auto data_channel = it->second;
    data_channel->Send(data);
  }
  void Close(const webrtc::DataBuffer& disconnect_message,
             std::function<void(boost::system::error_code)> on_close,
             double disconnect_wait_timeout) {
    auto it = labels_.find("signaling");
    if (it == labels_.end()) {
      on_close(boost::system::errc::make_error_code(
          boost::system::errc::not_connected));
      return;
    }

    timer_.expires_from_now(
        boost::posix_time::milliseconds((int)(disconnect_wait_timeout * 1000)));
    timer_.async_wait([on_close](boost::system::error_code ec) {
      if (ec == boost::asio::error::operation_aborted) {
        return;
      }
      on_close(
          boost::system::errc::make_error_code(boost::system::errc::timed_out));
    });

    on_close_ = on_close;
    auto data_channel = it->second;
    data_channel->Send(disconnect_message);
  }

 public:
  void AddDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
    boost::asio::post(*ioc_, [this, data_channel]() {
      std::shared_ptr<Thunk> thunk(new Thunk());
      thunk->p = this;
      thunk->dc = data_channel;
      data_channel->RegisterObserver(thunk.get());
      thunks_.insert(std::make_pair(thunk, data_channel));
      labels_.insert(std::make_pair(data_channel->label(), data_channel));
    });
  }

 private:
  void OnStateChange(std::shared_ptr<Thunk> thunk) {
    boost::asio::post(*ioc_, [this, thunk]() {
      auto data_channel = thunks_.at(thunk);
      if (data_channel->state() == webrtc::DataChannelInterface::kClosed) {
        labels_.erase(data_channel->label());
        thunks_.erase(thunk);
        data_channel->UnregisterObserver();
        RTC_LOG(LS_INFO) << "DataChannel closed label="
                         << data_channel->label();
      }
      auto observer = observer_;
      auto on_close = on_close_;
      auto empty = thunks_.empty();
      if (on_close != nullptr && empty) {
        on_close_ = nullptr;
        timer_.cancel();
      }
      auto ob = observer.lock();
      if (ob != nullptr) {
        ob->OnStateChange(data_channel);
      }
      // すべての Data Channel が閉じたら通知する
      if (on_close != nullptr && empty) {
        RTC_LOG(LS_INFO) << "DataChannel closed all";
        on_close(boost::system::error_code());
      }
    });
  }

  void OnMessage(std::shared_ptr<Thunk> thunk,
                 const webrtc::DataBuffer& buffer) {
    boost::asio::post(*ioc_, [this, thunk, buffer]() {
      auto observer = observer_;
      auto data_channel = thunks_.at(thunk);
      auto ob = observer.lock();
      if (ob != nullptr) {
        ob->OnMessage(data_channel, buffer);
      }
    });
  }
  void OnBufferedAmountChange(std::shared_ptr<Thunk> thunk,
                              uint64_t previous_amount) {}

 private:
  boost::asio::io_context* ioc_;
  std::map<std::shared_ptr<Thunk>,
           rtc::scoped_refptr<webrtc::DataChannelInterface>>
      thunks_;
  std::map<std::string, rtc::scoped_refptr<webrtc::DataChannelInterface>>
      labels_;
  std::weak_ptr<DataChannelObserver> observer_;
  std::function<void(boost::system::error_code)> on_close_;
  boost::asio::deadline_timer timer_;
};

}  // namespace sora

#endif