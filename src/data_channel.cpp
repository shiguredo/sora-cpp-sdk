#include "sora/data_channel.h"

namespace sora {

void DataChannel::Thunk::OnStateChange() {
  p->OnStateChange(shared_from_this());
}
void DataChannel::Thunk::OnMessage(const webrtc::DataBuffer& buffer) {
  p->OnMessage(shared_from_this(), buffer);
}
void DataChannel::Thunk::OnBufferedAmountChange(uint64_t previous_amount) {
  p->OnBufferedAmountChange(shared_from_this(), previous_amount);
}

DataChannel::DataChannel(boost::asio::io_context& ioc,
                         std::weak_ptr<DataChannelObserver> observer)
    : ioc_(&ioc), timer_(ioc), observer_(observer) {}
DataChannel::~DataChannel() {
  RTC_LOG(LS_INFO) << "dtor DataChannel";
}
bool DataChannel::IsOpen(std::string label) const {
  auto it = labels_.find(label);
  if (it == labels_.end()) {
    return false;
  }
  if (it->second->state() != webrtc::DataChannelInterface::kOpen) {
    return false;
  }
  return true;
}
bool DataChannel::Send(std::string label, const webrtc::DataBuffer& data) {
  auto it = labels_.find(label);
  if (it == labels_.end()) {
    return false;
  }
  if (it->second->state() != webrtc::DataChannelInterface::kOpen) {
    return false;
  }
  if (!data.binary) {
    std::string str((const char*)data.data.cdata(),
                    (const char*)data.data.cdata() + data.size());
    RTC_LOG(LS_INFO) << "Send DataChannel label=" << label << " data=" << str;
  }
  auto data_channel = it->second;
  data_channel->Send(data);
  return true;
}
void DataChannel::Close(const webrtc::DataBuffer& disconnect_message,
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
void DataChannel::SetOnClose(
    std::function<void(boost::system::error_code)> on_close) {
  on_close_ = on_close;
}

void DataChannel::AddDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
  boost::asio::post(*ioc_, [self = shared_from_this(), data_channel]() {
    std::shared_ptr<Thunk> thunk(new Thunk());
    thunk->p = self.get();
    thunk->dc = data_channel;
    data_channel->RegisterObserver(thunk.get());
    self->thunks_.insert(std::make_pair(thunk, data_channel));
    self->labels_.insert(std::make_pair(data_channel->label(), data_channel));
    // 初期状態以外だったら OnStateChange を呼ぶ
    if (data_channel->state() != webrtc::DataChannelInterface::kConnecting) {
      self->OnStateChange(thunk);
    }
  });
}

void DataChannel::OnStateChange(std::shared_ptr<Thunk> thunk) {
  boost::asio::post(*ioc_, [self = shared_from_this(), thunk]() {
    if (self->thunks_.find(thunk) == self->thunks_.end()) {
      return;
    }

    auto data_channel = self->thunks_.at(thunk);
    auto state = data_channel->state();
    auto label = data_channel->label();
    if (state == webrtc::DataChannelInterface::kOpen) {
      RTC_LOG(LS_INFO) << "DataChannel opened label=" << label;
    }
    if (state == webrtc::DataChannelInterface::kClosed) {
      self->labels_.erase(label);
      self->thunks_.erase(thunk);
      data_channel->UnregisterObserver();
      RTC_LOG(LS_INFO) << "DataChannel closed label=" << label;
    }
    auto observer = self->observer_;
    auto on_close = self->on_close_;
    auto empty = self->thunks_.empty();
    if (on_close != nullptr && empty) {
      self->on_close_ = nullptr;
      self->timer_.cancel();
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

void DataChannel::OnMessage(std::shared_ptr<Thunk> thunk,
                            const webrtc::DataBuffer& buffer) {
  boost::asio::post(*ioc_, [self = shared_from_this(), thunk, buffer]() {
    if (self->thunks_.find(thunk) == self->thunks_.end()) {
      return;
    }

    auto observer = self->observer_;
    auto data_channel = self->thunks_.at(thunk);
    auto ob = observer.lock();
    if (ob != nullptr) {
      ob->OnMessage(data_channel, buffer);
    }
  });
}
void DataChannel::OnBufferedAmountChange(std::shared_ptr<Thunk> thunk,
                                         uint64_t previous_amount) {}

}  // namespace sora
