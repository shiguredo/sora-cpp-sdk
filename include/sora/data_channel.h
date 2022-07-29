#ifndef SORA_DATA_CHANNEL_H_
#define SORA_DATA_CHANNEL_H_

#include <functional>
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
class DataChannel : public std::enable_shared_from_this<DataChannel> {
  struct Thunk : webrtc::DataChannelObserver,
                 std::enable_shared_from_this<Thunk> {
    DataChannel* p;
    rtc::scoped_refptr<webrtc::DataChannelInterface> dc;
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;
    void OnBufferedAmountChange(uint64_t previous_amount) override;
  };

 public:
  DataChannel(boost::asio::io_context& ioc,
              std::weak_ptr<DataChannelObserver> observer);
  ~DataChannel();
  bool IsOpen(std::string label) const;
  bool Send(std::string label, const webrtc::DataBuffer& data);
  void Close(const webrtc::DataBuffer& disconnect_message,
             std::function<void(boost::system::error_code)> on_close,
             double disconnect_wait_timeout);

  void AddDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel);

 private:
  void OnStateChange(std::shared_ptr<Thunk> thunk);
  void OnMessage(std::shared_ptr<Thunk> thunk,
                 const webrtc::DataBuffer& buffer);
  void OnBufferedAmountChange(std::shared_ptr<Thunk> thunk,
                              uint64_t previous_amount);

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