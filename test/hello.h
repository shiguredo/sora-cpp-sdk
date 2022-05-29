#ifndef TEST_HELLO_H_
#define TEST_HELLO_H_

#include "sora/sora_default_client.h"

struct HelloSoraConfig : sora::SoraDefaultClientConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string role;
};

class HelloSora : public std::enable_shared_from_this<HelloSora>,
                  public sora::SoraDefaultClient {
 public:
  HelloSora(HelloSoraConfig config);
  ~HelloSora();

  void Run();

  void* GetAndroidApplicationContext(void* env) override;
  void OnSetOffer() override;
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override;

 private:
  HelloSoraConfig config_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
};

#endif