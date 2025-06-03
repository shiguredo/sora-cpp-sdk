// デバイス一覧の取得と、それで得られたデバイスの動作確認

#include <iostream>

// WebRTC
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

#include "sora/camera_device_capturer.h"
#include "sora/device_list.h"
#include "sora/java_context.h"

int main(int argc, char* argv[]) {
#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  if (!com_initializer.Succeeded()) {
    std::cerr << "CoInitializeEx failed" << std::endl;
    return 1;
  }
#endif

  webrtc::LogMessage::LogToDebug(rtc::LS_WARNING);
  webrtc::LogMessage::LogTimestamps();
  webrtc::LogMessage::LogThreads();

  // デバイス一覧の取得
  std::string last_device_name;
  sora::DeviceList::EnumVideoCapturer(
      [&last_device_name](std::string device_name, std::string unique_name) {
        std::cout << "VideoCapturer: " << device_name << ", " << unique_name
                  << std::endl;
        last_device_name = device_name;
      },
      nullptr);

  sora::CameraDeviceCapturerConfig cam_config;
  cam_config.width = 640;
  cam_config.height = 480;
  cam_config.fps = 30;
  cam_config.device_name = last_device_name;
  auto capturer = sora::CreateCameraDeviceCapturer(cam_config);
  if (capturer == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create capturer";
    return 1;
  }
  capturer = nullptr;

  sora::DeviceList::EnumAudioRecording(
      [](std::string device_name, std::string unique_name) {
        std::cout << "AudioRecording: " << device_name << ", " << unique_name
                  << std::endl;
      });

  sora::DeviceList::EnumAudioPlayout(
      [](std::string device_name, std::string unique_name) {
        std::cout << "AudioPlayout: " << device_name << ", " << unique_name
                  << std::endl;
      });

  std::cout << "Test OK" << std::endl;

  return 0;
}
