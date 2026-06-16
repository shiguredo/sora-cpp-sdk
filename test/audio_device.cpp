// FindAudioDeviceIndex() のマッチングロジックを固定のダミーデータで検証する
// 実際の ADM を使わず、名前/GUID の完全一致、存在しないデバイス名、
// 空のデバイス一覧の各ケースを確認する
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sora/audio_device_helper.h"

TEST_CASE("FindAudioDeviceIndex は名前でデバイスを探せること",
          "[audio_device]") {
  std::vector<std::tuple<std::string, std::string> > devices = {
      {"Device A", "guid-a"},
      {"Device B", "guid-b"},
      {"Device C", "guid-c"},
  };

  REQUIRE(sora::FindAudioDeviceIndex("Device B", devices) == 1);
}

TEST_CASE("FindAudioDeviceIndex は GUID でデバイスを探せること",
          "[audio_device]") {
  std::vector<std::tuple<std::string, std::string> > devices = {
      {"Device A", "guid-a"},
      {"Device B", "guid-b"},
      {"Device C", "guid-c"},
  };

  REQUIRE(sora::FindAudioDeviceIndex("guid-c", devices) == 2);
}

TEST_CASE("FindAudioDeviceIndex は存在しないデバイス名で -1 を返すこと",
          "[audio_device]") {
  std::vector<std::tuple<std::string, std::string> > devices = {
      {"Device A", "guid-a"},
      {"Device B", "guid-b"},
  };

  REQUIRE(sora::FindAudioDeviceIndex("Device C", devices) == -1);
  REQUIRE(sora::FindAudioDeviceIndex("guid-c", devices) == -1);
}

TEST_CASE("FindAudioDeviceIndex は空の一覧で -1 を返すこと",
          "[audio_device]") {
  std::vector<std::tuple<std::string, std::string> > devices;

  REQUIRE(sora::FindAudioDeviceIndex("Device A", devices) == -1);
}
