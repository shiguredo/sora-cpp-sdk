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
  return labels_.find(label) != labels_.end();
}
void DataChannel::Send(std::string label, const webrtc::DataBuffer& data) {
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

void DataChannel::AddDataChannel(
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

void DataChannel::OnStateChange(std::shared_ptr<Thunk> thunk) {
  boost::asio::post(*ioc_, [this, thunk]() {
    auto data_channel = thunks_.at(thunk);
    if (data_channel->state() == webrtc::DataChannelInterface::kClosed) {
      labels_.erase(data_channel->label());
      thunks_.erase(thunk);
      data_channel->UnregisterObserver();
      RTC_LOG(LS_INFO) << "DataChannel closed label=" << data_channel->label();
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

void DataChannel::OnMessage(std::shared_ptr<Thunk> thunk,
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
void DataChannel::OnBufferedAmountChange(std::shared_ptr<Thunk> thunk,
                                         uint64_t previous_amount) {}

}  // namespace sora
